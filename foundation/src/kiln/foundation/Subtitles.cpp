#include "kiln/foundation/Subtitles.h"

#include "kiln/foundation/Draw.h"
#include "kiln/foundation/text/TextMeasurer.h"
#include "kiln/media/Webvtt.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Flow.h"

#include <cmath>

namespace kiln::foundation {

    namespace {
        using media::CaptionStyle;
        using media::Cue;

        constexpr float kInnerPaddingRatio = 0.125f;

        bool HasAlpha(std::uint32_t argb) { return (argb >> 24) != 0; }

        // SubtitleViewUtils.resolveTextSize.
        float ResolveTextSize(int textSizeType, float textSize, int rawViewHeight, int viewHeightMinusPadding) {
            if (textSize == Cue::kDimenUnset) return Cue::kDimenUnset;
            switch (textSizeType) {
                case Cue::kTextSizeTypeAbsolute: return textSize;
                case Cue::kTextSizeTypeFractional: return textSize * float(viewHeightMinusPadding);
                case Cue::kTextSizeTypeFractionalIgnorePadding: return textSize * float(rawViewHeight);
                default: return Cue::kDimenUnset;
            }
        }

        // removeAllEmbeddedStyling: the window colour and every span but the language features
        // (ruby, voice, text-combine-upright); removeEmbeddedFontSizes: sizes only.
        Cue StripStyling(Cue cue, bool all) {
            cue.textSize = Cue::kDimenUnset;
            cue.textSizeType = Cue::kTypeUnset;
            if (all) cue.windowColorSet = false;
            std::vector<text::Range<text::Annotation>> kept;
            for (const auto& a : cue.text.Annotations()) {
                if (const auto* span = std::get_if<text::SpanStyle>(&a.item)) {
                    if (all) continue;
                    text::SpanStyle s = *span;
                    s.fontSize = {};
                    if (s == text::SpanStyle{}) continue;
                    kept.push_back({ s, a.start, a.end, a.tag });
                } else if (std::holds_alternative<text::StringAnnotation>(a.item) || !all) {
                    kept.push_back(a);
                }
            }
            cue.text = text::AnnotatedString(cue.text.Text(), std::move(kept));
            return cue;
        }

        // CanvasSubtitleOutput.repositionVerticalCue: drawn horizontally, its line mirrored.
        Cue RepositionVerticalCue(Cue cue) {
            cue.position = Cue::kDimenUnset;
            cue.positionAnchor = Cue::kTypeUnset;
            cue.textAlignment.reset();
            if (cue.lineType == Cue::kLineTypeFraction) cue.line = 1.0f - cue.line;
            else {
                cue.line = -cue.line - 1.0f;
                cue.lineType = Cue::kLineTypeNumber;
            }
            if (cue.lineAnchor == Cue::kAnchorTypeEnd) cue.lineAnchor = Cue::kAnchorTypeStart;
            else if (cue.lineAnchor == Cue::kAnchorTypeStart) cue.lineAnchor = Cue::kAnchorTypeEnd;
            return cue;
        }

        // Without foreground colours: the edge drawn under the text keeps the edge colour.
        text::AnnotatedString WithoutForeground(const text::AnnotatedString& t) {
            std::vector<text::Range<text::Annotation>> kept;
            for (const auto& a : t.Annotations()) {
                if (const auto* span = std::get_if<text::SpanStyle>(&a.item); span && span->color != ui::Color::Unspecified()) {
                    text::SpanStyle s = *span;
                    s.color = ui::Color::Unspecified();
                    if (!(s == text::SpanStyle{})) kept.push_back({ s, a.start, a.end, a.tag });
                    continue;
                }
                kept.push_back(a);
            }
            return text::AnnotatedString(t.Text(), std::move(kept));
        }

