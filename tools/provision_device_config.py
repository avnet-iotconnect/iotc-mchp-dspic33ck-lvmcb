# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Avnet
#
# One-time (or re-run any time you need to change something) provisioning step 2:
# resolves this device's IoTConnect MQTT connection info via the DRA discovery/identity
# API (using iotconnect-sdk-lite's public DeviceRestApi - no MQTT connection is made),
# then pushes WiFi + IoTConnect config into the dsPIC33's on-chip flash over its debug
# console UART. The board must already be running the quickstart firmware and printing
# "Not provisioned yet." Run tools/provision_rnwf11_cert.py first (see the README) - the
# cert/key/CA filenames it uploaded to the RNWF11 are passed here so the device knows
# which stored files to use for TLS.

import argparse
import sys
import time

import serial

from avnet.iotconnect.sdk.sdklib.config import DeviceProperties
from avnet.iotconnect.sdk.sdklib.dra import DeviceRestApi
from avnet.iotconnect.sdk.sdklib.error import DeviceConfigError

MQTT_PORT = 8883


def resolve_connection_info(cpid: str, env: str, duid: str, platform: str):
    config = DeviceProperties(duid=duid, cpid=cpid, env=env, platform=platform)
    config.validate()
    print(f"Resolving IoTConnect connection info for DUID \"{duid}\"...")
    identity = DeviceRestApi(config, verbose=True).get_identity_data()
    if not identity.topics.rpt:
        raise DeviceConfigError("Identity response did not include a telemetry (rpt) topic")
    return identity


def send_provisioning_protocol(ser: serial.Serial, fields: dict, overall_timeout=90, retry_interval=5):
    # The firmware's WiFi/MQTT connect attempts each block for many seconds
    # at a time (and this is a polled, non-interrupt-driven UART, so bytes
    # sent while it's not actively reading are lost, not just delayed) - a
    # single one-shot send has a real chance of landing in one of those
    # windows and getting no response at all, even though the device is
    # working fine. Resend the whole handshake periodically until either it
    # succeeds or we've comfortably outlasted a full connect-retry cycle.
    def send_line(line: str):
        ser.write((line + "\n").encode("ascii"))

    deadline = time.time() + overall_timeout
    attempt = 0
    while True:
        attempt += 1
        ser.reset_input_buffer()  # discard anything stale from a previous attempt

        send_line("PROVISION")
        for key, value in fields.items():
            send_line(f"{key}={value}")
        send_line("END")

        response = ser.readline().decode("ascii", errors="replace").strip()
        if response == "OK":
            print("Device confirmed: OK")
            return True
        if response:
            print(f"FAILED: device reported an error: {response}")
            return False

        if time.time() >= deadline:
            print("FAILED: device never responded - check the port/baud rate, and that the firmware is running")
            return False

        print(f"No response yet (attempt {attempt}) - the board may be mid-connection-attempt "
              f"and not listening right now; retrying...")
        time.sleep(retry_interval)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True, help="Serial port for the dsPIC33's debug console (e.g. COM5 or /dev/ttyACM0)")
    parser.add_argument("--baud", type=int, default=115200, help="Debug console baud rate (default: 115200, matches UART1_Initialize() in the firmware)")
    parser.add_argument("--wifi-ssid", required=True)
    parser.add_argument("--wifi-password", required=True)
    parser.add_argument("--cpid", required=True, help="IoTConnect account CPID (Settings -> Key Value in the IoTConnect console)")
    parser.add_argument("--env", required=True, help="IoTConnect account Environment (Settings -> Key Value)")
    parser.add_argument("--duid", required=True, help="This device's Unique ID, as entered when you created the device in the IoTConnect console")
    parser.add_argument("--platform", choices=["aws", "az"], default="aws", help="IoTConnect backend platform (default: aws)")
    parser.add_argument("--ca-name", default="root-ca", help="CA/root cert filename already uploaded to the RNWF11 (see provision_rnwf11_cert.py; default: root-ca)")
    parser.add_argument("--cert-name", default="device-cert", help="Device cert filename already uploaded to the RNWF11 (default: device-cert)")
    parser.add_argument("--key-name", default="device-key", help="Device private key filename already uploaded to the RNWF11 (default: device-key)")
    args = parser.parse_args()

    try:
        identity = resolve_connection_info(args.cpid, args.env, args.duid, args.platform)
    except DeviceConfigError as e:
        print(f"FAILED: could not resolve device connection info: {e}")
        sys.exit(1)

    print(f"Resolved broker host: {identity.host}")
    print(f"Resolved MQTT client ID: {identity.client_id}")
    print(f"Resolved MQTT username: {identity.username or '(none)'}")
    print(f"Resolved telemetry topic: {identity.topics.rpt}")

    fields = {
        "WIFI_SSID": args.wifi_ssid,
        "WIFI_PASSWORD": args.wifi_password,
        "IOTC_CPID": args.cpid,
        "IOTC_ENV": args.env,
        # The firmware only ever uses this field as the MQTT client ID, so
        # use IoTConnect's own resolved client_id here rather than the raw
        # --duid: they're the same for "dedicated" instances, but for
        # "shared" instances (e.g. POC/trial accounts) IoTConnect assigns a
        # different client ID (often "CPID-DUID"), and sending the wrong
        # one gets the connection silently rejected by the broker.
        "IOTC_DUID": identity.client_id,
        "MQTT_BROKER_HOST": identity.host,
        "MQTT_BROKER_PORT": MQTT_PORT,
        "MQTT_USERNAME": identity.username or "",
        "MQTT_PUB_TOPIC": identity.topics.rpt,
        "RNWF_CA_NAME": args.ca_name,
        "RNWF_CERT_NAME": args.cert_name,
        "RNWF_KEY_NAME": args.key_name,
    }

    print(f"Connecting to {args.port} at {args.baud} baud...")
    try:
        with serial.Serial(args.port, args.baud, timeout=5) as ser:
            time.sleep(0.2)  # let the port settle before writing
            if not send_provisioning_protocol(ser, fields):
                sys.exit(1)
    except serial.SerialException as e:
        print(f"FAILED: could not open {args.port}: {e}")
        sys.exit(1)

    print("SUCCESS: device provisioned. It should now connect to WiFi and IoTConnect.")


if __name__ == "__main__":
    main()
