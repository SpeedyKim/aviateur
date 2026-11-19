#include "mavlink_lite.h"

#include <cmath>
#include <cstring>

#ifdef _WIN32
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
using socket_t = SOCKET;
static constexpr socket_t kBadSocket = INVALID_SOCKET;
static void close_socket(socket_t s) {
    closesocket(s);
}
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/select.h>
    #include <sys/socket.h>
    #include <unistd.h>
using socket_t = int;
static constexpr socket_t kBadSocket = -1;
static void close_socket(socket_t s) {
    close(s);
}
#endif

#ifdef MAVLINK_LITE_TEST
    #include <cstdio>
    #define TLOG_INFO(...) std::puts("telemetry info")
    #define TLOG_ERROR(...) std::puts("telemetry error")
#else
    #include "../gui_interface.h"
    #define TLOG_INFO(...) GuiInterface::Instance().PutLog(LogLevel::Info, __VA_ARGS__)
    #define TLOG_ERROR(...) GuiInterface::Instance().PutLog(LogLevel::Error, __VA_ARGS__)
#endif

namespace telemetry {

namespace {

// Message IDs (MAVLink common dialect).
constexpr uint32_t MSG_HEARTBEAT = 0;
constexpr uint32_t MSG_SYS_STATUS = 1;
constexpr uint32_t MSG_GPS_RAW_INT = 24;
constexpr uint32_t MSG_ATTITUDE = 30;
constexpr uint32_t MSG_GLOBAL_POSITION_INT = 33;
constexpr uint32_t MSG_VFR_HUD = 74;

constexpr uint8_t MAV_MODE_FLAG_SAFETY_ARMED = 128;
constexpr uint8_t MAV_TYPE_GCS = 6;

constexpr uint8_t STX_V1 = 0xFE;
constexpr uint8_t STX_V2 = 0xFD;
constexpr size_t HEADER_V1 = 6;  // STX len seq sys comp msgid
constexpr size_t HEADER_V2 = 10; // STX len incompat compat seq sys comp msgid(3)
constexpr size_t SIGNATURE_LEN = 13;

// Little-endian field readers.
template <typename T>
T rd(const std::vector<uint8_t> &p, size_t off) {
    T v;
    std::memcpy(&v, p.data() + off, sizeof(T));
    return v;
}

} // namespace

// ---------------------------------------------------------------- Parser

uint16_t MavlinkParser::crc_x25(const uint8_t *data, size_t len, uint16_t crc) {
    for (size_t i = 0; i < len; i++) {
        uint8_t tmp = data[i] ^ static_cast<uint8_t>(crc & 0xFF);
        tmp ^= static_cast<uint8_t>(tmp << 4);
        crc = static_cast<uint16_t>((crc >> 8) ^ (tmp << 8) ^ (tmp << 3) ^ (tmp >> 4));
    }
    return crc;
}

bool MavlinkParser::message_info(uint32_t msg_id, uint8_t &crc_extra, uint8_t &full_len) {
    switch (msg_id) {
        case MSG_HEARTBEAT:
            crc_extra = 50;
            full_len = 9;
            return true;
        case MSG_SYS_STATUS:
            crc_extra = 124;
            full_len = 43; // 31 base + MAVLink2 extensions
            return true;
        case MSG_GPS_RAW_INT:
            crc_extra = 24;
            full_len = 52; // 30 base + extensions
            return true;
        case MSG_ATTITUDE:
            crc_extra = 39;
            full_len = 28;
            return true;
        case MSG_GLOBAL_POSITION_INT:
            crc_extra = 104;
            full_len = 28;
            return true;
        case MSG_VFR_HUD:
            crc_extra = 20;
            full_len = 20;
            return true;
        default:
            return false;
    }
}

std::vector<MavlinkParser::Packet> MavlinkParser::feed(const uint8_t *data, size_t len, uint64_t *bad_crc_counter) {
    std::vector<Packet> out;
    buffer_.insert(buffer_.end(), data, data + len);

    size_t pos = 0;
    while (pos < buffer_.size()) {
        const uint8_t stx = buffer_[pos];
        if (stx != STX_V1 && stx != STX_V2) {
            pos++;
            continue;
        }

        const bool v2 = stx == STX_V2;
        const size_t header = v2 ? HEADER_V2 : HEADER_V1;
        if (buffer_.size() - pos < header) {
            break; // need more bytes
        }

        const uint8_t payload_len = buffer_[pos + 1];
        size_t total = header + payload_len + 2;
        if (v2 && (buffer_[pos + 2] & 0x01)) {
            total += SIGNATURE_LEN;
        }
        if (buffer_.size() - pos < total) {
            break; // need more bytes
        }

        const uint8_t *frame = buffer_.data() + pos;
        uint32_t msg_id;
        uint8_t sys_id, comp_id;
        if (v2) {
            sys_id = frame[5];
            comp_id = frame[6];
            msg_id = frame[7] | (frame[8] << 8) | (frame[9] << 16);
        } else {
            sys_id = frame[3];
            comp_id = frame[4];
            msg_id = frame[5];
        }

        uint8_t crc_extra = 0, full_len = 0;
        if (!message_info(msg_id, crc_extra, full_len)) {
            // Unknown message: we can't verify its CRC, so we can't trust its length either
            // (this may be a false start inside noise). Step one byte and resync; any STX found
            // inside its payload will fail the CRC check for the messages we do decode.
            pos++;
            continue;
        }

        uint16_t crc = crc_x25(frame + 1, header - 1 + payload_len);
        crc = crc_x25(&crc_extra, 1, crc);
        const uint16_t wire_crc = frame[header + payload_len] | (frame[header + payload_len + 1] << 8);

        if (crc != wire_crc) {
            if (bad_crc_counter) {
                (*bad_crc_counter)++;
            }
            pos++; // false start; resync
            continue;
        }

        Packet pkt;
        pkt.msg_id = msg_id;
        pkt.sys_id = sys_id;
        pkt.comp_id = comp_id;
        // MAVLink2 trims trailing zero bytes; pad back to the full length.
        pkt.payload.assign(full_len, 0);
        std::memcpy(pkt.payload.data(), frame + header, std::min<size_t>(payload_len, full_len));
        out.push_back(std::move(pkt));

        pos += total;
    }

    buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<long>(pos));
    // Don't let garbage grow without bound.
    if (buffer_.size() > 4096) {
        buffer_.clear();
    }
    return out;
}

