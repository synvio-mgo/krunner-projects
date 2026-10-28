#!/usr/bin/env python3
"""Read Chrome's saved tab groups straight from the profile on disk.

Chrome keeps saved tab groups in the sync data type store, a LevelDB database at
``<profile>/Sync Data/LevelDB``.  Chrome holds the LevelDB lock while it runs, so
this module never opens the database through LevelDB itself.  Instead it parses
the ``.ldb`` table files and the ``.log`` write-ahead files directly, which only
needs read access.

No third-party dependencies: snappy decompression and protobuf decoding are
implemented here (the subset LevelDB and Chrome actually use).

Usage as a CLI::

    chromegroups.py [--profile DIR] [--json]
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys
from dataclasses import dataclass, field, asdict
from pathlib import Path

LEVELDB_MAGIC = 0xDB4775248B80FB57
KEY_PREFIX = b"saved_tab_group-dt-"

COLORS = {
    0: "grey",  # unspecified
    1: "grey",
    2: "blue",
    3: "red",
    4: "yellow",
    5: "green",
    6: "pink",
    7: "purple",
    8: "cyan",
    9: "orange",
}

DEFAULT_PROFILE_DIRS = [
    "~/.config/google-chrome/Default",
    "~/.config/chromium/Default",
    "~/snap/chromium/common/chromium/Default",
    "~/.config/google-chrome-beta/Default",
    "~/.config/google-chrome-unstable/Default",
]


# ---------------------------------------------------------------- snappy ----
def snappy_decompress(data: bytes) -> bytes:
    """Decompress a raw snappy block (not the framing format)."""
    pos = 0
    length = 0
    shift = 0
    while True:
        b = data[pos]
        pos += 1
        length |= (b & 0x7F) << shift
        if not b & 0x80:
            break
        shift += 7
    out = bytearray()
    n = len(data)
    while pos < n:
        tag = data[pos]
        pos += 1
        kind = tag & 3
        if kind == 0:  # literal
            size = tag >> 2
            if size < 60:
                size += 1
            else:
                nbytes = size - 59
                size = int.from_bytes(data[pos:pos + nbytes], "little") + 1
                pos += nbytes
            out += data[pos:pos + size]
            pos += size
            continue
        if kind == 1:  # copy, 1-byte offset
            size = 4 + ((tag >> 2) & 7)
            offset = ((tag >> 5) << 8) | data[pos]
            pos += 1
        elif kind == 2:  # copy, 2-byte offset
            size = (tag >> 2) + 1
            offset = int.from_bytes(data[pos:pos + 2], "little")
            pos += 2
        else:  # copy, 4-byte offset
            size = (tag >> 2) + 1
            offset = int.from_bytes(data[pos:pos + 4], "little")
            pos += 4
        if offset == 0 or offset > len(out):
            raise ValueError("corrupt snappy stream")
        start = len(out) - offset
        if size <= offset:
            out += out[start:start + size]
        else:  # overlapping copy, byte by byte
            for i in range(size):
                out.append(out[start + i])
    if len(out) != length:
        raise ValueError("snappy length mismatch")
    return bytes(out)


# --------------------------------------------------------------- varints ----
def read_varint(buf: bytes, pos: int) -> tuple[int, int]:
    result = 0
    shift = 0
    while True:
        b = buf[pos]
        pos += 1
        result |= (b & 0x7F) << shift
        if not b & 0x80:
            return result, pos
        shift += 7


# --------------------------------------------------------------- leveldb ----
@dataclass
class Entry:
    key: bytes
    value: bytes | None  # None = deletion
    seq: int


def _decode_block(raw: bytes) -> bytes:
    """Strip the 5-byte trailer (compression type + crc) and decompress."""
    body, ctype = raw[:-5], raw[-5]
    if ctype == 0:
        return body
    if ctype == 1:
        return snappy_decompress(body)
    if ctype == 2:
        try:
            import zstandard  # type: ignore
        except ImportError as exc:  # pragma: no cover
            raise ValueError("zstd-compressed LevelDB block, python3-zstandard missing") from exc
        return zstandard.ZstdDecompressor().decompress(body)
    raise ValueError(f"unknown block compression {ctype}")


def _iter_block_entries(block: bytes):
    """Yield (key, value) pairs from a LevelDB data/index block."""
    if len(block) < 4:
        return
    num_restarts = struct.unpack_from("<I", block, len(block) - 4)[0]
    end = len(block) - 4 - 4 * num_restarts
    pos = 0
    key = b""
    while pos < end:
        shared, pos = read_varint(block, pos)
        non_shared, pos = read_varint(block, pos)
        vlen, pos = read_varint(block, pos)
        key = key[:shared] + block[pos:pos + non_shared]
        pos += non_shared
        value = block[pos:pos + vlen]
        pos += vlen
        yield key, value


def _read_handle(buf: bytes, pos: int) -> tuple[int, int, int]:
    offset, pos = read_varint(buf, pos)
    size, pos = read_varint(buf, pos)
    return offset, size, pos


def read_table(path: Path) -> list[Entry]:
    data = path.read_bytes()
    if len(data) < 48:
        return []
    footer = data[-48:]
    magic = struct.unpack_from("<Q", footer, 40)[0]
    if magic != LEVELDB_MAGIC:
        raise ValueError(f"{path}: not a LevelDB table")
    _, _, pos = _read_handle(footer, 0)  # metaindex
    idx_off, idx_size, _ = _read_handle(footer, pos)
    index = _decode_block(data[idx_off:idx_off + idx_size + 5])
    entries: list[Entry] = []
    for _, handle in _iter_block_entries(index):
        off, size, _ = _read_handle(handle, 0)
        block = _decode_block(data[off:off + size + 5])
        for ikey, value in _iter_block_entries(block):
            if len(ikey) < 8:
                continue
            user_key = ikey[:-8]
            tail = struct.unpack("<Q", ikey[-8:])[0]
            seq, ktype = tail >> 8, tail & 0xFF
            entries.append(Entry(user_key, value if ktype == 1 else None, seq))
    return entries


def read_log(path: Path) -> list[Entry]:
    data = path.read_bytes()
    entries: list[Entry] = []
    block_size = 32768
    pos = 0
    record = bytearray()
    while pos + 7 <= len(data):
        # skip trailer padding at block end
        if block_size - (pos % block_size) < 7:
            pos += block_size - (pos % block_size)
            continue
        length, rtype = struct.unpack_from("<HB", data, pos + 4)
        pos += 7
        chunk = data[pos:pos + length]
        pos += length
        if rtype == 0:  # zero type / preallocated
            continue
        if rtype in (1, 2):  # full / first
            record = bytearray(chunk)
        else:  # middle / last
            record += chunk
        if rtype in (1, 4):
            try:
                entries.extend(_parse_batch(bytes(record)))
            except (IndexError, struct.error):
                pass  # truncated tail record
            record = bytearray()
    return entries


def _parse_batch(batch: bytes) -> list[Entry]:
    seq, count = struct.unpack_from("<QI", batch, 0)
    pos = 12
    out = []
    for i in range(count):
        kind = batch[pos]
        pos += 1
        klen, pos = read_varint(batch, pos)
        key = batch[pos:pos + klen]
        pos += klen
        if kind == 1:
            vlen, pos = read_varint(batch, pos)
            value = batch[pos:pos + vlen]
            pos += vlen
            out.append(Entry(key, value, seq + i))
        else:
            out.append(Entry(key, None, seq + i))
    return out


def read_leveldb(db_dir: Path, prefix: bytes = b"") -> dict[bytes, bytes]:
    """Return the live key/value pairs (latest sequence wins)."""
    latest: dict[bytes, Entry] = {}
    files = sorted(db_dir.glob("*.ldb")) + sorted(db_dir.glob("*.sst")) + sorted(db_dir.glob("*.log"))
    for f in files:
        try:
            entries = read_log(f) if f.suffix == ".log" else read_table(f)
        except (ValueError, struct.error, IndexError) as exc:
            print(f"warning: skipping {f.name}: {exc}", file=sys.stderr)
            continue
        for e in entries:
            if prefix and not e.key.startswith(prefix):
                continue
            cur = latest.get(e.key)
            if cur is None or e.seq >= cur.seq:
                latest[e.key] = e
    return {k: e.value for k, e in latest.items() if e.value is not None}


# -------------------------------------------------------------- protobuf ----
def decode_proto(buf: bytes) -> list[tuple[int, int, object]]:
    """Decode a protobuf message into (field, wiretype, value) triples."""
    out = []
    pos = 0
    while pos < len(buf):
        tag, pos = read_varint(buf, pos)
        fnum, wt = tag >> 3, tag & 7
        if wt == 0:
            val, pos = read_varint(buf, pos)
        elif wt == 1:
            val = struct.unpack_from("<Q", buf, pos)[0]
            pos += 8
        elif wt == 2:
            ln, pos = read_varint(buf, pos)
            val = buf[pos:pos + ln]
            pos += ln
        elif wt == 5:
            val = struct.unpack_from("<I", buf, pos)[0]
            pos += 4
        else:
            raise ValueError(f"unsupported wire type {wt}")
        out.append((fnum, wt, val))
    return out


def _fields(buf: bytes) -> dict[int, list]:
    d: dict[int, list] = {}
    for f, _, v in decode_proto(buf):
        d.setdefault(f, []).append(v)
    return d


def _first(d: dict, f: int, default=None):
    return d[f][0] if f in d else default


def _text(v, default="") -> str:
    if v is None:
        return default
    return v.decode("utf-8", "replace") if isinstance(v, (bytes, bytearray)) else str(v)


# ----------------------------------------------------------------- model ----
@dataclass
class Tab:
    url: str
    title: str
    position: int


@dataclass
class Group:
    guid: str
    title: str
    color: str
    pinned_position: int | None
    created_us: int
    updated_us: int
    tabs: list[Tab] = field(default_factory=list)


def _unwrap_specifics(value: bytes) -> bytes:
    """Chrome stores a SavedTabGroupData wrapper {version=1, specifics=2};
    older versions stored the SavedTabGroupSpecifics directly."""
    outer = _fields(value)
    inner = _first(outer, 2)
    if isinstance(inner, (bytes, bytearray)):
        probe = _fields(inner)
        if 1 in probe and isinstance(probe[1][0], (bytes, bytearray)) and len(probe[1][0]) == 36:
            return bytes(inner)
    return value


def load_groups(profile_dir: str | os.PathLike | None = None) -> list[Group]:
    profile = Path(os.path.expanduser(profile_dir)) if profile_dir else find_profile()
    if profile is None:
        return []
    db_dir = profile / "Sync Data" / "LevelDB"
    if not db_dir.is_dir():
        return []
    rows = read_leveldb(db_dir, KEY_PREFIX)
    groups: dict[str, Group] = {}
    tabs: dict[str, list[Tab]] = {}
    for key, value in rows.items():
        try:
            spec = _fields(_unwrap_specifics(value))
        except (ValueError, IndexError, struct.error):
            continue
        guid = _text(_first(spec, 1))
        if 4 in spec:
            g = _fields(spec[4][0])
            groups[guid] = Group(
                guid=guid,
                title=_text(_first(g, 2)),
                color=COLORS.get(_first(g, 3, 0), "grey"),
                pinned_position=_first(g, 4),
                created_us=_first(spec, 2, 0),
                updated_us=_first(spec, 3, 0),
            )
        elif 5 in spec:
            t = _fields(spec[5][0])
            tabs.setdefault(_text(_first(t, 1)), []).append(
                Tab(url=_text(_first(t, 3)), title=_text(_first(t, 4)), position=_first(t, 2, 0))
            )
    for guid, g in groups.items():
        g.tabs = sorted(tabs.get(guid, []), key=lambda t: t.position)
    result = [g for g in groups.values() if g.tabs]
    result.sort(key=lambda g: (g.pinned_position is None, g.pinned_position or 0, -g.updated_us))
    return result


def find_profile() -> Path | None:
    for d in DEFAULT_PROFILE_DIRS:
        p = Path(os.path.expanduser(d))
        if (p / "Sync Data" / "LevelDB").is_dir():
            return p
    return None


def groups_to_json(groups: list[Group]) -> str:
    return json.dumps([asdict(g) for g in groups], ensure_ascii=False, indent=None)


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="List Chrome saved tab groups")
    ap.add_argument("--profile", help="Chrome profile directory (default: auto-detect)")
    ap.add_argument("--json", action="store_true", help="print JSON instead of a table")
    args = ap.parse_args(argv)
    groups = load_groups(args.profile)
    if args.json:
        print(groups_to_json(groups))
        return 0
    if not groups:
        print("no saved tab groups found", file=sys.stderr)
        return 1
    for g in groups:
        pin = f" pinned#{g.pinned_position}" if g.pinned_position is not None else ""
        print(f"{g.title or '(untitled)'}  [{g.color}]{pin}  {g.guid}")
        for t in g.tabs:
            print(f"    - {t.title or t.url}  <{t.url}>")
    return 0


if __name__ == "__main__":
    sys.exit(main())
