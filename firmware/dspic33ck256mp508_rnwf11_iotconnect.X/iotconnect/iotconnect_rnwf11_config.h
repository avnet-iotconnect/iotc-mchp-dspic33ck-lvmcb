#ifndef IOTCONNECT_RNWF11_CONFIG_H
#define IOTCONNECT_RNWF11_CONFIG_H

/* Set to 1 after completing the IoTConnect settings below. */
#define IOTC_RNWF11_ENABLE 1

/* RNWF11 uses 230400, 8-N-1 on UART2. */
#define IOTC_RNWF11_BAUD 230400UL

/*
 * UART2 is routed to the mikroBUS B header, where the RNWF11 is seated. UART1
 * stays on RD13/RD14 for the debug console on the PKoB4 virtual COM (J13).
 * Per the MCSK schematic, Click B TX is RP69 (RD5) and RX is RP70 (RD6).
 */
#define IOTC_RNWF11_RX_RPn 70
#define IOTC_RNWF11_TX_RPnR _RP69R

/* RPnR output code for U2TX, measured on silicon during bring-up. */
#define IOTC_RNWF11_TX_RPnR_U2TX 0b000011

/* Wi-Fi credentials. Fill these in before flashing, or provision them at
 * runtime instead (see tools/provision_device_config.py). Security: 0=open,
 * 2=WPA2-Personal (mixed TKIP/CCMP), 3=WPA2-Personal (CCMP only). */
#define IOTC_WIFI_SSID "YOUR_WIFI_SSID"
#define IOTC_WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define IOTC_WIFI_SECURITY 2U

/* IoTConnect MQTT endpoint values resolved during device provisioning. */
#define IOTC_MQTT_BROKER_HOST "a3etk4e19usyja-ats.iot.us-east-1.amazonaws.com"
#define IOTC_MQTT_BROKER_PORT 8883U
#define IOTC_MQTT_CLIENT_ID "YOUR_DEVICE_DUID"
/* AWS IoT Core authenticates by client certificate; it takes no username. */
#define IOTC_MQTT_USERNAME ""
#define IOTC_MQTT_TELEMETRY_TOPIC "$aws/rules/msg_d2c_rpt/YOUR_DEVICE_DUID/2.1/0"

/* Read back TLS and MQTT config after provisioning. */
#define IOTC_DIAG_QUERIES 1

/*
 * CA is a built-in entry in the module's certificate store. The device
 * certificate and private key live in the ECC secure element and are used
 * automatically, so leaving these empty skips AT+TLSC=1,2 and 1,3.
 */
#define IOTC_RNWF11_CA_NAME "root-ca"
#define IOTC_RNWF11_CERT_NAME "device-cert"
#define IOTC_RNWF11_KEY_NAME "device-key"

/* Publish a trivial payload instead of JSON, to isolate quoting problems. */
/* 0 = real JSON, 1 = plain "hello", 2 = short escaped JSON. */
#define IOTC_PUBLISH_TEST 0

#define IOTC_TELEMETRY_PERIOD_MS 10000UL

/* Wait between provisioning attempts so failures do not flood the console. */
#define IOTC_RETRY_PERIOD_MS 15000UL

#endif
