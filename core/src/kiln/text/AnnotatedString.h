#pragma once

// AnnotatedString (ui-text AnnotatedString.kt, LinkAnnotation.kt, TextLinkStyles.kt): text with
// ranges of SpanStyle, ParagraphStyle and annotations (string, link, TTS). Text is UTF-8;
// offsets are UTF-16 code units, as in Compose (and in what IMEs and the platform layout use).

#include "kiln/text/TextStyle.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

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
    using LinkInteractionListener = std::function<void(const LinkAnnotation&)>;

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

    using Annotation = std::variant<SpanStyle, ParagraphStyle, StringAnnotation, LinkAnnotation, TtsAnnotation>;

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

        // pushStyle / pushStringAnnotation / pushLink ... pop: open until popped (or built).
        int PushStyle(SpanStyle style) { return Push({ std::move(style), m_Length, -1, {} }); }
        int PushStyle(ParagraphStyle style) { return Push({ std::move(style), m_Length, -1, {} }); }
        int PushStringAnnotation(std::string tag, std::string value) { return Push({ StringAnnotation{ std::move(value) }, m_Length, -1, std::move(tag) }); }
        int PushLink(LinkAnnotation link) { return Push({ std::move(link), m_Length, -1, {} }); }
        int PushTtsAnnotation(TtsAnnotation tts) { return Push({ std::move(tts), m_Length, -1, {} }); }
        void Pop();
        // pop(index): pops everything pushed since `index` was pushed, then it.
        void Pop(int index);

        // withStyle { } / withLink { } and friends.
        template<typename F> void WithStyle(SpanStyle style, F&& block) { const int i = PushStyle(std::move(style)); block(); Pop(i); }
        template<typename F> void WithStyle(ParagraphStyle style, F&& block) { const int i = PushStyle(std::move(style)); block(); Pop(i); }
        template<typename F> void WithLink(LinkAnnotation link, F&& block) { const int i = PushLink(std::move(link)); block(); Pop(i); }
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
    };

    template<typename F>
    AnnotatedString BuildAnnotatedString(F&& block) {
        AnnotatedStringBuilder b;
        block(b);
        return b.ToAnnotatedString();
    }

}
