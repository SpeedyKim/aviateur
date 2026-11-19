#include "player_rect.h"

#include "../gui_interface.h"
#include "control_panel.h"
#include "glass_theme.h"
#include "src/player/ffmpeg/video_player.h"

constexpr uint32_t TIP_FONT_SIZE = 20;

void PlayerRect::show_red_tip(std::string tip) {
    tip_label_->theme_override_bg.value().bg_color = vecgui::ColorU(glass::kRed.r_, glass::kRed.g_, glass::kRed.b_, 190);
    tip_label_->show_tip(tip);
}

void PlayerRect::show_green_tip(std::string tip) {
    tip_label_->theme_override_bg.value().bg_color =
        vecgui::ColorU(glass::kGreen.r_ / 2, glass::kGreen.g_ / 2, glass::kGreen.b_ / 2, 200);
    tip_label_->show_tip(tip);
}

void PlayerRect::on_input(vecgui::InputEvent &event) {
    if (event.type == vecgui::InputEventType::Key) {
        auto key_args = event.args.key;

        // if (key_args.key == vecgui::KeyCode::F11) {
        //     if (key_args.pressed) {
        //         fullscreen_button_->set_toggled(!fullscreen_button_->get_toggled());
        //     }
        // }

        if (playing_ && key_args.key == vecgui::KeyCode::F10) {
            if (key_args.pressed) {
                record_button_->trigger();
            }
        }
    }
}

