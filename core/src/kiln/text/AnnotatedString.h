#pragma once

// AnnotatedString (ui-text AnnotatedString.kt, LinkAnnotation.kt, TextLinkStyles.kt): text with
// ranges of SpanStyle, ParagraphStyle and annotations (string, link, TTS). Text is UTF-8;
// offsets are UTF-16 code units, as in Compose (and in what IMEs and the platform layout use).

#include "kiln/graphics/Shape.h"
#include <kiln/Function.h>
#include "kiln/text/TextStyle.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace kiln::ui { class DrawScope; }

namespace kiln::text {

    // UTF-16 lengths and offset conversions over UTF-8 text.
    int Utf16Length(std::string_view utf8);
    std::size_t Utf16ToUtf8Offset(std::string_view utf8, int offset16);
    int Utf8ToUtf16Offset(std::string_view utf8, std::size_t offset8);

    // TextLinkStyles: a link's style, and the styles merged on top while focused, hovered or
    // pressed.
    struct TextLinkStyles {
        std::optional<SpanStyle> style, focusedStyle, hoveredStyle, pressedStyle;
        bool operator==(const TextLinkStyles&) const = default;
    };

    struct LinkAnnotation;
    using LinkInteractionListener = kiln::Function<void(const LinkAnnotation&)>;

    // LinkAnnotation.Url (opened with the UriHandler unless a listener handles it) or
    // LinkAnnotation.Clickable (a tag for the listener).
    struct LinkAnnotation {
        enum class Kind : std::uint8_t { Url, Clickable };
        Kind kind = Kind::Url;
        std::string url;   // Url
        std::string tag;   // Clickable
        TextLinkStyles styles;
        std::shared_ptr<LinkInteractionListener> listener;
        bool operator==(const LinkAnnotation& o) const { return kind == o.kind && url == o.url && tag == o.tag && styles == o.styles && listener == o.listener; }
        static LinkAnnotation Url(std::string url, TextLinkStyles styles = {}, LinkInteractionListener listener = {}) {
            return { Kind::Url, std::move(url), {}, std::move(styles), listener ? std::make_shared<LinkInteractionListener>(std::move(listener)) : nullptr };
        }
        static LinkAnnotation Clickable(std::string tag, TextLinkStyles styles = {}, LinkInteractionListener listener = {}) {
            return { Kind::Clickable, {}, std::move(tag), std::move(styles), listener ? std::make_shared<LinkInteractionListener>(std::move(listener)) : nullptr };
        }
    };

    struct StringAnnotation { std::string value; bool operator==(const StringAnnotation&) const = default; };
    // TtsAnnotation (VerbatimTtsAnnotation): read out as this text by text-to-speech.
    struct TtsAnnotation { std::string verbatim; bool operator==(const TtsAnnotation&) const = default; };

    // Bullet: a mark drawn before the first line of a list item's paragraph (withBulletList /
    // withBulletListItem), in the text's colour unless `brush` is given. Sizes in em follow the
    // paragraph's font size; the paragraph's indent grows when it cannot hold the bullet and its
    // padding.
    struct Bullet {
        std::shared_ptr<const gfx::Shape> shape;   // null: a circle
        TextUnit width = Em(0.25f), height = Em(0.25f), padding = Em(0.25f);
        std::optional<gfx::Brush> brush;
        float alpha = NAN;
        DrawStyle drawStyle;
        bool operator==(const Bullet& o) const {
            return shape == o.shape && width == o.width && height == o.height && padding == o.padding && brush == o.brush &&
                   (alpha == o.alpha || (alpha != alpha && o.alpha != o.alpha)) && drawStyle == o.drawStyle;
        }
        static TextUnit DefaultIndentation() { return Em(1); }
        static TextUnit DefaultSize() { return Em(0.25f); }
        static TextUnit DefaultPadding() { return Em(0.25f); }
        static Bullet Default() { return {}; }
    };

    class TextLayoutResult;
    // DrawAnnotationScope: where a draw annotation draws (the text's draw scope, its layout and
    // the annotated range).
    struct DrawAnnotationScope {
        ui::DrawScope& drawScope;
        const TextLayoutResult& textLayoutResult;
        int start = 0, end = 0;
    };
    // DrawAnnotation: drawing behind and over the annotated text, in the draw phase only (so a
    // change to it does not lay the text out again).
    struct DrawAnnotation {
        using Block = kiln::Function<void(DrawAnnotationScope&)>;
        std::shared_ptr<const Block> drawBehind, drawOver;
        bool operator==(const DrawAnnotation&) const = default;
    };
    inline DrawAnnotation MakeDrawAnnotation(DrawAnnotation::Block drawBehind = {}, DrawAnnotation::Block drawOver = {}) {
        return { drawBehind ? std::make_shared<const DrawAnnotation::Block>(std::move(drawBehind)) : nullptr,
                 drawOver ? std::make_shared<const DrawAnnotation::Block>(std::move(drawOver)) : nullptr };
    }

