#pragma once

// Home-screen widgets in Glance's model (androidx glance-appwidget): the widget's content is a
// tree of Glance elements (Box, Row, Column, Text, Image, Button, LazyColumn...) built by a
// function, which kiln turns into RemoteViews (RemoteViewsTranslator) for each size the launcher
// shows it at (SizeMode), and redraws when the app calls Update.
//
//   // At namespace scope: the system starts the app's process to draw or update it.
//   kiln::glance::WidgetRegistration notes("notes", {
//       .content = [](const kiln::glance::WidgetContext& w) {
//           using namespace kiln::glance;
//           Column(FillMaxSize(Background(GlanceModifier{}, 0xFFFFFFFF)), {}, [] {
//               Text("Notes", {}, { .fontSize = 18, .fontWeight = FontWeight::Bold });
//               Button("Add", RunCallback("add"));
//           });
//       },
//   });
//   kiln::glance::ActionCallbackRegistration add("add", [](const kiln::glance::ActionContext&) { ... });
//
// The app declares each widget: WIDGETS in kiln_add_app (or a receiver extending
// dev.kiln.KilnWidgetReceiver in a Gradle app) and res/xml/<name>_widget.xml, its
// appwidget-provider info.

#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kiln::glance {

    // A colour: ARGB, with an optional one for night mode (DayNightColorProvider).
    struct ColorProvider {
        std::uint32_t day = 0, night = 0;
        bool set = false;
        ColorProvider() = default;
        ColorProvider(std::uint32_t argb) : day(argb), night(argb), set(true) {}
        ColorProvider(std::uint32_t dayArgb, std::uint32_t nightArgb) : day(dayArgb), night(nightArgb), set(true) {}
        bool operator==(const ColorProvider&) const = default;
    };

    // Dimension: wrap the content, fill the parent, share the parent's remaining space (Expand,
    // GlanceModifier.defaultWeight), or a size in dp.
    struct Dimension {
        enum class Kind { Wrap, Fill, Expand, Dp };
        Kind kind = Kind::Wrap;
        float dp = 0;
        static Dimension Wrap() { return {}; }
        static Dimension Fill() { return { Kind::Fill, 0 }; }
        static Dimension Expand() { return { Kind::Expand, 0 }; }
        static Dimension Dp(float v) { return { Kind::Dp, v }; }
        bool operator==(const Dimension&) const = default;
    };

    // What a tap does: open the app at a deep link (actionStartActivity), or run a registered
    // callback in the app's process (actionRunCallback), the widget redrawn after it.
    struct Action {
        enum class Kind { None, StartActivity, RunCallback };
        Kind kind = Kind::None;
        std::string uri;      // StartActivity: the deep link the activity gets (empty: just open the app)
        std::string key;      // RunCallback: the callback's name
        std::string params;   // RunCallback: what it is given
        bool operator==(const Action&) const = default;
    };
    Action StartActivity(std::string deepLink = {});
    Action RunCallback(std::string key, std::string params = {});

    enum class Visibility { Visible, Invisible, Gone };

    class GlanceModifier {
    public:
        struct Item {
            enum class Kind { Width, Height, Padding, BackgroundColor, BackgroundResource, CornerRadius, Clickable, Visibility, ContentDescription, Enabled };
            Kind kind;
            Dimension dimension;
            float left = 0, top = 0, right = 0, bottom = 0;
            ColorProvider color;
            std::string text;
            Action action;
            int value = 0;
        };
        GlanceModifier Then(Item item) const {
            GlanceModifier m = *this;
            m.m_Items.push_back(std::move(item));
            return m;
        }
        const std::vector<Item>& Items() const { return m_Items; }

    private:
        std::vector<Item> m_Items;
    };

    GlanceModifier Width(GlanceModifier m, Dimension width);
    GlanceModifier Height(GlanceModifier m, Dimension height);
    GlanceModifier Size(GlanceModifier m, float dp);
    GlanceModifier Size(GlanceModifier m, float widthDp, float heightDp);
    GlanceModifier FillMaxWidth(GlanceModifier m);
    GlanceModifier FillMaxHeight(GlanceModifier m);
    GlanceModifier FillMaxSize(GlanceModifier m);
    GlanceModifier WrapContentSize(GlanceModifier m);
    // In a Row or Column: share the remaining space with the other weighted children.
    GlanceModifier DefaultWeight(GlanceModifier m);
    GlanceModifier Padding(GlanceModifier m, float all);
    GlanceModifier Padding(GlanceModifier m, float horizontal, float vertical);
    GlanceModifier Padding(GlanceModifier m, float start, float top, float end, float bottom);
    GlanceModifier Background(GlanceModifier m, ColorProvider color);
    // An app drawable by name (res/drawable/<name>).
    GlanceModifier BackgroundResource(GlanceModifier m, std::string drawable);
    GlanceModifier CornerRadius(GlanceModifier m, float dp);
    GlanceModifier Clickable(GlanceModifier m, Action action);
    GlanceModifier VisibilityOf(GlanceModifier m, Visibility v);
    GlanceModifier ContentDescription(GlanceModifier m, std::string description);

    // Alignment, as Android gravity.
    enum class HorizontalAlignment { Start, CenterHorizontally, End };
    enum class VerticalAlignment { Top, CenterVertically, Bottom };
    struct Alignment {
        HorizontalAlignment horizontal = HorizontalAlignment::Start;
        VerticalAlignment vertical = VerticalAlignment::Top;
        static Alignment TopStart() { return {}; }
        static Alignment Center() { return { HorizontalAlignment::CenterHorizontally, VerticalAlignment::CenterVertically }; }
        static Alignment CenterStart() { return { HorizontalAlignment::Start, VerticalAlignment::CenterVertically }; }
        static Alignment BottomEnd() { return { HorizontalAlignment::End, VerticalAlignment::Bottom }; }
        bool operator==(const Alignment&) const = default;
    };
    int Gravity(Alignment a);

    enum class FontWeight { Normal, Medium, Bold };
    enum class TextAlign { Start, Center, End };
    struct TextStyle {
        ColorProvider color;          // unset: the platform's
        float fontSize = 0;           // sp; 0: the platform's
        FontWeight fontWeight = FontWeight::Normal;
        bool italic = false;
        bool underline = false, lineThrough = false;
        TextAlign textAlign = TextAlign::Start;
        bool operator==(const TextStyle&) const = default;
    };

    enum class ContentScale { Fit, Crop, FillBounds };
    // An image: an app drawable by name, or encoded bytes (PNG, JPEG, WebP).
    struct ImageProvider {
        std::string resource;
        std::vector<std::uint8_t> bytes;
        static ImageProvider Resource(std::string drawable) { return { std::move(drawable), {} }; }
        static ImageProvider Bitmap(std::vector<std::uint8_t> encoded) { return { {}, std::move(encoded) }; }
    };

    // --- elements (Emittables): call inside a widget's content ------------------------------
    void Box(GlanceModifier modifier, Alignment contentAlignment, kiln::Function<void()> content = {});
    void Row(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content = {});
    void Column(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content = {});
    void Text(std::string text, GlanceModifier modifier = {}, TextStyle style = {}, int maxLines = 0);
    void Image(ImageProvider provider, std::string contentDescription, GlanceModifier modifier = {},
               ContentScale contentScale = ContentScale::Fit, ColorProvider tint = {}, float alpha = 1);
    void Button(std::string text, Action onClick, GlanceModifier modifier = {}, bool enabled = true,
                ColorProvider backgroundColor = {}, ColorProvider contentColor = {});
    void Spacer(GlanceModifier modifier = {});
    // progress < 0: indeterminate.
    void LinearProgressIndicator(float progress, GlanceModifier modifier = {}, ColorProvider color = {}, ColorProvider backgroundColor = {});
    void CircularProgressIndicator(GlanceModifier modifier = {}, ColorProvider color = {});
    // onCheckedChange (a RunCallback) gets "checked=true" or "checked=false" in ActionContext::checked.
    void CheckBox(bool checked, Action onCheckedChange, std::string text = {}, GlanceModifier modifier = {}, ColorProvider textColor = {});
    void Switch(bool checked, Action onCheckedChange, std::string text = {}, GlanceModifier modifier = {}, ColorProvider textColor = {});
    // A scrolling list; each item (one element, its id stable across updates) is added with Item.
    class LazyListScope {
    public:
        void Item(std::int64_t id, kiln::Function<void()> content);
        void Items(int count, kiln::Function<std::int64_t(int)> id, kiln::Function<void(int)> content);
    };
    void LazyColumn(GlanceModifier modifier, kiln::Function<void(LazyListScope&)> content);

    // --- widgets ------------------------------------------------------------------------------

    // SizeMode: Single draws once, at the widget's minimum size; Exact draws for each size the
    // launcher shows it at; Responsive draws for the given sizes, the launcher picking the best.
    enum class SizeMode { Single, Exact, Responsive };

    struct WidgetContext {
        std::string name;
        int appWidgetId = 0;
        ui::Size size;   // dp: what this drawing is for (LocalSize)
    };

    struct GlanceAppWidget {
        SizeMode sizeMode = SizeMode::Single;
        std::vector<ui::Size> responsiveSizes;   // dp
        kiln::Function<void(const WidgetContext&)> content;
    };
    void RegisterWidget(std::string name, GlanceAppWidget widget);
    struct WidgetRegistration {
        WidgetRegistration(std::string name, GlanceAppWidget widget) { RegisterWidget(std::move(name), std::move(widget)); }
    };

    struct ActionContext {
        std::string widget;       // the widget's name
        int appWidgetId = 0;
        std::string params;
        std::optional<bool> checked;   // a CheckBox's or Switch's new state
    };
    void RegisterActionCallback(std::string key, kiln::Function<void(const ActionContext&)> callback);
    struct ActionCallbackRegistration {
        ActionCallbackRegistration(std::string key, kiln::Function<void(const ActionContext&)> callback) {
            RegisterActionCallback(std::move(key), std::move(callback));
        }
    };

    // Redraws every placed instance of the widget (GlanceAppWidget.updateAll).
    void UpdateAll(const std::string& widget);
    // The ids of its placed instances.
    std::vector<int> AppWidgetIds(const std::string& widget);
    // Asks the launcher to place one (requestPinAppWidget; false: the launcher cannot).
    bool RequestPin(const std::string& widget);

    // --- what the platform's host uses ---------------------------------------------------------

    // The sizes to draw for: the launcher's (Exact), the responsive ones, or the minimum (Single).
    std::vector<ui::Size> SizesToDraw(const GlanceAppWidget& widget, const std::vector<ui::Size>& launcherSizes, ui::Size minimum);
    // The content for one size, encoded for the host's RemoteViews translation (KilnGlance.java).
    // Empty when the widget is not registered.
    std::vector<std::uint8_t> Render(const std::string& widget, const WidgetContext& context);
    const GlanceAppWidget* FindWidget(const std::string& name);
    // Runs a callback (true when it exists); the host redraws the widget after it.
    bool RunActionCallback(const std::string& key, const ActionContext& context);

    struct GlanceHost {
        kiln::Function<void(const std::string&)> updateAll;
        kiln::Function<std::vector<int>(const std::string&)> appWidgetIds;
        kiln::Function<bool(const std::string&)> requestPin;
    };
    void SetGlanceHost(GlanceHost host);

}
