# BOTROL

**BOTROL** is an ESP32-powered, cloud-coordinated desktop automation system developed under **Terminal Joint**.

The project connects a physical ESP32 device to the BOTROL cloud service. The cloud side coordinates device communication and commands, while the ESP32 acts as the hardware endpoint. The system is designed around a hybrid communication model: WebSocket for live communication and HTTPS polling as a fallback.

> **Project status:** Development project. Features, deployment endpoints, and setup flows may change between versions. Always use the current firmware and deployment configuration from this repository.

## Table of contents

- [What BOTROL does](#what-botrol-does)
- [Architecture](#architecture)
- [Hardware and software](#hardware-and-software)
- [Quick setup](#quick-setup)
- [Step-by-step device provisioning](#step-by-step-device-provisioning)
- [Cloud service](#cloud-service)
- [How communication works](#how-communication-works)
- [Troubleshooting](#troubleshooting)
- [Security notes](#security-notes)
- [License and ownership](#license-and-ownership)

## What BOTROL does

BOTROL is built to let an ESP32 device communicate with an online service that coordinates automation tasks.

The system is intended to provide:

- **ESP32 as the hardware endpoint:** the device connects to Wi-Fi and communicates with the cloud.
- **Cloud-based coordination:** device registration, status reporting, command delivery, and command queuing are handled by the service.
- **Browser-based provisioning:** the setup website provides a browser interface for the initial device configuration using Web Bluetooth.
- **Desktop interaction:** the ESP32 firmware can use Bluetooth HID functionality for supported keyboard-style interactions with a paired computer.
- **Hybrid networking:** WebSocket is intended for live communication, with HTTPS polling available as a fallback.

BOTROL is not simply a script running locally on a computer. Its design separates the hardware endpoint, cloud coordination, and setup interface.

## Architecture

```text
                 ┌─────────────────────────┐
                 │      BOTROL Setup       │
                 │  Browser-based setup    │
                 │  Web Bluetooth          │
                 └────────────┬────────────┘
                              │ Provisioning
                              ▼
┌──────────────────┐    Wi-Fi / Internet    ┌──────────────────────┐
│   ESP32-WROOM-32 │ ◄────────────────────► │     BOTROL Cloud     │
│                  │                         │                      │
│ Hardware endpoint│   WebSocket / HTTPS    │ Device registration  │
│ Wi-Fi connection │                         │ Device status        │
│ BLE HID support  │                         │ Command coordination │
└─────────┬────────┘                         └──────────┬───────────┘
          │                                            │
          │ Bluetooth HID                             │
          ▼                                            │
┌──────────────────┐                                   │
│ Windows computer │ ◄─────────────────────────────────┘
│ Desktop actions  │       Commands coordinated by
└──────────────────┘       the BOTROL system
```

The diagram describes the intended system roles. Exact command capabilities depend on the firmware and cloud version being used.

## Hardware and software

### Hardware

- ESP32 DevKit V1
- ESP32-WROOM-32 module
- USB data cable
- A computer with Bluetooth support for the supported HID workflow
- A Wi-Fi network with internet access

### Software and services

- A modern Chromium-based browser such as Google Chrome or Microsoft Edge for Web Bluetooth provisioning
- BOTROL setup website: <https://botrol-setup.vercel.app/>
- BOTROL cloud service: <https://botrol-cloud.onrender.com>
- Firmware and source code from this repository

## Quick setup

The following is the high-level first-time setup flow:

1. Obtain the BOTROL firmware source from this repository.
2. Build and flash the firmware for the ESP32-WROOM-32 using the build configuration supplied with that firmware version.
3. Connect the ESP32 to USB power.
4. Open the BOTROL setup website in a supported Chromium-based browser.
5. Use the website's device connection/provisioning flow and grant the requested Bluetooth permission.
6. Configure the device's Wi-Fi connection and the cloud settings requested by the current setup interface.
7. Allow the ESP32 to connect to the internet and register with BOTROL Cloud.
8. Verify that the device appears online before sending commands.

> **Important:** Web Bluetooth requires a supported browser and a secure context (HTTPS or localhost). Browser support and Bluetooth permissions vary by operating system. Do not use a browser that does not support Web Bluetooth for this setup flow.

## Step-by-step device provisioning

### Step 1: Prepare the ESP32

1. Use an ESP32 DevKit V1 / ESP32-WROOM-32 board.
2. Connect it to your computer using a USB data cable.
3. Make sure the computer detects the board's serial/USB interface.
4. Use the firmware source and its included build configuration from this repository.
5. Select the matching ESP32 board and upload the firmware using the toolchain specified by that firmware project.

Do not flash firmware intended for a different ESP32 variant. The exact build command and source entry point must match the checked-out firmware version.

### Step 2: Open the setup website

1. Open <https://botrol-setup.vercel.app/> in Google Chrome or Microsoft Edge.
2. Keep the ESP32 powered and within Bluetooth range.
3. Start the device connection or provisioning flow shown on the page.
4. When the browser displays a Bluetooth device picker, select the BOTROL device exposed by the firmware.
5. Approve the browser's Bluetooth permission request.

If the device does not appear, check that the firmware is running and that Bluetooth provisioning is enabled.

### Step 3: Configure Wi-Fi

1. Follow the setup page's Wi-Fi configuration screen.
2. Select or enter the Wi-Fi network the ESP32 should use.
3. Enter the network password carefully.
4. Submit the configuration.
5. Wait for the ESP32 to connect.

The ESP32 needs internet access to reach BOTROL Cloud. A successful connection to the Wi-Fi access point alone does not prove that cloud registration has succeeded.

### Step 4: Connect to BOTROL Cloud

1. Use the cloud endpoint configured by the current setup interface: <https://botrol-cloud.onrender.com>.
2. Allow the device to complete its registration process.
3. Wait for the first successful status check or poll.
4. Confirm that the device is shown as registered/online in the cloud interface or service logs.

A device that has joined Wi-Fi but cannot reach the cloud is not fully connected.

### Step 5: Verify the device

Check the following before testing automation:

- The ESP32 has joined the intended Wi-Fi network.
- The device has completed cloud registration.
- The cloud service recognizes the device.
- The device is sending status updates or polling for commands.
- A basic connectivity command such as `PING`, where supported by the running version, receives a response.

### Step 6: Test desktop interaction

1. Ensure the computer's Bluetooth is enabled.
2. Pair or connect the ESP32 using the Bluetooth HID procedure supported by the firmware.
3. Start with a harmless test action.
4. Confirm that the computer receives the expected keyboard-style input.
5. Only then test additional supported commands.

Bluetooth HID makes the ESP32 behave like an input device. Use it only on computers you own or are authorized to control.

## Cloud service

The BOTROL cloud service is the coordination layer between registered devices and the setup/command workflow.

The development deployment has used:

- **Setup website:** <https://botrol-setup.vercel.app/>
- **Cloud API:** <https://botrol-cloud.onrender.com>

The cloud implementation has used Python with FastAPI and Uvicorn. The development setup has included device registration, device polling, and command queue handling.

For a self-hosted deployment:

1. Obtain the cloud source code and its dependency/configuration files from the repository.
2. Install the Python dependencies declared by the project.
3. Configure the required environment variables using the project's configuration files or deployment instructions.
4. Start the FastAPI application using the entry point defined by the source.
5. Configure the setup frontend to use the correct cloud API base URL.
6. Deploy the frontend and backend separately if using separate hosting services.
7. Verify registration and command polling before connecting a physical device.

Do not guess environment-variable names or start commands. Use the actual configuration and application entry point in the version being deployed.

## How communication works

BOTROL uses a hybrid communication approach:

- **WebSocket:** intended for persistent, low-latency communication while a live connection is available.
- **HTTPS polling:** provides a fallback path for devices that need to periodically check the cloud for pending work.
- **Device registration:** allows the cloud service to recognize a device.
- **Command queue:** allows the service to coordinate commands for a device.
- **Status reporting:** allows the service to track whether a device is communicating.

The exact retry intervals, message schema, authentication mechanism, and command set are implementation details and may change with firmware or cloud releases.

## Troubleshooting

| Problem | What to check |
|---|---|
| Browser cannot find the ESP32 | Use Chrome or Edge, enable Bluetooth, keep the board nearby, and confirm provisioning is enabled in the firmware. |
| Bluetooth permission is denied | Reopen the setup page and grant permission when prompted. Check browser and OS Bluetooth permissions. |
| ESP32 cannot join Wi-Fi | Recheck the SSID and password. Confirm the network is available and compatible with the ESP32's 2.4 GHz Wi-Fi support. |
| Wi-Fi works but cloud registration fails | Check internet access, the configured cloud URL, server availability, and device authentication/configuration. |
| HTTP 401 | Check whether the device and server are using matching authentication credentials. |
| HTTP 503 | Check whether the cloud service is running and whether required server-side configuration is present. |
| Device appears offline | Check power, Wi-Fi connectivity, cloud reachability, registration state, and the device's latest poll/status logs. |
| Desktop does not receive keyboard input | Check Bluetooth pairing, HID support in the firmware, and the computer's Bluetooth settings. |

When reporting an issue, include the firmware version, board type, relevant serial logs, cloud response/status code, and the steps that reproduce the problem. Never post passwords, private tokens, or secret keys.

## Security notes

- Never commit Wi-Fi passwords, API tokens, private keys, or production secrets to Git.
- Use environment variables or the project's intended secret-management mechanism for cloud credentials.
- Do not share device credentials in screenshots, logs, or public issue reports.
- Restrict administrative commands and cloud management endpoints to authorized users.
- Test desktop automation only on systems you own or have permission to control.
- Treat the project as development software unless a release explicitly states otherwise.

## License and ownership

BOTROL is an open-source / Free Software project released under the **GNU Affero General Public License v3.0 (AGPL-3.0)**. See [LICENSE](./LICENSE) for the complete license text.

Copyright and project authorship notices are maintained in the repository's dedicated legal/notice files. Please preserve those notices when redistributing or modifying the project.

---

**BOTROL**  
A Terminal Joint project.
