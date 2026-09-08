#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "provisioning.h"
#include "../hal/uart1.h"
#include "../hal/debug_console.h"

#define LINE_BUF_MAX 256

static uint16_t line_len = 0;
static char line_buf[LINE_BUF_MAX];

// Blocking: reads one \n or \r\n terminated line (stripped of the
// terminator) into line_buf. Returns its length, or 0 on overflow (the
// caller treats an empty/overflowed line as a protocol error).
static uint16_t ReadLineBlocking(void)
{
    line_len = 0;
    for (;;)
    {
        if (!UART1_IsReceiveBufferDataReady())
        {
            continue;
        }
        char c = (char)UART1_DataRead();
        if (c == '\n')
        {
            line_buf[line_len] = '\0';
            return line_len;
        }
        if (c == '\r')
        {
            continue;
        }
        if (line_len >= (LINE_BUF_MAX - 1U))
        {
            line_len = 0; // overflow - discard and keep reading this line
            continue;
        }
        line_buf[line_len++] = c;
    }
}

static bool ApplyField(device_config_t *cfg, const char *key, const char *value)
{
    if (strcmp(key, "WIFI_SSID") == 0)
        snprintf(cfg->wifi_ssid, sizeof(cfg->wifi_ssid), "%s", value);
    else if (strcmp(key, "WIFI_PASSWORD") == 0)
        snprintf(cfg->wifi_password, sizeof(cfg->wifi_password), "%s", value);
    else if (strcmp(key, "IOTC_CPID") == 0)
        snprintf(cfg->iotc_cpid, sizeof(cfg->iotc_cpid), "%s", value);
    else if (strcmp(key, "IOTC_ENV") == 0)
        snprintf(cfg->iotc_env, sizeof(cfg->iotc_env), "%s", value);
    else if (strcmp(key, "IOTC_DUID") == 0) // script's resolved MQTT client ID, not the raw Unique ID - see device_config.h
        snprintf(cfg->mqtt_client_id, sizeof(cfg->mqtt_client_id), "%s", value);
    else if (strcmp(key, "MQTT_BROKER_HOST") == 0)
        snprintf(cfg->mqtt_broker_host, sizeof(cfg->mqtt_broker_host), "%s", value);
    else if (strcmp(key, "MQTT_BROKER_PORT") == 0)
        cfg->mqtt_broker_port = (uint16_t)atoi(value);
    else if (strcmp(key, "MQTT_USERNAME") == 0)
        snprintf(cfg->mqtt_username, sizeof(cfg->mqtt_username), "%s", value);
    else if (strcmp(key, "MQTT_PUB_TOPIC") == 0)
        snprintf(cfg->mqtt_pub_topic, sizeof(cfg->mqtt_pub_topic), "%s", value);
    else if (strcmp(key, "RNWF_CA_NAME") == 0)
        snprintf(cfg->rnwf_ca_name, sizeof(cfg->rnwf_ca_name), "%s", value);
    else if (strcmp(key, "RNWF_CERT_NAME") == 0)
        snprintf(cfg->rnwf_cert_name, sizeof(cfg->rnwf_cert_name), "%s", value);
    else if (strcmp(key, "RNWF_KEY_NAME") == 0)
        snprintf(cfg->rnwf_key_name, sizeof(cfg->rnwf_key_name), "%s", value);
    else
        return false; // unknown key
    return true;
}

bool PROVISIONING_CheckForRequest(void)
{
    static uint16_t peek_len = 0;
    static char peek_buf[16]; // just long enough to match "PROVISION"

    while (UART1_IsReceiveBufferDataReady())
    {
        char c = (char)UART1_DataRead();
        if (c == '\n')
        {
            peek_buf[peek_len] = '\0';
            bool match = (peek_len == 9U) && (strcmp(peek_buf, "PROVISION") == 0);
            peek_len = 0;
            if (match)
            {
                return true;
            }
            continue;
        }
        if (c == '\r')
        {
            continue;
        }
        if (peek_len < (sizeof(peek_buf) - 1U))
        {
            peek_buf[peek_len++] = c;
        }
        else
        {
            peek_len = 0; // not "PROVISION" - resync on the next line
        }
    }
    return false;
}

bool PROVISIONING_RunOnce(device_config_t *out)
{
    memset(out, 0, sizeof(*out));

    for (;;)
    {
        uint16_t len = ReadLineBlocking();
        if (len == 0)
        {
            continue; // blank or overflowed line - keep waiting
        }
        if (strcmp(line_buf, "END") == 0)
        {
            break;
        }

        char *eq = strchr(line_buf, '=');
        if (eq == NULL)
        {
            DEBUG_Printf("ERROR:malformed line\r\n");
            return false;
        }
        *eq = '\0';
        const char *key = line_buf;
        const char *value = eq + 1;

        if (!ApplyField(out, key, value))
        {
            DEBUG_Printf("ERROR:unknown key %s\r\n", key);
            return false;
        }
    }

    if (!DEVICE_CONFIG_Save(out))
    {
        DEBUG_Printf("ERROR:flash write failed\r\n");
        return false;
    }

    DEBUG_Printf("OK\r\n");
    return true;
}
