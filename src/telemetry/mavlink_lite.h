#pragma once

// Minimal MAVLink (v1 + v2) receiver for the on-screen display.
//
// We only decode the handful of messages the OSD needs, so we don't pull in the full
// generated MAVLink C library. Every packet is CRC-checked (including CRC_EXTRA), so a
// corrupted or unknown packet is simply ignored.

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace telemetry {

using Clock = std::chrono::steady_clock;

/// A copy of everything the OSD knows about the vehicle. Fields are only meaningful
/// when the matching `has_*` flag is true AND the data is fresh (see TelemetryHub).
struct TelemetrySnapshot {
    // HEARTBEAT
    bool has_heartbeat = false;
    bool armed = false;
    uint8_t vehicle_type = 0; // MAV_TYPE
    uint8_t autopilot = 0;    // MAV_AUTOPILOT
    uint32_t custom_mode = 0;

    // ATTITUDE (radians)
    bool has_attitude = false;
    float roll = 0, pitch = 0, yaw = 0;

    // VFR_HUD
    bool has_vfr = false;
    float airspeed = 0;    // m/s
    float groundspeed = 0; // m/s
    float alt_msl = 0;     // m
    float climb = 0;       // m/s
    int16_t heading = 0;   // deg 0..360
    uint16_t throttle = 0; // %

    // GLOBAL_POSITION_INT
    bool has_position = false;
    double lat = 0, lon = 0; // deg
    float relative_alt = 0;  // m above home

    // GPS_RAW_INT
    bool has_gps = false;
    uint8_t fix_type = 0; // 0-1 no fix, 2 = 2D, 3 = 3D, ...
    uint8_t satellites = 0;

    // SYS_STATUS
    bool has_battery = false;
    float battery_voltage = 0; // V
    float battery_current = -1; // A, -1 = unknown
    int8_t battery_remaining = -1; // %, -1 = unknown

    // Derived
    bool has_home = false;
    double home_lat = 0, home_lon = 0;
    float home_distance = 0; // m

    // Bookkeeping
    Clock::time_point last_packet{};
    Clock::time_point last_heartbeat{};
    Clock::time_point last_attitude{};
    Clock::time_point last_position{};
    uint64_t packets_ok = 0;
    uint64_t packets_bad_crc = 0;
};

/// Byte-stream parser. Feed it whatever arrives from UDP; it calls back into the hub for each good packet.
class MavlinkParser {
public:
    struct Packet {
        uint32_t msg_id = 0;
        uint8_t sys_id = 0;
        uint8_t comp_id = 0;
        std::vector<uint8_t> payload; // zero-padded to the message's full length
    };

    /// Returns the packets decoded from this chunk.
    std::vector<Packet> feed(const uint8_t *data, size_t len, uint64_t *bad_crc_counter = nullptr);

    /// CRC_EXTRA and full payload length for the messages we understand. Returns false if unknown.
    static bool message_info(uint32_t msg_id, uint8_t &crc_extra, uint8_t &full_len);

    static uint16_t crc_x25(const uint8_t *data, size_t len, uint16_t crc = 0xFFFF);

private:
    std::vector<uint8_t> buffer_;
};

/// Thread-safe store of the latest telemetry plus a UDP listener thread.
class TelemetryHub {
public:
    static TelemetryHub &instance();

    /// Start listening for MAVLink on UDP `port` (all interfaces). Safe to call more than once.
    void start(uint16_t port = 14550);
    void stop();
    bool running() const {
        return running_;
    }
    uint16_t port() const {
        return port_;
    }

    /// Thread-safe copy of the latest state.
    TelemetrySnapshot snapshot() const;

    /// Feed raw bytes directly (used by the listener, and by tests).
    void ingest(const uint8_t *data, size_t len);

    /// True if any MAVLink packet arrived within `max_age_s` seconds.
    static bool fresh(Clock::time_point t, double max_age_s = 2.0);

private:
    TelemetryHub() = default;
    ~TelemetryHub();

    void apply(const MavlinkParser::Packet &pkt);
    void run();

    mutable std::mutex mutex_;
    TelemetrySnapshot state_;
    MavlinkParser parser_;

    std::atomic<bool> running_{false};
    std::atomic<bool> stop_requested_{false};
    std::unique_ptr<std::thread> thread_;
    uint16_t port_ = 14550;
};

/// Great-circle distance in metres.
double haversine_m(double lat1, double lon1, double lat2, double lon2);

} // namespace telemetry
