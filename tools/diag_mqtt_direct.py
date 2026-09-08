# SPDX-License-Identifier: MIT
# Copyright (C) 2026 Avnet
#
# Diagnostic-only script, not part of the normal quickstart flow. Attempts an
# MQTT/TLS connection straight from this PC to IoTConnect's resolved broker,
# using the same cert/key/client-id/username the RNWF11 is configured with -
# bypassing the module and firmware entirely. Use this to tell whether an
# "MQTT connect rejected" symptom on the device is a device/module issue or
# a cert/policy/platform issue on the IoTConnect/AWS side: if this script
# also fails, it's not the RNWF11 or the firmware.

import argparse
import ssl
import sys

import paho.mqtt.client as mqtt

from avnet.iotconnect.sdk.sdklib.config import DeviceProperties
from avnet.iotconnect.sdk.sdklib.dra import DeviceRestApi
from avnet.iotconnect.sdk.sdklib.error import DeviceConfigError

MQTT_PORT = 8883


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpid", required=True)
    parser.add_argument("--env", required=True)
    parser.add_argument("--duid", required=True)
    parser.add_argument("--platform", choices=["aws", "az"], default="aws")
    parser.add_argument("--ca-cert-path", required=True, help="Same CA cert file passed to provision_rnwf11_cert.py")
    parser.add_argument("--cert-path", required=True, help="The <duid>-cert.pem file provision_rnwf11_cert.py generated")
    parser.add_argument("--key-path", required=True, help="The <duid>-pkey.pem file provision_rnwf11_cert.py generated")
    args = parser.parse_args()

    try:
        config = DeviceProperties(duid=args.duid, cpid=args.cpid, env=args.env, platform=args.platform)
        config.validate()
        identity = DeviceRestApi(config, verbose=True).get_identity_data()
    except DeviceConfigError as e:
        print(f"FAILED: could not resolve device connection info: {e}")
        sys.exit(1)

    print(f"\nHost: {identity.host}")
    print(f"Client ID: {identity.client_id}")
    print(f"Username: {identity.username}")

    connect_result = {"rc": None}

    def on_connect(client, userdata, flags, rc, properties=None):
        connect_result["rc"] = rc
        print(f"on_connect: rc={rc} ({mqtt.connack_string(rc) if hasattr(mqtt, 'connack_string') else rc})")

    client = mqtt.Client(client_id=identity.client_id, protocol=mqtt.MQTTv311)
    if identity.username:
        client.username_pw_set(identity.username)
    client.tls_set(
        ca_certs=args.ca_cert_path,
        certfile=args.cert_path,
        keyfile=args.key_path,
        cert_reqs=ssl.CERT_REQUIRED,
        tls_version=ssl.PROTOCOL_TLS_CLIENT,
    )
    client.on_connect = on_connect

    print(f"\nConnecting directly to {identity.host}:{MQTT_PORT} from this PC...")
    try:
        client.connect(identity.host, MQTT_PORT, keepalive=60)
    except Exception as e:
        print(f"FAILED: connect() raised: {e}")
        sys.exit(1)

    client.loop_start()
    import time
    time.sleep(5)
    client.loop_stop()

    if connect_result["rc"] == 0:
        print("\nSUCCESS: this PC connected directly with the same credentials.")
        print("This points to something RNWF11/firmware-specific, not the platform side.")
    else:
        print(f"\nFAILED: direct connection also rejected (rc={connect_result['rc']}).")
        print("This confirms it's a cert/policy/platform issue, not the RNWF11 or firmware.")


if __name__ == "__main__":
    main()
