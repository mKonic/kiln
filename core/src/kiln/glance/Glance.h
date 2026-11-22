#pragma once

// Home-screen widgets in Glance's model (androidx glance and glance-appwidget): the widget's
// content is a tree of Glance elements (Box, Row, Column, Text, Image, Button, LazyColumn...)
// built by a function, which kiln normalizes as Glance does (NormalizeCompositionTree: ripples,
// background images, buttons) and turns into RemoteViews (RemoteViewsTranslator) for each size
// the launcher shows it at (SizeMode), redrawn when the app calls Update.
//
//   // At namespace scope: the system starts the app's process to draw or update it.
//   kiln::glance::WidgetRegistration notes("notes", {
//       .content = [](const kiln::glance::WidgetContext& w) {
//           using namespace kiln::glance;
//           Column(FillMaxSize(Background(GlanceModifier{}, GlanceTheme::colors().widgetBackground)), {}, [] {
//               Text("Notes", {}, { .color = GlanceTheme::colors().onSurface, .fontSize = 18, .fontWeight = FontWeight::Bold });
//               Button("Add", ActionRunCallback("add"));
//           });
//       },
//   });
//   kiln::glance::ActionCallbackRegistration add("add", [](const kiln::glance::ActionContext&) { ... });
//
// The app declares each widget: WIDGETS in kiln_add_app (or a receiver extending
// dev.kiln.KilnWidgetReceiver in a Gradle app) and res/xml/<name>_widget.xml, its
// appwidget-provider info.

#include "kiln/datastore/DataStore.h"
#include "kiln/datastore/Preferences.h"
#include "kiln/runtime/Flow.h"
#include "kiln/runtime/Task.h"
#include "kiln/ui/ActivityIntent.h"
#include "kiln/ui/Credentials.h"
#include "kiln/ui/Geometry.h"
#include <kiln/Function.h>

