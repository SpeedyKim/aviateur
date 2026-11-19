#include "osd.h"

#include <cmath>
#include <format>

#include "glass_theme.h"

namespace {

constexpr float kRefHeight = 720.f; // layout is designed at 1280x720 and scaled
constexpr float kMargin = 16.f;
constexpr float kPadX = 14.f;
constexpr float kPadY = 7.f;
constexpr float kValueFont = 19.f;
constexpr float kCaptionFont = 13.f;
constexpr float kPitchPxPerDeg = 6.f;
constexpr float kRad2Deg = 57.29577951f;

std::string format_bitrate(uint64_t bps) {
    if (bps >= 1000 * 1000) {
        return std::format("{:.1f} Mbps", bps / 1e6);
    }
    if (bps >= 1000) {
        return std::format("{:.0f} kbps", bps / 1e3);
    }
    return std::format("{} bps", bps);
}

std::string format_distance(float m) {
    if (m >= 1000.f) {
        return std::format("{:.2f} km", m / 1000.f);
    }
    return std::format("{:.0f} m", m);
}

const char *cardinal(int deg) {
    static const char *names[] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
    deg = ((deg % 360) + 360) % 360;
    return names[((deg + 22) / 45) % 8];
}

vecgui::ColorU with_alpha(vecgui::ColorU c, uint8_t a) {
    c.a_ = a;
    return c;
}

} // namespace

// ---------------------------------------------------------------- GlassPill

void GlassPill::on_ready() {
    set_mouse_filter(vecgui::MouseFilter::Ignore);
    label_ = std::make_shared<vecgui::Label>();
    label_->set_mouse_filter(vecgui::MouseFilter::Ignore);
    label_->theme_override_bg = vecgui::StyleBox::from_empty();
    label_->set_horizontal_alignment(vecgui::Alignment::Begin);
    label_->set_bidi_alignment(vecgui::BidiAlignment::Begin);
    add_child(label_);
}

void GlassPill::set_items(const std::vector<Item> &items, float scale) {
    // Only rebuild the text when something visible changed; re-shaping text every frame is wasted CPU.
    std::string key = std::to_string(int(scale * 100)) + "|" + std::to_string(int(left_inset));
    for (const auto &it : items) {
        key += it.caption + "\x1f" + it.value + "\x1f" + std::to_string(it.color.a_) + std::to_string(it.color.r_) +
               "\x1e";
    }
    if (key == cache_key_ && label_) {
        return;
    }
    cache_key_ = key;
    scale_ = scale;

    label_->set_text("");
    label_->clear_spans();
    label_->set_font_size(uint32_t(kValueFont * scale));
    bool first = true;
    for (const auto &it : items) {
        if (!first) {
            label_->add_span({.text = "    ",
                              .style = vecgui::TextStyle(glass::kTextDim),
                              .font_size = uint32_t(kCaptionFont * scale)});
        }
        first = false;
        if (!it.caption.empty()) {
            label_->add_span({.text = it.caption + " ",
                              .style = vecgui::TextStyle(glass::kTextDim),
                              .font_size = uint32_t(kCaptionFont * scale)});
        }
        label_->add_span(
            {.text = it.value, .style = vecgui::TextStyle(it.color), .font_size = uint32_t(kValueFont * scale)});
    }

    fit_to_text();
}

void GlassPill::fit_to_text() {
    // The label re-measures its text lazily, so we re-fit every frame (cheap: no text shaping here).
    // Measure from the shaped glyphs: the label's own minimum size ignores per-span font sizes.
    auto text_size = label_->get_effective_minimum_size();
    float width = 0;
    for (const auto &g : label_->get_glyphs()) {
        width += g.x_advance;
    }
    text_size.x = width;
    const float inset = left_inset * scale_;
    label_->set_position({kPadX * scale_ + inset, kPadY * scale_});
    label_->set_size(text_size);
    set_size({text_size.x + 2 * kPadX * scale_ + inset, text_size.y + 2 * kPadY * scale_});
}

void GlassPill::on_update(double dt) {
    if (label_) {
        fit_to_text();
    }
}

void GlassPill::on_draw() {
    auto context = get_context();
    if (!context) {
        return;
    }
    auto box = glass::panel(size.y * 0.5f, tint.a_ ? tint : glass::kTint);
    context->vector_server->draw_style_box(box, get_global_position(), size);
    if (decorate) {
        decorate(get_global_position(), size);
    }
}

// ---------------------------------------------------------------- Osd

float Osd::scale() const {
    return std::clamp(size.y / kRefHeight, 0.6f, 2.5f);
}