void PlayerRect::on_ready() {
    auto onRtpStream = [this](std::string sdp_file) {
        play_mutex_.lock();
        play_url_ = sdp_file;
        play_mutex_.unlock();
    };
    GuiInterface::Instance().rtpStreamCallbacks.emplace_back(onRtpStream);

    auto context = get_context();

    auto show_hide_button = std::make_shared<vecgui::Button>();
    add_child(show_hide_button);
    show_hide_button->set_anchor_flag(vecgui::AnchorFlag::FullRect);
    show_hide_button->set_text("");
    show_hide_button->theme_override_normal = vecgui::StyleBox::from_empty();
    show_hide_button->theme_override_hovered = vecgui::StyleBox::from_empty();
    show_hide_button->theme_override_pressed = vecgui::StyleBox::from_empty();

    // On-screen display sits above the video but below the control panel. It ignores the mouse,
    // so clicking anywhere on the video still toggles the control panel.
    osd_ = std::make_shared<Osd>();
    osd_->set_anchor_flag(vecgui::AnchorFlag::FullRect);
    add_child(osd_);

    // MAVLink telemetry for the OSD (from the radio link, or tools/fake_telemetry.py).
    telemetry::TelemetryHub::instance().start(14550);

    auto control_panel = std::make_shared<ControlPanel>();
    control_panel->set_anchor_flag(vecgui::AnchorFlag::Center);
    control_panel->set_custom_minimum_size({640, 400});
    add_child(control_panel);
    control_panel_ = control_panel;

    std::weak_ptr control_panel_weak = control_panel;

    auto on_wifi_stopped = [control_panel_weak, this] {
        if (!control_panel_weak.expired()) {
            stop_playing();
            control_panel_weak.lock()->update_adapter_start_button_looking(true);
        }
    };
    GuiInterface::Instance().wifiStopCallbacks.emplace_back(on_wifi_stopped);

    auto on_control_panel_triggered = [control_panel_weak]() {
        bool visible = false;
        if (!control_panel_weak.expired()) {
            visible = control_panel_weak.lock()->get_visibility();
        }
        control_panel_weak.lock()->set_visibility(!visible);
    };
    show_hide_button->connect_signal("triggered", on_control_panel_triggered);

    const int button_height = 48;

    auto video_tab = std::make_shared<vecgui::MarginContainer>();
    video_tab->set_margin_all(8);

    auto video_tab_grid = std::make_shared<vecgui::GridContainer>();
    video_tab_grid->set_column_limit(3);
    video_tab->add_child(video_tab_grid);
    control_panel->video_tab_ = video_tab;

    logo_ = std::make_shared<vecgui::VectorImage>(context, vecgui::get_asset_dir("openipc-logo-white.svg"));
    texture = logo_;

    render_image_ = std::make_shared<vecgui::RenderImage>(context, vecgui::Vec2I{1920, 1080});

    set_stretch_mode(StretchMode::KeepAspectCentered);

    tip_label_ = std::make_shared<TipLabel>();
    tip_label_->set_visibility(false);
    tip_label_->set_word_wrap(false);
    tip_label_->set_horizontal_alignment(vecgui::Alignment::Center);
    tip_label_->set_vertical_alignment(vecgui::Alignment::Center);
    tip_label_->set_mouse_filter(vecgui::MouseFilter::Ignore);
    add_child(tip_label_);

    // Video info for the OSD.
    auto on_decoder_ready = [this](uint32_t width, uint32_t height, float fps, std::string decoder_name) {
        osd_->video_text = std::format("{}\u00D7{}  {} fps", width, height, int(round(fps)));
        osd_->decoder_text = decoder_name;
    };
    GuiInterface::Instance().decoderReadyCallbacks.emplace_back(on_decoder_ready);

    // Link stats for the OSD, 10x per second.
    rx_status_update_timer = std::make_shared<vecgui::Timer>();
    add_child(rx_status_update_timer);

    auto callback = [this] {
        osd_->using_wifi = GuiInterface::Instance().is_using_wifi;
        if (GuiInterface::Instance().link_) {
            auto link_score = GuiInterface::Instance().link_->get_link_score();
            for (int j = 0; j < 2 && j < ANTENNA_COUNT; ++j) {
                osd_->link_score[j] = link_score[j];
            }
            osd_->packet_loss = GuiInterface::Instance().link_->get_packet_loss();
        }
        rx_status_update_timer->start_timer(0.1);
    };
    rx_status_update_timer->connect_signal("timeout", callback);
    rx_status_update_timer->start_timer(0.1);

    auto capture_button = std::make_shared<vecgui::Button>();
    capture_button->set_custom_minimum_size({0, button_height});
    capture_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    video_tab_grid->add_child(capture_button);
    capture_button->set_text(context->translation_server->get_translation("capture frame"));
    auto icon = std::make_shared<vecgui::VectorImage>(context, vecgui::get_asset_dir("CaptureImage.svg"), true);
    capture_button->set_icon_normal(icon);
    auto capture_callback = [this, context] {
        std::string output_file;
        if (player_) {
            output_file = player_->capture_jpeg();
        }

        if (output_file.empty()) {
            show_red_tip(context->translation_server->get_translation("capture fail"));
        } else {
            show_green_tip(context->translation_server->get_translation("frame saved") + output_file);
        }
    };
    capture_button->connect_signal("triggered", capture_callback);

    record_button_ = std::make_shared<vecgui::Button>();
    record_button_->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
    video_tab_grid->add_child(record_button_);
    auto icon2 = std::make_shared<vecgui::VectorImage>(context, vecgui::get_asset_dir("RecordVideo.svg"), true);
    record_button_->set_icon_normal(icon2);
    record_button_->set_text(context->translation_server->get_translation("record mp4") + " (F10)");

    auto record_button_raw = record_button_.get();
    auto record_callback = [record_button_raw, this, context] {
        if (!is_recording) {
            if (player_) {
                is_recording = player_->start_mp4_recording();
            }

            if (is_recording) {
                record_button_raw->set_text(context->translation_server->get_translation("stop recording") + " (F10)");

                record_start_time = std::chrono::steady_clock::now();
            } else {
                show_red_tip(context->translation_server->get_translation("record fail"));
            }
        } else {
            is_recording = false;

            auto output_file = player_->stop_mp4_recording();

            record_button_raw->set_text(context->translation_server->get_translation("record mp4") + " (F10)");

            if (output_file.empty()) {
                show_red_tip(context->translation_server->get_translation("save record fail"));
            } else {
                show_green_tip(context->translation_server->get_translation("video saved") + output_file);
            }
        }
    };
    record_button_->connect_signal("triggered", record_callback);

    {
        auto open_capture_folder_button = std::make_shared<vecgui::MenuButton>();

        open_capture_folder_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        video_tab_grid->add_child(open_capture_folder_button);
        open_capture_folder_button->set_text(context->translation_server->get_translation("capture folder"));

        auto callback = [] { open_explorer(GuiInterface::GetCaptureDir()); };
        open_capture_folder_button->connect_signal("triggered", callback);
    }

    {
        auto sw_button = std::make_shared<vecgui::CheckButton>();
        sw_button->set_custom_minimum_size({0, button_height});
        sw_button->container_sizing.flag_h = vecgui::ContainerSizingFlag::Fill;
        sw_button->set_text(context->translation_server->get_translation("force sw decoding"));
        video_tab_grid->add_child(sw_button);

        auto callback = [this](bool toggled) {
            force_software_decoding = toggled;
            if (playing_) {
                player_->stop();
                player_->play(play_url_, force_software_decoding);
            }
        };
        sw_button->connect_signal("toggled", callback);
    }

    // {
    //     video_stabilization_button_ = std::make_shared<vecgui::CheckButton>();
    //     video_stabilization_button_->set_text(FTR("video stab"));
    //     vbox->add_child(video_stabilization_button_);
    //
    //     auto callback = [this](bool toggled) {
    //         player_->yuvRenderer_->mStabilize = toggled;
    //         if (toggled) {
    //             show_red_tip(FTR("video stab warning"));
    //         }
    //     };
    //     video_stabilization_button_->connect_signal("toggled", callback);
    // }
    //
    // {
    //     low_light_enhancement_button_ = std::make_shared<vecgui::CheckButton>();
    //     low_light_enhancement_button_->set_text(FTR("low light enhancement"));
    //     vbox->add_child(low_light_enhancement_button_);
    //
    //     auto callback = [this](bool toggled) { player_->yuvRenderer_->mLowLightEnhancement = toggled; };
    //     low_light_enhancement_button_->connect_signal("toggled", callback);
    // }

    auto onBitrateUpdate = [this](uint64_t bitrate) { osd_->bitrate_bps = bitrate; };
    GuiInterface::Instance().bitrateUpdateCallbacks.emplace_back(onBitrateUpdate);

    auto onTipUpdate = [this](std::string msg, bool bad_news) {
        auto context = this->get_context();
        auto translated_msg = context->translation_server->get_translation(msg);

        if (bad_news) {
            show_red_tip(translated_msg);
        } else {
            show_green_tip(translated_msg);
        }
    };
    GuiInterface::Instance().tipCallbacks.emplace_back(onTipUpdate);

    auto onUrlStreamShouldStop = [this] { stop_playing(); };
    GuiInterface::Instance().urlStreamShouldStopCallbacks.emplace_back(onUrlStreamShouldStop);
}

