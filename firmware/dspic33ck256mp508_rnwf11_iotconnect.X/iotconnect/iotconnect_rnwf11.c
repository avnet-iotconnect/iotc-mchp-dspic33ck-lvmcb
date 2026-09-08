#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <xc.h>
#include "clock.h"
#include <libpic30.h> /* needs FCY from clock.h */
#include "debug_console.h"
#include "uart2.h"
#include "iotconnect_rnwf11.h"
#include "iotconnect_rnwf11_config.h"
#include "iotcl.h"
#include "iotcl_telemetry.h"
#include "../hal/device_config.h"
#include "../hal/nvm_flash.h"
#include "provisioning.h"

#define IOTC_RNWF11_RESPONSE_SIZE 384U
#define IOTC_RNWF11_COMMAND_TIMEOUT 5000000UL
#define IOTC_RNWF11_PROBE_TIMEOUT 200000UL
#define IOTC_RNWF11_WIFI_WAIT_MS 20000UL

static bool iotcConnected;
static bool iotcModulePresent;
static volatile uint32_t telemetryMilliseconds;
static volatile uint32_t retryMilliseconds;
static IOTC_RNWF11_Telemetry_t telemetry;
static char lastResponse[IOTC_RNWF11_RESPONSE_SIZE];
static bool lastOverrun;
static bool mqttLinkUp;
/* Assume up: the module stays associated across dsPIC resets and only
 * re-announces its IP on a fresh association. */
static bool netUp = true;

/* Populated at boot from flash (see IOTC_RNWF11_Initialize()) if the
 * device has been provisioned via tools/provision_device_config.py/.ps1,
 * falling back to the compile-time IOTC_* defaults below otherwise - see
 * device_config.h. Updated in place by IOTC_RNWF11_CheckProvisioning()
 * whenever a new provisioning request comes in over the console. */
static device_config_t s_cfg;

static bool IOTC_RNWF11_IsConfigured(void)
{
    return (s_cfg.wifi_ssid[0] != '\0') &&
           (s_cfg.mqtt_broker_host[0] != '\0') &&
           (s_cfg.mqtt_client_id[0] != '\0') &&
           (s_cfg.mqtt_pub_topic[0] != '\0');
}

static void IOTC_RNWF11_UART2_Initialize(void)
{
    UART2_Initialize(IOTC_RNWF11_BAUD);
}

/* The module echoes every character, so keep draining while transmitting. */
static void IOTC_RNWF11_Collect(uint16_t *length)
{
    while (UART2_IsReceiveDataAvailable())
    {
        char c = (char)UART2_Read();
        if (*length < (IOTC_RNWF11_RESPONSE_SIZE - 1U))
        {
            lastResponse[*length] = c;
            (*length)++;
            lastResponse[*length] = '\0';
        }
    }
}

/* The module echoes every character; discard it so long commands still fit. */
static void IOTC_RNWF11_DiscardEcho(void)
{
    while (UART2_IsReceiveDataAvailable())
    {
        (void)UART2_Read();
    }
}

static void IOTC_RNWF11_Write(const char *text)
{
    while (*text != '\0')
    {
        UART2_Write((uint8_t)*text++);
        while (!UART2_IsTransmitComplete())
        {
            IOTC_RNWF11_DiscardEcho();
        }
        IOTC_RNWF11_DiscardEcho();
    }
}

static void IOTC_RNWF11_PollEvents(void);

static bool IOTC_RNWF11_CommandWithTimeout(const char *command, uint32_t timeout)
{
    uint16_t length = 0;

    /* Print anything pending rather than discarding a failure notification. */
    IOTC_RNWF11_PollEvents();

    lastResponse[0] = '\0';
    UART2_ReceiveFlush();
    lastOverrun = false;

    IOTC_RNWF11_Write(command);

    while (timeout-- != 0U)
    {
        IOTC_RNWF11_Collect(&length);
        if (UART2_IsOverrun())
        {
            lastOverrun = true;
            UART2_ClearOverrun();
        }
        if (strstr(lastResponse, "ERROR") != NULL)
        {
            return false;
        }
        if (strstr(lastResponse, "OK") != NULL)
        {
            return true;
        }
    }
    return false;
}