void Osd::on_ready() {
    set_mouse_filter(vecgui::MouseFilter::Ignore);

    auto make = [this] {
        auto p = std::make_shared<GlassPill>();
        add_child(p);
        p->set_visibility(false);
        return p;
    };
    rec_pill_ = make();
    rec_pill_->left_inset = 16;
    rec_pill_->decorate = [this](vecgui::Vec2F p, vecgui::Vec2F sz) {
        // Blinking red recording dot in the pill's left inset.
        if (std::fmod(blink_, 1.0) < 0.6) {
            const float s = scale();
            get_context()->vector_server->draw_circle(
                {p.x + kPadX * s + 5.f * s, p.y + sz.y * 0.5f}, 5.f * s, 0, true, glass::kRed);
        }
    };
    heading_pill_ = make();
    link_pill_ = make();
    link_pill_->left_inset = 26;
    link_pill_->decorate = [this](vecgui::Vec2F p, vecgui::Vec2F sz) {
        if (using_wifi) {
            const float s = scale();
            const float q = ((link_score[0] + link_score[1]) * 0.5f - 1000.f) / 1000.f;
            draw_signal_bars({p.x + kPadX * s, p.y + sz.y * 0.5f + 7.f * s}, q);
        }
    };
    video_pill_ = make();
    flight_pill_ = make();
    status_pill_ = make();
}

void Osd::on_update(double dt) {
    blink_ += dt;

    auto &hub = telemetry::TelemetryHub::instance();
    tlm_ = hub.snapshot();
    tlm_fresh_ = telemetry::TelemetryHub::fresh(tlm_.last_packet, 2.0);
    attitude_fresh_ = tlm_.has_attitude && telemetry::TelemetryHub::fresh(tlm_.last_attitude, 1.0);
    tlm_ever_ = tlm_.packets_ok > 0;

    const float s = scale();
    const float m = kMargin * s;
    const auto W = size.x;
    const auto H = size.y;

    // ---- Top left: recording ----
    rec_pill_->set_visibility(playing && recording);
    if (playing && recording) {
        const int h = recording_seconds / 3600, mi = (recording_seconds % 3600) / 60, se = recording_seconds % 60;
        std::string t = h > 0 ? std::format("{}:{:02}:{:02}", h, mi, se) : std::format("{:02}:{:02}", mi, se);
        rec_pill_->set_items({{"REC", t}}, s);
        rec_pill_->set_position({m, m});
    }

    // ---- Top right: link ----
    link_pill_->set_visibility(playing);
    if (playing) {
        std::vector<GlassPill::Item> items;
        items.push_back({using_wifi ? "" : "UDP", format_bitrate(bitrate_bps)});
        if (using_wifi && packet_loss > 0) {
            items.push_back({"LOST", std::to_string(packet_loss), glass::kAmber});
        }
        link_pill_->left_inset = using_wifi ? 26 : 0;
        link_pill_->set_items(items, s);
        link_pill_->set_position({W - m - link_pill_->get_size().x, m});
    }

    // ---- Bottom left: video ----
    video_pill_->set_visibility(playing && !video_text.empty());
    if (playing && !video_text.empty()) {
        std::vector<GlassPill::Item> items = {{"", video_text}};
        if (!decoder_text.empty()) {
            items.push_back({"", decoder_text, glass::kTextDim});
        }
        video_pill_->set_items(items, s);
        video_pill_->set_position({m, H - m - video_pill_->get_size().y});
    }

    // ---- Top centre: heading ----
    const bool show_heading = playing && tlm_fresh_ && (tlm_.has_vfr || tlm_.has_attitude);
    heading_pill_->set_visibility(show_heading);
    if (show_heading) {
        int hdg = tlm_.has_vfr ? tlm_.heading : int(std::lround(tlm_.yaw * kRad2Deg));
        hdg = ((hdg % 360) + 360) % 360;
        heading_pill_->set_items({{cardinal(hdg), std::format("{:03}°", hdg)}}, s);
        heading_pill_->set_position({(W - heading_pill_->get_size().x) * 0.5f, m});
    }

    // ---- Bottom centre: flight data ----
    std::vector<GlassPill::Item> flight;
    if (tlm_fresh_) {
        if (tlm_.has_battery) {
            auto color = glass::kText;
            if (tlm_.battery_remaining >= 0 && tlm_.battery_remaining < 20) {
                color = glass::kRed;
            } else if (tlm_.battery_remaining >= 0 && tlm_.battery_remaining < 35) {
                color = glass::kAmber;
            }
            std::string v = std::format("{:.1f} V", tlm_.battery_voltage);
            if (tlm_.battery_remaining >= 0) {
                v += std::format("  {}%", tlm_.battery_remaining);
            }
            flight.push_back({"BAT", v, color});
        }
        if (tlm_.has_vfr) {
            flight.push_back({"SPD", std::format("{:.0f} km/h", tlm_.groundspeed * 3.6f)});
        }
        if (tlm_.has_position) {
            flight.push_back({"ALT", std::format("{:.0f} m", tlm_.relative_alt)});
        } else if (tlm_.has_vfr) {
            flight.push_back({"ALT", std::format("{:.0f} m", tlm_.alt_msl)});
        }
        if (tlm_.has_home) {
            flight.push_back({"HOME", format_distance(tlm_.home_distance)});
        }
        if (tlm_.has_gps) {
            auto color = tlm_.fix_type >= 3 ? glass::kText : glass::kAmber;
            flight.push_back({"GPS", std::to_string(tlm_.satellites), color});
        }
    }
    flight_pill_->set_visibility(playing && !flight.empty());
    if (playing && !flight.empty()) {
        flight_pill_->set_items(flight, s);
        flight_pill_->set_position(
            {(W - flight_pill_->get_size().x) * 0.5f, H - m - flight_pill_->get_size().y});
    }

    // ---- Bottom right: armed / telemetry status ----
    bool show_status = false;
    if (playing && tlm_fresh_ && tlm_.has_heartbeat) {
        show_status = true;
        status_pill_->tint = {};
        status_pill_->set_items({{"", tlm_.armed ? "ARMED" : "DISARMED", tlm_.armed ? glass::kAmber : glass::kTextDim}},
                                s);
    } else if (playing && tlm_ever_ && !tlm_fresh_) {
        show_status = true;
        status_pill_->tint = with_alpha(glass::kRed, std::fmod(blink_, 1.0) < 0.5 ? 170 : 110);
        status_pill_->set_items({{"", "TELEMETRY LOST"}}, s);
    }
    status_pill_->set_visibility(show_status);
    if (show_status) {
        status_pill_->set_position(
            {W - m - status_pill_->get_size().x, H - m - status_pill_->get_size().y});
    }
}

