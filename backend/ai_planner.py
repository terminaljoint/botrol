"""Cloud-side deterministic intent planner for the BOTROL prototype.

This is a rule-based parser, not an LLM. It creates generic, validated actions;
the ESP32 firmware implements only the small hardware capability interface.
"""
import re

INTENTS = {
    "notepad": ("notepad", ("open notepad", "launch notepad", "start notepad", "notepad")),
    "calculator": ("calculator", ("open calculator", "launch calculator", "start calculator", "calculator", "calc")),
}

class PlannerError(Exception):
    pass

def plan_task(transcript: str) -> dict:
    text = " ".join(transcript.lower().strip().split())
    if not text:
        raise PlannerError("Please say or type a task.")
    matches = []
    for app, phrases in INTENTS.values():
        positions = []
        for phrase in phrases:
            for match in re.finditer(r"(?<![a-z0-9])" + re.escape(phrase) + r"(?![a-z0-9])", text):
                positions.append(match.start())
        if positions:
            matches.append((min(positions), app))
    if not matches:
        if any(term in text for term in ("ping", "check connection", "check device", "test connection")):
            return {"summary": "Connection test", "steps": [{"command": "ping", "payload": {}}]}
        raise PlannerError("Unsupported task. Try: 'open Notepad, then open Calculator'.")
    matches.sort(key=lambda item: item[0])
    # Avoid silently repeating an intent from overlapping synonyms.
    unique = []
    for _, app in matches:
        if app not in unique:
            unique.append(app)
    if len(unique) > 8:
        raise PlannerError("A task can contain at most 8 actions.")
    steps = [{"command": "hid_action", "payload": {"action": "launch_app", "app": app}}
             for app in unique]
    return {"summary": " then ".join(app.title() for app in unique), "steps": steps}
