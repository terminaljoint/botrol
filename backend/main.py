import hashlib
import json
import os
import secrets
import string
from datetime import datetime, timedelta, timezone
from typing import Any
from uuid import uuid4

import firebase_admin
from firebase_admin import auth as firebase_auth, credentials, firestore
from fastapi import Depends, FastAPI, Header, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, Field

from ai_planner import plan_task, PlannerError

VERSION = "1.0.0"
PAIRING_TTL_SECONDS = 300
COMMAND_TTL_SECONDS = 120

app = FastAPI(
    title="BOTROL Cloud API",
    version=VERSION,
    description="Multi-user BOTROL cloud, secure ESP32 pairing and command relay.",
)

allowed_origins = [
    origin.strip()
    for origin in os.getenv(
        "BOTROL_ALLOWED_ORIGINS",
        "https://botrol.vercel.app,https://botrol-setup.vercel.app",
    ).split(",")
    if origin.strip()
]
app.add_middleware(
    CORSMiddleware,
    allow_origins=allowed_origins,
    allow_credentials=False,
    allow_methods=["GET", "POST", "DELETE"],
    allow_headers=["Authorization", "Content-Type"],
)

_firebase_app = None
_db = None


def firebase_db():
    global _firebase_app, _db
    if _db is not None:
        return _db
    raw = os.getenv("FIREBASE_SERVICE_ACCOUNT_JSON", "")
    project_id = os.getenv("FIREBASE_PROJECT_ID", "")
    if not raw or not project_id:
        raise HTTPException(
            status_code=503,
            detail="BOTROL Cloud is not configured with Firebase Admin credentials.",
        )
    try:
        try:
            _firebase_app = firebase_admin.get_app()
        except ValueError:
            service_account = json.loads(raw)
            cert = credentials.Certificate(service_account)
            _firebase_app = firebase_admin.initialize_app(
                cert, {"projectId": project_id}
            )
        _db = firestore.client(app=_firebase_app)
        return _db
    except HTTPException:
        raise
    except Exception as exc:
        raise HTTPException(
            status_code=503,
            detail="Firebase initialization failed. Check Render Firebase settings.",
        ) from exc


def now_dt() -> datetime:
    return datetime.now(timezone.utc)


def iso(value: Any) -> Any:
    if isinstance(value, datetime):
        return value.astimezone(timezone.utc).isoformat()
    return value


def require_user(authorization: str | None = Header(default=None)) -> str:
    supplied = authorization.removeprefix("Bearer ").strip() if authorization else ""
    if not supplied:
        raise HTTPException(status_code=401, detail="Sign in with Google first.")
    db = firebase_db()
    try:
        decoded = firebase_auth.verify_id_token(
            supplied, app=_firebase_app, check_revoked=True
        )
        return str(decoded["uid"])
    except Exception as exc:
        raise HTTPException(
            status_code=401,
            detail="Your Google sign-in expired. Sign in again.",
        ) from exc


def hash_secret(value: str) -> str:
    return hashlib.sha256(value.encode("utf-8")).hexdigest()


def device_record(device_id: str, uid: str) -> dict[str, Any]:
    snap = firebase_db().collection("devices").document(device_id).get()
    if not snap.exists:
        raise HTTPException(status_code=404, detail="Device is not paired to this account.")
    data = snap.to_dict() or {}
    if data.get("owner_uid") != uid:
        raise HTTPException(status_code=404, detail="Device is not paired to this account.")
    if data.get("status") != "active":
        raise HTTPException(status_code=403, detail="This device has been revoked.")
    return data


def require_device(device_id: str, authorization: str | None = Header(default=None)) -> dict[str, Any]:
    supplied = authorization.removeprefix("Bearer ").strip() if authorization else ""
    if not supplied:
        raise HTTPException(status_code=401, detail="Device credential missing.")
    snap = firebase_db().collection("devices").document(device_id).get()
    if not snap.exists:
        raise HTTPException(status_code=401, detail="Unknown device.")
    data = snap.to_dict() or {}
    expected = str(data.get("token_hash", ""))
    if data.get("status") != "active" or not expected:
        raise HTTPException(status_code=401, detail="Device credential revoked or invalid.")
    if not secrets.compare_digest(hash_secret(supplied), expected):
        raise HTTPException(status_code=401, detail="Invalid device credential.")
    return data