        // SubtitlePainter.draw for a text cue in the box [left, right) x [top, bottom).
        void PaintCue(ui::DrawScope& d, TextMeasurer& measurer, const Cue& cue, const CaptionStyle& style, float defaultTextSizePx,
                      float cueTextSizePx, float bottomPaddingFraction, int parentLeft, int parentTop, int parentRight, int parentBottom) {
            if (cue.text.Empty()) return;
            const std::uint32_t windowColor = cue.windowColorSet ? cue.windowColor : style.windowColor;
            const int parentWidth = parentRight - parentLeft, parentHeight = parentBottom - parentTop;
            const int textPaddingX = int(defaultTextSizePx * kInnerPaddingRatio + 0.5f);
            int availableWidth = parentWidth - textPaddingX * 2;
            if (cue.size != Cue::kDimenUnset) availableWidth = int(float(availableWidth) * cue.size);
            if (availableWidth <= 0) return;   // insufficient space

            // The text paint's size and colour, the cue's own size over it (a priority span), and
            // the style's background under the cue's own backgrounds.
            const float sizePx = cueTextSizePx > 0 ? float(int(cueTextSizePx)) : defaultTextSizePx;
            text::TextStyle textStyle;
            textStyle.span.fontSize = text::Sp(sizePx / (d.density * d.fontScale));
            textStyle.span.color = ui::Color(style.foregroundColor);
            // The caption typeface: a generic family, else a system one by name.
            const std::string& face = style.typeface;
            if (face == "serif") textStyle.span.fontFamily = text::FontFamily::Serif();
            else if (face == "sans-serif") textStyle.span.fontFamily = text::FontFamily::SansSerif();
            else if (face == "monospace") textStyle.span.fontFamily = text::FontFamily::Monospace();
            else if (face == "cursive") textStyle.span.fontFamily = text::FontFamily::Cursive();
            else if (!face.empty() && face != "default") textStyle.span.fontFamily = text::FontFamily::Of({ text::Font::Device(face) });
            text::TextStyle edgeStyle = textStyle;
            const bool onePass = style.edgeType == CaptionStyle::kEdgeTypeNone || style.edgeType == CaptionStyle::kEdgeTypeDropShadow;
            if (HasAlpha(style.backgroundColor)) (onePass ? textStyle : edgeStyle).span.background = ui::Color(style.backgroundColor);
            const text::AnnotatedString edgeText = style.edgeType == CaptionStyle::kEdgeTypeOutline ? WithoutForeground(cue.text) : cue.text;
            const auto alignment = cue.textAlignment.value_or(Cue::Alignment::Center);
            const text::TextAlign align = alignment == Cue::Alignment::Normal ? text::TextAlign::Start
                                          : alignment == Cue::Alignment::Opposite ? text::TextAlign::End : text::TextAlign::Center;
            textStyle.paragraph.textAlign = align;
            edgeStyle.paragraph.textAlign = align;

            auto measure = [&](const text::AnnotatedString& t, const text::TextStyle& s, int width) {
                return measurer.Measure(t, s, text::TextOverflow::Clip, true, INT_MAX, {}, ui::Constraints::FixedWidth(width));
            };
            TextLayoutResult layout = measure(cue.text, textStyle, availableWidth);
            const int textHeight = layout.Size().height;
            int textWidth = 0;
            for (int i = 0; i < layout.LineCount(); ++i)
                textWidth = std::max(int(std::ceil(layout.LineRight(i) - layout.LineLeft(i))), textWidth);
            if (cue.size != Cue::kDimenUnset && textWidth < availableWidth) textWidth = availableWidth;
            textWidth += textPaddingX * 2;

            int textLeft, textRight;
            if (cue.position != Cue::kDimenUnset) {
                const int anchor = int(std::lround(double(parentWidth) * cue.position)) + parentLeft;
                switch (cue.positionAnchor) {
                    case Cue::kAnchorTypeEnd: textLeft = anchor - textWidth; break;
                    case Cue::kAnchorTypeMiddle: textLeft = (anchor * 2 - textWidth) / 2; break;
                    default: textLeft = anchor; break;
                }
                textLeft = std::max(textLeft, parentLeft);
                textRight = std::min(textLeft + textWidth, parentRight);
            } else {
                textLeft = (parentWidth - textWidth) / 2 + parentLeft;
                textRight = textLeft + textWidth;
            }
            textWidth = textRight - textLeft;
            if (textWidth <= 0) return;   // invalid horizontal positioning

            int textTop;
            if (cue.line != Cue::kDimenUnset) {
                if (cue.lineType == Cue::kLineTypeFraction) {
                    const int anchor = int(std::lround(double(parentHeight) * cue.line)) + parentTop;
                    textTop = cue.lineAnchor == Cue::kAnchorTypeEnd ? anchor - textHeight
                              : cue.lineAnchor == Cue::kAnchorTypeMiddle ? (anchor * 2 - textHeight) / 2 : anchor;
                } else {
                    const float firstLineHeight = layout.LineBottom(0) - layout.LineTop(0);
                    textTop = cue.line >= 0 ? int(std::lround(cue.line * firstLineHeight)) + parentTop
                                            : int(std::lround((cue.line + 1) * firstLineHeight)) + parentBottom - textHeight;
                }
                if (textTop + textHeight > parentBottom) textTop = parentBottom - textHeight;
                else if (textTop < parentTop) textTop = parentTop;
            } else {
                textTop = parentBottom - textHeight - int(float(parentHeight) * bottomPaddingFraction);
            }

            layout = measure(cue.text, textStyle, textWidth);
            const TextLayoutResult edgeLayout = measure(edgeText, edgeStyle, textWidth);

            // drawTextLayout.
            const float outlineWidth = std::round(2 * d.density), shadowRadius = outlineWidth, shadowOffset = outlineWidth;
            const ui::Offset at{ float(textLeft), float(textTop) };
            if (HasAlpha(windowColor))
                d.DrawRect(ui::Color(windowColor), { at.x - float(textPaddingX), at.y },
                           { float(layout.Size().width + textPaddingX * 2), float(layout.Size().height) });
            std::optional<text::Shadow> textShadow;
            switch (style.edgeType) {
                case CaptionStyle::kEdgeTypeOutline:
                    DrawText(d, edgeLayout, ui::Color(style.edgeColor), at, std::numeric_limits<float>::quiet_NaN(), std::nullopt, std::nullopt,
                             text::DrawStyle{ .stroke = true, .width = outlineWidth });
                    break;
                case CaptionStyle::kEdgeTypeDropShadow:
                    textShadow = text::Shadow{ ui::Color(style.edgeColor), { shadowOffset, shadowOffset }, shadowRadius };
                    break;
                case CaptionStyle::kEdgeTypeRaised:
                case CaptionStyle::kEdgeTypeDepressed: {
                    const bool raised = style.edgeType == CaptionStyle::kEdgeTypeRaised;
                    const std::uint32_t up = raised ? 0xFFFFFFFFu : style.edgeColor, down = raised ? style.edgeColor : 0xFFFFFFFFu;
                    const float offset = shadowRadius / 2;
                    DrawText(d, edgeLayout, ui::Color(style.foregroundColor), at, std::numeric_limits<float>::quiet_NaN(),
                             text::Shadow{ ui::Color(up), { -offset, -offset }, shadowRadius });
                    textShadow = text::Shadow{ ui::Color(down), { offset, offset }, shadowRadius };
                    break;
                }
                default: break;
            }
            DrawText(d, layout, ui::Color::Unspecified(), at, std::numeric_limits<float>::quiet_NaN(), textShadow);
        }
    }

