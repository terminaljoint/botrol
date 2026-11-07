"""Cloud-side intent planner.

All task interpretation and application policy live in the cloud. The ESP32
receives only generic BLE-HID events and contains no app/task vocabulary.
"""

import re

INTENTS = {
    "notepad": ("notepad", ("open notepad", "launch notepad", "start notepad", "notepad")),
    "calculator": ("calc", ("open calculator", "launch calculator", "start calculator", "calculator", "calc")),
}


class PlannerError(Exception):
    pass


def _launch_sequence(run_text: str) -> dict:
    # HID usage 21 = R, modifier bit 8 = left GUI, usage 40 = Enter.
    return {
        "command": "hid_sequence",
        "payload": {
            "events": [
                {"type": "key", "code": 21, "modifiers": 8},
                {"type": "delay", "ms": 900},
                {"type": "text", "text": run_text},
                {"type": "delay", "ms": 250},
                {"type": "key", "code": 40, "modifiers": 0},
            ]
        },
    }


def plan_task(transcript: str) -> dict:
    text = " ".join(transcript.lower().strip().split())
    if not text:
        raise PlannerError("Please say or type a task.")
    matches = []
    for app, (_, phrases) in INTENTS.items():
        positions = []
        for phrase in phrases:
            for match in re.finditer(r"(?<![a-z0-9])" + re.escape(phrase) + r"(?![a-z0-9])", text):
                positions.append(match.start())
        if positions:
            matches.append((min(positions), app))
    if not matches:
        raise PlannerError("Unsupported task. Try: 'open Notepad, then open Calculator'.")
    matches.sort(key=lambda item: item[0])
    unique = []
    for _, app in matches:
        if app not in unique:
            unique.append(app)
    if len(unique) > 8:
        raise PlannerError("A task can contain at most 8 actions.")
    steps = [_launch_sequence(INTENTS[app][0]) for app in unique]
    return {"summary": " then ".join(app.title() for app in unique), "steps": steps}