void Osd::draw_signal_bars(vecgui::Vec2F origin, float quality) {
    auto vs = get_context()->vector_server;
    const float s = scale();
    const int bars = 4;
    const float bw = 3.f * s, gap = 2.f * s, max_h = 14.f * s;
    const int lit = int(std::ceil(std::clamp(quality, 0.f, 1.f) * bars));
    const auto on = quality < 0.25f ? glass::kRed : (quality < 0.5f ? glass::kAmber : glass::kText);
    for (int i = 0; i < bars; i++) {
        const float h = max_h * (i + 1) / bars;
        const float x = origin.x + i * (bw + gap) + bw * 0.5f;
        vs->draw_line({x, origin.y}, {x, origin.y - h}, bw, i < lit ? on : vecgui::ColorU(255, 255, 255, 60));
    }
}

void Osd::draw_horizon(const telemetry::TelemetrySnapshot &t) {
    auto vs = get_context()->vector_server;
    const float s = scale();
    const auto c = get_global_position() + size * 0.5f;

    const float theta = -t.roll;
    const vecgui::Vec2F dir = {std::cos(theta), std::sin(theta)};
    const vecgui::Vec2F nrm = {-std::sin(theta), std::cos(theta)};
    const float pitch_deg = std::clamp(t.pitch * kRad2Deg, -25.f, 25.f);
    const auto mid = c + nrm * (pitch_deg * kPitchPxPerDeg * s);

    const float half = 150.f * s, gap = 46.f * s;
    const auto shadow = vecgui::ColorU(0, 0, 0, 90);
    const auto line = vecgui::ColorU(255, 255, 255, 215);

    for (int side : {-1, 1}) {
        const auto a = mid + dir * (side * gap);
        const auto b = mid + dir * (side * half);
        vs->draw_line(a, b, 4.f * s, shadow);
        vs->draw_line(a, b, 2.f * s, line);
    }

    // Fixed aircraft symbol.
    const float w1 = 30.f * s, w0 = 12.f * s;
    for (int side : {-1, 1}) {
        const vecgui::Vec2F a = {c.x + side * w0, c.y};
        const vecgui::Vec2F b = {c.x + side * w1, c.y};
        vs->draw_line(a, b, 4.f * s, shadow);
        vs->draw_line(a, b, 2.f * s, glass::kAmber);
    }
    vs->draw_circle(c, 3.f * s, 0, true, glass::kAmber);
}

void Osd::on_draw() {
    if (!playing) {
        return;
    }
    auto context = get_context();
    if (!context) {
        return;
    }
    if (attitude_fresh_) {
        draw_horizon(tlm_);
    }
}
