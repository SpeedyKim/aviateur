#pragma once

// On-screen display drawn over the video: link/video stats, recording, and (when MAVLink
// telemetry is arriving) artificial horizon, heading, speed, altitude, battery, GPS and home distance.

#include <vecgui/app.h>

#include <array>
#include <functional>
#include <string>
#include <vector>

#include "../telemetry/mavlink_lite.h"

/// A rounded glass "pill" holding a row of caption/value pairs, e.g.  ALT 120 m   SPD 42 km/h
class GlassPill : public vecgui::NodeUi {
public:
    struct Item {
        std::string caption; // small, dim (may be empty)
        std::string value;   // large, bright
        vecgui::ColorU color = vecgui::ColorU(255, 255, 255, 240);
    };

    void set_items(const std::vector<Item> &items, float scale);

    /// Room on the left for an icon the parent draws (recording dot, signal bars), in unscaled px.
    float left_inset = 0;

    /// Tint override (e.g. red for warnings). Alpha 0 = default glass tint.
    vecgui::ColorU tint = vecgui::ColorU(0, 0, 0, 0);

    /// Optional extra drawing on top of the glass, before the text (pill's global position and size).
    std::function<void(vecgui::Vec2F pos, vecgui::Vec2F size)> decorate;

    void on_ready() override;
    void on_update(double dt) override;
    void on_draw() override;

private:
    void fit_to_text();

    std::shared_ptr<vecgui::Label> label_;
    std::string cache_key_;
    float scale_ = 1;
};

class Osd : public vecgui::NodeUi {
public:
    // ---- Inputs, set by PlayerRect ----
    bool playing = false;
    bool recording = false;
    int recording_seconds = 0;
    bool using_wifi = false;
    std::string video_text; // e.g. "1280×720 · 30 fps"
    std::string decoder_text;
    uint64_t bitrate_bps = 0;
    int packet_loss = 0;
    std::array<int, 2> link_score{}; // 1000..2000 per antenna (wifi only)
    int render_fps = 0;

    void on_ready() override;
    void on_update(double dt) override;
    void on_draw() override;

private:
    float scale() const;
    void draw_horizon(const telemetry::TelemetrySnapshot &t);
    void draw_signal_bars(vecgui::Vec2F origin, float quality);

    std::shared_ptr<GlassPill> rec_pill_;
    std::shared_ptr<GlassPill> heading_pill_;
    std::shared_ptr<GlassPill> link_pill_;
    std::shared_ptr<GlassPill> video_pill_;
    std::shared_ptr<GlassPill> flight_pill_;
    std::shared_ptr<GlassPill> status_pill_;

    telemetry::TelemetrySnapshot tlm_;
    bool tlm_fresh_ = false;
    bool attitude_fresh_ = false;
    bool tlm_ever_ = false;
    double blink_ = 0;
};
