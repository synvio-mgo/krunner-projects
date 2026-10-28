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
        results["open_closed"] = json.loads(iface.OpenGroup(target["guid"], timeout=10))
        results["open_open"] = json.loads(iface.OpenGroup(target["guid"], timeout=10))
        results["missing"] = json.loads(iface.OpenGroup("nope", timeout=10))

    t = threading.Thread(target=dbus_calls, daemon=True)
    t.start()

    # Play the extension: first two listOpen answers say "nothing open", the
    # third says the target group is open, so OpenGroup takes both branches.
    seen = []
    open_state = []
    while t.is_alive():
        try:
            msg = recv(proc)
        except EOFError:
            break
        seen.append(msg["cmd"])
        if msg["cmd"] == "listOpen":
            reply = {"ok": True, "groups": list(open_state)}
        elif msg["cmd"] == "create":
            assert msg["args"]["title"] == target["title"]
            assert msg["args"]["urls"] == [t["url"] for t in target["tabs"]]
            open_state.append({"id": 7, "windowId": 3, "title": target["title"], "color": target["color"], "tabs": []})
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
    assert len(results["list"]) == len(groups) and results["list"][0]["open"] is False
    assert results["open_closed"].get("created") is True, results["open_closed"]
    assert results["open_open"].get("focused") is True, results["open_open"]
    assert results["missing"]["ok"] is False
    assert seen == ["listOpen", "listOpen", "create", "listOpen", "focus"], seen
    print("host test OK:", seen)


if __name__ == "__main__":
    main()