static bool IOTC_RNWF11_Command(const char *command)
{
    return IOTC_RNWF11_CommandWithTimeout(command, IOTC_RNWF11_COMMAND_TIMEOUT);
}

/* Labelled so failures are traceable without printing Wi-Fi credentials. */
static bool IOTC_RNWF11_Step(const char *label, const char *command)
{
    if (IOTC_RNWF11_Command(command))
    {
        return true;
    }
    DEBUG_Printf("IOTC: step %s failed%s, resp=[%s]\r\n",
                 label, lastOverrun ? " (rx overrun)" : "",
                 (lastResponse[0] != '\0') ? lastResponse : "<timeout>");
    return false;
}

/* Sends a query and prints whatever comes back, ignoring OK/ERROR. */
static void IOTC_RNWF11_Query(const char *command)
{
    uint16_t length = 0;

    lastResponse[0] = '\0';
    UART2_ReceiveFlush();
    IOTC_RNWF11_Write(command);

    for (uint16_t slice = 0; slice < 2000U; slice++)
    {
        __delay_us(250);
        IOTC_RNWF11_Collect(&length);
    }
    DEBUG_Printf("IOTC: query -> [%s]\r\n", lastResponse);
}

static void IOTC_RNWF11_Diagnose(void)
{
    static const char *const queries[] = {
        "AT+TIME\r\n",     /* confirm SNTP supplied a valid clock */
        "AT+TLSC=1\r\n",   /* did the certificate names actually stick */
        "AT+MQTTC\r\n",    /* host, port, client id, TLS selection */
    };

    DEBUG_Printf("IOTC: --- diagnostics ---\r\n");
    for (uint16_t i = 0; i < (sizeof(queries) / sizeof(queries[0])); i++)
    {
        IOTC_RNWF11_Query(queries[i]);
    }
    DEBUG_Printf("IOTC: --- end ---\r\n");
}

/* Expected to fail once the station is associated, so do not log it. */
static void IOTC_RNWF11_StepQuiet(const char *command)
{
    (void)IOTC_RNWF11_Command(command);
}

/* Best effort: the module may already keep time, so a failure is not fatal. */
static void IOTC_RNWF11_StepOptional(const char *label, const char *command)
{
    (void)IOTC_RNWF11_Step(label, command);
}

static bool IOTC_RNWF11_WaitForNetwork(void)
{
    netUp = false;
    /* Close any MQTT session first - tearing down WiFi out from under a
     * still-open MQTT session (below) leaves the module's own MQTT client
     * state confused, which was seen to make every later AT+MQTTCONN=1
     * fail instantly even after a fresh WiFi reassociation. Only the very
     * first connect attempt after a full power cycle ever worked. */
    IOTC_RNWF11_StepQuiet("AT+MQTTDISCONN\r\n");
    /* Force a real disconnect+reconnect: AT+WSTA=1 alone is a no-op if the
     * module is already associated from a prior boot (host resets/reflashes
     * don't power-cycle the module), which was seen to reach MQTTCONN in a
     * state that consistently fails - a genuinely fresh association is the
     * one thing that correlated with MQTTCONN actually succeeding. */
    IOTC_RNWF11_StepQuiet("AT+WSTA=0\r\n");
    IOTC_RNWF11_StepQuiet("AT+WSTA=1\r\n");

    for (uint32_t elapsed = 0; elapsed < IOTC_RNWF11_WIFI_WAIT_MS; elapsed++)
    {
        IOTC_RNWF11_PollEvents();
        if (netUp)
        {
            return true;
        }
        __delay_ms(1);
    }
    DEBUG_Printf("IOTC: step WiFi-IP failed\r\n");
    return false;
}

