#!/usr/bin/env python3
"""Write a binary file of known MAVLink packets (v1 and v2, with noise) for tools/mavlink_lite_test.cpp."""
import os
import sys


def packets(v2):
    if v2:
        os.environ["MAVLINK20"] = "1"
    else:
        os.environ.pop("MAVLINK20", None)
    import importlib
    from pymavlink import mavutil
    mod = importlib.import_module("pymavlink.dialects.v20.common" if v2 else "pymavlink.dialects.v10.common")
    m = mod.MAVLink(None, srcSystem=1, srcComponent=1)
    out = []
    if v2:  # second ground station heartbeat that must be ignored
        out.append(m.heartbeat_encode(6, 8, 0, 0, 0).pack(m))
    out.append(m.heartbeat_encode(1, 3, 128 | 1, 10, 4).pack(m))
    out.append(m.attitude_encode(1000, 0.25, -0.125, 1.5, 0, 0, 0).pack(m))
    out.append(m.vfr_hud_encode(31.5, 30.25, 90, 55, 1165.5, -1.25).pack(m))
    out.append(m.gps_raw_int_encode(0, 3, 510447000, -1140719000, 0, 90, 120, 3025, 9000, 14).pack(m))
    out.append(m.global_position_int_encode(1000, 510447000, -1140719000, 1165500, 120500, 0, 0, 0, 9000).pack(m))
    out.append(m.global_position_int_encode(2000, 510474000, -1140719000, 1165500, 120500, 0, 0, 0, 9000).pack(m))
    out.append(m.sys_status_encode(0, 0, 0, 300, 15870, 1250, 72, 0, 0, 0, 0, 0, 0).pack(m))
    return out


v2 = sys.argv[2] == "v2"
data = b"\x00\xfe\x07garbage\xfd"  # noise and false starts
for p in packets(v2):
    data += bytes(p) + b"\x55\xaa"
# A corrupted copy of a packet must be rejected.
bad = bytearray(packets(v2)[-1])
bad[-1] ^= 0xFF
data += bytes(bad)
open(sys.argv[1], "wb").write(data)