#include <any>
#include <climits>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace kiln::glance {

    // --- colours ------------------------------------------------------------------------------

    // ColorProvider: a fixed colour (ARGB), one per night mode (DayNightColorProvider), or a colour
    // resource resolved by the launcher when it draws (ResourceColorProvider): an app colour by
    // name, or "android:color/<name>" for the platform's.
    struct ColorProvider {
        std::uint32_t day = 0, night = 0;
        std::string resource;
        bool set = false;
        ColorProvider() = default;
        ColorProvider(std::uint32_t argb) : day(argb), night(argb), set(true) {}
        ColorProvider(std::uint32_t dayArgb, std::uint32_t nightArgb) : day(dayArgb), night(nightArgb), set(true) {}
        static ColorProvider Resource(std::string colorResource) {
            ColorProvider c;
            c.resource = std::move(colorResource);
            c.set = true;
            return c;
        }
        bool IsResource() const { return !resource.empty(); }
        bool operator==(const ColorProvider&) const = default;
    };

    // ColorProviders: the Material 3 roles a widget draws with (glance color/ColorProviders).
    struct ColorProviders {
        ColorProvider primary, onPrimary, primaryContainer, onPrimaryContainer;
        ColorProvider secondary, onSecondary, secondaryContainer, onSecondaryContainer;
        ColorProvider tertiary, onTertiary, tertiaryContainer, onTertiaryContainer;
        ColorProvider error, errorContainer, onError, onErrorContainer;
        ColorProvider background, onBackground, surface, onSurface, surfaceVariant, onSurfaceVariant;
        ColorProvider outline, inverseOnSurface, inverseSurface, inversePrimary;
        ColorProvider widgetBackground;   // unset: none (the colorProviders overload without it)
        bool operator==(const ColorProviders&) const = default;
    };
    // DynamicThemeColorProviders: the system's dynamic colours (kiln_glance_color* resources,
    // the wallpaper's tones in day and night), GlanceTheme's default.
    const ColorProviders& DynamicThemeColorProviders();

    // GlanceTheme.colors: the theme's colours at this point of the content.
    struct GlanceTheme {
        static const ColorProviders& colors();
    };
    // GlanceTheme(colors) { content }: the colours the content (and the defaults of Button,
    // CheckBox...) uses.
    void GlanceTheme(ColorProviders colors, kiln::Function<void()> content);
    inline void GlanceTheme(kiln::Function<void()> content) { GlanceTheme(GlanceTheme::colors(), std::move(content)); }

    // ColorFilter.tint: an image tinted with a colour.
    struct ColorFilter {
        ColorProvider tint;
        static ColorFilter Tint(ColorProvider color) { return { std::move(color) }; }
        bool operator==(const ColorFilter&) const = default;
    };

    // --- dimensions and images ------------------------------------------------------------------

    // Dimension: wrap the content, fill the parent, share the parent's remaining space (Expand,
    // GlanceModifier.defaultWeight), a size in dp, or a dimension resource (an app one by name,
    // or "android:dimen/<name>").
    struct Dimension {
        enum class Kind { Wrap, Fill, Expand, Dp, Resource };
        Kind kind = Kind::Wrap;
        float dp = 0;
        std::string resource;
        static Dimension Wrap() { return {}; }
        static Dimension Fill() { return { Kind::Fill, 0, {} }; }
        static Dimension Expand() { return { Kind::Expand, 0, {} }; }
        static Dimension Dp(float v) { return { Kind::Dp, v, {} }; }
        static Dimension Resource(std::string name) { return { Kind::Resource, 0, std::move(name) }; }
        bool operator==(const Dimension&) const = default;
    };

    enum class ContentScale { Fit, Crop, FillBounds };
    // ImageProvider: an app drawable by name (or "android:drawable/<name>"), encoded bytes (PNG,
    // JPEG, WebP: the Bitmap overload), or a content/file URI (ImageProvider(uri)).
    struct ImageProvider {
        std::string resource;
        std::vector<std::uint8_t> bytes;
        std::string uri;
        static ImageProvider Resource(std::string drawable) { return { std::move(drawable), {}, {} }; }
        static ImageProvider Bitmap(std::vector<std::uint8_t> encoded) { return { {}, std::move(encoded), {} }; }
        static ImageProvider Uri(std::string uri) { return { {}, {}, std::move(uri) }; }
        bool operator==(const ImageProvider&) const = default;
    };

    // --- actions --------------------------------------------------------------------------------

    // ActionParameters: typed values an action carries (to its callback, or as the started
    // activity's extras). Keys with the same name are the same key.
    class ActionParameters {
    public:
        using Value = std::variant<bool, std::int32_t, std::int64_t, float, double, std::string>;
        struct Pair {
            std::string name;
            Value value;
            bool operator==(const Pair&) const = default;
        };
        template<typename T>
        struct Key {
            std::string name;
            Pair to(T value) const { return { name, Value(std::move(value)) }; }
            bool operator==(const Key&) const = default;
        };

        ActionParameters() = default;
        template<typename T> bool Contains(const Key<T>& key) const { return m_Values.contains(key.name); }
        // get: unset (or stored with another type, Kotlin's ClassCastException): none.
        template<typename T> std::optional<T> Get(const Key<T>& key) const {
            auto it = m_Values.find(key.name);
            if (it == m_Values.end()) return std::nullopt;
            if (const T* v = std::get_if<T>(&it->second)) return *v;
            return std::nullopt;
        }
        template<typename T> T GetOrDefault(const Key<T>& key, T defaultValue) const {
            auto v = Get(key);
            return v ? *v : std::move(defaultValue);
        }
        const std::map<std::string, Value>& AsMap() const { return m_Values; }
        bool IsEmpty() const { return m_Values.empty(); }
        bool operator==(const ActionParameters&) const = default;

    protected:
        std::map<std::string, Value> m_Values;
        friend class MutableActionParameters;
        friend ActionParameters ParametersFromBytes(const std::vector<std::uint8_t>&);
    };

    class MutableActionParameters : public ActionParameters {
    public:
        MutableActionParameters() = default;
        explicit MutableActionParameters(const ActionParameters& p) : ActionParameters(p) {}
        // set: the previous value; none removes the key.
        template<typename T> std::optional<T> Set(const Key<T>& key, std::optional<T> value) {
            std::optional<T> previous = Get(key);
            if (value) m_Values[key.name] = Value(std::move(*value));
            else m_Values.erase(key.name);
            return previous;
        }
        template<typename T> std::optional<T> Remove(const Key<T>& key) {
            std::optional<T> previous = Get(key);
            m_Values.erase(key.name);
            return previous;
        }
        void Clear() { m_Values.clear(); }
        void Put(const Pair& p) { m_Values[p.name] = p.value; }
    };

    MutableActionParameters MutableActionParametersOf(std::initializer_list<ActionParameters::Pair> pairs = {});
    inline ActionParameters ActionParametersOf(std::initializer_list<ActionParameters::Pair> pairs = {}) { return MutableActionParametersOf(pairs); }
    inline MutableActionParameters ToMutableParameters(const ActionParameters& p) { return MutableActionParameters(p); }
    inline ActionParameters ToParameters(const ActionParameters& p) { return ActionParameters(p); }
    // Preferences.Key.toParametersKey: the same name.
    template<typename T> ActionParameters::Key<T> ToParametersKey(const datastore::PreferenceKey<T>& key) { return { key.name }; }

    // ToggleableStateKey: a CheckBox's, Switch's or RadioButton's new state, in its action's
    // parameters (RemoteViews.EXTRA_CHECKED).
    inline const ActionParameters::Key<bool> ToggleableStateKey{ "android.widget.extra.CHECKED" };

    // What a tap does (Action): start the app's activity at a deep link with the parameters as
    // extras (actionStartActivity), run a registered callback in the app's process
    // (actionRunCallback), run a lambda of this drawing (action { }), send a broadcast
    // (actionSendBroadcast) or start a service (actionStartService). The widget is redrawn after a
    // callback or a lambda.
    struct Action {
        enum class Kind { None, StartActivity, RunCallback, Lambda, SendBroadcast, StartService };
        Kind kind = Kind::None;
        std::string uri;          // StartActivity: the deep link (empty: just open the app); SendBroadcast: the intent action
        std::string key;          // RunCallback: the callback's name; Lambda: its key
        std::string component;    // SendBroadcast / StartService: the receiver's or service's class (this package's)
        ActionParameters parameters;
        bool foregroundService = false;
        kiln::Function<void()> block;   // Lambda
        bool operator==(const Action& o) const {
            return kind == o.kind && uri == o.uri && key == o.key && component == o.component && parameters == o.parameters &&
                   foregroundService == o.foregroundService;
        }
    };
    Action ActionStartActivity(std::string deepLink = {}, ActionParameters parameters = {});
    Action ActionRunCallback(std::string key, ActionParameters parameters = {});
    // action(key) { block }: without a key, its position in the content names it.
    Action LambdaAction(kiln::Function<void()> block, std::string key = {});
    // actionSendBroadcast(action, componentName) / (receiver class).
    Action ActionSendBroadcast(std::string intentAction, std::string receiverClass = {});
    Action ActionSendBroadcastTo(std::string receiverClass);
    Action ActionStartService(std::string serviceClass, bool isForegroundService = false);
    // The earlier spellings.
    inline Action StartActivity(std::string deepLink = {}) { return ActionStartActivity(std::move(deepLink)); }
    inline Action RunCallback(std::string key, ActionParameters parameters = {}) { return ActionRunCallback(std::move(key), std::move(parameters)); }

    enum class Visibility { Visible, Invisible, Gone };

    // --- semantics --------------------------------------------------------------------------------

    // SemanticsPropertyKey: a semantics value's name and how a parent's and a child's merge.
    template<typename T>
    struct SemanticsPropertyKey {
        std::string name;
        kiln::Function<std::optional<T>(const std::optional<T>&, const T&)> mergePolicy;
        std::optional<T> Merge(const std::optional<T>& parent, const T& child) const {
            if (mergePolicy) return mergePolicy(parent, child);
            return parent ? parent : std::optional<T>(child);
        }
    };
    struct SemanticsProperties {
        static const SemanticsPropertyKey<std::vector<std::string>>& ContentDescription();
        static const SemanticsPropertyKey<std::string>& TestTag();
    };
    // SemanticsConfiguration: what semantics { } set (SemanticsPropertyReceiver).
    class SemanticsConfiguration {
    public:
        using Value = std::variant<std::vector<std::string>, std::string>;
        template<typename T> void Set(const SemanticsPropertyKey<T>& key, T value) { m_Props[key.name] = Value(std::move(value)); }
        // get: throws std::logic_error when the key is not set.
        template<typename T> const T& Get(const SemanticsPropertyKey<T>& key) const {
            auto it = m_Props.find(key.name);
            if (it == m_Props.end()) throw std::logic_error("Key not present: " + key.name);
            return std::get<T>(it->second);
        }
        template<typename T> std::optional<T> GetOrElseNullable(const SemanticsPropertyKey<T>& key, kiln::Function<std::optional<T>()> defaultValue) const {
            auto it = m_Props.find(key.name);
            if (it == m_Props.end()) return defaultValue ? defaultValue() : std::nullopt;
            return std::get<T>(it->second);
        }
        template<typename T> std::optional<T> GetOrNull(const SemanticsPropertyKey<T>& key) const { return GetOrElseNullable<T>(key, {}); }
        // contentDescription = / testTag =
        void SetContentDescription(std::string d) { Set(SemanticsProperties::ContentDescription(), std::vector<std::string>{ std::move(d) }); }
        void SetTestTag(std::string t) { Set(SemanticsProperties::TestTag(), std::move(t)); }
        bool operator==(const SemanticsConfiguration&) const = default;

    private:
        std::map<std::string, Value> m_Props;
    };
    using SemanticsPropertyReceiver = SemanticsConfiguration;

    // --- modifiers --------------------------------------------------------------------------------

    // The ripple drawable a clickable shows (an app drawable by name); none: Glance's.
    inline const std::string NoRippleOverride{};

    class GlanceModifier {
    public:
        struct Item {
            enum class Kind {
                Width, Height, Padding, BackgroundColor, BackgroundImage, CornerRadius, Clickable, Visibility, Semantics, Enabled,
                AppWidgetBackground, ClipToOutline, SelectableGroup,
            };
            Kind kind;
            Dimension dimension;   // Width, Height, CornerRadius
            // Padding: dp per side, plus dimension resources added to them; absolute: left and
            // right, not start and end.
            float left = 0, top = 0, right = 0, bottom = 0;
            std::string leftRes, topRes, rightRes, bottomRes;
            bool absolute = false;
            ColorProvider color;
            ImageProvider image;           // BackgroundImage
            ContentScale contentScale = ContentScale::FillBounds;
            std::optional<ColorFilter> colorFilter;
            float alpha = 1;
            Action action;                 // Clickable
            std::string rippleOverride;    // Clickable
            SemanticsConfiguration semantics;
            int value = 0;                 // Visibility, Enabled, ClipToOutline
        };
        GlanceModifier Then(Item item) const {
            GlanceModifier m = *this;
            m.m_Items.push_back(std::move(item));
            return m;
        }
        // then: this modifier's elements, then the other's (CombinedGlanceModifier).
        GlanceModifier Then(const GlanceModifier& other) const {
            GlanceModifier m = *this;
            m.m_Items.insert(m.m_Items.end(), other.m_Items.begin(), other.m_Items.end());
            return m;
        }
        const std::vector<Item>& Items() const { return m_Items; }
        bool Any(const kiln::Function<bool(const Item&)>& predicate) const;
        bool All(const kiln::Function<bool(const Item&)>& predicate) const;
        // foldIn: from the first element; foldOut: from the last.
        template<typename R> R FoldIn(R initial, const kiln::Function<R(R, const Item&)>& operation) const {
            for (const Item& i : m_Items) initial = operation(std::move(initial), i);
            return initial;
        }
        template<typename R> R FoldOut(R initial, const kiln::Function<R(const Item&, R)>& operation) const {
            for (auto it = m_Items.rbegin(); it != m_Items.rend(); ++it) initial = operation(*it, std::move(initial));
            return initial;
        }

    private:
        std::vector<Item> m_Items;
    };

    GlanceModifier Width(GlanceModifier m, Dimension width);
    GlanceModifier Height(GlanceModifier m, Dimension height);
    GlanceModifier Width(GlanceModifier m, float dp);
    GlanceModifier Height(GlanceModifier m, float dp);
    GlanceModifier Size(GlanceModifier m, float dp);
    GlanceModifier Size(GlanceModifier m, float widthDp, float heightDp);
    GlanceModifier Size(GlanceModifier m, Dimension width, Dimension height);
    GlanceModifier FillMaxWidth(GlanceModifier m);
    GlanceModifier FillMaxHeight(GlanceModifier m);
    GlanceModifier FillMaxSize(GlanceModifier m);
    GlanceModifier WrapContentWidth(GlanceModifier m);
    GlanceModifier WrapContentHeight(GlanceModifier m);
    GlanceModifier WrapContentSize(GlanceModifier m);
    // In a Row or Column: share the remaining space with the other weighted children.
    GlanceModifier DefaultWeight(GlanceModifier m);
    // Paddings add up (PaddingModifier.plus).
    GlanceModifier Padding(GlanceModifier m, float all);
    GlanceModifier Padding(GlanceModifier m, float horizontal, float vertical);
    GlanceModifier Padding(GlanceModifier m, float start, float top, float end, float bottom);
    // padding(@DimenRes): dimension resources by name (empty: none on that side).
    GlanceModifier PaddingResource(GlanceModifier m, std::string start, std::string top, std::string end, std::string bottom);
    GlanceModifier AbsolutePadding(GlanceModifier m, float left, float top, float right, float bottom);
    GlanceModifier AbsolutePaddingResource(GlanceModifier m, std::string left, std::string top, std::string right, std::string bottom);
    GlanceModifier Background(GlanceModifier m, ColorProvider color);
    // background(day, night)
    GlanceModifier Background(GlanceModifier m, std::uint32_t day, std::uint32_t night);
    // background(imageProvider, contentScale, colorFilter) and its alpha overload.
    GlanceModifier Background(GlanceModifier m, ImageProvider image, ContentScale contentScale = ContentScale::FillBounds,
                              std::optional<ColorFilter> colorFilter = std::nullopt, float alpha = 1);
    // An app drawable by name (res/drawable/<name>).
    GlanceModifier BackgroundResource(GlanceModifier m, std::string drawable);
    GlanceModifier CornerRadius(GlanceModifier m, float dp);
    GlanceModifier CornerRadius(GlanceModifier m, Dimension radius);
    GlanceModifier Clickable(GlanceModifier m, Action action, std::string rippleOverride = NoRippleOverride);
    // clickable(key, rippleOverride) { block }
    GlanceModifier Clickable(GlanceModifier m, kiln::Function<void()> block, std::string key = {}, std::string rippleOverride = NoRippleOverride);
    GlanceModifier VisibilityOf(GlanceModifier m, Visibility v);
    GlanceModifier Semantics(GlanceModifier m, const kiln::Function<void(SemanticsPropertyReceiver&)>& properties);
    GlanceModifier ContentDescription(GlanceModifier m, std::string description);
    GlanceModifier Enabled(GlanceModifier m, bool enabled);
    GlanceModifier ClipToOutline(GlanceModifier m, bool clip);

    // --- layout -----------------------------------------------------------------------------------

    // Alignment, as Android gravity.
    enum class HorizontalAlignment { Start, CenterHorizontally, End };
    enum class VerticalAlignment { Top, CenterVertically, Bottom };
    struct Alignment {
        HorizontalAlignment horizontal = HorizontalAlignment::Start;
        VerticalAlignment vertical = VerticalAlignment::Top;
        static Alignment TopStart() { return {}; }
        static Alignment TopCenter() { return { HorizontalAlignment::CenterHorizontally, VerticalAlignment::Top }; }
        static Alignment TopEnd() { return { HorizontalAlignment::End, VerticalAlignment::Top }; }
        static Alignment CenterStart() { return { HorizontalAlignment::Start, VerticalAlignment::CenterVertically }; }
        static Alignment Center() { return { HorizontalAlignment::CenterHorizontally, VerticalAlignment::CenterVertically }; }
        static Alignment CenterEnd() { return { HorizontalAlignment::End, VerticalAlignment::CenterVertically }; }
        static Alignment BottomStart() { return { HorizontalAlignment::Start, VerticalAlignment::Bottom }; }
        static Alignment BottomCenter() { return { HorizontalAlignment::CenterHorizontally, VerticalAlignment::Bottom }; }
        static Alignment BottomEnd() { return { HorizontalAlignment::End, VerticalAlignment::Bottom }; }
        bool operator==(const Alignment&) const = default;
    };
    int Gravity(Alignment a);

    // --- text -------------------------------------------------------------------------------------

    enum class FontWeight { Normal, Medium, Bold };
    enum class FontStyle { Normal, Italic };
    enum class TextAlign { Left, Right, Center, Justify, Start, End };
    // TextDecoration: a set of Underline and LineThrough.
    struct TextDecoration {
        int mask = 0;
        static TextDecoration None() { return { 0 }; }
        static TextDecoration Underline() { return { 1 }; }
        static TextDecoration LineThrough() { return { 2 }; }
        static TextDecoration Combine(const std::vector<TextDecoration>& decorations) {
            int m = 0;
            for (const TextDecoration& d : decorations) m |= d.mask;
            return { m };
        }
        TextDecoration operator+(TextDecoration o) const { return { mask | o.mask }; }
        bool Contains(TextDecoration o) const { return (mask | o.mask) == mask; }
        bool operator==(const TextDecoration&) const = default;
    };
    // FontFamily: a family by name, as TypefaceSpan takes it.
    struct FontFamily {
        std::string family;
        static FontFamily Serif() { return { "serif" }; }
        static FontFamily SansSerif() { return { "sans-serif" }; }
        static FontFamily Monospace() { return { "monospace" }; }
        static FontFamily Cursive() { return { "cursive" }; }
        bool operator==(const FontFamily&) const = default;
    };
    struct TextStyle {
        ColorProvider color = ColorProvider(0xFF000000u);   // TextDefaults.defaultTextColor
        float fontSize = 0;   // sp; 0: the platform's
        std::optional<FontWeight> fontWeight;
        std::optional<FontStyle> fontStyle;
        std::optional<TextAlign> textAlign;
        std::optional<TextDecoration> textDecoration;
        std::optional<FontFamily> fontFamily;
        bool operator==(const TextStyle&) const = default;
    };
    struct TextDefaults {
        static ColorProvider defaultTextColor() { return ColorProvider(0xFF000000u); }
        static TextStyle defaultTextStyle() { return TextStyle{ .color = defaultTextColor() }; }
    };

    // ButtonColors / ButtonDefaults.buttonColors: unset colours take the theme's primary and onPrimary.
    struct ButtonColors {
        ColorProvider backgroundColor, contentColor;
        bool operator==(const ButtonColors&) const = default;
    };
    struct ButtonDefaults {
        static ButtonColors buttonColors(ColorProvider backgroundColor = {}, ColorProvider contentColor = {});
    };

    // --- elements (Emittables): call inside a widget's content ------------------------------
    void Box(GlanceModifier modifier, Alignment contentAlignment, kiln::Function<void()> content = {});
    void Row(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content = {});
    void Column(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content = {});
    void Text(std::string text, GlanceModifier modifier = {}, TextStyle style = TextDefaults::defaultTextStyle(), int maxLines = INT_MAX);
    // contentDescription none: decorative (not read by accessibility services).
    void Image(ImageProvider provider, std::optional<std::string> contentDescription, GlanceModifier modifier = {},
               ContentScale contentScale = ContentScale::Fit, std::optional<ColorFilter> colorFilter = std::nullopt, float alpha = 1);
    // style none: the platform's text style in the colours' content colour.
    void Button(std::string text, Action onClick, GlanceModifier modifier = {}, bool enabled = true,
                std::optional<TextStyle> style = std::nullopt, std::optional<ButtonColors> colors = std::nullopt, int maxLines = INT_MAX);
    void Button(std::string text, kiln::Function<void()> onClick, GlanceModifier modifier = {}, bool enabled = true,
                std::optional<TextStyle> style = std::nullopt, std::optional<ButtonColors> colors = std::nullopt, int maxLines = INT_MAX,
                std::string key = {});
    void Spacer(GlanceModifier modifier = {});
    // ProgressIndicatorDefaults
    struct ProgressIndicatorDefaults {
        static ColorProvider IndicatorColorProvider() { return ColorProvider(0xFF6200EEu); }
        static ColorProvider BackgroundColorProvider() { return ColorProvider(0x3D6200EEu); }   // the indicator's at 0.24 alpha
    };
    void LinearProgressIndicator(float progress, GlanceModifier modifier = {}, ColorProvider color = ProgressIndicatorDefaults::IndicatorColorProvider(),
                                 ColorProvider backgroundColor = ProgressIndicatorDefaults::BackgroundColorProvider());
    // Indeterminate.
    void LinearProgressIndicator(GlanceModifier modifier = {}, ColorProvider color = ProgressIndicatorDefaults::IndicatorColorProvider(),
                                 ColorProvider backgroundColor = ProgressIndicatorDefaults::BackgroundColorProvider());
    void CircularProgressIndicator(GlanceModifier modifier = {}, ColorProvider color = ProgressIndicatorDefaults::IndicatorColorProvider());

    // A toggle's colours, checked and unchecked (CheckedUncheckedColorProvider), or a colour
    // state list resource (ResourceCheckableColorProvider): the theme's default.
    struct CheckableColors {
        std::string resource;
        ColorProvider checked, unchecked;
        bool operator==(const CheckableColors&) const = default;
    };
    struct CheckBoxColors {
        CheckableColors checkBox;
        bool operator==(const CheckBoxColors&) const = default;
    };
    struct SwitchColors {
        CheckableColors thumb, track;
        bool operator==(const SwitchColors&) const = default;
    };
    struct RadioButtonColors {
        CheckableColors radio;
        bool operator==(const RadioButtonColors&) const = default;
    };
    // The colours of resources throw std::invalid_argument, as Glance's do.
    struct CheckboxDefaults {
        static CheckBoxColors colors(ColorProvider checkedColor, ColorProvider uncheckedColor);
        static CheckBoxColors colors();
    };
    struct SwitchDefaults {
        static SwitchColors colors(ColorProvider checkedThumbColor, ColorProvider uncheckedThumbColor, ColorProvider checkedTrackColor,
                                   ColorProvider uncheckedTrackColor);
        static SwitchColors colors();
    };
    struct RadioButtonDefaults {
        static RadioButtonColors colors(ColorProvider checkedColor, ColorProvider uncheckedColor);
        static RadioButtonColors colors();
    };

    // onCheckedChange's parameters carry the new state as ToggleableStateKey. Style none: the
    // platform's text style.
    void CheckBox(bool checked, Action onCheckedChange, GlanceModifier modifier = {}, std::string text = {}, std::optional<TextStyle> style = std::nullopt,
                  std::optional<CheckBoxColors> colors = std::nullopt, int maxLines = INT_MAX);
    void CheckBox(bool checked, kiln::Function<void()> onCheckedChange, GlanceModifier modifier = {}, std::string text = {},
                  std::optional<TextStyle> style = std::nullopt, std::optional<CheckBoxColors> colors = std::nullopt, int maxLines = INT_MAX,
                  std::string key = {});
    void Switch(bool checked, Action onCheckedChange, GlanceModifier modifier = {}, std::string text = {}, std::optional<TextStyle> style = std::nullopt,
                std::optional<SwitchColors> colors = std::nullopt, int maxLines = INT_MAX);
    void Switch(bool checked, kiln::Function<void()> onCheckedChange, GlanceModifier modifier = {}, std::string text = {},
                std::optional<TextStyle> style = std::nullopt, std::optional<SwitchColors> colors = std::nullopt, int maxLines = INT_MAX,
                std::string key = {});
    // A radio button's tap is onClick (no new state); put the buttons of a group in a Row or Column
    // with SelectableGroup.
    void RadioButton(bool checked, Action onClick, GlanceModifier modifier = {}, bool enabled = true, std::string text = {},
                     std::optional<TextStyle> style = std::nullopt, std::optional<RadioButtonColors> colors = std::nullopt, int maxLines = INT_MAX);
    void RadioButton(bool checked, kiln::Function<void()> onClick, GlanceModifier modifier = {}, bool enabled = true, std::string text = {},
                     std::optional<TextStyle> style = std::nullopt, std::optional<RadioButtonColors> colors = std::nullopt, int maxLines = INT_MAX,
                     std::string key = {});
    // selectableGroup: the Row's or Column's radio buttons are one group (accessibility services
    // read them as one).
    GlanceModifier SelectableGroup(GlanceModifier m);
    // appWidgetBackground: this element is the widget's background (android:id/background), which
    // the launcher clips to its corner radius and uses in its transitions. At most one per widget.
    GlanceModifier AppWidgetBackground(GlanceModifier m);

    // --- lazy lists and grids ------------------------------------------------------------------

    // An item without an id of its own gets one from Glance's reserved range; ids at or below
    // ReservedItemIdRangeEnd are refused (std::invalid_argument).
    inline constexpr std::int64_t UnspecifiedItemId = INT64_MIN;
    inline constexpr std::int64_t ReservedItemIdRangeEnd = -0x4000000000000000LL;
    class LazyListScope {
    public:
        void Item(std::int64_t itemId, kiln::Function<void()> content);
        void Item(kiln::Function<void()> content) { Item(UnspecifiedItemId, std::move(content)); }
        void Items(int count, kiln::Function<std::int64_t(int)> itemId, kiln::Function<void(int)> itemContent);
        void Items(int count, kiln::Function<void(int)> itemContent) { Items(count, {}, std::move(itemContent)); }
        // items(list) / itemsIndexed(list)
        template<typename T> void Items(const std::vector<T>& items, kiln::Function<std::int64_t(const std::type_identity_t<T>&)> itemId,
                                        kiln::Function<void(const std::type_identity_t<T>&)> itemContent) {
            Items(int(items.size()), itemId ? kiln::Function<std::int64_t(int)>([&items, itemId](int i) { return itemId(items[std::size_t(i)]); })
                                            : kiln::Function<std::int64_t(int)>{},
                  [&items, itemContent](int i) { itemContent(items[std::size_t(i)]); });
        }
        template<typename T> void ItemsIndexed(const std::vector<T>& items, kiln::Function<std::int64_t(int, const std::type_identity_t<T>&)> itemId,
                                               kiln::Function<void(int, const std::type_identity_t<T>&)> itemContent) {
            Items(int(items.size()), itemId ? kiln::Function<std::int64_t(int)>([&items, itemId](int i) { return itemId(i, items[std::size_t(i)]); })
                                            : kiln::Function<std::int64_t(int)>{},
                  [&items, itemContent](int i) { itemContent(i, items[std::size_t(i)]); });
        }
    };
    using LazyVerticalGridScope = LazyListScope;
    // VerticalScrollMode: SnapScrollMatchHeight snaps to items only with RemoteCompose; with
    // RemoteViews it scrolls as Normal, as in Glance.
    struct VerticalScrollMode {
        enum class Kind { Normal, SnapScrollMatchHeight };
        Kind kind = Kind::Normal;
        float initialChildHeight = 0;   // dp
        static VerticalScrollMode Normal() { return {}; }
        static VerticalScrollMode SnapScrollMatchHeight(float initialChildHeightDp) { return { Kind::SnapScrollMatchHeight, initialChildHeightDp }; }
        bool operator==(const VerticalScrollMode&) const = default;
    };
    // The options of the activities an item's tap starts (LazyColumn(activityOptions)).
    struct LazyOptions {
        VerticalScrollMode verticalScrollMode;
        std::optional<ui::ActivityOptions> activityOptions;
    };
    void LazyColumn(GlanceModifier modifier, HorizontalAlignment horizontalAlignment, kiln::Function<void(LazyListScope&)> content, LazyOptions options = {});
    inline void LazyColumn(GlanceModifier modifier, kiln::Function<void(LazyListScope&)> content) {
        LazyColumn(std::move(modifier), HorizontalAlignment::Start, std::move(content));
    }
    // GridCells: a fixed count of columns (1 to 5), or as many as fit at a minimum width.
    struct GridCells {
        int count = 0;
        float minSize = 0;   // dp, Adaptive
        static GridCells Fixed(int count) { return { count, 0 }; }
        static GridCells Adaptive(float minSizeDp) { return { 0, minSizeDp }; }
        bool operator==(const GridCells&) const = default;
    };
    void LazyVerticalGrid(GridCells gridCells, GlanceModifier modifier, HorizontalAlignment horizontalAlignment, kiln::Function<void(LazyVerticalGridScope&)> content,
                          std::optional<ui::ActivityOptions> activityOptions = std::nullopt);
    inline void LazyVerticalGrid(GridCells gridCells, GlanceModifier modifier, kiln::Function<void(LazyVerticalGridScope&)> content) {
        LazyVerticalGrid(gridCells, std::move(modifier), HorizontalAlignment::Start, std::move(content));
    }

    // AndroidRemoteViews: an app layout by name, its view `containerViewId` (an id by name)
    // holding the content's elements; without content, the layout as it is.
    void AndroidRemoteViews(std::string layout, std::string containerViewId, GlanceModifier modifier = {}, kiln::Function<void()> content = {});
    inline void AndroidRemoteViews(std::string layout, GlanceModifier modifier = {}) { AndroidRemoteViews(std::move(layout), {}, std::move(modifier)); }

    // --- widgets ------------------------------------------------------------------------------

    // SizeMode: Single draws once, at the widget's minimum size; Exact draws for each size the
    // launcher shows it at; Responsive draws for the given sizes, the launcher picking the best.
    enum class SizeMode { Single, Exact, Responsive };

    // The drawn widget: its name, LocalGlanceId's AppWidgetId, LocalSize and LocalAppWidgetOptions
    // (the launcher's options for it: OPTION_APPWIDGET_MIN_WIDTH, the host category...).
    struct WidgetContext {
        std::string name;
        int appWidgetId = 0;
        ui::Size size;   // dp: what this drawing is for (LocalSize)
        ui::Bundle options;
    };
    using GlanceId = int;   // AppWidgetId.appWidgetId

    // --- state ---------------------------------------------------------------------------------

    // GlanceStateDefinition: where each placed widget's state lives, a store per instance under
    // its file key ("appWidget-<id>"). The content reads it with CurrentState.
    class StateDefinition {
    public:
        virtual ~StateDefinition() = default;
        // getLocation: the store's file.
        virtual std::string GetLocation(const std::string& fileKey) const = 0;
        // The value now (GlanceState.getValue), type-erased for the content.
        virtual rt::Task<std::any> GetAny(std::string fileKey) const = 0;
        // GlanceState.deleteStore: forgets the store and deletes its file.
        virtual void DeleteStore(const std::string& fileKey) const = 0;
    };
    // A typed DataStore per instance.
    template<typename T>
    class GlanceStateDefinition : public StateDefinition {
    public:
        using GetDataStoreFn = kiln::Function<std::shared_ptr<datastore::DataStore<T>>(const std::string& fileKey)>;
        using GetLocationFn = kiln::Function<std::string(const std::string& fileKey)>;
        GlanceStateDefinition(GetDataStoreFn getDataStore, GetLocationFn getLocation)
            : m_GetDataStore(std::move(getDataStore)), m_GetLocation(std::move(getLocation)) {}

        // The instance's store, one per file key while it is in use.
        std::shared_ptr<datastore::DataStore<T>> GetDataStore(const std::string& fileKey) const {
            std::lock_guard lock(m_Mutex);
            auto& slot = m_Stores[fileKey];
            if (!slot) slot = m_GetDataStore(fileKey);
            return slot;
        }
        std::string GetLocation(const std::string& fileKey) const override { return m_GetLocation(fileKey); }
        rt::Task<T> GetValue(std::string fileKey) const { co_return co_await rt::First(GetDataStore(fileKey)->Data()); }
        rt::Task<T> UpdateValue(std::string fileKey, kiln::Function<T(T)> update) const {
            co_return co_await GetDataStore(fileKey)->UpdateData(std::move(update));
        }
        rt::Task<std::any> GetAny(std::string fileKey) const override { co_return std::any(co_await GetValue(std::move(fileKey))); }
        void DeleteStore(const std::string& fileKey) const override {
            {
                std::lock_guard lock(m_Mutex);
                m_Stores.erase(fileKey);
            }
            std::remove(GetLocation(fileKey).c_str());
        }

    private:
        GetDataStoreFn m_GetDataStore;
        GetLocationFn m_GetLocation;
        mutable std::mutex m_Mutex;
        mutable std::map<std::string, std::shared_ptr<datastore::DataStore<T>>> m_Stores;
    };
    // PreferencesGlanceStateDefinition: Preferences per instance (datastore's preferences file for
    // the file key), every widget's state unless it says otherwise.
    class PreferencesStateDefinition : public StateDefinition {
    public:
        std::shared_ptr<datastore::PreferencesDataStore> GetDataStore(const std::string& fileKey) const;
        std::string GetLocation(const std::string& fileKey) const override;
        rt::Task<datastore::Preferences> GetValue(std::string fileKey) const;
        rt::Task<datastore::Preferences> UpdateValue(std::string fileKey, kiln::Function<void(datastore::MutablePreferences&)> update) const;
        rt::Task<std::any> GetAny(std::string fileKey) const override;
        void DeleteStore(const std::string& fileKey) const override;

    private:
        mutable std::mutex m_Mutex;
        mutable std::map<std::string, std::shared_ptr<datastore::PreferencesDataStore>> m_Stores;
    };
    const std::shared_ptr<PreferencesStateDefinition>& PreferencesGlanceStateDefinition();
    // createUniqueRemoteUiName: an instance's file key.
    std::string AppWidgetStateKey(GlanceId id);

    namespace detail {
        const std::any& CurrentStateAny();
    }
    // currentState<T>(): the drawn instance's state (LocalState); std::bad_any_cast for another type.
    template<typename T> const T& CurrentState() { return std::any_cast<const T&>(detail::CurrentStateAny()); }
    // currentState(key): a preference of it.
    template<typename T> std::optional<T> CurrentState(const datastore::PreferenceKey<T>& key) { return CurrentState<datastore::Preferences>().Get(key); }

    // --- widgets ------------------------------------------------------------------------------

    struct GlanceAppWidget {
        SizeMode sizeMode = SizeMode::Single;
        std::vector<ui::Size> responsiveSizes;   // dp
        // provideContent's content: drawn for each size.
        kiln::Function<void(const WidgetContext&)> content;
        // provideGlance: loads what the content needs, then provides it (ProvideContent); run
        // before each drawing. Unset: `content`.
        kiln::Function<rt::Task<kiln::Function<void(const WidgetContext&)>>(WidgetContext)> provideGlance;
        // stateDefinition: none for a widget without state.
        std::shared_ptr<StateDefinition> stateDefinition = PreferencesGlanceStateDefinition();
        // onDelete: an instance was removed (its state is deleted after it).
        kiln::Function<rt::Task<>(GlanceId)> onDelete;
        // errorUiLayout: the layout shown when the content throws (empty: nothing shown, the error
        // logged). onCompositionError replaces showing it.
        std::string errorUiLayout = "kiln_glance_error";
        kiln::Function<void(const WidgetContext&, std::exception_ptr)> onCompositionError;
        // providePreview: the content of the widget picker's generated preview (API 35) for a
        // widget category, drawn at previewSizeMode's sizes (Single: the provider's minimum;
        // Responsive: previewSizes). Taps are left out of previews.
        kiln::Function<rt::Task<kiln::Function<void(const WidgetContext&)>>(int widgetCategory)> providePreview;
        SizeMode previewSizeMode = SizeMode::Single;
        std::vector<ui::Size> previewSizes;   // dp
    };
    // provideContent: the content provideGlance provides.
    inline kiln::Function<void(const WidgetContext&)> ProvideContent(kiln::Function<void(const WidgetContext&)> content) { return content; }
    void RegisterWidget(std::string name, GlanceAppWidget widget);
    struct WidgetRegistration {
        WidgetRegistration(std::string name, GlanceAppWidget widget) { RegisterWidget(std::move(name), std::move(widget)); }
    };

    struct ActionContext {
        std::string widget;       // the widget's name
        int appWidgetId = 0;
        ActionParameters parameters;   // a toggle's new state as ToggleableStateKey
    };
    // ActionCallback.onAction: in the app's process; the widget is redrawn when it finishes.
    void RegisterActionCallback(std::string key, kiln::Function<void(const ActionContext&)> callback);
    void RegisterActionCallback(std::string key, kiln::Function<rt::Task<>(ActionContext)> callback);
    struct ActionCallbackRegistration {
        ActionCallbackRegistration(std::string key, kiln::Function<void(const ActionContext&)> callback) {
            RegisterActionCallback(std::move(key), std::move(callback));
        }
        ActionCallbackRegistration(std::string key, kiln::Function<rt::Task<>(ActionContext)> callback) {
            RegisterActionCallback(std::move(key), std::move(callback));
        }
    };

    // getAppWidgetState: an instance's state under a definition, or the widget's own.
    template<typename T> rt::Task<T> GetAppWidgetState(const GlanceStateDefinition<T>& definition, GlanceId id) {
        co_return co_await definition.GetValue(AppWidgetStateKey(id));
    }
    rt::Task<datastore::Preferences> GetAppWidgetState(const PreferencesStateDefinition& definition, GlanceId id);
    rt::Task<std::any> GetAppWidgetState(std::string widget, GlanceId id);
    // updateAppWidgetState: changes an instance's state (redraw it with Update after).
    template<typename T> rt::Task<T> UpdateAppWidgetState(const GlanceStateDefinition<T>& definition, GlanceId id, kiln::Function<T(T)> update) {
        co_return co_await definition.UpdateValue(AppWidgetStateKey(id), std::move(update));
    }
    rt::Task<> UpdateAppWidgetState(GlanceId id, kiln::Function<void(datastore::MutablePreferences&)> update);

    // Redraws every placed instance of the widget (GlanceAppWidget.updateAll), or one (update).
    void UpdateAll(const std::string& widget);
    void Update(const std::string& widget, GlanceId id);
    // updateIf: redraws the instances whose state passes.
    template<typename State> rt::Task<> UpdateIf(std::string widget, kiln::Function<bool(const State&)> predicate);
    // The ids of its placed instances (GlanceAppWidgetManager.getGlanceIds).
    std::vector<int> AppWidgetIds(const std::string& widget);
    inline std::vector<GlanceId> GetGlanceIds(const std::string& widget) { return AppWidgetIds(widget); }
    // getGlanceIdBy / getAppWidgetId: a GlanceId is its app widget id.
    inline GlanceId GetGlanceIdBy(int appWidgetId) { return appWidgetId; }
    inline int GetAppWidgetId(GlanceId id) { return id; }
    // getAppWidgetSizes: the sizes the launcher shows an instance at (dp).
    std::vector<ui::Size> GetAppWidgetSizes(GlanceId id);
    // requestPinGlanceAppWidget: asks the launcher to place one (false: it cannot), showing a
    // drawing of it at `previewSize` in its dialog, and opening the app at `successDeepLink`
    // once placed (empty: neither).
    bool RequestPin(const std::string& widget);
    rt::Task<bool> RequestPinGlanceAppWidget(std::string widget, std::optional<ui::Size> previewSize = std::nullopt,
                                             std::string successDeepLink = {});

    // --- what the platform's host uses ---------------------------------------------------------

    // The sizes to draw for: the launcher's (Exact), the responsive ones, or the minimum (Single).
    std::vector<ui::Size> SizesToDraw(const GlanceAppWidget& widget, const std::vector<ui::Size>& launcherSizes, ui::Size minimum);
    // GlanceAppWidget.resize: whether new options need a redraw (not for Single, nor Responsive
    // past API 31, whose launcher picks among the sizes drawn).
    bool RedrawsOnResize(const GlanceAppWidget& widget, int sdkInt);
    // The content for one size, normalized and encoded for the host's RemoteViews translation
    // (KilnGlance.java), with the given state. Empty when the widget is not registered.
    std::vector<std::uint8_t> Render(const std::string& widget, const WidgetContext& context, std::any state = {});
    // A drawing of each size: the state read, provideGlance run, then the content. `failed`: the
    // content threw; the host shows `errorLayout` unless it is empty (onCompositionError ran, or
    // the widget shows nothing).
    struct Drawing {
        std::vector<std::vector<std::uint8_t>> contents;
        bool failed = false;
        std::string errorLayout;
        bool found = true;
    };
    rt::Task<Drawing> Draw(std::string widget, WidgetContext context, std::vector<ui::Size> sizes);
    // AppWidgetProviderInfo's widget categories.
    inline constexpr int WIDGET_CATEGORY_HOME_SCREEN = 1, WIDGET_CATEGORY_KEYGUARD = 2, WIDGET_CATEGORY_SEARCHBOX = 4;
    // setWidgetPreviews: the generated previews for each category (API 35; one category may
    // combine several). SET_WIDGET_PREVIEWS_RESULT_RATE_LIMITED when the system refused one.
    inline constexpr int SET_WIDGET_PREVIEWS_RESULT_SUCCESS = 0, SET_WIDGET_PREVIEWS_RESULT_RATE_LIMITED = 1;
    rt::Task<int> SetWidgetPreviews(std::string widget, std::vector<int> widgetCategories = { WIDGET_CATEGORY_HOME_SCREEN | WIDGET_CATEGORY_KEYGUARD | WIDGET_CATEGORY_SEARCHBOX });
    // composeForPreview: a preview's drawing for each preview size (`minimum`: the provider's
    // minimum size, for Single).
    rt::Task<Drawing> ComposeForPreview(std::string widget, int widgetCategory, ui::Size minimum = {});

    const GlanceAppWidget* FindWidget(const std::string& name);
    // Runs a callback (true when it exists); the host redraws the widget after it.
    rt::Task<bool> RunActionCallback(std::string key, ActionContext context);
    // Runs a lambda of the widget's last drawing (true when it exists), drawing it first when the
    // process has not drawn it yet.
    rt::Task<bool> RunLambdaAction(std::string widget, int appWidgetId, std::string key);
    // GlanceAppWidget.deleted: onDelete, then the instance's state deleted.
    rt::Task<> Deleted(std::string widget, GlanceId id);
    // Parameters as the host carries them in an intent (and back).
    std::vector<std::uint8_t> ParametersToBytes(const ActionParameters& p);
    ActionParameters ParametersFromBytes(const std::vector<std::uint8_t>& bytes);

    struct GlanceHost {
        kiln::Function<void(const std::string&)> updateAll;
        kiln::Function<void(const std::string&, int)> update;
        kiln::Function<std::vector<int>(const std::string&)> appWidgetIds;
        kiln::Function<std::vector<ui::Size>(int)> appWidgetSizes;
        // The widget's name, a drawing for the pin dialog (empty: none), the deep link to open once placed.
        kiln::Function<bool(const std::string&, const std::vector<std::uint8_t>&, const std::string&)> requestPin;
        // The provider's minimum size (dp), and setWidgetPreview for a category with a drawing
        // per size (false: rate-limited).
        kiln::Function<ui::Size(const std::string&)> minimumSize;
        kiln::Function<bool(const std::string&, int, const std::vector<ui::Size>&, const std::vector<std::vector<std::uint8_t>>&)> setWidgetPreview;
    };
    void SetGlanceHost(GlanceHost host);

    template<typename State> rt::Task<> UpdateIf(std::string widget, kiln::Function<bool(const State&)> predicate) {
        const GlanceAppWidget* w = FindWidget(widget);
        if (!w || !w->stateDefinition) throw std::logic_error("GlanceAppWidget.updateIf cannot be used if no state is defined.");
        for (GlanceId id : GetGlanceIds(widget)) {
            std::any state = co_await w->stateDefinition->GetAny(AppWidgetStateKey(id));
            if (predicate(std::any_cast<const State&>(state))) Update(widget, id);
        }
    }

}
