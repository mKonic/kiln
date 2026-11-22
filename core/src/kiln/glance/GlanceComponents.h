#pragma once

// Glance's Material 3 widget components (glance-appwidget components/): a scaffold, a title
// bar and buttons in the widget's theme colours.

#include "kiln/glance/Glance.h"

namespace kiln::glance {

    // Scaffold: the widget's background (the theme's widgetBackground, the launcher's corner
    // radius, appWidgetBackground) with an optional title bar above the content, which is padded
    // horizontally.
    void Scaffold(GlanceModifier modifier, kiln::Function<void()> titleBar, kiln::Function<void()> content, ColorProvider backgroundColor = {},
                  float horizontalPadding = 12);
    inline void Scaffold(kiln::Function<void()> content) { Scaffold({}, {}, std::move(content)); }

    // TitleBar: an icon, the title and the actions (a row's content). iconColor none: the icon
    // untinted; unset colours: the theme's onSurface.
    void TitleBar(ImageProvider startIcon, std::string title, std::optional<ColorProvider> iconColor = ColorProvider{}, ColorProvider textColor = {},
                  GlanceModifier modifier = {}, std::optional<FontFamily> fontFamily = std::nullopt, kiln::Function<void()> actions = {});

    // FilledButton: a pill in the colours' background with an optional icon.
    void FilledButton(std::string text, Action onClick, GlanceModifier modifier = {}, bool enabled = true, std::optional<ImageProvider> icon = std::nullopt,
                      std::optional<ButtonColors> colors = std::nullopt, int maxLines = INT_MAX);
    void FilledButton(std::string text, kiln::Function<void()> onClick, GlanceModifier modifier = {}, bool enabled = true,
                      std::optional<ImageProvider> icon = std::nullopt, std::optional<ButtonColors> colors = std::nullopt, int maxLines = INT_MAX,
                      std::string key = {});
    // OutlineButton: a pill outlined in the content colour.
    void OutlineButton(std::string text, ColorProvider contentColor, Action onClick, GlanceModifier modifier = {}, bool enabled = true,
                       std::optional<ImageProvider> icon = std::nullopt, int maxLines = INT_MAX);
    void OutlineButton(std::string text, ColorProvider contentColor, kiln::Function<void()> onClick, GlanceModifier modifier = {}, bool enabled = true,
                       std::optional<ImageProvider> icon = std::nullopt, int maxLines = INT_MAX, std::string key = {});
    // SquareIconButton (60 dp; unset colours: primary and onPrimary) and CircleIconButton (48 dp;
    // unset: background and onSurface; backgroundColor none: no background).
    void SquareIconButton(ImageProvider imageProvider, std::optional<std::string> contentDescription, Action onClick, GlanceModifier modifier = {},
                          bool enabled = true, ColorProvider backgroundColor = {}, ColorProvider contentColor = {});
    void SquareIconButton(ImageProvider imageProvider, std::optional<std::string> contentDescription, kiln::Function<void()> onClick,
                          GlanceModifier modifier = {}, bool enabled = true, ColorProvider backgroundColor = {}, ColorProvider contentColor = {},
                          std::string key = {});
    void CircleIconButton(ImageProvider imageProvider, std::optional<std::string> contentDescription, Action onClick, GlanceModifier modifier = {},
                          bool enabled = true, std::optional<ColorProvider> backgroundColor = ColorProvider{}, ColorProvider contentColor = {});
    void CircleIconButton(ImageProvider imageProvider, std::optional<std::string> contentDescription, kiln::Function<void()> onClick,
                          GlanceModifier modifier = {}, bool enabled = true, std::optional<ColorProvider> backgroundColor = ColorProvider{},
                          ColorProvider contentColor = {}, std::string key = {});

}