class PairingSessionRequest(BaseModel):
    label: str = Field(default="BOTROL ESP32", min_length=1, max_length=60)


class PairDeviceRequest(BaseModel):
    pairing_code: str = Field(min_length=6, max_length=12)
    device_id: str = Field(min_length=1, max_length=80, pattern=r"^[A-Za-z0-9._:-]+$")
    firmware_version: str = Field(default="unknown", max_length=40)


class TaskRequest(BaseModel):
    device_id: str = Field(min_length=1, max_length=80, pattern=r"^[A-Za-z0-9._:-]+$")
    transcript: str = Field(min_length=1, max_length=2000)


class CommandResult(BaseModel):
    status: str = Field(pattern=r"^(completed|failed)$")
    message: str = Field(default="", max_length=500)


def normalize_command(command: str, payload: dict[str, Any]) -> tuple[str, dict[str, Any]]:
    if command != "hid_sequence" or set(payload.keys()) != {"events"}:
        raise HTTPException(status_code=422, detail="Only generic hid_sequence payloads are accepted")
    events = payload.get("events")
    if not isinstance(events, list) or not 1 <= len(events) <= 12:
        raise HTTPException(status_code=422, detail="events must contain 1 to 12 HID events")
    clean_events = []
    for event in events:
        if not isinstance(event, dict):
            raise HTTPException(status_code=422, detail="Each HID event must be an object")
        kind = event.get("type")
        if kind == "key" and set(event) == {"type", "code", "modifiers"}:
            code, modifiers = event["code"], event["modifiers"]
            if (
                not isinstance(code, int) or isinstance(code, bool)
                or not (4 <= code <= 39 or 40 <= code <= 44 or 79 <= code <= 82)
                or not isinstance(modifiers, int) or isinstance(modifiers, bool)
                or not 0 <= modifiers <= 15
            ):
                raise HTTPException(status_code=422, detail="Key usage or modifier is outside supported HID bounds")
            clean_events.append({"type": "key", "code": code, "modifiers": modifiers})
        elif kind == "text" and set(event) == {"type", "text"}:
            value = event["text"]
            if (
                not isinstance(value, str) or not 1 <= len(value) <= 80
                or not all(32 <= ord(char) <= 126 for char in value)
            ):
                raise HTTPException(status_code=422, detail="Text must be 1 to 80 printable ASCII characters")
            clean_events.append({"type": "text", "text": value})
        elif kind == "delay" and set(event) == {"type", "ms"}:
            duration = event["ms"]
            if not isinstance(duration, int) or isinstance(duration, bool) or not 0 <= duration <= 2000:
                raise HTTPException(status_code=422, detail="Delay must be between 0 and 2000 ms")
            clean_events.append({"type": "delay", "ms": duration})
        else:
            raise HTTPException(status_code=422, detail="Malformed or unsupported generic HID event")
    return "hid_sequence", {"events": clean_events}


@app.get("/health")
def health() -> dict[str, Any]:
    configured = bool(os.getenv("FIREBASE_SERVICE_ACCOUNT_JSON") and os.getenv("FIREBASE_PROJECT_ID"))
    return {
        "service": "BOTROL Cloud API",
        "status": "ok" if configured else "degraded",
        "storage": "firestore" if configured else "unconfigured",
        "version": VERSION,
    }


@app.post("/api/v1/pairing-sessions")
def create_pairing_session(body: PairingSessionRequest, uid: str = Depends(require_user)) -> dict[str, Any]:
    db = firebase_db()
    alphabet = string.ascii_uppercase + string.digits
    code = "".join(secrets.choice(alphabet) for _ in range(8))
    created = now_dt()
    expires = created + timedelta(seconds=PAIRING_TTL_SECONDS)
    db.collection("pairing_codes").document(hash_secret(code)).set({
        "owner_uid": uid,
        "label": body.label,
        "created_at": created,
        "expires_at": expires,
        "consumed": False,
    })
    return {
        "ok": True,
        "pairing_code": code,
        "expires_at": expires.isoformat(),
        "expires_in_seconds": PAIRING_TTL_SECONDS,
        "instructions": "Send this one-time code to your ESP32 over its local USB setup connection. It can be used once.",
    }