static bool IOTC_RNWF11_Configure(void)
{
    char command[256];

    /* Order matches the reference RNWF11 driver (main branch): WiFi is
     * brought up completely first as its own phase, then TLS+MQTT are
     * configured together right before connecting - not interleaved with
     * TLS config first, the way this file did before. */
    if (!IOTC_RNWF11_Step("AT", "AT\r\n")) return false;

    /* These are rejected while the station is already associated, which is fine. */
    snprintf(command, sizeof(command), "AT+WSTAC=1,\"%s\"\r\n", s_cfg.wifi_ssid);
    IOTC_RNWF11_StepQuiet(command);
    snprintf(command, sizeof(command), "AT+WSTAC=2,%u\r\n", IOTC_WIFI_SECURITY);
    IOTC_RNWF11_StepQuiet(command);
    snprintf(command, sizeof(command), "AT+WSTAC=3,\"%s\"\r\n", s_cfg.wifi_password);
    IOTC_RNWF11_StepQuiet(command);
    IOTC_RNWF11_StepQuiet("AT+WSTAC=4,0\r\n");
    if (!IOTC_RNWF11_WaitForNetwork()) return false;

    /* These are rejected once the module already has the identical value
     * persisted from an earlier provision_rnwf11_cert.py run (its own
     * non-volatile storage, unaffected by power cycling the host board) -
     * same "reject a no-op update" behavior already seen on WSTAC/SNTP, so
     * treat rejection as fine rather than aborting the whole sequence. */
    snprintf(command, sizeof(command), "AT+TLSC=1,1,\"%s\"\r\n", s_cfg.rnwf_ca_name);
    IOTC_RNWF11_StepQuiet(command);
    if (s_cfg.rnwf_cert_name[0] != '\0')
    {
        snprintf(command, sizeof(command), "AT+TLSC=1,2,\"%s\"\r\n", s_cfg.rnwf_cert_name);
        IOTC_RNWF11_StepQuiet(command);
    }
    if (s_cfg.rnwf_key_name[0] != '\0')
    {
        snprintf(command, sizeof(command), "AT+TLSC=1,3,\"%s\"\r\n", s_cfg.rnwf_key_name);
        IOTC_RNWF11_StepQuiet(command);
    }
    snprintf(command, sizeof(command), "AT+TLSC=1,5,\"%s\"\r\n", s_cfg.mqtt_broker_host);
    IOTC_RNWF11_StepQuiet(command);
    /* Field 8 is USE_ECC608 (use the module's secure-element key instead of
     * the uploaded device-key file), not a generic "verify" toggle - the
     * reference driver explicitly sends 0 here for the file-based cert path
     * used by provision_rnwf11_cert.py. Sending 1 makes the module try to
     * authenticate with its own secure-element identity instead of the
     * uploaded key matching the cert actually registered with AWS IoT.
     * TEMP DIAGNOSTIC: not resending this - it's already 0 in the module's
     * persisted config from an earlier successful run, and re-sending the
     * same value gets rejected with "0.6 Configuration Update Blocked"
     * every boot, which may be leaving the TLS config slot in a state that
     * breaks the MQTTCONN attempt right after it. */

    /* The module's SNTP client never produces a real synced clock on this
     * board (still investigating why), and the TLS stack validates AWS's
     * server certificate against whatever clock it has - a wildly wrong
     * one (module was seen free-running at ~year 2039/2096) makes a
     * perfectly valid server cert look expired/not-yet-valid and the
     * handshake gets rejected. AT+TIME=1,<unix> sets the clock directly,
     * bypassing SNTP - approximate is fine, cert validity windows span
     * years, not seconds. */
    IOTC_RNWF11_StepQuiet("AT+TIME=1,1788998400\r\n");

    snprintf(command, sizeof(command), "AT+MQTTC=1,\"%s\"\r\n", s_cfg.mqtt_broker_host);
    if (!IOTC_RNWF11_Step("MQTTC1-host", command)) return false;
    snprintf(command, sizeof(command), "AT+MQTTC=2,%u\r\n", s_cfg.mqtt_broker_port);
    if (!IOTC_RNWF11_Step("MQTTC2-port", command)) return false;
    snprintf(command, sizeof(command), "AT+MQTTC=3,\"%s\"\r\n", s_cfg.mqtt_client_id);
    if (!IOTC_RNWF11_Step("MQTTC3-clientid", command)) return false;
    /* Match the working RNWF11 flow: use the MQTT protocol version it sets. */
    if (!IOTC_RNWF11_Step("MQTTC8-protocol", "AT+MQTTC=8,3\r\n")) return false;
    /*
     * Always written, even when empty: the module keeps the previous value
     * across resets, and AWS rejects a CONNECT that carries a username.
     */
    snprintf(command, sizeof(command), "AT+MQTTC=4,\"%s\"\r\n", s_cfg.mqtt_username);
    IOTC_RNWF11_StepOptional("MQTTC4-user", command);
    IOTC_RNWF11_StepOptional("MQTTC5-pass", "AT+MQTTC=5,\"\"\r\n");
    if (!IOTC_RNWF11_Step("MQTTC7-tls", "AT+MQTTC=7,1\r\n")) return false;
    if (!IOTC_RNWF11_Step("MQTTC6-keepalive", "AT+MQTTC=6,60\r\n")) return false;
    if (!IOTC_RNWF11_Step("MQTTCONN", "AT+MQTTCONN=1\r\n"))
    {
        return false;
    }

    /* The broker result arrives asynchronously; watch for it rather than guess. */
    for (uint16_t slice = 0; slice < 10000U; slice++)
    {
        __delay_us(500);
        IOTC_RNWF11_PollEvents();
        if (mqttLinkUp)
        {
            break;
        }
    }
    return mqttLinkUp;
}