    using Annotation = std::variant<SpanStyle, ParagraphStyle, StringAnnotation, LinkAnnotation, TtsAnnotation, Bullet, DrawAnnotation>;

    template<typename T>
    struct Range {
        T item;
        int start = 0, end = 0;
        std::string tag;
        bool operator==(const Range&) const = default;
    };

    class AnnotatedString {
    public:
        AnnotatedString() = default;
        AnnotatedString(std::string text) : m_Text(std::move(text)), m_Length(Utf16Length(m_Text)) {}
        AnnotatedString(const char* text) : AnnotatedString(std::string(text)) {}
        AnnotatedString(std::string text, std::vector<Range<Annotation>> annotations);

        const std::string& Text() const { return m_Text; }
        int Length() const { return m_Length; }   // UTF-16 code units
        bool Empty() const { return m_Text.empty(); }
        const std::vector<Range<Annotation>>& Annotations() const { return m_Annotations; }

        std::vector<Range<SpanStyle>> SpanStyles() const;
        std::vector<Range<ParagraphStyle>> ParagraphStyles() const;
        // getStringAnnotations(tag?, start, end): those intersecting [start, end).
        std::vector<Range<std::string>> GetStringAnnotations(int start, int end) const;
        std::vector<Range<std::string>> GetStringAnnotations(std::string_view tag, int start, int end) const;
        std::vector<Range<LinkAnnotation>> GetLinkAnnotations(int start, int end) const;
        std::vector<Range<TtsAnnotation>> GetTtsAnnotations(int start, int end) const;
        std::vector<Range<DrawAnnotation>> GetDrawAnnotations(int start, int end) const;
        bool HasDrawAnnotations(int start, int end) const { return !GetDrawAnnotations(start, end).empty(); }
        std::vector<Range<Bullet>> Bullets() const;
        bool HasLinkAnnotations(int start, int end) const { return !GetLinkAnnotations(start, end).empty(); }

        // subSequence(start, end): annotations clipped and shifted.
        AnnotatedString SubSequence(int start, int end) const;
        AnnotatedString operator+(const AnnotatedString& o) const;
        bool operator==(const AnnotatedString&) const = default;

        // normalizedParagraphStyles: the whole text as consecutive paragraph ranges, each with
        // the default merged under it (nested paragraphs merged, gaps filled with the default).
        std::vector<Range<ParagraphStyle>> NormalizedParagraphStyles(const ParagraphStyle& defaultStyle) const;

    private:
        std::string m_Text;
        int m_Length = 0;
        std::vector<Range<Annotation>> m_Annotations;
    };

    // AnnotatedString.Builder / buildAnnotatedString.
    class AnnotatedStringBuilder {
    public:
        AnnotatedStringBuilder() = default;
        explicit AnnotatedStringBuilder(const AnnotatedString& s) { Append(s); }

        int Length() const { return m_Length; }
        AnnotatedStringBuilder& Append(std::string_view text);
        AnnotatedStringBuilder& Append(const char* text) { return Append(std::string_view(text)); }
        AnnotatedStringBuilder& Append(const std::string& text) { return Append(std::string_view(text)); }
        AnnotatedStringBuilder& Append(const AnnotatedString& text);
        // Ranges in UTF-16 offsets.
        void AddStyle(SpanStyle style, int start, int end) { m_Annotations.push_back({ std::move(style), start, end, {} }); }
        void AddStyle(ParagraphStyle style, int start, int end) { m_Annotations.push_back({ std::move(style), start, end, {} }); }
        void AddStringAnnotation(std::string tag, std::string value, int start, int end) {
            m_Annotations.push_back({ StringAnnotation{ std::move(value) }, start, end, std::move(tag) });
        }
        void AddLink(LinkAnnotation link, int start, int end) { m_Annotations.push_back({ std::move(link), start, end, {} }); }
        void AddTtsAnnotation(TtsAnnotation tts, int start, int end) { m_Annotations.push_back({ std::move(tts), start, end, {} }); }
        void AddBullet(Bullet bullet, int start, int end) { m_Annotations.push_back({ std::move(bullet), start, end, {} }); }
        // addBullet(bullet, indentation, start, end): the bullet with its paragraph's indent.
        void AddBullet(Bullet bullet, TextUnit indentation, int start, int end) {
            AddStyle(ParagraphStyle{ .textIndent = TextIndent{ indentation, indentation } }, start, end);
            AddBullet(std::move(bullet), start, end);
        }
        void AddDrawAnnotation(DrawAnnotation annotation, int start, int end) { m_Annotations.push_back({ std::move(annotation), start, end, {} }); }