@app.post("/api/v1/devices/pair")
def pair_device(body: PairDeviceRequest) -> dict[str, Any]:
    db = firebase_db()
    code = body.pairing_code.strip().upper()
    code_ref = db.collection("pairing_codes").document(hash_secret(code))
    device_ref = db.collection("devices").document(body.device_id)
    token = secrets.token_urlsafe(36)
    transaction = db.transaction()

    @firestore.transactional
    def consume_pairing(txn):
        code_snap = code_ref.get(transaction=txn)
        if not code_snap.exists:
            raise HTTPException(status_code=401, detail="Pairing code is invalid or expired.")
        pairing = code_snap.to_dict() or {}
        expiry = pairing.get("expires_at")
        if pairing.get("consumed") or not expiry or expiry <= now_dt():
            raise HTTPException(status_code=401, detail="Pairing code is invalid, expired or already used.")
        existing = device_ref.get(transaction=txn)
        if existing.exists and (existing.to_dict() or {}).get("status") == "active":
            raise HTTPException(status_code=409, detail="This ESP32 is already paired. Revoke it from its owner account before pairing again.")
        created = now_dt()
        txn.update(code_ref, {"consumed": True, "consumed_at": created, "device_id": body.device_id})
        txn.set(device_ref, {
            "device_id": body.device_id,
            "owner_uid": pairing["owner_uid"],
            "label": pairing.get("label", "BOTROL ESP32"),
            "firmware_version": body.firmware_version,
            "token_hash": hash_secret(token),
            "status": "active",
            "paired_at": created,
            "last_seen": created,
        })
        return pairing["owner_uid"]

    owner_uid = consume_pairing(transaction)
    return {
        "ok": True,
        "device_id": body.device_id,
        "device_token": token,
        "owner_uid": owner_uid,
        "message": "Pairing successful. Save this device credential securely in ESP32 NVS. It is shown only once.",
    }


@app.get("/api/v1/devices")
def list_devices(uid: str = Depends(require_user)) -> dict[str, Any]:
    docs = firebase_db().collection("devices").where("owner_uid", "==", uid).stream()
    devices = []
    for snap in docs:
        item = snap.to_dict() or {}
        devices.append({
            "device_id": snap.id,
            "label": item.get("label", "BOTROL ESP32"),
            "firmware_version": item.get("firmware_version", "unknown"),
            "status": item.get("status", "unknown"),
            "paired_at": iso(item.get("paired_at")),
            "last_seen": iso(item.get("last_seen")),
        })
    return {"ok": True, "devices": devices}


@app.delete("/api/v1/devices/{device_id}")
def revoke_device(device_id: str, uid: str = Depends(require_user)) -> dict[str, bool]:
    device_record(device_id, uid)
    firebase_db().collection("devices").document(device_id).update({
        "status": "revoked",
        "revoked_at": now_dt(),
        "token_hash": "",
    })
    return {"ok": True}


@app.get("/api/v1/devices/{device_id}/commands/next")
def get_next_command(device_id: str, device: dict[str, Any] = Depends(require_device)) -> dict[str, Any]:
    db = firebase_db()
    device_ref = db.collection("devices").document(device_id)
    device_ref.update({"last_seen": now_dt()})
    docs = db.collection("commands").where("device_id", "==", device_id).stream()
    pending = []
    current = now_dt()
    for snap in docs:
        item = snap.to_dict() or {}
        if item.get("status") != "queued":
            continue
        expiry = item.get("expires_at")
        if expiry and expiry <= current:
            snap.reference.update({"status": "expired"})
            continue
        pending.append((snap, item))
    if not pending:
        return {"ok": True, "command": None}
    pending.sort(key=lambda pair: (pair[1].get("created_at") or current, pair[1].get("step") or 0))
    for candidate, candidate_data in pending:
        transaction = db.transaction()

        @firestore.transactional
        def claim(txn):
            fresh = candidate.reference.get(transaction=txn)
            if not fresh.exists:
                return False
            fresh_data = fresh.to_dict() or {}
            if fresh_data.get("status") != "queued":
                return False
            expiry = fresh_data.get("expires_at")
            claimed_at = now_dt()
            if expiry and expiry <= claimed_at:
                txn.update(candidate.reference, {"status": "expired"})
                return False
            txn.update(candidate.reference, {"status": "delivered", "delivered_at": claimed_at})
            return True

        if claim(transaction):
            candidate_data["status"] = "delivered"
            candidate_data["delivered_at"] = now_dt()
            candidate_data["created_at"] = iso(candidate_data.get("created_at"))
            candidate_data["expires_at"] = iso(candidate_data.get("expires_at"))
            return {"ok": True, "command": candidate_data}
    return {"ok": True, "command": None}