// ---------------------------------------------------------------- Hub

TelemetryHub &TelemetryHub::instance() {
    static TelemetryHub hub;
    return hub;
}

TelemetryHub::~TelemetryHub() {
    stop();
}

bool TelemetryHub::fresh(Clock::time_point t, double max_age_s) {
    if (t == Clock::time_point{}) {
        return false;
    }
    return std::chrono::duration<double>(Clock::now() - t).count() < max_age_s;
}

TelemetrySnapshot TelemetryHub::snapshot() const {
    std::lock_guard lock(mutex_);
    return state_;
}

void TelemetryHub::ingest(const uint8_t *data, size_t len) {
    std::lock_guard lock(mutex_);
    for (const auto &pkt : parser_.feed(data, len, &state_.packets_bad_crc)) {
        apply(pkt);
    }
}

double haversine_m(double lat1, double lon1, double lat2, double lon2) {
    constexpr double R = 6371000.0;
    constexpr double D2R = 3.14159265358979323846 / 180.0;
    const double dlat = (lat2 - lat1) * D2R;
    const double dlon = (lon2 - lon1) * D2R;
    const double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
                     std::cos(lat1 * D2R) * std::cos(lat2 * D2R) * std::sin(dlon / 2) * std::sin(dlon / 2);
    return 2 * R * std::atan2(std::sqrt(a), std::sqrt(1 - a));
}