void PlayerRect::on_update(double dt) {
    if (!playing_) {
        play_mutex_.lock();
        const auto has_url = !play_url_.empty();
        play_mutex_.unlock();

        if (has_url) {
            start_playing(play_url_);
        }
    }

    auto context = get_context();

    if (player_) {
        player_->update(dt);

        // Dynamic resolution adaptation: Resize render image if video dimensions changed
        if (playing_ && player_->video_width() > 0 && player_->video_height() > 0) {
            auto current_size = render_image_->get_size();
            if (current_size.x != player_->video_width() || current_size.y != player_->video_height()) {
                GuiInterface::Instance().PutLog(LogLevel::Info,
                                                "Resizing render image to {}x{}",
                                                player_->video_width(),
                                                player_->video_height());
                render_image_ = std::make_shared<vecgui::RenderImage>(
                    context,
                    Pathfinder::Vec2I{player_->video_width(), player_->video_height()});
                texture = render_image_;
            }
        }
    }

    osd_->playing = playing_;
    osd_->render_fps = context->engine->get_fps_int();
    osd_->recording = is_recording;
    if (is_recording) {
        std::chrono::duration<double> duration = std::chrono::steady_clock::now() - record_start_time;
        osd_->recording_seconds = int(duration.count());
    }

    // Toast messages: a glass pill near the top centre.
    if (tip_label_->get_visibility()) {
        const float s = std::clamp(get_size().y / 720.f, 0.6f, 2.5f);
        tip_label_->set_font_size(uint32_t(TIP_FONT_SIZE * s));
        auto text_size = tip_label_->get_effective_minimum_size();
        vecgui::Vec2F box = {text_size.x + 36.f * s, text_size.y + 16.f * s};
        box.x = std::min(box.x, get_size().x - 32.f * s);
        tip_label_->set_size(box);
        tip_label_->set_position({(get_size().x - box.x) * 0.5f, 64.f * s});
        tip_label_->theme_override_bg->corner_radius = box.y * 0.5f;
    }
}

void PlayerRect::on_draw() {
    if (!playing_) {
        return;
    }
    if (player_) {
        auto render_image = (vecgui::RenderImage *)texture.get();
        player_->render(render_image->get_texture());
    }
}

template <class DstType, class SrcType>
bool IsType(const SrcType *src) {
    return dynamic_cast<const DstType *>(src) != nullptr;
}

void PlayerRect::start_playing(const std::string &url) {
    if (playing_) {
        return;
    }

    playing_ = true;

    auto render_context = get_context()->render_context;

    if (!player_) {
        GuiInterface::Instance().PutLog(LogLevel::Info, "Creating video player");
        player_ = std::make_shared<VideoPlayerFfmpeg>(render_context->get_device(), render_context->get_queue());
    }

    player_->play(url, force_software_decoding);

    texture = render_image_;

    // Get the menus out of the way once video starts; click the video to bring them back.
    if (auto panel = control_panel_.lock()) {
        panel->set_visibility(false);
    }
}

void PlayerRect::stop_playing() {
    playing_ = false;

    play_mutex_.lock();
    play_url_ = "";
    play_mutex_.unlock();

    osd_->link_score = {};
    osd_->video_text.clear();
    osd_->decoder_text.clear();
    osd_->bitrate_bps = 0;

    if (is_recording) {
        record_button_->trigger();
    }

    // Fix crash in WfbReceiver destructor.
    if (player_) {
        player_->stop();
    }

    texture = logo_;

    if (auto panel = control_panel_.lock()) {
        panel->set_visibility(true);
    }
}