@app.post("/api/v1/devices/{device_id}/commands/{command_id}/result")
def report_result(device_id: str, command_id: str, body: CommandResult,
                  device: dict[str, Any] = Depends(require_device)) -> dict[str, bool]:
    ref = firebase_db().collection("commands").document(command_id)
    snap = ref.get()
    if not snap.exists:
        raise HTTPException(status_code=404, detail="Command not found.")
    command = snap.to_dict() or {}
    if command.get("device_id") != device_id or command.get("status") != "delivered":
        raise HTTPException(status_code=409, detail="Command is not awaiting a result for this device.")
    ref.update({"status": body.status, "result": body.message, "completed_at": now_dt()})
    firebase_db().collection("devices").document(device_id).update({"last_seen": now_dt()})
    return {"ok": True}


def enqueue_command(db, uid: str, device_id: str, command: str, payload: dict[str, Any],
                    task_id: str, step: int, summary: str) -> dict[str, Any]:
    command_id = str(uuid4())
    created = now_dt()
    row = {
        "command_id": command_id,
        "task_id": task_id,
        "owner_uid": uid,
        "step": step,
        "task_summary": summary,
        "device_id": device_id,
        "command": command,
        "payload": payload,
        "status": "queued",
        "result": "",
        "created_at": created,
        "expires_at": created + timedelta(seconds=COMMAND_TTL_SECONDS),
    }
    db.collection("commands").document(command_id).set(row)
    return {
        "command_id": command_id,
        "command": command,
        "payload": payload,
        "status": "queued",
    }


@app.post("/api/v1/tasks/execute")
def execute_task(body: TaskRequest, uid: str = Depends(require_user)) -> dict[str, Any]:
    device_record(body.device_id, uid)
    try:
        plan = plan_task(body.transcript)
    except PlannerError as exc:
        raise HTTPException(status_code=422, detail=str(exc)) from exc
    db = firebase_db()
    task_id = str(uuid4())
    db.collection("tasks").document(task_id).set({
        "task_id": task_id,
        "owner_uid": uid,
        "device_id": body.device_id,
        "summary": plan["summary"],
        "status": "queued",
        "created_at": now_dt(),
    })
    steps = []
    for index, step in enumerate(plan["steps"], start=1):
        command, payload = normalize_command(step["command"], step["payload"])
        steps.append(enqueue_command(db, uid, body.device_id, command, payload,
                                     task_id, index, plan["summary"]))
    return {"ok": True, "task": {"task_id": task_id, "summary": plan["summary"], "steps": steps}}


@app.get("/api/v1/tasks/{task_id}")
def get_task_status(task_id: str, uid: str = Depends(require_user)) -> dict[str, Any]:
    task_snap = firebase_db().collection("tasks").document(task_id).get()
    if not task_snap.exists or (task_snap.to_dict() or {}).get("owner_uid") != uid:
        raise HTTPException(status_code=404, detail="Task not found.")
    docs = firebase_db().collection("commands").where("task_id", "==", task_id).stream()
    rows = []
    for snap in docs:
        item = snap.to_dict() or {}
        rows.append({
            "command_id": item.get("command_id"),
            "command": item.get("command"),
            "status": item.get("status"),
            "result": item.get("result", ""),
            "step": item.get("step"),
        })
    rows.sort(key=lambda item: item.get("step") or 0)
    return {"ok": True, "task_id": task_id, "steps": rows}
