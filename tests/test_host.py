#!/usr/bin/env python3
"""End-to-end test of krunner_projects_host.py without Chrome.

Spawns the host with pipes (playing the role of the extension), calls its D-Bus
methods from a second thread, and answers the host's native-messaging requests
with canned data.  Run:  python3 tests/test_host.py
"""
import json
import os
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

import dbus

HERE = Path(__file__).resolve().parent
HOST = HERE.parent / "host" / "krunner_projects_host.py"
BUS = "be.synvio.KRunnerProjects"


def send(proc, obj):
    data = json.dumps(obj).encode()
    proc.stdin.write(struct.pack("<I", len(data)) + data)
    proc.stdin.flush()


def recv(proc):
    hdr = proc.stdout.read(4)
    if len(hdr) < 4:
        raise EOFError
    (ln,) = struct.unpack("<I", hdr)
    return json.loads(proc.stdout.read(ln))


def main():
    # Chrome's own host may already own the bus name; then our test host would
    # exit and the calls would land in the real Chrome.  Use a private bus.
    if dbus.SessionBus().name_has_owner(BUS) and not os.environ.get("KRUNNER_PROJECTS_PRIVATE_BUS"):
        os.environ["KRUNNER_PROJECTS_PRIVATE_BUS"] = "1"
        os.execvp("dbus-run-session", ["dbus-run-session", "--", sys.executable, *sys.argv])

    groups = json.loads(subprocess.check_output([sys.executable, "-I", str(HERE.parent / "host" / "chromegroups.py"), "--json"]))
    assert groups, "need at least one saved group in the Chrome profile"
    target = groups[0]
    print(f"saved groups: {len(groups)}, target: {target['title']!r} ({len(target['tabs'])} tabs)")

    proc = subprocess.Popen([sys.executable, "-I", str(HOST)], stdin=subprocess.PIPE, stdout=subprocess.PIPE)
    send(proc, {"hello": "test", "version": "0"})

    results = {}

    def dbus_calls():
        bus = dbus.SessionBus()
        for _ in range(50):
            if bus.name_has_owner(BUS):
                break
            time.sleep(0.1)
        obj = bus.get_object(BUS, "/Chrome")
        iface = dbus.Interface(obj, "be.synvio.KRunnerProjects.Chrome")
        results["ping"] = bool(iface.Ping())
        results["list"] = json.loads(iface.ListGroups(timeout=10))
        # closed, but a window with its tabs sits in the recently-closed list -> restore
        state["recent"] = [{"lastModified": 1, "window": {"sessionId": "w1", "tabs": [{"url": t["url"]} for t in target["tabs"]]}}]
        results["restore"] = json.loads(iface.OpenGroup(target["guid"], timeout=10))
        # now open -> focus
        results["open_open"] = json.loads(iface.OpenGroup(target["guid"], timeout=10))
        # closed and gone from the recently-closed list -> nothing is created
        state["open"].clear()
        state["recent"] = [{"lastModified": 1, "tab": {"sessionId": "t1", "url": "https://example.invalid/"}}]
        results["not_restorable"] = json.loads(iface.OpenGroup(target["guid"], timeout=10))
        results["recreate"] = json.loads(iface.RecreateGroup(target["guid"], timeout=10))
        results["missing"] = json.loads(iface.OpenGroup("nope", timeout=10))

    t = threading.Thread(target=dbus_calls, daemon=True)
    t.start()

    # Play the extension.
    seen = []
    state = {"open": [], "recent": []}
    open_entry = {"id": 7, "windowId": 3, "title": target["title"], "color": target["color"], "tabs": []}
    while t.is_alive():
        try:
            msg = recv(proc)
        except EOFError:
            break
        seen.append(msg["cmd"])
        if msg["cmd"] == "listOpen":
            reply = {"ok": True, "groups": list(state["open"])}
        elif msg["cmd"] == "recentlyClosed":
            reply = {"ok": True, "sessions": list(state["recent"])}
        elif msg["cmd"] == "restore":
            assert msg["args"]["sessionId"] == "w1", msg
            state["open"].append(open_entry)
            reply = {"ok": True, "restored": True, "relinked": True}
        elif msg["cmd"] == "create":
            assert msg["args"]["title"] == target["title"]
            assert msg["args"]["urls"] == [t["url"] for t in target["tabs"]]
            state["open"].append(open_entry)
            reply = {"ok": True, "created": True, "groupId": 7}
        elif msg["cmd"] == "focus":
            assert msg["args"]["groupId"] == 7
            reply = {"ok": True, "focused": True}
        else:
            reply = {"ok": False, "error": "unexpected"}
        send(proc, {"id": msg["id"], "result": reply})
        t.join(0.05)

    proc.stdin.close()
    proc.wait(timeout=5)

    assert results["ping"] is True
    assert len(results["list"]) == len(groups), (len(results["list"]), len(groups))
    assert all(g["open"] is False for g in results["list"]), [g["title"] for g in results["list"] if g["open"]]
    assert results["restore"].get("focused") and results["restore"].get("restored"), results["restore"]
    assert results["open_open"].get("focused") is True and "restored" not in results["open_open"], results["open_open"]
    assert results["not_restorable"] == {"ok": False, "reason": "closed_not_restorable", "title": target["title"]}, results["not_restorable"]
    assert results["recreate"].get("created") is True, results["recreate"]
    assert results["missing"]["ok"] is False
    assert seen == ["listOpen",
                    "listOpen", "recentlyClosed", "restore", "listOpen", "focus",
                    "listOpen", "focus",
                    "listOpen", "recentlyClosed",
                    "create"], seen
    print("host test OK:", seen)


if __name__ == "__main__":
    main()