// Called with mutex_ held.
void TelemetryHub::apply(const MavlinkParser::Packet &pkt) {
    auto &s = state_;
    const auto now = Clock::now();
    const auto &p = pkt.payload;

    s.packets_ok++;
    s.last_packet = now;

    switch (pkt.msg_id) {
        case MSG_HEARTBEAT: {
            const auto type = rd<uint8_t>(p, 4);
            if (type == MAV_TYPE_GCS) {
                return; // ignore other ground stations on the same network
            }
            s.custom_mode = rd<uint32_t>(p, 0);
            s.vehicle_type = type;
            s.autopilot = rd<uint8_t>(p, 5);
            s.armed = (rd<uint8_t>(p, 6) & MAV_MODE_FLAG_SAFETY_ARMED) != 0;
            s.has_heartbeat = true;
            s.last_heartbeat = now;
        } break;
        case MSG_SYS_STATUS: {
            const auto mv = rd<uint16_t>(p, 14);
            const auto ca = rd<int16_t>(p, 16);
            s.battery_voltage = mv == UINT16_MAX ? 0.f : mv / 1000.f;
            s.battery_current = ca < 0 ? -1.f : ca / 100.f;
            s.battery_remaining = rd<int8_t>(p, 30);
            s.has_battery = mv != UINT16_MAX && mv > 0;
        } break;
        case MSG_GPS_RAW_INT: {
            s.fix_type = rd<uint8_t>(p, 28);
            s.satellites = rd<uint8_t>(p, 29);
            s.has_gps = true;
        } break;
        case MSG_ATTITUDE: {
            s.roll = rd<float>(p, 4);
            s.pitch = rd<float>(p, 8);
            s.yaw = rd<float>(p, 12);
            s.has_attitude = true;
            s.last_attitude = now;
        } break;
        case MSG_GLOBAL_POSITION_INT: {
            s.lat = rd<int32_t>(p, 4) / 1e7;
            s.lon = rd<int32_t>(p, 8) / 1e7;
            s.relative_alt = rd<int32_t>(p, 16) / 1000.f;
            s.has_position = !(s.lat == 0 && s.lon == 0);
            s.last_position = now;

            // Home = first position with a 3D fix (or first position if no GPS_RAW_INT is sent).
            if (!s.has_home && s.has_position && (!s.has_gps || s.fix_type >= 3)) {
                s.home_lat = s.lat;
                s.home_lon = s.lon;
                s.has_home = true;
            }
            if (s.has_home && s.has_position) {
                s.home_distance = static_cast<float>(haversine_m(s.home_lat, s.home_lon, s.lat, s.lon));
            }
        } break;
        case MSG_VFR_HUD: {
            s.airspeed = rd<float>(p, 0);
            s.groundspeed = rd<float>(p, 4);
            s.alt_msl = rd<float>(p, 8);
            s.climb = rd<float>(p, 12);
            s.heading = rd<int16_t>(p, 16);
            s.throttle = rd<uint16_t>(p, 18);
            s.has_vfr = true;
        } break;
        default:
            break;
    }
}

void TelemetryHub::start(uint16_t port) {
    if (running_) {
        return;
    }
    port_ = port;
    stop_requested_ = false;
    running_ = true;
    thread_ = std::make_unique<std::thread>([this] { run(); });
}

void TelemetryHub::stop() {
    stop_requested_ = true;
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();
    running_ = false;
}

void TelemetryHub::run() {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    socket_t sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == kBadSocket) {
        TLOG_ERROR("Telemetry: could not create UDP socket");
        running_ = false;
        return;
    }

    int reuse = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char *>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port_);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) != 0) {
        TLOG_ERROR("Telemetry: UDP port {} is busy (is another ground station running?)", port_);
        close_socket(sock);
        running_ = false;
        return;
    }

    TLOG_INFO("Telemetry: listening for MAVLink on UDP {}", port_);

    uint8_t buf[2048];
    while (!stop_requested_) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock, &fds);
        timeval tv{0, 200 * 1000}; // wake up 5x/s to check for stop

        const int ready = select(static_cast<int>(sock) + 1, &fds, nullptr, nullptr, &tv);
        if (ready <= 0) {
            continue;
        }

        const auto n = recv(sock, reinterpret_cast<char *>(buf), sizeof(buf), 0);
        if (n > 0) {
            ingest(buf, static_cast<size_t>(n));
        }
    }

    close_socket(sock);
    running_ = false;
}

} // namespace telemetry
