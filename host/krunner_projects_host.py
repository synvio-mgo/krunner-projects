#!/usr/bin/env python3
"""Native messaging host for the KRunner Projects Chrome extension.

Chrome starts this process when the extension connects.  It does two things:

* speaks the Chrome native messaging protocol on stdin/stdout, and
* owns the session D-Bus name ``be.synvio.KRunnerProjects`` so the KRunner
  plugin (or anything else, e.g. ``qdbus``) can ask Chrome to open a saved tab
  group.

D-Bus interface ``be.synvio.KRunnerProjects.Chrome`` at ``/Chrome``:

    ListGroups()        -> JSON list of saved groups, each with an ``open`` flag
    OpenGroup(guid)     -> JSON result; focuses the group if open, else restores it
                           from Chrome's recently-closed list (Chrome re-links the
                           restored tabs to the existing saved group, so no copy is
                           made).  If neither works the result is
                           {"ok": false, "reason": "closed_not_restorable"}.
    RecreateGroup(guid) -> JSON result; builds the group anew from its urls.  Chrome
                           saves that as a second group, so only on explicit request.
    OpenUrls(urls json) -> JSON result; opens the urls in a plain new window
    Call(cmd, args json)-> raw pass-through to the extension (debugging)
    Ping()              -> True

Saved groups come from the profile on disk (see chromegroups.py); which of them
are currently open is answered live by the extension.
"""

from __future__ import annotations

import json
import os
import struct
import sys
import time
from pathlib import Path

import dbus
import dbus.service
from dbus.mainloop.glib import DBusGMainLoop
from gi.repository import GLib

sys.path.insert(0, str(Path(__file__).resolve().parent))
import chromegroups  # noqa: E402

