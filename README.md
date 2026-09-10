# dsPIC33CK256MP508 Motor Control Starter Kit + RNWF11 /IOTCONNECT Quickstart

A baremetal C quickstart connecting the Microchip **dsPIC33CK256MP508** (on
the **dsPIC33CK Motor Control Starter Kit**, running Microchip's AN957 BLDC
motor-control reference application - see
[`docs/AN957 Demo ReadMe MCSK.pdf`](firmware/dspic33ck256mp508_rnwf11_iotconnect.X/docs))
to [Avnet /IOTCONNECT](https://www.iotconnect.io/) using /IOTCONNECT's
[C SDK](https://github.com/avnet-iotconnect/iotc-c-lib), over the Microchip
**RNWF11 UART to Cloud Add-on Board**. No RTOS, no MQTT/TLS stack on the
MCU - the RNWF11 owns the WiFi/MQTT/TLS connection itself, using a
certificate and key stored on its own filesystem, and the dsPIC33 just
talks to it over UART with AT commands. While the motor control loop runs
in real time, the demo publishes live motor telemetry (run state, speed,
current, duty cycle, etc.) to /IOTCONNECT every 10 seconds.

<img src="media/mcsk-product.png" width="400"/>

## Table of Contents

1. [Prerequisites](#1-prerequisites)
2. [Get the Quickstart Source](#2-get-the-quickstart-source)
3. [Import the Device Template](#3-import-the-device-template)
4. [Generate and Upload the Device Certificate](#4-generate-and-upload-the-device-certificate)
5. [Create the Device in /IOTCONNECT](#5-create-the-device-in-iotconnect)
6. [Mount the RNWF11 on the Starter Kit](#6-mount-the-rnwf11-on-the-starter-kit)
7. [Configure the Firmware](#7-configure-the-firmware)
8. [Build the Firmware](#8-build-the-firmware)
9. [Flash and Run the Demo](#9-flash-and-run-the-demo)
10. [Resources](#10-resources)

## 1. Prerequisites

### Hardware

1. [dsPIC33CK Motor Control Starter Kit](https://www.microchip.com/en-us/development-tool/EV12F76A)

2. [RNWF11 UART to Cloud Add-on Board (EV12H55A)](https://www.microchip.com/en-us/development-tool/ev12h55a)
3. 1 micro-USB cable
4. 1 USB-C cable
5. A 2.4 GHz WiFi network

### Software

1. [MPLAB X IDE](https://www.microchip.com/mplabx) 6.25 or later, with the **XC-DSC** compiler (4.00 or later) and the `dsPIC33CK-MP_DFP` device pack - required to build the firmware
2. `openssl` on your `PATH` (already present on most Linux systems; on Windows it's included with [Git for Windows](https://git-scm.com/downloads/win), among other sources)
3. Either Python 3.9+ **or** PowerShell 5.1+ (Windows ships this by default; PowerShell 7+ also works on Linux) to run the provisioning scripts - pick whichever you're more comfortable with, both do the same thing
4. A serial terminal (PuTTY, Tera Term, MPLAB Data Visualizer's terminal, etc.) to watch the device's console output
5. An [/IOTCONNECT](https://www.iotconnect.io/) account

> [!NOTE]
> This project pins `dsPIC33CK-MP_DFP` 1.15.423 in
> `firmware/dspic33ck256mp508_rnwf11_iotconnect.X/bldc.X/nbproject/configurations.xml`.
> If your installed pack is a different version, MPLAB X will prompt to
> resolve it on first open - accepting the update (or editing that pinned
> version to match what you have installed) is normally enough.

## 2. Get the Quickstart Source

This firmware has to be built from source, so clone the repository:

```bash
git clone https://github.com/avnet-iotconnect/iotc-mchp-dspic33ck-lvmcb.git
cd iotc-mchp-dspic33ck-lvmcb
git submodule update --init --recursive
```

See [tools/](tools/) for the provisioning scripts you'll use in the next few steps.

## 3. Import the Device Template

This demo publishes live motor telemetry and accepts motor control commands -
import [`templates/dspic33MC-template.json`](templates/dspic33MC-template.json).

1. Log in at [console.iotconnect.io](https://console.iotconnect.io).
2. Open the **Device** module:

   <img src="media/device-page.png" width="300"/>

3. At the bottom of the page, click **Templates**:

   <img src="media/templates-button.png" width="500"/>

4. Click **Create Template**:

   <img src="media/create-template-button.png" width="300"/>

5. Click **Import**, and select
   [`templates/dspic33MC-template.json`](templates/dspic33MC-template.json)
   from the repo you cloned in Step 2:

   <img src="media/import-button.png" width="300"/>

## 4. Generate and Upload the Device Certificate

The RNWF11 board has its own USB-C port and power-select jumper
(`PC3V3` / `HOST3V3`), independent of the starter kit - this step uses it
standalone, **not** mounted on the starter kit yet.

Move the jumper to **PC3V3** and plug the RNWF11's USB-C port directly into
your PC.

<table>
  <tr>
    <td align="center"><img src="media/jumper-flashing.png" width="270"><br><b>PC3V3</b> - flashing/provisioning (this step)</td>
    <td align="center"><img src="media/jumper-running.png" width="280"><br><b>HOST3V3</b> - normal operation (Step 6)</td>
  </tr>
</table>

**Before running the command below**, find the serial port name it just
enumerated as - **the full path/name, not just the last part** (e.g.
`/dev/ttyACM0`, not `ttyACM0`):
- **Linux**: run `ls /dev/serial/by-id/` (or `dmesg | tail` right after
  plugging it in) - look for the RNWF11's MCP2200 USB-to-UART bridge, e.g.
  `/dev/ttyACM0`.
- **Windows**: open Device Manager &rarr; **Ports (COM & LPT)** - look for
  "MCP2200 USB Serial Port Emulator" and note its `COMx` number (e.g. `COM6`).

This board's firmware expects the default filenames on the RNWF11's own
filesystem (`root-ca` / `device-cert` / `device-key`, set in
`iotconnect/iotconnect_rnwf11_config.h`), so the `--ca-name`/`--cert-name`/
`--key-name` flags can stay at their defaults.

From the `iotc-mchp-dspic33ck-lvmcb` directory you cloned:

**Linux:**
```bash
cd tools
curl -fsSLO https://www.amazontrust.com/repository/AmazonRootCA1.pem
```

Replace `MYPORTNAME` with the port you found above, and `MYUNIQUEID` with a
Unique ID of your own choosing for this device - pick something memorable,
e.g. `my-desk-dspic33ck`. You'll reuse whatever you pick later, both when
creating the device in /IOTCONNECT and when resolving connection info.
```bash
python3 provision_rnwf11_cert.py --port MYPORTNAME --duid MYUNIQUEID --ca-cert-path AmazonRootCA1.pem
```
```bash
cd ..
```

**Windows (PowerShell):**
```powershell
Set-Location tools
Invoke-WebRequest https://www.amazontrust.com/repository/AmazonRootCA1.pem -OutFile AmazonRootCA1.pem
```

Replace `MYPORTNAME` with the port you found above, and `MYUNIQUEID` with a
Unique ID of your own choosing for this device - pick something memorable,
e.g. `my-desk-dspic33ck`. You'll reuse whatever you pick later, both when
creating the device in /IOTCONNECT and when resolving connection info.
```powershell
.\provision_rnwf11_cert.ps1 -Port MYPORTNAME -Duid MYUNIQUEID -CaCertPath AmazonRootCA1.pem
```
```powershell
Set-Location ..
```

This generates a self-signed device certificate, prints it to the terminal,
and uploads the CA cert, device cert, and device key to the RNWF11's own
filesystem via `AT+FS`. You'll paste the printed device certificate into the 
/IOTCONNECT console in the next step.

> [!NOTE]
> This can take up to 60 seconds to finish depending on your host PC environment.

## 5. Create the Device in /IOTCONNECT

1. After logging into your /IOTCONNECT account on
   [console.iotconnect.io](https://console.iotconnect.io), go to the
   **Device** page and click **Create Device**:

   <img src="media/create-device-button.png" width="300"/>

2. Set the Unique ID and Device Name:

   <img src="media/device-name.png" width="700"/>

   - **Unique ID**: must be the **exact same** `MYUNIQUEID` value you passed
     to `provision_rnwf11_cert.py`/`.ps1` earlier - this is the DUID and
     it's what ties everything together.
   - **Device Name**: a separate display name shown in the
     /IOTCONNECT console with looser character constraints (e.g. can use spaces)

3. Select your **Entity**:

   <img src="media/select-entity.png" width="400"/>

4. Select the template you imported earlier in
   [Step 3](#3-import-the-device-template):

   <img src="media/template-select.png" width="500"/>

5. Under **Device certificate**, choose **Use my certificate**, and paste
   the certificate PEM that `provision_rnwf11_cert.py`/`.ps1` printed:

   <img src="media/use-my-cert.png" width="400"/>

6. Click **Save & View**.

## 6. Mount the RNWF11 on the Starter Kit

Move the RNWF11's power jumper back to **HOST3V3**.

> [!IMPORTANT]
> The RNWF11 goes in **mikroBUS/Click socket B**, not A. Per
> `iotconnect/iotconnect_rnwf11_config.h`: *"UART2 is routed to the mikroBUS
> B header, where the RNWF11 is seated."*

<img src="media/mcsk-rnwf-connection.png" width="400"/>

## 7. Configure the Firmware

`provision_device_config.py`/`.ps1` resolves your device's /IOTCONNECT MQTT
connection info via /IOTCONNECT's discovery/identity API, then writes it -
along with your WiFi credentials - directly into
[`iotconnect/iotconnect_rnwf11_config.h`](firmware/dspic33ck256mp508_rnwf11_iotconnect.X/iotconnect/iotconnect_rnwf11_config.h)
so it's compiled into the firmware. It finds that file on its own, relative
to its own location, so there's nothing to copy-paste by hand.

**Linux:**
```bash
cd tools
```

Replace `MYSSID`/`MYPASSWORD` with your real WiFi credentials,
`MYCPID`/`MYENVIRONMENT` with the values under **Settings &rarr; Key
Vault** in the /IOTCONNECT console, and `MYUNIQUEID` with the same Unique ID
you used in Steps 4 and 5:
```bash
python3 provision_device_config.py --wifi-ssid MYSSID --wifi-password MYPASSWORD --cpid MYCPID --env MYENVIRONMENT --duid MYUNIQUEID
```
```bash
cd ..
```

**Windows (PowerShell):**
```powershell
Set-Location tools
```

Replace `MYSSID`/`MYPASSWORD` with your real WiFi credentials,
`MYCPID`/`MYENVIRONMENT` with the values under **Settings &rarr; Key
Vault** in the /IOTCONNECT console, and `MYUNIQUEID` with the same Unique ID
you used in Steps 4 and 5:
```powershell
.\provision_device_config.ps1 -WifiSsid MYSSID -WifiPassword MYPASSWORD -Cpid MYCPID -Env MYENVIRONMENT -Duid MYUNIQUEID
```
```powershell
Set-Location ..
```

> [!NOTE]
> Pass `--port`/`-Port` (e.g. `--port /dev/ttyACM0` or `-Port COM5`) to
> *also* push this same config live, over serial, into an already-flashed,
> already-running board's on-chip flash - useful for reconfiguring a board
> without rebuilding. It's optional; omit it and the script only updates the
> header file above.

## 8. Build the Firmware

In MPLAB X:

1. Open [`firmware/dspic33ck256mp508_rnwf11_iotconnect.X/bldc.X`](firmware/dspic33ck256mp508_rnwf11_iotconnect.X/bldc.X).
2. Clean and Build. The output `.hex` lands in
   `bldc.X/dist/default/production/`.

## 9. Flash and Run the Demo

Connect the board's power supply, and connect the included micro-USB cable
between your PC and the board's **PKOB4** port. The RNWF11 stays mounted
from Step 6.

<img src="media/mcsk-connections-flash.png" width="500"/>

Program the board via the onboard debugger (**Make and Program Device** in
MPLAB X).

To watch the boot log, open a serial terminal at 115200 8-N-1.

> [!NOTE]
> The micro-USB connection can stay on the PKOB4 port on the board for connecting to the 
> serial console. The firmware routes the serial communications through this port to 
> prevent users from needing to swap their USB connection between ports on the board.

Once connected, the firmware publishes live motor telemetry to /IOTCONNECT
every 10 seconds:

```json
{"run": 1, "st": 3, "sec": 4, "rpm": 2300, "spd": 2287, "ic": 0, "im": 12, "duty": 9821, "vdc": 15234}
```

(`run` = motor running, `st` = state machine state, `sec` = commutation
sector, `rpm`/`spd` = requested/measured speed, `ic`/`im` = requested/measured
current, `duty` = PWM duty cycle, `vdc` = DC bus voltage ADC reading.) Watch
it arrive on the device's **Live Data** tab in the /IOTCONNECT console.

### Motor Control Commands

The motor behavior is primarily driven by /IOTCONNECT C2D commands, but **SW1** on the board itself can be 
used to toggle the motor power manually.

| Command         | Parameter         | Effect                                   |
|-----------------|--------------------|-------------------------------------------|
| `motor-start`   | none               | Starts the motor (same effect as pressing SW1 while stopped) |
| `motor-stop`    | none               | Stops the motor (same effect as pressing SW1 while running)  |
| `motor-reverse` | none               | Reverses direction                        |
| `motor-speed`   | integer, `0`-`100` | Sets speed as a percent of max RPM; the motor holds this speed until the next `motor-speed` command (defaults to 50% at boot, until one is sent) |

## 10. Resources

- [AN957 Demo ReadMe MCSK.pdf](firmware/dspic33ck256mp508_rnwf11_iotconnect.X/docs) - Microchip's motor-control reference application this quickstart is built on
- [iotc-mchp-dspic33-curosity-rnwf11](https://github.com/avnet-iotconnect/iotc-mchp-dspic33-curosity-rnwf11) - a related /IOTCONNECT quickstart for the dsPIC33AK512MPS512 Curiosity board, using the same RNWF11 add-on board
- [RNWF11 UART to Cloud Add-on Board User's Guide](https://ww1.microchip.com/downloads/aemDocuments/documents/WSG/ProductDocuments/UserGuides/RNWF11-UART-to-Cloud-Add-on-Board-User-Guide-DS50003638.pdf)
- [RNWF11 Application Developer's Guide](https://onlinedocs.microchip.com/oxy/GUID-209426F5-2F78-4B3F-80A0-AD79A119381E) (AT command reference)
- [iotc-c-lib](https://github.com/avnet-iotconnect/iotc-c-lib) - /IOTCONNECT's C SDK
