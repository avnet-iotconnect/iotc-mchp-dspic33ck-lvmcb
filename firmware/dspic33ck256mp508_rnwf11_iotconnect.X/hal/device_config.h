/**
 * @file device_config.h
 * @brief WiFi + IoTConnect connection settings, persisted in on-chip flash
 *        (see hal/nvm_flash.c) and written by
 *        tools/provision_device_config.py/.ps1 over the UART1 debug
 *        console using the same line-based protocol as the main
 *        dsPIC33AK512MPS512 branch (see iotconnect/provisioning.c) - the
 *        scripts are unmodified between branches, so every field they send
 *        (including IOTC_CPID/IOTC_ENV, which this board's runtime never
 *        reads) has to be accepted here or provisioning fails outright.
 */
#ifndef DEVICE_CONFIG_H
#define DEVICE_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#define DEVICE_CONFIG_MAGIC 0x4B435431UL // "KCT1"

typedef struct
{
    uint32_t magic;   // DEVICE_CONFIG_MAGIC if this page holds valid provisioned data
    uint32_t version;

    char wifi_ssid[33];
    char wifi_password[65];

    // Not used by this board's connect logic at runtime - stored only so
    // the unmodified provisioning scripts (which always send these) don't
    // hit an "unknown key" error.
    char iotc_cpid[17];
    char iotc_env[17];

    // Resolved once by tools/provision_device_config.py (IoTConnect DRA
    // discovery+identity) and baked in here, rather than the firmware doing
    // live HTTPS discovery itself - matches the main branch's approach.
    // The script sends this as "IOTC_DUID", but it's IoTConnect's resolved
    // MQTT client ID (not necessarily the raw Unique ID), so it's named to
    // match this board's existing IOTC_MQTT_CLIENT_ID convention.
    char mqtt_client_id[65];
    char mqtt_broker_host[128];
    uint16_t mqtt_broker_port;
    char mqtt_username[192]; // IoTConnect's device MQTT username, resolved at provisioning time; empty string if unused
    char mqtt_pub_topic[128]; // IoTConnect's per-device telemetry publish topic, also resolved at provisioning time

    // Filenames of the CA/root, device cert, and device key already
    // uploaded to the RNWF11's own filesystem by
    // tools/provision_rnwf11_cert.py (AT+FS), so this firmware never
    // handles private key material directly.
    char rnwf_ca_name[33];
    char rnwf_cert_name[33];
    char rnwf_key_name[33];

    uint32_t crc32; // over every field above, see device_config.c
} device_config_t;

/**
 * @brief Loads the config from flash into *out.
 * @return true if a validly-provisioned config was found (magic + CRC32 match).
 *         false if the device has not been provisioned yet (or the flash
 *         page was erased/corrupt) - the caller should fall back to the
 *         compile-time IOTC_* defaults in iotconnect_rnwf11_config.h rather
 *         than trust *out.
 */
bool DEVICE_CONFIG_Load(device_config_t *out);

/**
 * @brief Erases the config flash page and writes *cfg (with a freshly
 *        computed CRC32 and DEVICE_CONFIG_MAGIC).
 * @return true on success.
 */
bool DEVICE_CONFIG_Save(device_config_t *cfg);

#endif // DEVICE_CONFIG_H