BUS_NAME = "be.synvio.KRunnerProjects"
OBJECT_PATH = "/Chrome"
IFACE = "be.synvio.KRunnerProjects.Chrome"
LOG = Path(os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache"))) / "krunner-projects-host.log"


def log(msg: str) -> None:
    try:
        with LOG.open("a") as fh:
            fh.write(f"{time.strftime('%H:%M:%S')} {msg}\n")
    except OSError:
        pass


class NativePort:
    """Framed JSON over stdin/stdout, as Chrome's native messaging expects."""

    def __init__(self, on_message, on_close):
        self._on_message = on_message
        self._on_close = on_close
        self._stdin = os.fdopen(sys.stdin.fileno(), "rb", buffering=0)
        self._stdout = os.fdopen(sys.stdout.fileno(), "wb", buffering=0)
        self._buf = b""
        GLib.io_add_watch(self._stdin, GLib.IO_IN | GLib.IO_HUP | GLib.IO_ERR, self._readable)

    def send(self, obj: dict) -> None:
        data = json.dumps(obj).encode()
        self._stdout.write(struct.pack("<I", len(data)) + data)

    def _readable(self, source, cond):
        chunk = self._stdin.read(65536)
        if not chunk:
            self._on_close()
            return False
        self._buf += chunk
        while len(self._buf) >= 4:
            (ln,) = struct.unpack("<I", self._buf[:4])
            if len(self._buf) < 4 + ln:
                break
            payload, self._buf = self._buf[4:4 + ln], self._buf[4 + ln:]
            try:
                self._on_message(json.loads(payload))
            except Exception as exc:  # keep the loop alive whatever happens
                log(f"bad message: {exc}")
        return True


class ChromeBridge(dbus.service.Object):
    def __init__(self, bus, loop):
        super().__init__(bus, OBJECT_PATH)
        self._loop = loop
        self._next_id = 1
        self._pending: dict[int, callable] = {}
        self._port = NativePort(self._on_extension_message, self._on_extension_closed)
        self._profile = None

    # -- extension side ---------------------------------------------------
    def _request(self, cmd: str, args: dict, callback) -> None:
        rid = self._next_id
        self._next_id += 1
        self._pending[rid] = callback
        self._port.send({"id": rid, "cmd": cmd, "args": args})
        GLib.timeout_add(8000, self._timeout, rid)

    def _timeout(self, rid):
        cb = self._pending.pop(rid, None)
        if cb:
            cb({"ok": False, "error": "extension did not answer in time"})
        return False

    def _on_extension_message(self, msg: dict) -> None:
        if "hello" in msg:
            self._profile = msg.get("profile") or None
            log(f"extension connected: {msg}")
            return
        cb = self._pending.pop(msg.get("id"), None)
        if cb:
            cb(msg.get("result", {}))

    def _on_extension_closed(self) -> None:
        log("extension disconnected, exiting")
        self._loop.quit()

    # -- helpers ---------------------------------------------------------
    def _saved_groups(self) -> list[chromegroups.Group]:
        return chromegroups.load_groups(os.environ.get("KRUNNER_PROJECTS_PROFILE"))

    @staticmethod
    def _match_open(group: chromegroups.Group, open_groups: list[dict]) -> dict | None:
        candidates = [g for g in open_groups if (g.get("title") or "") == group.title]
        if len(candidates) > 1:
            same_color = [g for g in candidates if g.get("color") == group.color]
            candidates = same_color or candidates
        return candidates[0] if candidates else None

    @staticmethod
    def _norm_url(url: str | None) -> str:
        url = (url or "").split("#", 1)[0]
        return url[:-1] if url.endswith("/") else url

    def _find_restorable(self, group: chromegroups.Group, sessions: list[dict]) -> tuple[str, str] | None:
        """Pick the recently-closed entry that held this group.

        Preferably the most recent closed window whose tabs cover at least half
        of the group's urls, otherwise a single closed tab with one of them.
        Returns (sessionId, "window" | "tab") or None.
        """
        wanted = {self._norm_url(t.url) for t in group.tabs}
        if not wanted:
            return None
        best, best_score = None, 0.0
        for s in sessions:  # most recent first, so ties keep the earlier hit
            w = s.get("window")
            if not w:
                continue
            urls = {self._norm_url(t.get("url")) for t in w.get("tabs", [])}
            score = len(wanted & urls) / len(wanted)
            if score > best_score:
                best, best_score = w, score
        if best and best_score >= 0.5:
            return best["sessionId"], "window"
        for s in sessions:
            t = s.get("tab")
            if t and self._norm_url(t.get("url")) in wanted:
                return t["sessionId"], "tab"
        return None

    # -- D-Bus API -------------------------------------------------------
    @dbus.service.method(IFACE, in_signature="", out_signature="b")
    def Ping(self):
        return True

    @dbus.service.method(IFACE, in_signature="", out_signature="s", async_callbacks=("reply", "error"))
    def ListGroups(self, reply, error):
        saved = self._saved_groups()

        def done(result):
            open_groups = result.get("groups", []) if result.get("ok") else []
            out = []
            for g in saved:
                d = chromegroups.asdict(g)
                hit = self._match_open(g, open_groups)
                d["open"] = hit is not None
                d["groupId"] = hit["id"] if hit else None
                d["windowId"] = hit["windowId"] if hit else None
                out.append(d)
            reply(json.dumps(out, ensure_ascii=False))

        self._request("listOpen", {}, done)

    def _group_or_reply(self, guid, reply):
        guid = str(guid)
        group = next((g for g in self._saved_groups() if g.guid == guid), None)
        if group is None:
            reply(json.dumps({"ok": False, "error": f"no saved group with guid {guid}"}))
        return group

    @dbus.service.method(IFACE, in_signature="s", out_signature="s", async_callbacks=("reply", "error"))
    def OpenGroup(self, guid, reply, error):
        group = self._group_or_reply(guid, reply)
        if group is None:
            return

        def finish(obj: dict) -> None:
            obj.setdefault("title", group.title)
            reply(json.dumps(obj, ensure_ascii=False))

        def focus(hit: dict, extra: dict) -> None:
            self._request("focus", {"groupId": hit["id"], "windowId": hit["windowId"]},
                          lambda r: finish({**r, **extra}))

        def after_list(result):
            hit = self._match_open(group, result.get("groups", [])) if result.get("ok") else None
            if hit:
                focus(hit, {})
            else:
                self._request("recentlyClosed", {}, after_recent)

        def after_recent(result):
            found = self._find_restorable(group, result.get("sessions", [])) if result.get("ok") else None
            if not found:
                log(f"group {group.title!r} is closed and not in the recently-closed list")
                finish({"ok": False, "reason": "closed_not_restorable"})
                return
            session_id, kind = found
            log(f"restoring {kind} {session_id} for group {group.title!r}")
            self._request("restore", {"sessionId": session_id}, after_restore)

        def after_restore(result):
            if not result.get("ok"):
                finish({"ok": False, "reason": "restore_failed", "error": result.get("error")})
                return
            # Chrome needs a moment to reopen the group before it shows up in tabGroups.
            GLib.timeout_add(800, lambda: (self._request("listOpen", {}, after_restore_list), False)[1])

        def after_restore_list(result):
            hit = self._match_open(group, result.get("groups", [])) if result.get("ok") else None
            if hit:
                focus(hit, {"restored": True})
            else:
                finish({"ok": False, "reason": "restore_failed", "error": "group not found after restore"})

        self._request("listOpen", {}, after_list)

    @dbus.service.method(IFACE, in_signature="s", out_signature="s", async_callbacks=("reply", "error"))
    def RecreateGroup(self, guid, reply, error):
        group = self._group_or_reply(guid, reply)
        if group is None:
            return
        self._request(
            "create",
            {"title": group.title, "color": group.color, "urls": [t.url for t in group.tabs]},
            lambda r: reply(json.dumps({**r, "title": group.title}, ensure_ascii=False)),
        )

    @dbus.service.method(IFACE, in_signature="ss", out_signature="s", async_callbacks=("reply", "error"))
    def Call(self, cmd, args_json, reply, error):
        """Raw pass-through to the extension, for debugging and experiments."""
        self._request(str(cmd), json.loads(str(args_json) or "{}"), lambda r: reply(json.dumps(r)))

    @dbus.service.method(IFACE, in_signature="s", out_signature="s", async_callbacks=("reply", "error"))
    def OpenUrls(self, urls_json, reply, error):
        urls = json.loads(str(urls_json))
        self._request("openWindow", {"urls": urls}, lambda r: reply(json.dumps(r)))


def main() -> int:
    DBusGMainLoop(set_as_default=True)
    bus = dbus.SessionBus()
    try:
        name = dbus.service.BusName(BUS_NAME, bus, do_not_queue=True)
    except dbus.exceptions.NameExistsException:
        log("another host already owns the bus name; exiting")
        return 1
    loop = GLib.MainLoop()
    bridge = ChromeBridge(bus, loop)
    bridge._name = name  # keep alive
    log("host started")
    loop.run()
    return 0


if __name__ == "__main__":
    sys.exit(main())