        // pushStyle / pushStringAnnotation / pushLink ... pop: open until popped (or built).
        int PushStyle(SpanStyle style) { return Push({ std::move(style), m_Length, -1, {} }); }
        int PushStyle(ParagraphStyle style) { return Push({ std::move(style), m_Length, -1, {} }); }
        int PushStringAnnotation(std::string tag, std::string value) { return Push({ StringAnnotation{ std::move(value) }, m_Length, -1, std::move(tag) }); }
        int PushLink(LinkAnnotation link) { return Push({ std::move(link), m_Length, -1, {} }); }
        int PushTtsAnnotation(TtsAnnotation tts) { return Push({ std::move(tts), m_Length, -1, {} }); }
        int PushBullet(Bullet bullet) { return Push({ std::move(bullet), m_Length, -1, {} }); }
        int PushDrawAnnotation(DrawAnnotation annotation) { return Push({ std::move(annotation), m_Length, -1, {} }); }
        void Pop();
        // pop(index): pops everything pushed since `index` was pushed, then it.
        void Pop(int index);

        // withStyle { } / withLink { } and friends.
        template<typename F> void WithStyle(SpanStyle style, F&& block) { const int i = PushStyle(std::move(style)); block(); Pop(i); }
        template<typename F> void WithStyle(ParagraphStyle style, F&& block) { const int i = PushStyle(std::move(style)); block(); Pop(i); }
        template<typename F> void WithLink(LinkAnnotation link, F&& block) { const int i = PushLink(std::move(link)); block(); Pop(i); }
        template<typename F> void WithDrawAnnotation(DrawAnnotation annotation, F&& block) {
            const int i = PushDrawAnnotation(std::move(annotation)); block(); Pop(i);
        }

        // BulletScope: the lists open around withBulletListItem (their indentation and bullet).
        class BulletScope {
        public:
            explicit BulletScope(AnnotatedStringBuilder& b) : builder(b) {}
            // withBulletListItem(bullet): a list item (a paragraph) with the list's indentation
            // and its bullet (or `bullet`).
            template<typename F> void WithBulletListItem(std::optional<Bullet> bullet, F&& block) {
                const auto* last = stack.empty() ? nullptr : &stack.back();
                const TextUnit indentation = last ? last->first : Bullet::DefaultIndentation();
                const Bullet itemBullet = bullet ? *bullet : (last ? last->second : Bullet::Default());
                const int par = builder.PushStyle(ParagraphStyle{ .textIndent = TextIndent{ indentation, indentation } });
                const int b = builder.PushBullet(itemBullet);
                block(builder);
                builder.Pop(b);
                builder.Pop(par);
            }
            template<typename F> void WithBulletListItem(F&& block) { WithBulletListItem(std::nullopt, std::forward<F>(block)); }
            AnnotatedStringBuilder& builder;
            std::vector<std::pair<TextUnit, Bullet>> stack;
        };
        // withBulletList(indentation, bullet): a list whose items share the indentation (nested
        // lists add theirs to it; the units must match) and bullet.
        template<typename F> void WithBulletList(TextUnit indentation, Bullet bullet, F&& block) {
            TextUnit adjusted = indentation;
            if (!m_Bullets.stack.empty()) {
                const TextUnit& outer = m_Bullets.stack.back().first;
                if (outer.IsEm() && indentation.IsEm()) adjusted = Em(outer.value + indentation.value);
                else if (!outer.IsEm() && !indentation.IsEm() && outer.IsSpecified() && indentation.IsSpecified()) adjusted = Sp(outer.value + indentation.value);
            }
            const int par = PushStyle(ParagraphStyle{ .textIndent = TextIndent{ adjusted, adjusted } });
            m_Bullets.stack.emplace_back(adjusted, std::move(bullet));
            block(m_Bullets);
            m_Bullets.stack.pop_back();
            Pop(par);
        }
        template<typename F> void WithBulletList(F&& block) { WithBulletList(Bullet::DefaultIndentation(), Bullet::Default(), std::forward<F>(block)); }

        template<typename F> void WithAnnotation(std::string tag, std::string value, F&& block) {
            const int i = PushStringAnnotation(std::move(tag), std::move(value)); block(); Pop(i);
        }

        AnnotatedString ToAnnotatedString() const;

    private:
        int Push(Range<Annotation> r) { m_Annotations.push_back(std::move(r)); m_Stack.push_back(int(m_Annotations.size() - 1)); return int(m_Stack.size() - 1); }
        std::string m_Text;
        int m_Length = 0;
        std::vector<Range<Annotation>> m_Annotations;
        std::vector<int> m_Stack;   // indices of open annotations
        BulletScope m_Bullets{ *this };
    };

    template<typename F>
    AnnotatedString BuildAnnotatedString(F&& block) {
        AnnotatedStringBuilder b;
        block(b);
        return b.ToAnnotatedString();
    }

}
