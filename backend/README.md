# BOTROL Cloud API

BOTROL is a cloud-coordinated IoT desktop-control prototype. The cloud interprets supported user intents, creates structured commands, queues them, and serves them to an authenticated ESP32 endpoint over HTTPS. The ESP32 performs only a narrow set of locally validated BLE-HID capabilities.

## Architecture

1. Windows voice panel submits a transcript to BOTROL Cloud using the admin credential.
2. The cloud-side deterministic planner converts supported intents into structured actions.
3. The cloud validates the action and queues a uniquely identified command with a two-minute expiry.
4. ESP32 polls the cloud using its device credential, validates the received schema and local capability allowlist, then performs the hardware action.
5. ESP32 reports the result to the cloud.

## Cloud routes

- `GET /health`
- `POST /api/v1/devices/register`
- `GET /api/v1/devices/{device_id}/commands/next`
- `POST /api/v1/devices/{device_id}/commands/{command_id}/result`
- Admin routes under `/api/v1/admin/*`

## Command model

The firmware does not contain task-specific command names such as `ble_open_notepad`. The cloud emits a generic capability command:

```json
{
  "command": "hid_action",
  "payload": {
    "action": "launch_app",
    "app": "notepad"
  }
}
```

The firmware accepts only the `launch_app` action and the locally approved application identifiers `notepad` and `calculator`. It rejects unknown commands, malformed payloads and unsupported applications. It does not execute arbitrary shell commands, scripts, URLs or raw keyboard sequences.

## Security controls in this prototype

- HTTPS certificate validation in ESP32 firmware using a trusted CA root (no insecure TLS bypass).
- Separate admin and device bearer credentials, compared in constant time by the API.
- Admin-only task submission and status routes.
- Device-only registration, command polling and result reporting routes.
- Strict command and payload allowlists on the cloud and device.
- UUID command identifiers and one-time queued-to-delivered state transition.
- Two-minute expiry for commands that remain queued.
- Result reports are accepted only for commands in the delivered state.
- No arbitrary command execution.

## Deployment configuration

Render service root directory: `backend`  
Build command: `pip install -r requirements.txt`  
Start command: `uvicorn main:app --host 0.0.0.0 --port $PORT`

Required Render environment variables:

- `BOTROL_DEVICE_TOKEN`
- `BOTROL_ADMIN_TOKEN`
- `BOTROL_ALLOWED_ORIGINS=https://botrol-setup.vercel.app`

Never commit real credentials. Provision the same device token into the ESP32 privately and store the admin token in Windows Credential Manager.

## Prototype limitations

- The current planner is deterministic rule-based intent matching, not an LLM or autonomous AI model.
- Device and command state is held in process memory. Render restarts or instance sleep can clear queued commands and device registrations; persistent database storage is a future requirement.
- The current prototype supports only ping, Notepad and Calculator launch actions.
- ESP32 certificate trust must be checked against the live Render certificate chain during device deployment. If the hosting CA changes, update the embedded trusted root.
- A global device token is used in this single-device prototype. A multi-device deployment should use unique per-device credentials and rotation/revocation.
- BLE HID requires prior pairing with the target computer.

## Local run

Set `BOTROL_DEVICE_TOKEN` and `BOTROL_ADMIN_TOKEN`, then run:

```bash
uvicorn main:app --reload
```

Open `/docs` to inspect the API.
