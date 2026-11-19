#pragma once

#include <vecgui/app.h>

#include "../player/video_player.h"
#include "osd.h"
#include "tip_label.h"

class GstDecoder;

class PlayerRect final : public vecgui::TextureRect {
public:
    std::shared_ptr<VideoPlayer> player_;

    // Preserved for reuse when restarting decoder.
    std::string play_url_;
    std::mutex play_mutex_;

    bool playing_ = false;

    bool force_software_decoding = false;

    std::shared_ptr<vecgui::VectorImage> logo_;
    std::shared_ptr<vecgui::RenderImage> render_image_;

    std::shared_ptr<TipLabel> tip_label_;

    bool is_recording = false;

    std::chrono::time_point<std::chrono::steady_clock> record_start_time;

    std::shared_ptr<vecgui::Timer> rx_status_update_timer;

    std::shared_ptr<Osd> osd_;

    std::weak_ptr<vecgui::Node> control_panel_;

    std::shared_ptr<vecgui::Button> video_stabilization_button_;
    std::shared_ptr<vecgui::Button> low_light_enhancement_button_;

    // std::shared_ptr<vecgui::Button> control_panel_button_;

    std::shared_ptr<vecgui::Button> record_button_;

    // Record when the signal had been lost.
    std::chrono::time_point<std::chrono::steady_clock> signal_lost_time_;

    void show_red_tip(std::string tip);

    void show_green_tip(std::string tip);

    void on_input(vecgui::InputEvent &event) override;

    void on_ready() override;

    void on_update(double dt) override;

    void on_draw() override;

    void start_playing(const std::string &url);

    void stop_playing();
};
