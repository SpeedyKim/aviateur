#pragma once

// "Glass" look for everything drawn over the video.
//
// Real frosted glass (blurring the video behind each panel) is not used on purpose: the vector
// renderer can only blur a shape's own shadow, not what is behind it, so a true backdrop blur would
// need an extra full-screen GPU pass every frame. Instead we fake the material the way most HUDs do:
// a dark, semi-transparent tint, a 1 px light rim, and rounded corners. It costs the same as any
// other flat panel, so it adds no measurable render time and no video latency.

#include <vecgui/resources/theme.h>

namespace glass {

// Colours (r, g, b, alpha 0-255).
inline const vecgui::ColorU kTint = vecgui::ColorU(16, 18, 24, 120);      // panel fill
inline const vecgui::ColorU kTintStrong = vecgui::ColorU(16, 18, 24, 175); // control panel / menus
inline const vecgui::ColorU kRim = vecgui::ColorU(255, 255, 255, 46);     // 1 px border highlight
inline const vecgui::ColorU kText = vecgui::ColorU(255, 255, 255, 240);
inline const vecgui::ColorU kTextDim = vecgui::ColorU(255, 255, 255, 150);
inline const vecgui::ColorU kAccent = vecgui::ColorU(10, 132, 255);        // system blue
inline const vecgui::ColorU kRed = vecgui::ColorU(255, 69, 58);
inline const vecgui::ColorU kAmber = vecgui::ColorU(255, 179, 64);
inline const vecgui::ColorU kGreen = vecgui::ColorU(48, 209, 88);

/// A rounded glass panel. `radius` < 0 means "pill" (radius = half the height, set at draw time).
inline vecgui::StyleBox panel(float radius = 14, vecgui::ColorU tint = kTint) {
    vecgui::StyleBox box;
    box.bg_color = tint;
    box.border_color = kRim;
    box.border_width = 1;
    box.corner_radius = radius;
    return box;
}

/// Glass version of the default dark theme, used for the control panel and menus.
inline std::shared_ptr<vecgui::Theme> make_theme() {
    auto theme = vecgui::Theme::default_dark();
    theme->accent_color = kAccent;

    // Buttons: faint frosted fill, brighter on hover.
    theme->button.colors["text"] = kText;
    theme->button.colors["text_disabled"] = kTextDim;
    {
        auto normal = panel(12, vecgui::ColorU(255, 255, 255, 26));
        theme->button.styles["normal"] = normal;

        auto hovered = normal;
        hovered.bg_color = vecgui::ColorU(255, 255, 255, 46);
        theme->button.styles["hovered"] = hovered;

        auto pressed = normal;
        pressed.bg_color = vecgui::ColorU(255, 255, 255, 60);
        pressed.border_color = kAccent;
        theme->button.styles["pressed"] = pressed;

        auto disabled = normal;
        disabled.bg_color = vecgui::ColorU(255, 255, 255, 10);
        disabled.border_color = vecgui::ColorU(255, 255, 255, 20);
        theme->button.styles["disabled"] = disabled;
    }

    theme->label.colors["text"] = kText;

    theme->panel.styles["background"] = panel(16, kTintStrong);

    // Tabs: no blocks, just a blue underline on the active tab.
    {
        auto none = vecgui::StyleBox::from_empty();
        theme->tab_container.styles["background"] = none;
        theme->tab_container.styles["tab_background"] = none;
        theme->tab_container.styles["tab_button_normal"] = none;

        auto pressed = none;
        pressed.border_widths = {0, 0, 0, 2};
        pressed.border_color = kAccent;
        theme->tab_container.styles["tab_button_pressed"] = pressed;
    }

    // Drop-down menus must stay readable, so they are nearly opaque.
    theme->popup_menu.styles["background"] = panel(12, vecgui::ColorU(28, 30, 36, 240));
    {
        auto hovered = theme->popup_menu.styles["item_hovered"];
        hovered.bg_color = kAccent;
        hovered.corner_radius = 8;
        theme->popup_menu.styles["item_hovered"] = hovered;
    }

    {
        auto normal = panel(10, vecgui::ColorU(0, 0, 0, 70));
        normal.border_color = vecgui::ColorU(255, 255, 255, 30);
        theme->text_edit.styles["normal"] = normal;
        auto focused = normal;
        focused.border_color = kAccent;
        theme->text_edit.styles["focused"] = focused;
        theme->text_edit.caret.color = kText;
    }

    theme->slider.colors["unfilled"] = vecgui::ColorU(255, 255, 255, 50);
    theme->slider.colors["filled"] = kAccent;
    theme->slider.colors["grabber_border"] = vecgui::ColorU(255, 255, 255, 0);
    theme->slider.colors["grabber_fill"] = vecgui::ColorU(255, 255, 255);

    return theme;
}

} // namespace glass
