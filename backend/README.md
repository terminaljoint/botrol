# BOTROL Cloud API

BOTROL is a cloud-coordinated desktop-control prototype. Task interpretation, application selection, action planning and command construction happen in BOTROL Cloud. The ESP32 is a generic BLE-HID event executor.

## Architecture

1. The Windows voice agent submits a task transcript to BOTROL Cloud using the admin credential.
2. The cloud-side planner interprets the task and builds a generic HID event sequence.
3. The cloud validates the sequence and queues it for the authenticated ESP32.
4. The ESP32 polls over HTTPS, validates generic event bounds and emits BLE keyboard events.
5. The ESP32 reports the execution result to the cloud.

## Firmware boundary

The firmware contains **no task names, application names, task-specific command handlers, intent parsing, or task planning**. It does not know what Notepad or Calculator is. It implements only three generic hardware event types:

- `key`: a USB HID keyboard usage code plus modifier bitmask
- `text`: printable ASCII text
- `delay`: a bounded pause in milliseconds

Example cloud-to-device payload:

```json
{
  "command": "hid_sequence",
  "payload": {
    "events": [
      {"type": "key", "code": 21, "modifiers": 8},
      {"type": "delay", "ms": 900},
      {"type": "text", "text": "notepad"},
      {"type": "delay", "ms": 250},
      {"type": "key", "code": 40, "modifiers": 0}
    ]
  }
}
```

The example sequence is planned by the cloud. The ESP32 only interprets generic HID usages and emits keyboard events. HID usage 21 is the R key, modifier bit 8 is GUI, and usage 40 is Enter.

## Cloud routes

- `GET /health`
- `POST /api/v1/devices/register`
- `GET /api/v1/devices/{device_id}/commands/next`
- `POST /api/v1/devices/{device_id}/commands/{command_id}/result`
- Admin routes under `/api/v1/admin/*`

## Security controls and limits

- HTTPS certificate validation in ESP32 firmware using a trusted CA root.
- Separate admin and device bearer credentials, compared in constant time by the API.
- Admin-only task submission and status routes.
- Device-only registration, command polling and result reporting routes.
- Cloud and device both validate event structure, key usage ranges, modifier masks, printable text and delay limits.
- Maximum 12 events per sequence; text length is limited to 80 characters; each delay is limited to 2 seconds.
- UUID command identifiers and queued-to-delivered state transition.
- Two-minute expiry for commands that remain queued.
- No firmware-side task-specific command vocabulary.

**Important security boundary:** generic keyboard injection is powerful. Only trusted cloud-side planners/admins should be allowed to enqueue sequences. The device-side bounds limit malformed or oversized payloads but are not a substitute for cloud authorization or human approval for sensitive actions.

## Deployment configuration

Render service root directory: `backend`  
Build command: `pip install -r requirements.txt`  
Start command: `uvicorn main:app --host 0.0.0.0 --port $PORT`

Required Render environment variables:

- `BOTROL_DEVICE_TOKEN`
- `BOTROL_ADMIN_TOKEN`
- `BOTROL_ALLOWED_ORIGINS=https://botrol-setup.vercel.app`

Never commit real credentials. Provision the device token into the ESP32 privately and store the admin token in Windows Credential Manager.

## Prototype limitations

- The planner is deterministic rule-based intent matching, not an LLM or autonomous AI model.
- The current cloud planner recognizes only Notepad and Calculator intents. This restriction is in the cloud, not in the ESP32 firmware.
- Device and command state is held in process memory. Render restarts or instance sleep can clear queued commands and device registrations.
- A global device token is used in this single-device prototype. Multi-device deployment should use unique per-device credentials and rotation/revocation.
- BLE HID requires prior pairing with the target computer.
- The embedded CA trust anchor must be validated against the live Render certificate chain before deployment.

## Local run

Set `BOTROL_DEVICE_TOKEN` and `BOTROL_ADMIN_TOKEN`, then run:

```bash
uvicorn main:app --reload
```

Open `/docs` to inspect the API.
