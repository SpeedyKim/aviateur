#!/usr/bin/env python3
"""Send fake MAVLink telemetry to Aviateur's on-screen display.

Simulates a fixed-wing flying a slow 300 m circle, with a draining battery, so you can
test the OSD without a flight controller.

    pip install pymavlink
    python tools/fake_telemetry.py                 # sends to 127.0.0.1:14550
    python tools/fake_telemetry.py --mavlink1      # older protocol version
    python tools/fake_telemetry.py --host 192.168.1.50 --port 14550

Ctrl+C to stop.
"""
import argparse
import math
import os
import time

parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument("--host", default="127.0.0.1")
parser.add_argument("--port", type=int, default=14550)
parser.add_argument("--rate", type=float, default=20.0, help="attitude updates per second")
parser.add_argument("--mavlink1", action="store_true", help="send MAVLink v1 instead of v2")
parser.add_argument("--seconds", type=float, default=0, help="stop after N seconds (0 = forever)")
args = parser.parse_args()

if not args.mavlink1:
    os.environ["MAVLINK20"] = "1"

from pymavlink import mavutil  # noqa: E402  (import after choosing the protocol version)

link = mavutil.mavlink_connection(f"udpout:{args.host}:{args.port}", source_system=1, source_component=1)
mav = link.mav

HOME_LAT, HOME_LON = 51.0447, -114.0719  # Calgary
RADIUS_M = 300.0
LAP_S = 60.0
CRUISE_ALT = 120.0

start = time.time()
last_slow = 0.0
print(f"Sending fake MAVLink to {args.host}:{args.port} (Ctrl+C to stop)")

try:
    while True:
        t = time.time() - start
        if args.seconds and t > args.seconds:
            break
        boot_ms = int(t * 1000)

        # Circle around a point 300 m north of home.
        ang = 2 * math.pi * t / LAP_S
        north = RADIUS_M + RADIUS_M * math.sin(ang - math.pi / 2)
        east = RADIUS_M * math.cos(ang - math.pi / 2)
        lat = HOME_LAT + north / 111_320.0
        lon = HOME_LON + east / (111_320.0 * math.cos(math.radians(HOME_LAT)))
        speed = 2 * math.pi * RADIUS_M / LAP_S  # m/s
        heading = (math.degrees(ang) + 90) % 360
        alt = CRUISE_ALT + 10 * math.sin(t / 7)
        climb = 10 / 7 * math.cos(t / 7)

        roll = math.radians(25 + 8 * math.sin(t * 0.9))
        pitch = math.radians(3 + 4 * math.sin(t * 0.5))
        yaw = math.radians(heading if heading <= 180 else heading - 360)

        mav.attitude_send(boot_ms, roll, pitch, yaw, 0, 0, 0)

        if t - last_slow >= 0.2:  # 5 Hz for the slower data
            last_slow = t
            armed = mavutil.mavlink.MAV_MODE_FLAG_SAFETY_ARMED if t > 3 else 0
            mav.heartbeat_send(mavutil.mavlink.MAV_TYPE_FIXED_WING, mavutil.mavlink.MAV_AUTOPILOT_ARDUPILOTMEGA,
                               armed | mavutil.mavlink.MAV_MODE_FLAG_CUSTOM_MODE_ENABLED, 10, 4)
            mav.vfr_hud_send(speed * 1.05, speed, int(heading), 55, alt + 1045, climb)
            mav.global_position_int_send(boot_ms, int(lat * 1e7), int(lon * 1e7), int((alt + 1045) * 1000),
                                         int(alt * 1000), 0, 0, 0, int(heading * 100))
            mav.gps_raw_int_send(boot_ms * 1000, 3, int(lat * 1e7), int(lon * 1e7), int((alt + 1045) * 1000),
                                 90, 120, int(speed * 100), int(heading * 100), 14)
            volts = 16.8 - min(t / 600.0, 1.0) * 2.4  # 4S pack draining over 10 min
            remaining = max(0, int(100 - t / 6))
            mav.sys_status_send(0, 0, 0, 300, int(volts * 1000), 1250, remaining, 0, 0, 0, 0, 0, 0)

        time.sleep(1.0 / args.rate)
except KeyboardInterrupt:
    pass
