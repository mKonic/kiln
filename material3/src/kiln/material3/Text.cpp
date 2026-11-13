#include "kiln/material3/Text.h"
#include <kiln/Function.h>

#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/foundation/VectorPainter.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/SmallIconButtonTokens.h"

namespace kiln::material3 {

    namespace {
        rt::Composition& Comp() { return *rt::Composition::Current(); }

        // style.merge(color, fontSize, fontWeight, textAlign, lineHeight, fontFamily,
        // textDecoration, fontStyle, letterSpacing): set values win.
        text::TextStyle Merged(const TextOptions& o, ui::Color color) {
            const text::TextStyle style = o.style ? *o.style : Comp().Read(LocalTextStyle());
            text::TextStyle over;
            over.span.color = color;
            over.span.fontSize = o.fontSize;
            over.span.fontWeight = o.fontWeight;
            over.span.fontStyle = o.fontStyle;
            over.span.fontFamily = o.fontFamily;
            over.span.letterSpacing = o.letterSpacing;
            over.span.textDecoration = o.textDecoration;
            over.paragraph.textAlign = o.textAlign.value_or(text::TextAlign::Unspecified);
            over.paragraph.lineHeight = o.lineHeight;
            return style.Merge(over);
        }

        // textColor = color.takeOrElse { style.color.takeOrElse { LocalContentColor.current } };
        // with a colour producer the style keeps its own colour.
        ui::Color TextColor(const TextOptions& o) {
            if (o.colorProducer) return ui::Color::Unspecified();
            if (o.color.Specified()) return o.color;
            const text::TextStyle style = o.style ? *o.style : Comp().Read(LocalTextStyle());
            return style.span.color.Specified() ? style.span.color : Comp().Read(LocalContentColor);
        }

        foundation::BasicTextOptions Basic(const TextOptions& o) {
            foundation::BasicTextOptions b;
            b.onTextLayout = o.onTextLayout;
            b.overflow = o.overflow;
            b.softWrap = o.softWrap;
            b.maxLines = o.maxLines;
            b.minLines = o.minLines;
            b.inlineContent = o.inlineContent;
            b.color = o.colorProducer;
            b.autoSize = o.autoSize;
            return b;
        }

        // createTextWithLinkStyles: links without styles take rememberTextLinkStyles (primary,
        // underlined). An empty TextLinkStyles stands for Compose's null.
        text::AnnotatedString WithLinkStyles(const text::AnnotatedString& t) {
            const ui::Color primary = MaterialTheme::colorScheme().primary;
            bool any = false;
            for (const auto& r : t.Annotations())
                if (const auto* l = std::get_if<text::LinkAnnotation>(&r.item); l && l->styles == text::TextLinkStyles{}) any = true;
            if (!any) return t;
            text::SpanStyle style;
            style.color = primary;
            style.textDecoration = text::TextDecoration::Underline;
            auto annotations = t.Annotations();
            for (auto& r : annotations)
                if (auto* l = std::get_if<text::LinkAnnotation>(&r.item); l && l->styles == text::TextLinkStyles{}) l->styles.style = style;
            return text::AnnotatedString(t.Text(), std::move(annotations));
        }
    }

    void Text(const std::string& text, const ui::Modifier& modifier, const TextOptions& options) {
        foundation::BasicText(text, modifier, Merged(options, TextColor(options)), Basic(options));
    }

    void Text(const text::AnnotatedString& text, const ui::Modifier& modifier, const TextOptions& options) {
        foundation::BasicText(WithLinkStyles(text), modifier, Merged(options, TextColor(options)), Basic(options));
    }

    void Icon(const gfx::vector::ImageVectorPtr& imageVector, const std::optional<std::string>& contentDescription, const ui::Modifier& modifier,
              std::optional<ui::Color> tint) {
        Icon(foundation::RememberVectorPainter(imageVector), contentDescription, modifier, tint);
    }

    void Icon(const gfx::ImageBitmap& bitmap, const std::optional<std::string>& contentDescription, const ui::Modifier& modifier,
              std::optional<ui::Color> tint) {
        auto& painter = Comp().Remember<std::shared_ptr<foundation::Painter>>(
            [&] { return std::make_shared<foundation::BitmapPainter>(bitmap); }, rt::Keys(std::hash<const void*>{}(bitmap.Pixels())));
        Icon(painter, contentDescription, modifier, tint);
    }

    void Icon(std::shared_ptr<foundation::Painter> painter, const std::optional<std::string>& contentDescription, const ui::Modifier& modifier,
              std::optional<ui::Color> tint) {
        const ui::Color t = tint ? *tint : Comp().Read(LocalContentColor);
        std::optional<gfx::ColorFilter> filter;
        if (t != ui::Color::Unspecified()) filter = gfx::ColorFilter::Tint(t);
        ui::Modifier m = modifier;
        // defaultSizeFor: painters without an intrinsic size take 24 dp.
        const auto intrinsic = painter ? painter->IntrinsicSize() : std::nullopt;
        if (!intrinsic || (std::isinf(intrinsic->width) && std::isinf(intrinsic->height)))
            m = foundation::SizeMod(std::move(m), tokens::SmallIconButtonTokens::IconSize, tokens::SmallIconButtonTokens::IconSize);
        m = foundation::Paint(std::move(m), std::move(painter), true, foundation::Alignment::Center(), foundation::ContentScale::Fit(), 1, filter);
        if (contentDescription) {
            m = foundation::Semantics(std::move(m), [d = *contentDescription](ui::SemanticsConfiguration& c) {
                c.contentDescription = { d };
                c.role = ui::Role::Image;
            });
        }
        foundation::Box(m);
    }

    void Icon(std::shared_ptr<foundation::Painter> painter, kiln::Function<ui::Color()> tint, const std::optional<std::string>& contentDescription,
              const ui::Modifier& modifier) {
        ui::Modifier m = foundation::DrawWithContent(modifier, [tint](ui::ContentDrawScope& s) {
            const gfx::ColorFilter filter = gfx::ColorFilter::Tint(tint ? tint() : ui::Color::Unspecified());
            s.BeginLayer({ 0, 0, s.GetSize().width, s.GetSize().height }, 1, nullptr, gfx::BlendMode::SrcOver, tint ? &filter : nullptr);
            s.DrawContent();
            s.EndLayer();
        });
        Icon(std::move(painter), contentDescription, m, ui::Color::Unspecified());
    }

    namespace internal {
        void ProvideContentColorTextStyle(ui::Color contentColor, const text::TextStyle& textStyle, kiln::Function<void()> content) {
            auto& c = Comp();
            text::TextStyle merged = c.Read(LocalTextStyle()).Merge(textStyle);
            c.Provide(LocalContentColor, contentColor, [&c, merged = std::move(merged), content = std::move(content)] {
                c.Provide(LocalTextStyle(), merged, content);
            });
        }
    }

}
