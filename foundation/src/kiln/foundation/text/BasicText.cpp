#include "kiln/foundation/text/BasicText.h"

#include "kiln/TextBackend.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Gestures.h"
#include "kiln/foundation/Hover.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/text/Selection.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/BringIntoView.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace kiln::foundation {

    namespace {

        using text::SpanStyle;

        // What the link boxes read: the latest layout, and the owner (for the UriHandler).
        struct LinkShared {
            std::shared_ptr<TextLayoutResult> layout;
            ui::Owner* owner = nullptr;
        };

        // SelectionController: the text's selectable inside a SelectionContainer, and its highlight.
        struct SelectionController {
            std::int64_t selectableId = 0;
            std::shared_ptr<selection::SelectionRegistrar> registrar;
            Color backgroundColor;
            std::shared_ptr<ui::PinnableContainer> pinnable;
            std::optional<selection::Coordinates> coordinates;
            std::shared_ptr<const TextLayoutResult> layout;
            std::shared_ptr<selection::Selectable> selectable;

            void OnPlaced(TextBackend* backend, ui::LayoutNode* node, const ModifierNode* at) {
                if (selectable) return;
                auto s = std::make_shared<selection::Selectable>();
                s->selectableId = selectableId;
                s->coordinatesCallback = [this] { return coordinates; };
                s->layoutCallback = [this] { return layout; };
                s->backend = backend;
                s->pinnableContainer = [this] { return pinnable; };
                s->bringIntoView = [node, at, alive = at->AliveToken()](Rect r) {
                    if (!alive.expired() && node->GetOwner()) ui::BringIntoView(*node, r, at);
                };
                selectable = registrar->Subscribe(std::move(s));
            }
            void OnUnplaced() {
                if (!selectable) return;
                registrar->Unsubscribe(*selectable);
                selectable.reset();
            }
            void UpdateTextLayout(std::shared_ptr<const TextLayoutResult> result) {
                if (layout && !(layout->LayoutInput().text == result->LayoutInput().text)) registrar->NotifySelectableChange(selectableId);
                layout = std::move(result);
            }
            void UpdateLayoutCoordinates(selection::Coordinates c) {
                coordinates = std::move(c);
                registrar->NotifyPositionChange(selectableId);
            }
            // The selected range's path behind the text (clipped like the text).
            void Draw(ui::DrawScope& s, bool clip) {
                const auto& subs = registrar->subselections.Get();
                auto it = subs.find(selectableId);
                if (it == subs.end() || !layout) return;
                const auto& sel = it->second;
                const int start = sel.handlesCrossed ? sel.end.offset : sel.start.offset;
                const int end = sel.handlesCrossed ? sel.start.offset : sel.end.offset;
                if (start == end) return;
                const int last = selectable ? selectable->GetLastVisibleOffset() : 0;
                const int a = std::min(start, last), b = std::min(end, last);
                if (clip) {
                    s.Save();
                    s.ClipRect({ 0, 0, s.GetSize().width, s.GetSize().height });
                }
                for (const RectF& r : layout->SelectionRects(std::min(a, b), std::max(a, b))) s.DrawRect(backgroundColor, { r.x, r.y }, { r.w, r.h });
                if (clip) s.Restore();
            }
        };

        struct TextParams {
            AnnotatedString text;
            TextStyle style;
            BasicTextOptions options;
            std::shared_ptr<LinkShared> links;
            std::vector<text::Range<text::Placeholder>> placeholders;
            std::shared_ptr<SelectionController> selection;
            bool operator==(const TextParams& o) const {
                // A callback is never equal: the node takes the new one (cheap, no relayout).
                return !options.onTextLayout && !o.options.onTextLayout && links == o.links && selection == o.selection && placeholders == o.placeholders && text == o.text && style == o.style &&
                       options.overflow == o.options.overflow && options.softWrap == o.options.softWrap &&
                       options.maxLines == o.options.maxLines && options.minLines == o.options.minLines;
            }
            bool SameLayout(const TextParams& o) const {
                return text == o.text && placeholders == o.placeholders && style.HasSameLayoutAffectingAttributes(o.style) && options.overflow == o.options.overflow &&
                       options.softWrap == o.options.softWrap && options.maxLines == o.options.maxLines && options.minLines == o.options.minLines;
            }
        };

        struct TextNode final : ModifierNode, LayoutModifier, DrawModifier, ui::GlobalPositionAwareModifier {
            TextParams params;
            explicit TextNode(TextParams p) : params(std::move(p)), m_Previous(params) {}
            LayoutModifier* AsLayout() override { return this; }
            DrawModifier* AsDraw() override { return this; }
            ui::GlobalPositionAwareModifier* AsGlobalPositionAware() override { return params.selection ? this : nullptr; }

            void OnAttach() override { if (params.selection) params.selection->OnPlaced(Backend(), Owner(), this); }
            void OnDetach() override { if (params.selection) params.selection->OnUnplaced(); }
            void OnGloballyPositioned() override {
                if (params.selection && Owner()) params.selection->UpdateLayoutCoordinates({ Owner(), this, AliveToken() });
            }

            void OnParamsChanged() {
                if (m_Previous.selection != params.selection) {
                    if (m_Previous.selection) m_Previous.selection->OnUnplaced();
                    if (params.selection && Owner() && Owner()->GetOwner()) {
                        params.selection->OnPlaced(Backend(), Owner(), this);
                        if (m_Drawn) params.selection->UpdateTextLayout(m_Drawn);
                    }
                }
                if (!params.SameLayout(m_Previous)) {
                    m_Layouts.clear();
                    m_Intrinsics.reset();
                    m_LineHeights.reset();
                    InvalidateMeasure();
                }
                if (!(params.style == m_Previous.style)) InvalidateDraw();
                m_Previous = params;
            }

            TextBackend* Backend() const {
                return Owner() && Owner()->GetOwner() ? Owner()->GetOwner()->Text() : nullptr;
            }

            // The layout for the constraints (a few are cached: intrinsics ask several).
            std::shared_ptr<TextLayoutResult> Layout(const ui::Density& d, ui::LayoutDirection dir, const ui::Constraints& c) {
                const int generation = Owner() && Owner()->GetOwner() ? Owner()->GetOwner()->FontGeneration() : 0;
                if (!(m_Density == d) || m_Direction != dir || generation != m_FontGeneration) {
                    m_FontGeneration = generation;
                    m_Layouts.clear();
                    m_Intrinsics.reset();
                    m_LineHeights.reset();
                    m_Density = d;
                    m_Direction = dir;
                }
                for (auto& [key, result] : m_Layouts)
                    if (key.minWidth == c.minWidth && key.maxWidth == c.maxWidth && key.minHeight == c.minHeight && key.maxHeight == c.maxHeight)
                        return result;
                text::TextLayoutInput in;
                in.text = params.text;
                in.style = params.style;
                in.maxLines = params.options.maxLines;
                in.softWrap = params.options.softWrap;
                in.overflow = params.options.overflow;
                in.placeholders = params.placeholders;
                in.density = d;
                in.layoutDirection = dir;
                in.constraints = c;
                auto result = std::make_shared<TextLayoutResult>(text::LayoutText(Backend(), std::move(in), &m_Intrinsics));
                if (m_Layouts.size() >= 4) m_Layouts.erase(m_Layouts.begin());
                m_Layouts.emplace_back(c, result);
                return result;
            }

            // HeightInLinesModifier: the heights of one and two lines of "H" in this style.
            std::pair<float, float> LineHeights(const ui::Density& d, ui::LayoutDirection dir) {
                if (!m_LineHeights) {
                    auto measure = [&](const char* s) {
                        text::MultiParagraph mp(Backend(), AnnotatedString(s), params.style, d, dir, {});
                        return mp.Height();
                    };
                    m_LineHeights = std::pair{ measure("H"), measure("H\nH") };
                }
                return *m_LineHeights;
            }

            // heightInLines: the min / max height for minLines / maxLines.
            ui::Constraints HeightInLines(const ui::Density& d, ui::LayoutDirection dir, ui::Constraints c) {
                const int minLines = std::max(1, params.options.minLines), maxLines = params.options.maxLines;
                if (minLines == 1 && maxLines == INT_MAX) return c;
                const auto [first, second] = LineHeights(d, dir);
                const float lineHeight = second - first;
                int minH = minLines == 1 ? 0 : int(std::lround(first + lineHeight * float(minLines - 1)));
                int maxH = maxLines == INT_MAX ? ui::Constraints::Infinity : int(std::lround(first + lineHeight * float(maxLines - 1)));
                if (c.HasBoundedHeight()) {
                    minH = std::clamp(minH, c.minHeight, c.maxHeight);
                    if (maxH != ui::Constraints::Infinity) maxH = std::clamp(maxH, c.minHeight, c.maxHeight);
                }
                ui::Constraints out = c;
                out.minHeight = std::max(c.minHeight, minH);
                if (maxH != ui::Constraints::Infinity) out.maxHeight = std::min(c.maxHeight, std::max(out.minHeight, maxH));
                return out;
            }

            MeasureResult Measure(MeasureScope& s, Measurable& m, const Constraints& in) override {
                const ui::Constraints c = HeightInLines(s, s.Direction(), in);
                auto result = Layout(s, s.Direction(), c);
                m_Drawn = result;
                if (params.links) {
                    params.links->layout = result;
                    params.links->owner = Owner() ? Owner()->GetOwner() : nullptr;
                }
                if (params.selection) params.selection->UpdateTextLayout(result);
                if (params.options.onTextLayout) params.options.onTextLayout(*result);
                const ui::IntSize size = result->Size();
                const int w = c.ConstrainWidth(size.width), h = std::clamp(std::max(size.height, c.minHeight), c.minHeight, c.maxHeight);
                // The content (link boxes) lays out over the text.
                Placeable& content = m.Measure(ui::Constraints::Fixed(w, h));
                return s.Layout(w, h, [&content](PlacementScope& p) { p.Place(content, 0, 0); },
                                { { &FirstBaseline, RoundJava(result->FirstBaseline()) }, { &LastBaseline, RoundJava(result->LastBaseline()) } });
            }

            int MinIntrinsicWidth(MeasureScope& s, Measurable&, int) override {
                Layout(s, s.Direction(), {});
                return int(std::ceil(m_Intrinsics ? m_Intrinsics->minWidth : 0));
            }
            int MaxIntrinsicWidth(MeasureScope& s, Measurable&, int) override {
                Layout(s, s.Direction(), {});
                return int(std::ceil(m_Intrinsics ? m_Intrinsics->maxWidth : 0));
            }
            int MinIntrinsicHeight(MeasureScope& s, Measurable&, int w) override {
                return Layout(s, s.Direction(), { 0, w, 0, ui::Constraints::Infinity })->Size().height;
            }
            int MaxIntrinsicHeight(MeasureScope& s, Measurable& m, int w) override { return MinIntrinsicHeight(s, m, w); }

            void Draw(ContentDrawScope& s) override {
                if (m_Drawn) {
                    const bool clip = m_Drawn->HasVisualOverflow() && params.options.overflow != TextOverflow::Visible;
                    if (params.selection) params.selection->Draw(s, clip);
                    if (clip) {
                        s.Save();
                        s.ClipRect({ 0, 0, s.GetSize().width, s.GetSize().height });
                    }
                    DrawTextLayout(s, *m_Drawn, params.style);
                    if (clip) s.Restore();
                }
                s.DrawContent();
            }

            TextParams m_Previous;
            std::vector<std::pair<ui::Constraints, std::shared_ptr<TextLayoutResult>>> m_Layouts;
            std::optional<ParagraphIntrinsicWidths> m_Intrinsics;
            std::optional<std::pair<float, float>> m_LineHeights;
            ui::Density m_Density{ -1, -1 };
            ui::LayoutDirection m_Direction = ui::LayoutDirection::Ltr;
            int m_FontGeneration = 0;
            std::shared_ptr<TextLayoutResult> m_Drawn;
        };

        Color Resolve(Color own, Color fallback) { return own.Specified() ? own : fallback; }

        // The colour a run draws in: its brush's colour if solid, else its colour, else the
        // style's; alpha multiplies a brush.
        Color RunColor(const SpanStyle& run, const SpanStyle& base) {
            const SpanStyle& s = run.brush || run.color.Specified() ? run : base;
            if (s.brush) {
                if (s.brush->kind != gfx::Brush::Kind::Solid || s.brush->colors.empty()) return Color::Unspecified();
                Color c = s.brush->colors.front();
                if (!std::isnan(s.alpha)) c = c.WithAlpha(c.Alpha() * s.alpha);
                return c;
            }
            return s.color.Specified() ? s.color : Color(0xFF000000u);
        }

        // Shadow.blurRadius to a Gaussian sigma (Skia's ConvertRadiusToSigma).
        float BlurSigma(float radius) { return radius > 0 ? 0.57735f * radius + 0.5f : 0; }
    }

    void DrawTextLayout(ui::DrawScope& s, const TextLayoutResult& layout, const TextStyle& style) {
        const SpanStyle& base = style.span;
        for (const auto& info : layout.Paragraphs().Paragraphs()) {
            const text::Paragraph& p = *info.paragraph;
            const ParagraphData& d = p.Data();
            const auto runs = p.RunStyles();
            const Offset origin{ 0, info.top };
            auto runAt = [&](int i) -> const SpanStyle& { static const SpanStyle none; return i >= 0 && std::size_t(i) < runs.size() ? runs[std::size_t(i)] : none; };

            // Backgrounds: the run's extent, line top to bottom.
            for (const RunSegment& seg : d.segments) {
                const Color bg = Resolve(runAt(seg.run).background, base.background);
                if (!bg.Specified() || bg.Alpha() <= 0 || std::size_t(seg.line) >= d.lines.size()) continue;
                const LineData& l = d.lines[std::size_t(seg.line)];
                s.DrawRect(bg, { origin.x + seg.left, origin.y + l.top }, { seg.right - seg.left, l.bottom - l.top });
            }

            std::vector<Color> colors(runs.size());
            for (std::size_t i = 0; i < runs.size(); ++i) colors[i] = RunColor(runs[i], base);

            // Shadows (blurred through a layer).
            for (std::size_t i = 0; i < runs.size(); ++i) {
                const auto& sh = runs[i].shadow ? runs[i].shadow : base.shadow;
                if (!sh || sh->color.Alpha() <= 0) continue;
                std::vector<Color> only(runs.size(), Color::Transparent());
                for (std::size_t k = i; k < runs.size(); ++k) {
                    const auto& other = runs[k].shadow ? runs[k].shadow : base.shadow;
                    if (other && *other == *sh) only[k] = sh->color;
                }
                if (std::count_if(only.begin(), only.begin() + std::ptrdiff_t(i), [](Color c) { return c.Alpha() > 0; })) continue;
                const float sigma = BlurSigma(sh->blurRadius);
                if (sigma > 0) {
                    const float pad = sigma * 3;
                    s.BeginLayer({ -pad, origin.y - pad, p.Width() + pad, origin.y + d.height + pad }, 1);
                    s.DrawParagraph(d, origin, Color::Transparent(), only);
                    s.EndLayerEffects({ { sigma, sigma, true } }, sh->offset);
                } else {
                    s.DrawParagraph(d, { origin.x + sh->offset.x, origin.y + sh->offset.y }, Color::Transparent(), only);
                }
            }

            // Glyphs in solid colours.
            s.DrawParagraph(d, origin, Color::Transparent(), colors);

            // Gradient brushes: the runs' glyphs as a mask for the brush over the paragraph.
            for (std::size_t i = 0; i < runs.size(); ++i) {
                const SpanStyle& src = runs[i].brush || runs[i].color.Specified() ? runs[i] : base;
                if (!src.brush || src.brush->kind == gfx::Brush::Kind::Solid) continue;
                std::vector<Color> mask(runs.size(), Color::Transparent());
                bool first = true;
                for (std::size_t k = 0; k < runs.size(); ++k) {
                    const SpanStyle& o = runs[k].brush || runs[k].color.Specified() ? runs[k] : base;
                    if (o.brush && *o.brush == *src.brush) { mask[k] = Color(0xFFFFFFFFu); if (k < i) first = false; }
                }
                if (!first) continue;
                const ui::Rect bounds{ 0, origin.y, p.Width(), origin.y + d.height };
                s.BeginLayer(bounds, 1);
                s.DrawParagraph(d, origin, Color::Transparent(), mask);
                s.Save();
                s.SetBlendMode(gfx::BlendMode::SrcIn);
                s.Translate(origin);
                s.DrawRect(*src.brush, {}, { p.Width(), d.height }, std::isnan(src.alpha) ? 1.0f : src.alpha);
                s.Restore();
                s.EndLayer();
            }

            // Decorations, in the run's colour.
            for (const RunSegment& seg : d.segments) {
                const SpanStyle& run = runAt(seg.run);
                const auto deco = run.textDecoration ? run.textDecoration : base.textDecoration;
                if (!deco || *deco == text::TextDecoration::None || std::size_t(seg.line) >= d.lines.size()) continue;
                const RunMetrics m = std::size_t(seg.run) < d.runMetrics.size() ? d.runMetrics[std::size_t(seg.run)] : RunMetrics{};
                const Color c = std::size_t(seg.run) < colors.size() && colors[std::size_t(seg.run)].Specified() ? colors[std::size_t(seg.run)] : Color(0xFF000000u);
                const float baseline = origin.y + d.lines[std::size_t(seg.line)].baseline + seg.baselineShift;
                if (text::Contains(*deco, text::TextDecoration::Underline))
                    s.DrawRect(c, { seg.left, baseline + m.underlinePosition }, { seg.right - seg.left, std::max(1.0f, m.underlineThickness) });
                if (text::Contains(*deco, text::TextDecoration::LineThrough))
                    s.DrawRect(c, { seg.left, baseline + m.strikePosition }, { seg.right - seg.left, std::max(1.0f, m.strikeThickness) });
            }
        }
    }

    namespace {
        using text::LinkAnnotation;
        using text::Range;

        // The visible part of a link (calculateVisibleLinkRange): none past the last line.
        std::optional<std::pair<int, int>> VisibleLinkRange(const Range<LinkAnnotation>& link, const TextLayoutResult& r) {
            if (r.LineCount() == 0) return std::nullopt;
            const int last = r.LineEnd(r.LineCount() - 1);
            if (link.start >= last) return std::nullopt;
            return std::pair{ link.start, std::min(link.end, last) };
        }

        // The link's rectangles and their rounded bounds.
        struct LinkGeometry { std::vector<RectF> rects; ui::IntOffset topLeft; ui::IntSize size; };
        std::optional<LinkGeometry> Geometry(const Range<LinkAnnotation>& link, const LinkShared& shared) {
            if (!shared.layout) return std::nullopt;
            const auto range = VisibleLinkRange(link, *shared.layout);
            if (!range) return std::nullopt;
            LinkGeometry g;
            g.rects = shared.layout->SelectionRects(range->first, range->second);
            if (g.rects.empty()) return std::nullopt;
            float l = INFINITY, t = INFINITY, r = -INFINITY, b = -INFINITY;
            for (const RectF& x : g.rects) { l = std::min(l, x.x); t = std::min(t, x.y); r = std::max(r, x.x + x.w); b = std::max(b, x.y + x.h); }
            g.topLeft = { RoundJava(l), RoundJava(t) };
            g.size = { std::max(0, RoundJava(r) - g.topLeft.x), std::max(0, RoundJava(b) - g.topLeft.y) };
            return g;
        }

        // clipLink: the link's path, in the link box's coordinates.
        struct LinkShape final : gfx::Shape {
            Range<LinkAnnotation> link;
            std::shared_ptr<LinkShared> shared;
            LinkShape(Range<LinkAnnotation> l, std::shared_ptr<LinkShared> s) : link(std::move(l)), shared(std::move(s)) {}
            gfx::Outline CreateOutline(ui::Size size, ui::LayoutDirection, const ui::Density&) const override {
                const auto g = Geometry(link, *shared);
                if (!g) return { ui::Rect{ 0, 0, size.width, size.height } };
                gfx::Path p;
                const float dx = float(g->topLeft.x), dy = float(g->topLeft.y);
                for (const RectF& r : g->rects) p.AddRect({ r.x - dx, r.y - dy, r.x + r.w - dx, r.y + r.h - dy });
                return { std::move(p) };
            }
        };

        void HandleLink(const LinkAnnotation& link, ui::Owner* owner) {
            if (link.listener && *link.listener) { (*link.listener)(link); return; }
            if (link.kind == LinkAnnotation::Kind::Url && owner) owner->OpenUri(link.url);
        }

        std::optional<SpanStyle> MergeOrUse(std::optional<SpanStyle> a, const std::optional<SpanStyle>& b) {
            if (!b) return a;
            return a ? std::optional(a->Merge(*b)) : b;
        }
    }

    void AppendInlineContent(text::AnnotatedStringBuilder& b, const std::string& id, const std::string& alternateText) {
        b.WithAnnotation(kInlineContentTag, id, [&] { b.Append(alternateText); });
    }

    void BasicText(const AnnotatedString& text, const Modifier& modifier, TextStyle style, BasicTextOptions options, rt::Key key) {
        // Inside a SelectionContainer: a selectable with an id that survives re-creation.
        auto* comp = rt::Composition::Current();
        const auto registrar = comp->Read(selection::LocalSelectionRegistrar);
        const std::int64_t selectableId = rt::RememberSaveable<std::int64_t>(
            [&] { return registrar ? registrar->NextSelectableId() : selection::kInvalidSelectableId; }, rt::AutoSaver<std::int64_t>(),
            rt::Keys(registrar.get()));
        std::shared_ptr<SelectionController> selectionController;
        Modifier selectionModifier = modifier;
        if (registrar) {
            const Color background = comp->Read(LocalTextSelectionColors).backgroundColor;
            selectionController = comp->Remember<std::shared_ptr<SelectionController>>([&] {
                auto c = std::make_shared<SelectionController>();
                c->selectableId = selectableId;
                c->registrar = registrar;
                c->backgroundColor = background;
                return c;
            }, rt::Keys(registrar.get(), selectableId, background.argb));
            selectionController->pinnable = comp->Read(ui::LocalPinnableContainer);
            selectionModifier = PointerHoverIcon(selection::SelectionGestures(modifier, registrar, selectableId, [c = selectionController] { return c->coordinates; }),
                                                 PointerIcon::Text);
        }
        std::vector<Range<LinkAnnotation>> links;
        for (auto& l : text.GetLinkAnnotations(0, text.Length()))
            if (l.start != l.end) links.push_back(std::move(l));
        // resolveInlineContent: the inline content ranges with content for their id.
        struct Inline { int start, end; std::function<void(const std::string&)> children; std::string alternate; };
        std::vector<text::Range<text::Placeholder>> placeholders;
        std::vector<Inline> inlines;
        if (!options.inlineContent.empty())
            for (const auto& a : text.GetStringAnnotations(kInlineContentTag, 0, text.Length())) {
                auto it = options.inlineContent.find(a.item);
                if (it == options.inlineContent.end() || a.start >= a.end) continue;
                placeholders.push_back({ it->second.placeholder, a.start, a.end, {} });
                inlines.push_back({ a.start, a.end, it->second.children, text.SubSequence(a.start, a.end).Text() });
            }
        std::sort(placeholders.begin(), placeholders.end(), [](const auto& x, const auto& y) { return x.start < y.start; });
        std::sort(inlines.begin(), inlines.end(), [](const auto& x, const auto& y) { return x.start < y.start; });
        // Text with its link and TTS spans for accessibility.
        std::vector<ui::TextSpanSemantics> spans;
        for (const auto& t : text.GetTtsAnnotations(0, text.Length()))
            if (t.start < t.end) spans.push_back({ ui::TextSpanSemantics::Kind::Tts, 0, t.start, t.end, t.item.verbatim, {} });
        Modifier m = Semantics(selectionModifier, [plain = text.Text(), spans, links](SemanticsConfiguration& c) {
            c.text.push_back(plain);
            c.textSpans = spans;
            for (const auto& l : links) {
                if (l.item.kind == LinkAnnotation::Kind::Url && !(l.item.listener && *l.item.listener))
                    c.textSpans.push_back({ ui::TextSpanSemantics::Kind::Url, 0, l.start, l.end, l.item.url, {} });
                else
                    c.textSpans.push_back({ ui::TextSpanSemantics::Kind::Clickable, 0, l.start, l.end, l.item.url,
                                            [link = l.item] { if (link.listener && *link.listener) (*link.listener)(link); } });
            }
        });
        if (links.empty() && inlines.empty()) {
            Box(m.With<TextNode>(TextParams{ text, std::move(style), std::move(options), nullptr, {}, selectionController }), Alignment::TopStart(), false, {}, key);
            return;
        }

        // A child scope: hover / press / focus changes recompose only it.
        rt::Composition::Current()->Child([text, m, selectionController, style = std::move(style), options = std::move(options), links = std::move(links),
                                           placeholders = std::move(placeholders), inlines = std::move(inlines)]() mutable {
            auto* c = rt::Composition::Current();
            // TextLinkScope: each link's style, merged with its focused / hovered / pressed
            // styles while those hold, goes in right after the link (so later spans still win).
            std::vector<std::shared_ptr<MutableInteractionSource>> sources;
            for (std::size_t i = 0; i < links.size(); ++i) sources.push_back(RememberInteractionSource(rt::Key(int(i))));
            auto shared = c->Remember<std::shared_ptr<LinkShared>>([] { return std::make_shared<LinkShared>(); });
            std::vector<Range<text::Annotation>> annotations;
            std::size_t index = 0;
            for (const auto& a : text.Annotations()) {
                annotations.push_back(a);
                const auto* link = std::get_if<LinkAnnotation>(&a.item);
                if (!link || a.start == a.end) continue;
                const auto& source = sources[index++];
                const auto& st = link->styles;
                if (!st.style && !st.focusedStyle && !st.hoveredStyle && !st.pressedStyle) continue;
                std::optional<SpanStyle> merged = st.style;
                if (source->IsFocused()) merged = MergeOrUse(merged, st.focusedStyle);
                if (source->IsHovered()) merged = MergeOrUse(merged, st.hoveredStyle);
                if (source->IsPressed()) merged = MergeOrUse(merged, st.pressedStyle);
                annotations.push_back({ merged.value_or(SpanStyle{}), a.start, a.end, {} });
            }
            AnnotatedString styled(text.Text(), std::move(annotations));

            // Children: the link boxes, then the inline content, placed over the text.
            const std::size_t nLinks = links.size();
            auto policy = std::make_shared<ui::LambdaMeasurePolicy>([shared, links, nLinks](MeasureScope& s, std::span<Measurable* const> children, const Constraints& con) {
                std::vector<std::pair<Placeable*, std::optional<ui::IntOffset>>> placed;
                for (std::size_t i = 0; i < children.size(); ++i) {
                    if (i < nLinks) {
                        const auto g = Geometry(links[i], *shared);
                        Placeable& p = children[i]->Measure(ui::Constraints::Fixed(g ? g->size.width : 0, g ? g->size.height : 0));
                        placed.emplace_back(&p, g ? std::optional(g->topLeft) : std::nullopt);
                        continue;
                    }
                    // TextMeasurePolicy: up to the placeholder's size, at its top-left.
                    std::optional<RectF> rect;
                    const std::size_t ph = i - nLinks;
                    if (shared->layout && ph < shared->layout->PlaceholderRects().size()) rect = shared->layout->PlaceholderRects()[ph];
                    Placeable& p = children[i]->Measure(rect ? ui::Constraints{ 0, int(std::floor(rect->w)), 0, int(std::floor(rect->h)) }
                                                             : ui::Constraints::Fixed(0, 0));
                    placed.emplace_back(&p, rect ? std::optional(ui::IntOffset{ RoundJava(rect->x), RoundJava(rect->y) }) : std::nullopt);
                }
                return s.Layout(con.minWidth, con.minHeight, [placed](PlacementScope& ps) {
                    for (auto& [p, at] : placed) if (at) ps.Place(*p, at->x, at->y);
                });
            });
            Layout(m.With<TextNode>(TextParams{ std::move(styled), style, options, shared, placeholders, selectionController }), policy, [&] {
                for (std::size_t i = 0; i < links.size(); ++i) {
                    const auto link = links[i];
                    ClickableOptions click;
                    click.interactionSource = sources[i];
                    click.useLocalIndication = false;
                    const std::string linkText = text.SubSequence(link.start, link.end).Text();
                    Modifier box = Semantics(Clip(Modifier{}, std::make_shared<LinkShape>(link, shared)),
                                             [linkText](SemanticsConfiguration& sc) { sc.text.push_back(linkText); });
                    box = PointerHoverIcon(Hoverable(std::move(box), sources[i]), PointerIcon::Hand);
                    box = Clickable(std::move(box), [link, shared] { HandleLink(link.item, shared->owner); }, click);
                    Box(box, Alignment::TopStart(), false, {}, rt::Key(int(i)));
                }
                for (std::size_t i = 0; i < inlines.size(); ++i) {
                    const auto& in = inlines[i];
                    Box(Modifier{}, Alignment::TopStart(), false, [&] { if (in.children) in.children(in.alternate); }, rt::Key(int(1000000 + i)));
                }
            });
        }, key);
    }

    void BasicText(const std::string& text, const Modifier& modifier, TextStyle style, BasicTextOptions options, rt::Key key) {
        BasicText(AnnotatedString(text), modifier, std::move(style), std::move(options), key);
    }

}