    void Subtitles(const Modifier& modifier, const media::CueGroup& group, SubtitleViewOptions options) {
        TextMeasurer& measurer = RememberTextMeasurer();
        // SubtitleView.getCuesWithStylingPreferencesApplied.
        std::vector<Cue> cues;
        for (const Cue& cue : group.cues) {
            if (!options.applyEmbeddedStyles) cues.push_back(StripStyling(cue, true));
            else if (!options.applyEmbeddedFontSizes) cues.push_back(StripStyling(cue, false));
            else cues.push_back(cue);
        }
        const CaptionStyle style = options.style ? *options.style : media::UserCaptionStyle();
        const float textSize = options.fractionalTextSize ? *options.fractionalTextSize
                                                          : SubtitleViewOptions::kDefaultTextSizeFraction * media::CaptioningFontScale();
        const float bottomPadding = options.bottomPaddingFraction;
        TextMeasurer* m = &measurer;
        Box(DrawBehind(modifier, [m, cues = std::move(cues), style, textSize, bottomPadding](ui::DrawScope& d) {
            // CanvasSubtitleOutput.dispatchDraw (no padding of its own).
            const int width = int(d.GetSize().width), height = int(d.GetSize().height);
            if (cues.empty() || width <= 0 || height <= 0) return;
            const float defaultTextSizePx = ResolveTextSize(Cue::kTextSizeTypeFractional, textSize, height, height);
            if (defaultTextSizePx <= 0) return;
            for (Cue cue : cues) {
                if (cue.verticalType != Cue::kTypeUnset) cue = RepositionVerticalCue(std::move(cue));
                const float cueTextSizePx = ResolveTextSize(cue.textSizeType, cue.textSize, height, height);
                PaintCue(d, *m, cue, style, defaultTextSizePx, cueTextSizePx, bottomPadding, 0, 0, width, height);
            }
        }));
    }

    void Subtitles(const Modifier& modifier, media::Player& player, SubtitleViewOptions options) {
        const media::PlayerState& s = rt::CollectAsState(player.StateFlow()).Get();
        Subtitles(modifier, s.currentCues, std::move(options));
    }

}
