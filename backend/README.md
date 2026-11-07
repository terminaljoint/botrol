# BOTROL Cloud API

BOTROL is a multi-user cloud-coordinated ESP32 desktop-control system. Users authenticate with Google through Firebase Authentication. BOTROL Cloud owns device pairing, authorization, command planning, command queues and execution history. Firestore stores persistent account/device/task state.

## Architecture

1. A user signs in with Google on the BOTROL setup page.
2. The setup page requests a five-minute, one-use pairing code from BOTROL Cloud.
3. The browser sends that code to the ESP32 over USB serial.
4. The ESP32 exchanges the code with BOTROL Cloud and receives a unique per-device credential.
5. The ESP32 stores that credential in NVS and uses it for HTTPS polling.
6. The browser submits tasks using its Firebase ID token. The API verifies the user and checks device ownership.
7. The cloud planner creates bounded generic HID events and stores them in Firestore.
8. The ESP32 polls, executes supported events over BLE HID, and reports the result.

## API

### User routes (Firebase ID token)

- `POST /api/v1/pairing-sessions` - create a short-lived one-use pairing code.
- `GET /api/v1/devices` - list devices owned by the signed-in user.
- `DELETE /api/v1/devices/{device_id}` - revoke a device credential.
- `POST /api/v1/tasks/execute` - plan and enqueue a task for an owned device.
- `GET /api/v1/tasks/{task_id}` - read an owned task's execution status.

### Device routes (unique per-device bearer credential)

- `POST /api/v1/devices/pair` - exchange a one-use pairing code for a unique device credential. This is the only device route that does not require a pre-existing device credential.
- `GET /api/v1/devices/{device_id}/commands/next` - atomically claim the next queued command.
- `POST /api/v1/devices/{device_id}/commands/{command_id}/result` - report execution result.

### Public

- `GET /health` - service and storage status.

## Firebase / Render configuration

Create a Firebase project and enable **Authentication → Google** and **Cloud Firestore**. Register the BOTROL web app and use its public web configuration in both `dashboard.html` and `provision.html`.

Set these environment variables in the Render service:

- `FIREBASE_PROJECT_ID`: Firebase project ID (currently `botrol-cloud`).
- `FIREBASE_SERVICE_ACCOUNT_JSON`: complete service-account JSON, stored only as a Render secret.
- `BOTROL_ALLOWED_ORIGINS`: comma-separated exact website origins, for example `https://botrol.vercel.app`. Add the actual Vercel production domain and any explicitly used preview/custom domains.

Remove the old single-owner / shared-token settings after deployment:

- `BOTROL_ADMIN_UID`
- `BOTROL_ADMIN_TOKEN`
- `BOTROL_DEVICE_TOKEN`

The backend no longer uses these global credentials. Do not place the Firebase service-account JSON in GitHub, frontend code, firmware, or a chat.

Add the deployed Vercel hostname to Firebase Authentication → Settings → Authorized domains. If the Vercel hostname is not `botrol.vercel.app`, use the actual hostname in both Firebase and `BOTROL_ALLOWED_ORIGINS`.

## Firestore

The API uses these collections:

- `pairing_codes`: SHA-256 hash of one-time pairing codes, owner UID, expiry and consumed state.
- `devices`: globally unique device ID, owner UID, SHA-256 hash of its per-device credential, firmware version, status and timestamps.
- `tasks`: task ownership, device, summary and creation time.
- `commands`: validated generic command payloads, owner/device IDs, queue state and execution timestamps.

The API uses Firebase Admin SDK, which bypasses client Firestore rules. The root `firestore.rules` file denies direct browser/device reads and writes. Deploy those rules to the Firebase project. All client access must go through BOTROL Cloud.

## Pairing and credential security

- Pairing codes are random, expire after five minutes and are consumed once in a Firestore transaction.
- The ESP32 receives a random per-device bearer credential once. The cloud stores only its SHA-256 hash.
- The ESP32 stores the credential in NVS and never prints it to USB serial.
- Device credentials can be revoked from the owner's Command Center.
- A revoked physical device can be paired again using a new owner-authorized code.
- User API routes verify Firebase ID tokens and enforce Firestore ownership.
- Device command delivery is claimed transactionally to avoid two polls receiving the same queued command.
- Commands are restricted to generic HID key/text/delay events with bounds validated by both cloud and firmware.
- No arbitrary shell execution or unconstrained command payloads are accepted.

## Current implementation boundaries

- The cloud planner is deterministic rule-based intent matching, not a hosted LLM. It currently recognizes Notepad and Calculator.
- Firestore persistence and multi-user ownership are implemented in the API.
- USB-based Wi-Fi setup and automatic cloud pairing are implemented in the firmware/setup page, but must be compiled and hardware-tested before treating the firmware release as production-ready.
- The existing Windows voice agent still uses the legacy admin-token flow and is not yet compatible with multi-user Firebase accounts. Use the web Command Center for this release.
- BLE HID requires prior pairing with the target computer.
- The embedded TLS trust anchor must be verified against the live Render certificate chain during hardware validation.

## Deployment

Render service root directory: `backend`  
Build command: `pip install -r requirements.txt`  
Start command: `uvicorn main:app --host 0.0.0.0 --port $PORT`

After updating Render environment variables, redeploy. Check `/health`; it should report `storage: firestore` and `status: ok`.

## Local run

Set `FIREBASE_PROJECT_ID` and `FIREBASE_SERVICE_ACCOUNT_JSON` in your local environment, then run:

```bash
pip install -r requirements.txt
uvicorn main:app --reload
```

Open `/docs` to inspect the API.