#define IOTC_RNWF11_ESCAPED_JSON_MAX 256U

/* AT+MQTTPUB wraps the message in "..." with no escaping of its own - any
 * '"' inside the JSON payload prematurely closes that quoted argument. */
static bool IOTC_RNWF11_EscapeJsonForAtCommand(const char *json, char *out, size_t out_size)
{
    size_t o = 0;
    for (size_t i = 0; json[i] != '\0'; i++)
    {
        char c = json[i];
        bool needs_escape = (c == '"') || (c == '\\');
        size_t needed = needs_escape ? 2U : 1U;
        if (o + needed >= out_size)
        {
            return false;
        }
        if (needs_escape)
        {
            out[o++] = '\\';
        }
        out[o++] = c;
    }
    out[o] = '\0';
    return true;
}

static bool IOTC_RNWF11_PublishTelemetry(void)
{
    IotclMessageHandle msg = iotcl_telemetry_create();
    if (msg == NULL)
    {
        DEBUG_Printf("IOTC: iotcl_telemetry_create failed\r\n");
        return false;
    }

    iotcl_telemetry_set_number(msg, "run", telemetry.motorRunning);
    iotcl_telemetry_set_number(msg, "st", telemetry.state);
    iotcl_telemetry_set_number(msg, "sec", telemetry.sector);
    iotcl_telemetry_set_number(msg, "rpm", telemetry.requestedSpeedRpm);
    iotcl_telemetry_set_number(msg, "spd", telemetry.measuredSpeedRpm);
    iotcl_telemetry_set_number(msg, "ic", telemetry.requestedCurrent);
    iotcl_telemetry_set_number(msg, "im", telemetry.measuredCurrent);
    iotcl_telemetry_set_number(msg, "duty", telemetry.dutyCycle);
    iotcl_telemetry_set_number(msg, "vdc", telemetry.dcBusAdc);

    char *json = iotcl_telemetry_create_serialized_string(msg, false);
    iotcl_telemetry_destroy(msg);
    if (json == NULL)
    {
        DEBUG_Printf("IOTC: iotcl_telemetry_create_serialized_string failed\r\n");
        return false;
    }

    char escaped[IOTC_RNWF11_ESCAPED_JSON_MAX];
    bool escapedOk = IOTC_RNWF11_EscapeJsonForAtCommand(json, escaped, sizeof(escaped));

    bool sent = false;
    if (!escapedOk)
    {
        DEBUG_Printf("IOTC: telemetry JSON too long to escape (%s)\r\n", json);
    }
    else
    {
        char command[384];
        int written = snprintf(command, sizeof(command), "AT+MQTTPUB=1,1,0,\"%s\",\"%s\"\r\n",
                                s_cfg.mqtt_pub_topic, escaped);
        if ((written < 0) || ((size_t)written >= sizeof(command)))
        {
            DEBUG_Printf("IOTC: telemetry command too long to send\r\n");
        }
        else
        {
            sent = IOTC_RNWF11_Step("MQTTPUB", command);
        }
    }

    iotcl_telemetry_destroy_serialized_string(json);
    return sent;
}

