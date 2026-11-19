// Standalone check of the MAVLink parser against packets produced by pymavlink.
// Build:  g++ -std=c++20 -I. -DMAVLINK_LITE_TEST tools/mavlink_lite_test.cpp src/telemetry/mavlink_lite.cpp
// Input:  a binary file of concatenated MAVLink packets (see tools/make_mavlink_fixture.py).
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

#include "src/telemetry/mavlink_lite.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s fixture.bin\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());

    auto &hub = telemetry::TelemetryHub::instance();
    // Feed in awkward chunk sizes to exercise packet reassembly.
    for (size_t i = 0; i < bytes.size(); i += 7) {
        hub.ingest(bytes.data() + i, std::min<size_t>(7, bytes.size() - i));
    }

    const auto s = hub.snapshot();
    std::printf("ok=%llu bad=%llu\n", (unsigned long long)s.packets_ok, (unsigned long long)s.packets_bad_crc);
    std::printf("armed=%d type=%d\n", s.armed, s.vehicle_type);
    std::printf("roll=%.4f pitch=%.4f yaw=%.4f\n", s.roll, s.pitch, s.yaw);
    std::printf("gs=%.2f as=%.2f alt=%.2f climb=%.2f hdg=%d thr=%d\n",
                s.groundspeed, s.airspeed, s.alt_msl, s.climb, s.heading, s.throttle);
    std::printf("lat=%.7f lon=%.7f relalt=%.3f\n", s.lat, s.lon, s.relative_alt);
    std::printf("fix=%d sats=%d\n", s.fix_type, s.satellites);
    std::printf("volt=%.3f curr=%.2f rem=%d\n", s.battery_voltage, s.battery_current, s.battery_remaining);
    std::printf("home=%d dist=%.1f\n", s.has_home, s.home_distance);
    return 0;
}
