#include "kiln/glance/Glance.h"

#include "kiln/Log.h"
#include "kiln/jvm/Codec.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace kiln::glance {

    namespace {
        // The element kinds and modifier codes KilnGlance.java reads.
        enum Kind : std::int32_t {
            kBox = 1, kRow, kColumn, kText, kImage, kButton, kSpacer, kLinearProgress, kCircularProgress,
            kCheckBox, kSwitch, kLazyColumn, kListItem,
        };

        struct Element {
            std::int32_t kind = 0;
            GlanceModifier modifier;
            jvm::Writer props;
            std::vector<Element> children;
        };

        // The children being built (the content function's current parent).
        thread_local std::vector<Element>* t_Children = nullptr;

        Element& Emit(std::int32_t kind, GlanceModifier modifier) {
            static thread_local std::vector<Element> outside;
            std::vector<Element>& into = t_Children ? *t_Children : outside;
            if (!t_Children) {
                outside.clear();
                log::Warn("kiln", "glance: an element outside a widget's content is ignored");
            }
            into.push_back({ kind, std::move(modifier), {}, {} });
            return into.back();
        }
        void Within(Element& e, const kiln::Function<void()>& content) {
            if (!content) return;
            std::vector<Element>* saved = t_Children;
            t_Children = &e.children;
            content();
            t_Children = saved;
        }

        void WriteColor(jvm::Writer& w, const ColorProvider& c) {
            w.Bool(c.set);
            w.I32(std::int32_t(c.day));
            w.I32(std::int32_t(c.night));
        }
        void WriteAction(jvm::Writer& w, const Action& a) {
            w.I32(std::int32_t(a.kind));
            w.Str(a.uri);
            w.Str(a.key);
            w.Str(a.params);
        }
        void WriteElement(jvm::Writer& w, const Element& e) {
            w.I32(e.kind);
            const auto& items = e.modifier.Items();
            w.I32(std::int32_t(items.size()));
            for (const auto& m : items) {
                w.I32(std::int32_t(m.kind));
                switch (m.kind) {
                    case GlanceModifier::Item::Kind::Width:
                    case GlanceModifier::Item::Kind::Height:
                        w.I32(std::int32_t(m.dimension.kind));
                        w.F32(m.dimension.dp);
                        break;
                    case GlanceModifier::Item::Kind::Padding:
                    case GlanceModifier::Item::Kind::CornerRadius:
                        w.F32(m.left); w.F32(m.top); w.F32(m.right); w.F32(m.bottom);
                        break;
                    case GlanceModifier::Item::Kind::BackgroundColor: WriteColor(w, m.color); break;
                    case GlanceModifier::Item::Kind::BackgroundResource:
                    case GlanceModifier::Item::Kind::ContentDescription: w.Str(m.text); break;
                    case GlanceModifier::Item::Kind::Clickable: WriteAction(w, m.action); break;
                    case GlanceModifier::Item::Kind::Visibility:
                    case GlanceModifier::Item::Kind::Enabled: w.I32(m.value); break;
                }
            }
            const std::vector<std::uint8_t>& props = const_cast<jvm::Writer&>(e.props).Data();
            w.Bytes(props);
            w.I32(std::int32_t(e.children.size()));
            for (const Element& c : e.children) WriteElement(w, c);
        }

        std::mutex& Mutex() {
            static std::mutex m;
            return m;
        }
        std::map<std::string, GlanceAppWidget>& Widgets() {
            static std::map<std::string, GlanceAppWidget> w;
            return w;
        }
        std::map<std::string, kiln::Function<void(const ActionContext&)>>& Callbacks() {
            static std::map<std::string, kiln::Function<void(const ActionContext&)>> c;
            return c;
        }
        GlanceHost& Host() {
            static GlanceHost h;
            return h;
        }

        GlanceModifier::Item Simple(GlanceModifier::Item::Kind k) {
            GlanceModifier::Item i{};
            i.kind = k;
            return i;
        }
    }

    Action StartActivity(std::string deepLink) { return { Action::Kind::StartActivity, std::move(deepLink), {}, {} }; }
    Action RunCallback(std::string key, std::string params) { return { Action::Kind::RunCallback, {}, std::move(key), std::move(params) }; }

    GlanceModifier Width(GlanceModifier m, Dimension d) {
        auto i = Simple(GlanceModifier::Item::Kind::Width);
        i.dimension = d;
        return m.Then(i);
    }
    GlanceModifier Height(GlanceModifier m, Dimension d) {
        auto i = Simple(GlanceModifier::Item::Kind::Height);
        i.dimension = d;
        return m.Then(i);
    }
    GlanceModifier Size(GlanceModifier m, float dp) { return Height(Width(std::move(m), Dimension::Dp(dp)), Dimension::Dp(dp)); }
    GlanceModifier Size(GlanceModifier m, float w, float h) { return Height(Width(std::move(m), Dimension::Dp(w)), Dimension::Dp(h)); }
    GlanceModifier FillMaxWidth(GlanceModifier m) { return Width(std::move(m), Dimension::Fill()); }
    GlanceModifier FillMaxHeight(GlanceModifier m) { return Height(std::move(m), Dimension::Fill()); }
    GlanceModifier FillMaxSize(GlanceModifier m) { return FillMaxHeight(FillMaxWidth(std::move(m))); }
    GlanceModifier WrapContentSize(GlanceModifier m) { return Height(Width(std::move(m), Dimension::Wrap()), Dimension::Wrap()); }
    GlanceModifier DefaultWeight(GlanceModifier m) {
        // Expand on both axes: the parent (Row or Column) uses its main axis.
        return Height(Width(std::move(m), Dimension::Expand()), Dimension::Expand());
    }
    GlanceModifier Padding(GlanceModifier m, float all) { return Padding(std::move(m), all, all, all, all); }
    GlanceModifier Padding(GlanceModifier m, float h, float v) { return Padding(std::move(m), h, v, h, v); }
    GlanceModifier Padding(GlanceModifier m, float start, float top, float end, float bottom) {
        auto i = Simple(GlanceModifier::Item::Kind::Padding);
        i.left = start; i.top = top; i.right = end; i.bottom = bottom;
        return m.Then(i);
    }
    GlanceModifier Background(GlanceModifier m, ColorProvider color) {
        auto i = Simple(GlanceModifier::Item::Kind::BackgroundColor);
        i.color = color;
        return m.Then(i);
    }
    GlanceModifier BackgroundResource(GlanceModifier m, std::string drawable) {
        auto i = Simple(GlanceModifier::Item::Kind::BackgroundResource);
        i.text = std::move(drawable);
        return m.Then(i);
    }
    GlanceModifier CornerRadius(GlanceModifier m, float dp) {
        auto i = Simple(GlanceModifier::Item::Kind::CornerRadius);
        i.left = i.top = i.right = i.bottom = dp;
        return m.Then(i);
    }
    GlanceModifier Clickable(GlanceModifier m, Action action) {
        auto i = Simple(GlanceModifier::Item::Kind::Clickable);
        i.action = std::move(action);
        return m.Then(i);
    }
    GlanceModifier VisibilityOf(GlanceModifier m, Visibility v) {
        auto i = Simple(GlanceModifier::Item::Kind::Visibility);
        i.value = int(v);
        return m.Then(i);
    }
    GlanceModifier ContentDescription(GlanceModifier m, std::string description) {
        auto i = Simple(GlanceModifier::Item::Kind::ContentDescription);
        i.text = std::move(description);
        return m.Then(i);
    }

    int Gravity(Alignment a) {
        // android.view.Gravity: START, CENTER_HORIZONTAL, END | TOP, CENTER_VERTICAL, BOTTOM.
        const int h = a.horizontal == HorizontalAlignment::Start ? 0x00800003 : a.horizontal == HorizontalAlignment::End ? 0x00800005 : 0x01;
        const int v = a.vertical == VerticalAlignment::Top ? 0x30 : a.vertical == VerticalAlignment::Bottom ? 0x50 : 0x10;
        return h | v;
    }

    void Box(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content) {
        Element& e = Emit(kBox, std::move(modifier));
        e.props.I32(Gravity(alignment));
        Within(e, content);
    }
    void Row(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content) {
        Element& e = Emit(kRow, std::move(modifier));
        e.props.I32(Gravity(alignment));
        Within(e, content);
    }
    void Column(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content) {
        Element& e = Emit(kColumn, std::move(modifier));
        e.props.I32(Gravity(alignment));
        Within(e, content);
    }
    void Text(std::string text, GlanceModifier modifier, TextStyle style, int maxLines) {
        Element& e = Emit(kText, std::move(modifier));
        e.props.Str(text);
        e.props.F32(style.fontSize);
        WriteColor(e.props, style.color);
        e.props.I32(style.fontWeight == FontWeight::Bold ? 700 : style.fontWeight == FontWeight::Medium ? 500 : 400);
        e.props.Bool(style.italic);
        e.props.I32((style.underline ? 1 : 0) | (style.lineThrough ? 2 : 0));
        e.props.I32(style.textAlign == TextAlign::Center ? 0x01 : style.textAlign == TextAlign::End ? 0x00800005 : 0x00800003);
        e.props.I32(maxLines);
    }
    void Image(ImageProvider provider, std::string description, GlanceModifier modifier, ContentScale scale, ColorProvider tint, float alpha) {
        Element& e = Emit(kImage, std::move(modifier));
        e.props.Str(provider.resource);
        e.props.Bytes(provider.bytes);
        e.props.Str(description);
        e.props.I32(int(scale));
        WriteColor(e.props, tint);
        e.props.F32(alpha);
    }
    void Button(std::string text, Action onClick, GlanceModifier modifier, bool enabled, ColorProvider background, ColorProvider content) {
        Element& e = Emit(kButton, std::move(modifier));
        e.props.Str(text);
        WriteAction(e.props, onClick);
        e.props.Bool(enabled);
        WriteColor(e.props, background);
        WriteColor(e.props, content);
    }
    void Spacer(GlanceModifier modifier) { Emit(kSpacer, std::move(modifier)); }
    void LinearProgressIndicator(float progress, GlanceModifier modifier, ColorProvider color, ColorProvider background) {
        Element& e = Emit(kLinearProgress, std::move(modifier));
        e.props.F32(progress);
        WriteColor(e.props, color);
        WriteColor(e.props, background);
    }
    void CircularProgressIndicator(GlanceModifier modifier, ColorProvider color) {
        Element& e = Emit(kCircularProgress, std::move(modifier));
        WriteColor(e.props, color);
    }
    namespace {
        void Toggle(std::int32_t kind, bool checked, Action onChange, std::string text, GlanceModifier modifier, ColorProvider textColor) {
            Element& e = Emit(kind, std::move(modifier));
            e.props.Bool(checked);
            WriteAction(e.props, onChange);
            e.props.Str(text);
            WriteColor(e.props, textColor);
        }
    }
    void CheckBox(bool checked, Action onChange, std::string text, GlanceModifier modifier, ColorProvider textColor) {
        Toggle(kCheckBox, checked, std::move(onChange), std::move(text), std::move(modifier), textColor);
    }
    void Switch(bool checked, Action onChange, std::string text, GlanceModifier modifier, ColorProvider textColor) {
        Toggle(kSwitch, checked, std::move(onChange), std::move(text), std::move(modifier), textColor);
    }
    void LazyListScope::Item(std::int64_t id, kiln::Function<void()> content) {
        Element& e = Emit(kListItem, {});
        e.props.I64(id);
        Within(e, content);
    }
    void LazyListScope::Items(int count, kiln::Function<std::int64_t(int)> id, kiln::Function<void(int)> content) {
        for (int i = 0; i < count; ++i) Item(id ? id(i) : i, [&content, i] { content(i); });
    }
    void LazyColumn(GlanceModifier modifier, kiln::Function<void(LazyListScope&)> content) {
        Element& e = Emit(kLazyColumn, std::move(modifier));
        if (!content) return;
        std::vector<Element>* saved = t_Children;
        t_Children = &e.children;
        LazyListScope scope;
        content(scope);
        t_Children = saved;
    }

    void RegisterWidget(std::string name, GlanceAppWidget widget) {
        std::lock_guard lock(Mutex());
        Widgets()[std::move(name)] = std::move(widget);
    }
    const GlanceAppWidget* FindWidget(const std::string& name) {
        std::lock_guard lock(Mutex());
        auto it = Widgets().find(name);
        return it == Widgets().end() ? nullptr : &it->second;
    }
    void RegisterActionCallback(std::string key, kiln::Function<void(const ActionContext&)> callback) {
        std::lock_guard lock(Mutex());
        Callbacks()[std::move(key)] = std::move(callback);
    }
    bool RunActionCallback(const std::string& key, const ActionContext& context) {
        kiln::Function<void(const ActionContext&)> f;
        {
            std::lock_guard lock(Mutex());
            auto it = Callbacks().find(key);
            if (it == Callbacks().end()) return false;
            f = it->second;
        }
        f(context);
        return true;
    }

    std::vector<ui::Size> SizesToDraw(const GlanceAppWidget& widget, const std::vector<ui::Size>& launcherSizes, ui::Size minimum) {
        switch (widget.sizeMode) {
            case SizeMode::Single: return { minimum };
            case SizeMode::Responsive:
                if (!widget.responsiveSizes.empty()) return widget.responsiveSizes;
                return { minimum };
            case SizeMode::Exact: {
                // The launcher's sizes, without repeats (AppWidgetUtils: an unknown set is the minimum).
                std::vector<ui::Size> out;
                for (const ui::Size& s : launcherSizes)
                    if (s.width > 0 && s.height > 0 && std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
                if (out.empty()) out.push_back(minimum);
                return out;
            }
        }
        return { minimum };
    }

    std::vector<std::uint8_t> Render(const std::string& name, const WidgetContext& context) {
        const GlanceAppWidget* widget = FindWidget(name);
        if (!widget || !widget->content) return {};
        // The content's elements go under a root that fills the widget.
        std::vector<Element> root;
        std::vector<Element>* saved = t_Children;
        t_Children = &root;
        widget->content(context);
        t_Children = saved;
        jvm::Writer w;
        w.I32(1);   // the format's version
        w.I32(std::int32_t(root.size()));
        for (const Element& e : root) WriteElement(w, e);
        return w.Take();
    }

    void SetGlanceHost(GlanceHost host) { Host() = std::move(host); }
    void UpdateAll(const std::string& widget) {
        if (Host().updateAll) Host().updateAll(widget);
    }
    std::vector<int> AppWidgetIds(const std::string& widget) { return Host().appWidgetIds ? Host().appWidgetIds(widget) : std::vector<int>{}; }
    bool RequestPin(const std::string& widget) { return Host().requestPin && Host().requestPin(widget); }

}