void IOTC_RNWF11_Initialize(void)
{
#if IOTC_RNWF11_ENABLE
    IOTC_RNWF11_UART2_Initialize();
    iotcModulePresent = IOTC_RNWF11_CommandWithTimeout("AT\r\n", IOTC_RNWF11_PROBE_TIMEOUT);
    iotcConnected = false;

    if (DEVICE_CONFIG_Load(&s_cfg))
    {
        DEBUG_Printf("IOTC: loaded provisioned config for client %s\r\n", s_cfg.mqtt_client_id);
    }
    else
    {
        // Not yet provisioned (or flash was blank/corrupt) - fall back to
        // the compile-time defaults so the board still works exactly as
        // before without going through tools/provision_device_config.py/.ps1.
        // Also the one safe moment to self-test the flash driver: this
        // page holds no valid config yet, so there is nothing to lose.
        snprintf(s_cfg.wifi_ssid, sizeof(s_cfg.wifi_ssid), "%s", IOTC_WIFI_SSID);
        snprintf(s_cfg.wifi_password, sizeof(s_cfg.wifi_password), "%s", IOTC_WIFI_PASSWORD);
        snprintf(s_cfg.mqtt_broker_host, sizeof(s_cfg.mqtt_broker_host), "%s", IOTC_MQTT_BROKER_HOST);
        s_cfg.mqtt_broker_port = IOTC_MQTT_BROKER_PORT;
        snprintf(s_cfg.mqtt_client_id, sizeof(s_cfg.mqtt_client_id), "%s", IOTC_MQTT_CLIENT_ID);
        snprintf(s_cfg.mqtt_username, sizeof(s_cfg.mqtt_username), "%s", IOTC_MQTT_USERNAME);
        snprintf(s_cfg.mqtt_pub_topic, sizeof(s_cfg.mqtt_pub_topic), "%s", IOTC_MQTT_TELEMETRY_TOPIC);
        snprintf(s_cfg.rnwf_ca_name, sizeof(s_cfg.rnwf_ca_name), "%s", IOTC_RNWF11_CA_NAME);
        snprintf(s_cfg.rnwf_cert_name, sizeof(s_cfg.rnwf_cert_name), "%s", IOTC_RNWF11_CERT_NAME);
        snprintf(s_cfg.rnwf_key_name, sizeof(s_cfg.rnwf_key_name), "%s", IOTC_RNWF11_KEY_NAME);

        DEBUG_Printf("IOTC: not provisioned, using compile-time config; flash self-test %s\r\n",
                     NVM_FLASH_SelfTest() ? "PASS" : "FAIL");
    }

    IotclClientConfig iotcl_cfg;
    iotcl_init_client_config(&iotcl_cfg);
    iotcl_cfg.device.instance_type = IOTCL_DCT_CUSTOM; // broker/topic are resolved at provisioning time - see iotconnect_rnwf11_config.h
    iotcl_init(&iotcl_cfg);

#if !IOTC_RNWF11_RX_RPn
    DEBUG_Printf("IOTC: UART2 pins unset, set IOTC_RNWF11_RX_RPn/TX_RPnR\r\n");
#endif
    DEBUG_Printf("IOTC: module %s, config %s\r\n",
                 iotcModulePresent ? "detected" : "not responding",
                 IOTC_RNWF11_IsConfigured() ? "ok" : "incomplete");
#else
    iotcModulePresent = false;
    iotcConnected = false;
#endif
}

void IOTC_RNWF11_CheckProvisioning(void)
{
#if IOTC_RNWF11_ENABLE
    if (PROVISIONING_CheckForRequest())
    {
        device_config_t new_cfg;
        if (PROVISIONING_RunOnce(&new_cfg))
        {
            s_cfg = new_cfg;
            iotcConnected = false;              // force a fresh connect with the new config
            mqttLinkUp = false;
            retryMilliseconds = IOTC_RETRY_PERIOD_MS; // don't wait out the retry timer
            DEBUG_Printf("IOTC: reprovisioned, reconnecting\r\n");
        }
    }
#endif
}

