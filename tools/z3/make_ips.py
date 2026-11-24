#!/usr/bin/env python3
"""Writes an IPS that turns <orig> (zero-padded to the size of <new>) into <new>, then replays it to check the result.

usage: make_ips.py <orig.sfc> <new.sfc> <out.ips>
The reader must grow the ROM to the patched size first (zero-filled), as alttpr's Rom::resize() does.
"""
import hashlib, sys

MAX_REC = 0xFFFF
MERGE_GAP = 4  # a short run of equal bytes is cheaper inside a record than as a new 5-byte header


def diff_ips(orig, new):
    orig = orig + bytes(len(new) - len(orig))
    out, i, n = bytearray(b"PATCH"), 0, len(new)
    while i < n:
        if orig[i] == new[i]:
            i += 1
            continue
        start, end, same = i, i, 0
        while end < n and end - start < MAX_REC:
            same = same + 1 if orig[end] == new[end] else 0
            end += 1
            if same > MERGE_GAP:
                break
        end -= same
        if start == 0x454F46:  # "EOF" as an offset ends the patch; start one byte earlier
            start -= 1
        out += start.to_bytes(3, "big") + (end - start).to_bytes(2, "big") + new[start:end]
        i = end
    return bytes(out + b"EOF")


def apply_ips(rom, ips, size):
    rom = bytearray(rom + bytes(size - len(rom)))
    assert ips[:5] == b"PATCH"
    p = 5
    while ips[p:p + 3] != b"EOF":
        off, ln = int.from_bytes(ips[p:p + 3], "big"), int.from_bytes(ips[p + 3:p + 5], "big")
        p += 5
        if ln == 0:
            ln, val = int.from_bytes(ips[p:p + 2], "big"), ips[p + 2]
            rom[off:off + ln] = bytes([val]) * ln
            p += 3
        else:
            rom[off:off + ln] = ips[p:p + ln]
            p += ln
    return bytes(rom)


if __name__ == "__main__":
    orig, new = open(sys.argv[1], "rb").read(), open(sys.argv[2], "rb").read()
    ips = diff_ips(orig, new)
    assert apply_ips(orig, ips, len(new)) == new, "IPS replay differs"
    open(sys.argv[3], "wb").write(ips)
    print(f"{len(ips)} bytes -> {sys.argv[3]}, patched md5 {hashlib.md5(new).hexdigest()}")
