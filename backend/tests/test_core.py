import pytest
from fastapi import HTTPException

from ai_planner import PlannerError, plan_task
from main import normalize_command


def test_planner_builds_generic_notepad_hid_sequence():
    plan = plan_task("Please open Notepad")
    assert plan["summary"] == "Notepad"
    assert plan["steps"][0]["command"] == "hid_sequence"
    assert plan["steps"][0]["payload"]["events"][-1] == {
        "type": "key", "code": 40, "modifiers": 0
    }


def test_planner_rejects_unsupported_action():
    with pytest.raises(PlannerError):
        plan_task("delete all files")


def test_normalizer_accepts_bounded_generic_events():
    command, payload = normalize_command("hid_sequence", {
        "events": [
            {"type": "key", "code": 21, "modifiers": 8},
            {"type": "delay", "ms": 100},
            {"type": "text", "text": "notepad"},
        ]
    })
    assert command == "hid_sequence"
    assert len(payload["events"]) == 3


@pytest.mark.parametrize("payload", [
    {"events": [{"type": "text", "text": "x" * 81}]},
    {"events": [{"type": "key", "code": 999, "modifiers": 0}]},
    {"events": [{"type": "delay", "ms": 5000}]},
    {"events": [{"type": "shell", "command": "whoami"}]},
    {"events": []},
])
def test_normalizer_rejects_unsafe_or_malformed_events(payload):
    with pytest.raises(HTTPException):
        normalize_command("hid_sequence", payload)