void IOTC_RNWF11_Tick1ms(void)
{
#if IOTC_RNWF11_ENABLE
    telemetryMilliseconds++;
    retryMilliseconds++;
#endif
}

void IOTC_RNWF11_SetTelemetry(const IOTC_RNWF11_Telemetry_t *sample)
{
    telemetry = *sample;
}

/*
 * The module reports real connection state asynchronously; command replies only
 * say whether the command itself was accepted.
 */
static void IOTC_RNWF11_PollEvents(void)
{
    static char line[128];
    static uint16_t len;

    while (UART2_IsReceiveDataAvailable())
    {
        char c = (char)UART2_Read();
        if ((c == '\r') || (c == '\n'))
        {
            if (len > 0U)
            {
                line[len] = '\0';
                DEBUG_Printf("IOTC: event %s\r\n", line);
                if (strstr(line, "+MQTTCONN:1") != NULL)
                {
                    mqttLinkUp = true;
                }
                else if (strstr(line, "+MQTTCONN:0") != NULL)
                {
                    mqttLinkUp = false;
                }
                else if (strstr(line, "+WSTAAIP:") != NULL)
                {
                    /* The leading number is a per-association counter, not
                     * a fixed interface id - it increments every time the
                     * module reconnects (seen going 1 -> 3 after adding an
                     * explicit AT+WSTA=0/1 disconnect+reconnect cycle), so
                     * matching only ":1" missed every later reconnect. */
                    netUp = true;
                }
                else if (strstr(line, "+WSTALU:0") != NULL)
                {
                    netUp = false;
                    mqttLinkUp = false;
                }
                else if (strstr(line, "20.2") != NULL)
                {
                    /* The module reports this when STA is already connected. */
                    netUp = true;
                }
                len = 0;
            }
        }
        else if (len < (sizeof(line) - 1U))
        {
            line[len++] = c;
        }
    }
}

void IOTC_RNWF11_Task(void)
{
#if IOTC_RNWF11_ENABLE
    if (!iotcModulePresent || !IOTC_RNWF11_IsConfigured())
    {
        return;
    }
    IOTC_RNWF11_PollEvents();
    if (!iotcConnected)
    {
        if (!netUp)
        {
            return;
        }
        if (retryMilliseconds < IOTC_RETRY_PERIOD_MS)
        {
            return;
        }
        retryMilliseconds = 0;
        /* Clear any stale value from a prior session before this attempt -
         * a genuine "+MQTTCONN:1" during Configure() below (processed via
         * its own PollEvents() calls) sets this back to true; unconditionally
         * clearing it AFTER Configure() returns (as this used to do) stomped
         * on that real success every single time, making a connection that
         * actually worked look immediately "not connected" on the very next
         * check and forcing a pointless reconnect before telemetry ever had
         * a chance to publish. */
        mqttLinkUp = false;
        iotcConnected = IOTC_RNWF11_Configure();
        DEBUG_Printf("IOTC: provisioning %s\r\n", iotcConnected ? "complete" : "failed");
#if IOTC_DIAG_QUERIES
        IOTC_RNWF11_Diagnose();
#endif
        telemetryMilliseconds = 0;
        return;
    }
    if (telemetryMilliseconds >= IOTC_TELEMETRY_PERIOD_MS)
    {
        telemetryMilliseconds = 0;
        if (!mqttLinkUp)
        {
            DEBUG_Printf("IOTC: broker not connected, retrying\r\n");
            iotcConnected = false;
            retryMilliseconds = 0;
            return;
        }
        bool sent = IOTC_RNWF11_PublishTelemetry();
        DEBUG_Printf("IOTC: publish %s state=%u rpm=%u duty=%d\r\n",
                 sent ? "ok" : "FAILED",
                 telemetry.state, telemetry.measuredSpeedRpm, telemetry.dutyCycle);
    }
#endif
}

bool IOTC_RNWF11_IsConnected(void)
{
    return iotcConnected;
}
