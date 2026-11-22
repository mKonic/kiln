#include "kiln/glance/Glance.h"

#include "kiln/Log.h"
#include "kiln/jvm/Codec.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace kiln::glance {

    namespace {
        // The element kinds KilnGlance.java reads.
        enum Kind : std::int32_t {
            kBox = 1, kRow, kColumn, kText, kImage, kButton, kSpacer, kLinearProgress, kCircularProgress,
            kCheckBox, kSwitch, kLazyColumn, kListItem,
        };
        constexpr std::int32_t kFormat = 2;
        using MK = GlanceModifier::Item::Kind;

        // An element of the content (an Emittable), typed until it is encoded.
        struct Node {
            std::int32_t kind = 0;
            GlanceModifier modifier;
            Alignment alignment;
            std::string text;
            std::optional<TextStyle> style;
            int maxLines = INT_MAX;
            int verticalTextGravity = 0x30;   // Gravity.TOP
            ImageProvider image;
            std::optional<std::string> description;
            ContentScale scale = ContentScale::Fit;
            std::optional<ColorFilter> filter;
            float alpha = 1;
            bool enabled = true, checked = false;
            float progress = 0;
            ColorProvider color, background;
            std::int64_t itemId = 0;
            std::vector<Node> children;
        };

        // The content being built.
        struct Build {
            std::vector<Node>* children = nullptr;
            ColorProviders colors;
            int lambdas = 0;   // default lambda keys, in the order made
        };
        thread_local Build* t_Build = nullptr;

        Node& Emit(std::int32_t kind, GlanceModifier modifier) {
            static thread_local std::vector<Node> outside;
            if (!t_Build || !t_Build->children) {
                outside.clear();
                log::Warn("kiln", "glance: an element outside a widget's content is ignored");
                outside.push_back({});
                return outside.back();
            }
            t_Build->children->push_back({});
            Node& n = t_Build->children->back();
            n.kind = kind;
            n.modifier = std::move(modifier);
            return n;
        }
        void Within(Node& n, const kiln::Function<void()>& content) {
            if (!content || !t_Build) return;
            std::vector<Node>* saved = t_Build->children;
            t_Build->children = &n.children;
            content();
            t_Build->children = saved;
        }

        // --- modifier lookups (findModifier / extractModifier) --------------------------------
        const GlanceModifier::Item* FindLast(const GlanceModifier& m, MK k) {
            const GlanceModifier::Item* out = nullptr;
            for (const auto& i : m.Items()) if (i.kind == k) out = &i;
            return out;
        }
        bool Has(const GlanceModifier& m, MK k) { return FindLast(m, k) != nullptr; }
        // The last element of a kind, and the modifier without any of that kind.
        std::pair<std::optional<GlanceModifier::Item>, GlanceModifier> Extract(const GlanceModifier& m, MK k) {
            std::optional<GlanceModifier::Item> found;
            GlanceModifier rest;
            for (const auto& i : m.Items()) {
                if (i.kind == k) found = i;
                else rest = rest.Then(i);
            }
            return { found, rest };
        }
        // Background colours and images are one BackgroundModifier upstream: the last of either.
        std::pair<std::optional<GlanceModifier::Item>, GlanceModifier> ExtractBackground(const GlanceModifier& m) {
            std::optional<GlanceModifier::Item> found;
            GlanceModifier rest;
            for (const auto& i : m.Items()) {
                if (i.kind == MK::BackgroundColor || i.kind == MK::BackgroundImage) found = i;
                else rest = rest.Then(i);
            }
            return { found, rest };
        }
        GlanceModifier::Item Simple(MK k) {
            GlanceModifier::Item i{};
            i.kind = k;
            return i;
        }

        // --- NormalizeCompositionTree --------------------------------------------------------
        bool IsContainer(const Node& n) { return n.kind == kBox || n.kind == kRow || n.kind == kColumn; }

        // normalizeSizes: a wrapping container of a filling child fills too.
        void NormalizeSizes(Node& n) {
            for (Node& c : n.children) NormalizeSizes(c);
            auto dim = [](const Node& x, MK k) {
                const auto* i = FindLast(x.modifier, k);
                return i ? i->dimension.kind : Dimension::Kind::Wrap;
            };
            if (dim(n, MK::Height) == Dimension::Kind::Wrap &&
                std::any_of(n.children.begin(), n.children.end(), [&](const Node& c) { return dim(c, MK::Height) == Dimension::Kind::Fill; }))
                n.modifier = FillMaxHeight(n.modifier);
            if (dim(n, MK::Width) == Dimension::Kind::Wrap &&
                std::any_of(n.children.begin(), n.children.end(), [&](const Node& c) { return dim(c, MK::Width) == Dimension::Kind::Fill; }))
                n.modifier = FillMaxWidth(n.modifier);
        }

        // findActionHolderInSingleChildChain
        Node* ActionHolderInSingleChildChain(Node& n) {
            Node* only = nullptr;
            for (Node& c : n.children) {
                if (c.kind == kSpacer) continue;
                if (only) return nullptr;
                only = &c;
            }
            if (!only || !IsContainer(*only)) return nullptr;
            if (Has(only->modifier, MK::Clickable)) return only;
            return ActionHolderInSingleChildChain(*only);
        }

        // normalizeLazyListItem: the item's content in a Box, a tap in a single-child chain
        // hoisted to the item's root (keyboard clicks reach the root view).
        void NormalizeListItem(Node& item, Alignment alignment) {
            Node* holder = Has(item.modifier, MK::Clickable) ? nullptr : ActionHolderInSingleChildChain(item);
            if (holder) {
                auto [action, rest] = Extract(holder->modifier, MK::Clickable);
                if (action) {
                    item.modifier = item.modifier.Then(*action);
                    holder->modifier = rest;
                }
            }
            Node box;
            box.kind = kBox;
            box.children = std::move(item.children);
            box.alignment = alignment;
            box.modifier = Extract(item.modifier, MK::Clickable).second;
            // The item's root is the box: its tap goes on it.
            if (const auto* a = FindLast(item.modifier, MK::Clickable)) box.modifier = box.modifier.Then(*a);
            item.children.clear();
            item.children.push_back(std::move(box));
            item.modifier = {};
        }

        Node ImageNode(ImageProvider provider, GlanceModifier modifier) {
            Node n;
            n.kind = kImage;
            n.image = std::move(provider);
            n.modifier = std::move(modifier);
            return n;
        }

        // transformBackgroundImageAndActionRipple: a background image and a tap's ripple become
        // images behind and in front of the element, in a Box carrying its size, corners and tap.
        Node TransformBackgroundAndRipple(Node target) {
            if (target.kind == kListItem) return target;
            const bool isButton = target.kind == kButton;
            if (isButton) {
                // Buttons cannot have a background image (Glance logs and drops it).
                GlanceModifier rest;
                for (const auto& i : target.modifier.Items()) {
                    if (i.kind == MK::BackgroundImage) log::Warn("kiln", "glance: Buttons should not have a background image modifier");
                    else rest = rest.Then(i);
                }
                target.modifier = rest;
            }
            // Switches, check boxes and buttons have a ripple of their own (API 31).
            const bool builtinRipple = target.kind == kSwitch || target.kind == kCheckBox || isButton;
            const bool wrap = target.modifier.Any([&](const GlanceModifier::Item& i) {
                return i.kind == MK::BackgroundImage || (i.kind == MK::Clickable && !builtinRipple);
            });
            if (!wrap) return target;

            GlanceModifier boxModifiers, targetModifiers;
            auto [bg, minusBg] = ExtractBackground(target.modifier);
            std::optional<Node> bgImage;
            if (bg) {
                if (bg->kind == MK::BackgroundImage) {
                    Node img = ImageNode(bg->image, FillMaxSize({}));
                    img.scale = bg->contentScale;
                    img.filter = bg->colorFilter;
                    img.alpha = bg->alpha;
                    bgImage = std::move(img);
                } else {
                    targetModifiers = targetModifiers.Then(*bg);
                }
            }
            int clickables = 0;
            for (const auto& i : minusBg.Items()) clickables += i.kind == MK::Clickable;
            if (clickables > 1) log::Warn("kiln", "glance: more than one clickable on the same GlanceModifier, only the last one is used");
            auto [action, minusAction] = Extract(minusBg, MK::Clickable);
            std::optional<Node> ripple;
            if (action) {
                boxModifiers = boxModifiers.Then(*action);
                if (!builtinRipple) {
                    const std::string drawable = !action->rippleOverride.empty() ? action->rippleOverride : "kiln_glance_ripple";
                    ripple = ImageNode(ImageProvider::Resource(drawable), FillMaxSize({}));
                }
            }
            // Sizes and corners go to the box, the target filling it.
            const bool hoist = minusAction.Any([](const GlanceModifier::Item& i) {
                return i.kind == MK::Width || i.kind == MK::Height || i.kind == MK::CornerRadius || i.kind == MK::AppWidgetBackground;
            });
            GlanceModifier rest;
            for (const auto& i : minusAction.Items()) {
                const bool sizeOrCorner = i.kind == MK::Width || i.kind == MK::Height || i.kind == MK::CornerRadius || i.kind == MK::AppWidgetBackground;
                if (hoist && sizeOrCorner) boxModifiers = boxModifiers.Then(i);
                else rest = rest.Then(i);
            }
            targetModifiers = targetModifiers.Then(FillMaxSize(rest));

            Node box;
            box.kind = kBox;
            box.modifier = boxModifiers;
            target.modifier = targetModifiers;
            if (bgImage) box.children.push_back(std::move(*bgImage));
            box.children.push_back(std::move(target));
            if (ripple) box.children.push_back(std::move(*ripple));
            return box;
        }

        // translateEmittableButton's adjustments: enabled, 16 dp corners, default padding.
        void AdjustButton(Node& n) {
            n.modifier = CornerRadius(Enabled(n.modifier, n.enabled), 16);
            if (!Has(n.modifier, MK::Padding)) n.modifier = Padding(n.modifier, 16, 8);
        }

        void Normalize(std::vector<Node>& children) {
            for (Node& c : children) {
                if (c.kind == kLazyColumn) {
                    for (Node& item : c.children) NormalizeListItem(item, Alignment::CenterStart());
                }
                c = TransformBackgroundAndRipple(std::move(c));
                Normalize(c.children);
            }
        }

        void AdjustButtons(std::vector<Node>& children) {
            for (Node& c : children) {
                if (c.kind == kButton) AdjustButton(c);
                AdjustButtons(c.children);
            }
        }

        // updateLambdaActionKeys: each lambda's key gets its position among its siblings.
        using Lambdas = std::map<std::string, std::vector<kiln::Function<void()>>>;
        void UpdateLambdaKeys(std::vector<Node>& children, Lambdas& out) {
            for (std::size_t index = 0; index < children.size(); ++index) {
                Node& child = children[index];
                GlanceModifier rewritten;
                for (auto item : child.modifier.Items()) {
                    if (item.kind == MK::Clickable && item.action.kind == Action::Kind::Lambda) {
                        item.action.key += "+" + std::to_string(index);
                        out[item.action.key].push_back(item.action.block);
                    }
                    rewritten = rewritten.Then(item);
                }
                child.modifier = rewritten;
                UpdateLambdaKeys(child.children, out);
            }
        }

        // --- encoding ----------------------------------------------------------------------
        void WriteColor(jvm::Writer& w, const ColorProvider& c) {
            w.Bool(c.set);
            w.I32(std::int32_t(c.day));
            w.I32(std::int32_t(c.night));
            w.Str(c.resource);
        }
        void WriteDimension(jvm::Writer& w, const Dimension& d) {
            w.I32(std::int32_t(d.kind));
            w.F32(d.dp);
            w.Str(d.resource);
        }
        void WriteAction(jvm::Writer& w, const Action& a) {
            w.I32(std::int32_t(a.kind));
            w.Str(a.uri);
            w.Str(a.key);
            w.Str(a.component);
            w.Bytes(ParametersToBytes(a.parameters));
            w.Bool(a.foregroundService);
        }
        int WeightValue(const std::optional<FontWeight>& f) {
            if (!f) return -1;
            return *f == FontWeight::Bold ? 700 : *f == FontWeight::Medium ? 500 : 400;
        }
        void WriteStyle(jvm::Writer& w, const std::optional<TextStyle>& s) {
            w.Bool(s.has_value());
            if (!s) return;
            WriteColor(w, s->color);
            w.F32(s->fontSize);
            w.I32(WeightValue(s->fontWeight));
            w.I32(s->fontStyle ? int(*s->fontStyle) : -1);
            w.I32(s->textAlign ? int(*s->textAlign) : -1);
            w.I32(s->textDecoration ? s->textDecoration->mask : -1);
            w.Str(s->fontFamily ? s->fontFamily->family : std::string());
        }
        void WriteModifiers(jvm::Writer& w, const GlanceModifier& m) {
            std::vector<const GlanceModifier::Item*> items;
            for (const auto& i : m.Items())
                if (i.kind != MK::BackgroundImage && i.kind != MK::AppWidgetBackground && i.kind != MK::SelectableGroup) items.push_back(&i);
            w.I32(std::int32_t(items.size()));
            for (const auto* p : items) {
                const auto& i = *p;
                w.I32(std::int32_t(i.kind));
                switch (i.kind) {
                    case MK::Width: case MK::Height: case MK::CornerRadius: WriteDimension(w, i.dimension); break;
                    case MK::Padding:
                        w.F32(i.left); w.F32(i.top); w.F32(i.right); w.F32(i.bottom);
                        w.Str(i.leftRes); w.Str(i.topRes); w.Str(i.rightRes); w.Str(i.bottomRes);
                        w.Bool(i.absolute);
                        break;
                    case MK::BackgroundColor: WriteColor(w, i.color); break;
                    case MK::Clickable: WriteAction(w, i.action); break;
                    case MK::Visibility: case MK::Enabled: case MK::ClipToOutline: w.I32(i.value); break;
                    case MK::Semantics: {
                        auto d = i.semantics.GetOrNull(SemanticsProperties::ContentDescription());
                        std::string joined;
                        if (d) for (std::size_t k = 0; k < d->size(); ++k) joined += (k ? ", " : "") + (*d)[k];
                        w.Bool(d.has_value());
                        w.Str(joined);
                        break;
                    }
                    case MK::BackgroundImage: case MK::AppWidgetBackground: case MK::SelectableGroup: break;
                }
            }
        }
        void WriteNode(jvm::Writer& w, const Node& n) {
            w.I32(n.kind);
            WriteModifiers(w, n.modifier);
            jvm::Writer p;
            switch (n.kind) {
                case kBox: case kRow: case kColumn: p.I32(Gravity(n.alignment)); break;
                case kText: case kButton:
                    p.Str(n.text);
                    WriteStyle(p, n.style);
                    p.I32(n.maxLines);
                    p.I32(n.verticalTextGravity);
                    break;
                case kImage:
                    p.Str(n.image.resource);
                    p.Bytes(n.image.bytes);
                    p.Str(n.image.uri);
                    p.Bool(n.description.has_value());
                    p.Str(n.description.value_or(""));
                    p.I32(int(n.scale));
                    p.Bool(n.filter.has_value());
                    WriteColor(p, n.filter ? n.filter->tint : ColorProvider{});
                    p.F32(n.alpha);
                    break;
                case kLinearProgress:
                    p.F32(n.progress);
                    WriteColor(p, n.color);
                    WriteColor(p, n.background);
                    break;
                case kCircularProgress: WriteColor(p, n.color); break;
                case kCheckBox: case kSwitch:
                    p.Bool(n.checked);
                    p.Str(n.text);
                    WriteColor(p, n.color);
                    break;
                case kListItem: p.I64(n.itemId); break;
                default: break;
            }
            w.Bytes(p.Data());
            w.I32(std::int32_t(n.children.size()));
            for (const Node& c : n.children) WriteNode(w, c);
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
        // Each placed widget's lambdas and the context of its last drawing.
        struct Drawn {
            Lambdas lambdas;
            WidgetContext context;
        };
        std::map<std::pair<std::string, int>, Drawn>& DrawnWidgets() {
            static std::map<std::pair<std::string, int>, Drawn> d;
            return d;
        }
        GlanceHost& Host() {
            static GlanceHost h;
            return h;
        }

        ColorProvider Res(const char* name) { return ColorProvider::Resource(name); }
    }

    // --- colours ----------------------------------------------------------------------------------

    const ColorProviders& DynamicThemeColorProviders() {
        static const ColorProviders c{
            Res("kiln_glance_colorPrimary"), Res("kiln_glance_colorOnPrimary"), Res("kiln_glance_colorPrimaryContainer"), Res("kiln_glance_colorOnPrimaryContainer"),
            Res("kiln_glance_colorSecondary"), Res("kiln_glance_colorOnSecondary"), Res("kiln_glance_colorSecondaryContainer"), Res("kiln_glance_colorOnSecondaryContainer"),
            Res("kiln_glance_colorTertiary"), Res("kiln_glance_colorOnTertiary"), Res("kiln_glance_colorTertiaryContainer"), Res("kiln_glance_colorOnTertiaryContainer"),
            Res("kiln_glance_colorError"), Res("kiln_glance_colorErrorContainer"), Res("kiln_glance_colorOnError"), Res("kiln_glance_colorOnErrorContainer"),
            Res("kiln_glance_colorBackground"), Res("kiln_glance_colorOnBackground"), Res("kiln_glance_colorSurface"), Res("kiln_glance_colorOnSurface"),
            Res("kiln_glance_colorSurfaceVariant"), Res("kiln_glance_colorOnSurfaceVariant"),
            Res("kiln_glance_colorOutline"), Res("kiln_glance_colorOnSurfaceInverse"), Res("kiln_glance_colorSurfaceInverse"), Res("kiln_glance_colorPrimaryInverse"),
            Res("kiln_glance_colorWidgetBackground"),
        };
        return c;
    }
    const ColorProviders& GlanceTheme::colors() { return t_Build ? t_Build->colors : DynamicThemeColorProviders(); }
    void GlanceTheme(ColorProviders colors, kiln::Function<void()> content) {
        if (!content) return;
        if (!t_Build) {
            content();
            return;
        }
        ColorProviders saved = std::move(t_Build->colors);
        t_Build->colors = std::move(colors);
        content();
        t_Build->colors = std::move(saved);
    }
    ButtonColors ButtonDefaults::buttonColors(ColorProvider backgroundColor, ColorProvider contentColor) {
        return { backgroundColor.set ? backgroundColor : GlanceTheme::colors().primary, contentColor.set ? contentColor : GlanceTheme::colors().onPrimary };
    }

    // --- actions ------------------------------------------------------------------------------

    MutableActionParameters MutableActionParametersOf(std::initializer_list<ActionParameters::Pair> pairs) {
        MutableActionParameters p;
        for (const auto& pair : pairs) p.Put(pair);
        return p;
    }
    Action ActionStartActivity(std::string deepLink, ActionParameters parameters) {
        Action a;
        a.kind = Action::Kind::StartActivity;
        a.uri = std::move(deepLink);
        a.parameters = std::move(parameters);
        return a;
    }
    Action ActionRunCallback(std::string key, ActionParameters parameters) {
        Action a;
        a.kind = Action::Kind::RunCallback;
        a.key = std::move(key);
        a.parameters = std::move(parameters);
        return a;
    }
    Action LambdaAction(kiln::Function<void()> block, std::string key) {
        Action a;
        a.kind = Action::Kind::Lambda;
        // Without a key, its place among the content's lambdas (currentCompositeKeyHash's role).
        a.key = !key.empty() ? std::move(key) : std::to_string(t_Build ? t_Build->lambdas++ : 0);
        a.block = std::move(block);
        return a;
    }
    Action ActionSendBroadcast(std::string intentAction, std::string receiverClass) {
        Action a;
        a.kind = Action::Kind::SendBroadcast;
        a.uri = std::move(intentAction);
        a.component = std::move(receiverClass);
        return a;
    }
    Action ActionSendBroadcastTo(std::string receiverClass) { return ActionSendBroadcast({}, std::move(receiverClass)); }
    Action ActionStartService(std::string serviceClass, bool isForegroundService) {
        Action a;
        a.kind = Action::Kind::StartService;
        a.component = std::move(serviceClass);
        a.foregroundService = isForegroundService;
        return a;
    }

    std::vector<std::uint8_t> ParametersToBytes(const ActionParameters& p) {
        jvm::Writer w;
        w.I32(std::int32_t(p.AsMap().size()));
        for (const auto& [name, value] : p.AsMap()) {
            w.Str(name);
            w.I32(std::int32_t(value.index()));
            std::visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>) w.Bool(v);
                else if constexpr (std::is_same_v<T, std::int32_t>) w.I32(v);
                else if constexpr (std::is_same_v<T, std::int64_t>) w.I64(v);
                else if constexpr (std::is_same_v<T, float>) w.F32(v);
                else if constexpr (std::is_same_v<T, double>) w.F64(v);
                else w.Str(v);
            }, value);
        }
        return w.Take();
    }
    ActionParameters ParametersFromBytes(const std::vector<std::uint8_t>& bytes) {
        ActionParameters p;
        if (bytes.empty()) return p;
        jvm::Reader r(bytes);
        const int n = r.I32();
        for (int i = 0; i < n; ++i) {
            std::string name = r.Str();
            switch (r.I32()) {
                case 0: p.m_Values[name] = r.Bool(); break;
                case 1: p.m_Values[name] = r.I32(); break;
                case 2: p.m_Values[name] = r.I64(); break;
                case 3: p.m_Values[name] = r.F32(); break;
                case 4: p.m_Values[name] = r.F64(); break;
                default: p.m_Values[name] = r.Str(); break;
            }
        }
        return p;
    }

    // --- semantics ------------------------------------------------------------------------------

    const SemanticsPropertyKey<std::vector<std::string>>& SemanticsProperties::ContentDescription() {
        static const SemanticsPropertyKey<std::vector<std::string>> k{
            "ContentDescription",
            [](const std::optional<std::vector<std::string>>& parent, const std::vector<std::string>& child) -> std::optional<std::vector<std::string>> {
                if (!parent) return child;
                std::vector<std::string> out = *parent;
                out.insert(out.end(), child.begin(), child.end());
                return out;
            },
        };
        return k;
    }
    const SemanticsPropertyKey<std::string>& SemanticsProperties::TestTag() {
        static const SemanticsPropertyKey<std::string> k{
            "TestTag", [](const std::optional<std::string>& parent, const std::string&) { return parent; },   // no merge
        };
        return k;
    }

    // --- modifiers --------------------------------------------------------------------------------

    bool GlanceModifier::Any(const kiln::Function<bool(const Item&)>& predicate) const {
        return std::any_of(m_Items.begin(), m_Items.end(), [&](const Item& i) { return predicate(i); });
    }
    bool GlanceModifier::All(const kiln::Function<bool(const Item&)>& predicate) const {
        return std::all_of(m_Items.begin(), m_Items.end(), [&](const Item& i) { return predicate(i); });
    }

    GlanceModifier Width(GlanceModifier m, Dimension d) {
        auto i = Simple(MK::Width);
        i.dimension = std::move(d);
        return m.Then(std::move(i));
    }
    GlanceModifier Height(GlanceModifier m, Dimension d) {
        auto i = Simple(MK::Height);
        i.dimension = std::move(d);
        return m.Then(std::move(i));
    }
    GlanceModifier Width(GlanceModifier m, float dp) { return Width(std::move(m), Dimension::Dp(dp)); }
    GlanceModifier Height(GlanceModifier m, float dp) { return Height(std::move(m), Dimension::Dp(dp)); }
    GlanceModifier Size(GlanceModifier m, float dp) { return Height(Width(std::move(m), Dimension::Dp(dp)), Dimension::Dp(dp)); }
    GlanceModifier Size(GlanceModifier m, float w, float h) { return Height(Width(std::move(m), Dimension::Dp(w)), Dimension::Dp(h)); }
    GlanceModifier Size(GlanceModifier m, Dimension w, Dimension h) { return Height(Width(std::move(m), std::move(w)), std::move(h)); }
    GlanceModifier FillMaxWidth(GlanceModifier m) { return Width(std::move(m), Dimension::Fill()); }
    GlanceModifier FillMaxHeight(GlanceModifier m) { return Height(std::move(m), Dimension::Fill()); }
    GlanceModifier FillMaxSize(GlanceModifier m) { return FillMaxHeight(FillMaxWidth(std::move(m))); }
    GlanceModifier WrapContentWidth(GlanceModifier m) { return Width(std::move(m), Dimension::Wrap()); }
    GlanceModifier WrapContentHeight(GlanceModifier m) { return Height(std::move(m), Dimension::Wrap()); }
    GlanceModifier WrapContentSize(GlanceModifier m) { return WrapContentHeight(WrapContentWidth(std::move(m))); }
    GlanceModifier DefaultWeight(GlanceModifier m) {
        // Expand on both axes: the parent (Row or Column) uses its main axis.
        return Height(Width(std::move(m), Dimension::Expand()), Dimension::Expand());
    }
    GlanceModifier Padding(GlanceModifier m, float all) { return Padding(std::move(m), all, all, all, all); }
    GlanceModifier Padding(GlanceModifier m, float h, float v) { return Padding(std::move(m), h, v, h, v); }
    GlanceModifier Padding(GlanceModifier m, float start, float top, float end, float bottom) {
        auto i = Simple(MK::Padding);
        i.left = start; i.top = top; i.right = end; i.bottom = bottom;
        return m.Then(std::move(i));
    }
    GlanceModifier PaddingResource(GlanceModifier m, std::string start, std::string top, std::string end, std::string bottom) {
        auto i = Simple(MK::Padding);
        i.leftRes = std::move(start); i.topRes = std::move(top); i.rightRes = std::move(end); i.bottomRes = std::move(bottom);
        return m.Then(std::move(i));
    }
    GlanceModifier AbsolutePadding(GlanceModifier m, float left, float top, float right, float bottom) {
        auto i = Simple(MK::Padding);
        i.left = left; i.top = top; i.right = right; i.bottom = bottom;
        i.absolute = true;
        return m.Then(std::move(i));
    }
    GlanceModifier AbsolutePaddingResource(GlanceModifier m, std::string left, std::string top, std::string right, std::string bottom) {
        auto i = Simple(MK::Padding);
        i.leftRes = std::move(left); i.topRes = std::move(top); i.rightRes = std::move(right); i.bottomRes = std::move(bottom);
        i.absolute = true;
        return m.Then(std::move(i));
    }
    GlanceModifier Background(GlanceModifier m, ColorProvider color) {
        auto i = Simple(MK::BackgroundColor);
        i.color = std::move(color);
        return m.Then(std::move(i));
    }
    GlanceModifier Background(GlanceModifier m, std::uint32_t day, std::uint32_t night) { return Background(std::move(m), ColorProvider(day, night)); }
    GlanceModifier Background(GlanceModifier m, ImageProvider image, ContentScale contentScale, std::optional<ColorFilter> colorFilter, float alpha) {
        auto i = Simple(MK::BackgroundImage);
        i.image = std::move(image);
        i.contentScale = contentScale;
        i.colorFilter = std::move(colorFilter);
        i.alpha = alpha;
        return m.Then(std::move(i));
    }
    GlanceModifier BackgroundResource(GlanceModifier m, std::string drawable) {
        return Background(std::move(m), ImageProvider::Resource(std::move(drawable)));
    }
    GlanceModifier CornerRadius(GlanceModifier m, float dp) { return CornerRadius(std::move(m), Dimension::Dp(dp)); }
    GlanceModifier CornerRadius(GlanceModifier m, Dimension radius) {
        auto i = Simple(MK::CornerRadius);
        i.dimension = std::move(radius);
        return m.Then(std::move(i));
    }
    GlanceModifier Clickable(GlanceModifier m, Action action, std::string rippleOverride) {
        auto i = Simple(MK::Clickable);
        i.action = std::move(action);
        i.rippleOverride = std::move(rippleOverride);
        return m.Then(std::move(i));
    }
    GlanceModifier Clickable(GlanceModifier m, kiln::Function<void()> block, std::string key, std::string rippleOverride) {
        return Clickable(std::move(m), LambdaAction(std::move(block), std::move(key)), std::move(rippleOverride));
    }
    GlanceModifier VisibilityOf(GlanceModifier m, Visibility v) {
        auto i = Simple(MK::Visibility);
        i.value = int(v);
        return m.Then(std::move(i));
    }
    GlanceModifier Semantics(GlanceModifier m, const kiln::Function<void(SemanticsPropertyReceiver&)>& properties) {
        auto i = Simple(MK::Semantics);
        if (properties) properties(i.semantics);
        return m.Then(std::move(i));
    }
    GlanceModifier ContentDescription(GlanceModifier m, std::string description) {
        return Semantics(std::move(m), [&](SemanticsPropertyReceiver& s) { s.SetContentDescription(description); });
    }
    GlanceModifier Enabled(GlanceModifier m, bool enabled) {
        auto i = Simple(MK::Enabled);
        i.value = enabled ? 1 : 0;
        return m.Then(std::move(i));
    }
    GlanceModifier ClipToOutline(GlanceModifier m, bool clip) {
        auto i = Simple(MK::ClipToOutline);
        i.value = clip ? 1 : 0;
        return m.Then(std::move(i));
    }

    int Gravity(Alignment a) {
        // android.view.Gravity: START, CENTER_HORIZONTAL, END | TOP, CENTER_VERTICAL, BOTTOM.
        const int h = a.horizontal == HorizontalAlignment::Start ? 0x00800003 : a.horizontal == HorizontalAlignment::End ? 0x00800005 : 0x01;
        const int v = a.vertical == VerticalAlignment::Top ? 0x30 : a.vertical == VerticalAlignment::Bottom ? 0x50 : 0x10;
        return h | v;
    }

    // --- elements ---------------------------------------------------------------------------------

    void Box(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content) {
        Node& n = Emit(kBox, std::move(modifier));
        n.alignment = alignment;
        Within(n, content);
    }
    void Row(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content) {
        Node& n = Emit(kRow, std::move(modifier));
        n.alignment = alignment;
        Within(n, content);
    }
    void Column(GlanceModifier modifier, Alignment alignment, kiln::Function<void()> content) {
        Node& n = Emit(kColumn, std::move(modifier));
        n.alignment = alignment;
        Within(n, content);
    }
    void Text(std::string text, GlanceModifier modifier, TextStyle style, int maxLines) {
        Node& n = Emit(kText, std::move(modifier));
        n.text = std::move(text);
        n.style = std::move(style);
        n.maxLines = maxLines;
    }
    void Image(ImageProvider provider, std::optional<std::string> description, GlanceModifier modifier, ContentScale scale,
               std::optional<ColorFilter> colorFilter, float alpha) {
        Node& n = Emit(kImage, std::move(modifier));
        n.image = std::move(provider);
        n.description = std::move(description);
        n.scale = scale;
        n.filter = std::move(colorFilter);
        n.alpha = alpha;
    }
    void Button(std::string text, Action onClick, GlanceModifier modifier, bool enabled, std::optional<TextStyle> style,
                std::optional<ButtonColors> colors, int maxLines) {
        // ButtonElement: clickable while enabled, the colours' background, the style in the
        // content colour.
        const ButtonColors c = colors ? *colors : ButtonDefaults::buttonColors();
        GlanceModifier m = enabled ? Clickable(std::move(modifier), std::move(onClick)) : std::move(modifier);
        m = Background(std::move(m), c.backgroundColor);
        TextStyle s = style ? *style : TextStyle{};
        s.color = c.contentColor;
        Node& n = Emit(kButton, std::move(m));
        n.text = std::move(text);
        n.style = std::move(s);
        n.enabled = enabled;
        n.maxLines = maxLines;
        n.verticalTextGravity = 0x10;   // Gravity.CENTER_VERTICAL
    }
    void Button(std::string text, kiln::Function<void()> onClick, GlanceModifier modifier, bool enabled, std::optional<TextStyle> style,
                std::optional<ButtonColors> colors, int maxLines, std::string key) {
        Button(std::move(text), LambdaAction(std::move(onClick), std::move(key)), std::move(modifier), enabled, std::move(style), std::move(colors), maxLines);
    }
    void Spacer(GlanceModifier modifier) { Emit(kSpacer, std::move(modifier)); }
    void LinearProgressIndicator(float progress, GlanceModifier modifier, ColorProvider color, ColorProvider background) {
        Node& n = Emit(kLinearProgress, std::move(modifier));
        n.progress = progress;
        n.color = std::move(color);
        n.background = std::move(background);
    }
    void CircularProgressIndicator(GlanceModifier modifier, ColorProvider color) {
        Node& n = Emit(kCircularProgress, std::move(modifier));
        n.color = std::move(color);
    }
    namespace {
        void Toggle(std::int32_t kind, bool checked, Action onChange, std::string text, GlanceModifier modifier, ColorProvider textColor) {
            // The action is a CompoundButtonAction: the host sets it as the checked-change response.
            if (onChange.kind != Action::Kind::None) modifier = Clickable(std::move(modifier), std::move(onChange));
            Node& n = Emit(kind, std::move(modifier));
            n.checked = checked;
            n.text = std::move(text);
            n.color = std::move(textColor);
        }
    }
    void CheckBox(bool checked, Action onChange, std::string text, GlanceModifier modifier, ColorProvider textColor) {
        Toggle(kCheckBox, checked, std::move(onChange), std::move(text), std::move(modifier), std::move(textColor));
    }
    void Switch(bool checked, Action onChange, std::string text, GlanceModifier modifier, ColorProvider textColor) {
        Toggle(kSwitch, checked, std::move(onChange), std::move(text), std::move(modifier), std::move(textColor));
    }
    void LazyListScope::Item(std::int64_t id, kiln::Function<void()> content) {
        Node& n = Emit(kListItem, {});
        n.itemId = id;
        Within(n, content);
    }
    void LazyListScope::Items(int count, kiln::Function<std::int64_t(int)> id, kiln::Function<void(int)> content) {
        for (int i = 0; i < count; ++i) Item(id ? id(i) : i, [&content, i] { content(i); });
    }
    void LazyColumn(GlanceModifier modifier, kiln::Function<void(LazyListScope&)> content) {
        Node& n = Emit(kLazyColumn, std::move(modifier));
        if (!content || !t_Build) return;
        std::vector<Node>* saved = t_Build->children;
        t_Build->children = &n.children;
        LazyListScope scope;
        content(scope);
        t_Build->children = saved;
    }

    // --- widgets ----------------------------------------------------------------------------------

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
    bool RunLambdaAction(const std::string& widget, int appWidgetId, const std::string& key) {
        auto find = [&]() -> std::vector<kiln::Function<void()>> {
            std::lock_guard lock(Mutex());
            auto it = DrawnWidgets().find({ widget, appWidgetId });
            if (it == DrawnWidgets().end()) return {};
            auto l = it->second.lambdas.find(key);
            return l == it->second.lambdas.end() ? std::vector<kiln::Function<void()>>{} : l->second;
        };
        auto blocks = find();
        if (blocks.empty()) {
            // A new process: draw the widget again to find its lambdas, at the size it was last drawn.
            WidgetContext c{ widget, appWidgetId, {} };
            {
                std::lock_guard lock(Mutex());
                auto it = DrawnWidgets().find({ widget, appWidgetId });
                if (it != DrawnWidgets().end()) c = it->second.context;
            }
            Render(widget, c);
            blocks = find();
        }
        for (auto& b : blocks) if (b) b();
        return !blocks.empty();
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
        // The content's elements go under the root (RemoteViewsRoot).
        Node root;
        root.kind = kBox;
        Build build;
        build.children = &root.children;
        build.colors = DynamicThemeColorProviders();
        Build* saved = t_Build;
        t_Build = &build;
        try {
            widget->content(context);
        } catch (...) {
            t_Build = saved;
            throw;
        }
        t_Build = saved;

        // normalizeCompositionTree: one child under the root, sizes, then each element.
        if (root.children.size() != 1) {
            Node box;
            box.kind = kBox;
            box.children = std::move(root.children);
            root.children.clear();
            root.children.push_back(std::move(box));
        }
        NormalizeSizes(root);
        AdjustButtons(root.children);
        Normalize(root.children);
        Lambdas lambdas;
        UpdateLambdaKeys(root.children, lambdas);
        {
            std::lock_guard lock(Mutex());
            Drawn& d = DrawnWidgets()[{ name, context.appWidgetId }];
            d.lambdas = std::move(lambdas);
            d.context = context;
        }

        jvm::Writer w;
        w.I32(kFormat);
        WriteNode(w, root.children[0]);
        return w.Take();
    }

    void SetGlanceHost(GlanceHost host) { Host() = std::move(host); }
    void UpdateAll(const std::string& widget) {
        if (Host().updateAll) Host().updateAll(widget);
    }
    std::vector<int> AppWidgetIds(const std::string& widget) { return Host().appWidgetIds ? Host().appWidgetIds(widget) : std::vector<int>{}; }
    bool RequestPin(const std::string& widget) { return Host().requestPin && Host().requestPin(widget); }

}
