#pragma once

// org.xmlpull's XmlPullParser and XmlSerializer as Android implements them (KXmlParser and
// KXmlSerializer), without namespace processing: what media3's manifest parsers read and write.
// A name keeps its prefix ("cenc:pssh"), and GetNamespace() is always "".

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace kiln::media::stream {

    class XmlPullParserException : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    class XmlPullParser {
    public:
        enum Event { START_DOCUMENT = 0, END_DOCUMENT = 1, START_TAG = 2, END_TAG = 3, TEXT = 4, CDSECT = 5, ENTITY_REF = 6, IGNORABLE_WHITESPACE = 7,
                     PROCESSING_INSTRUCTION = 8, COMMENT = 9, DOCDECL = 10 };

        // The document as UTF-8 (a byte order mark is skipped).
        explicit XmlPullParser(std::string input);

        // next(): the next START_TAG, END_TAG, TEXT (adjacent text, CDATA and entities merged)
        // or END_DOCUMENT; comments, processing instructions and whitespace outside the root
        // element are skipped.
        int Next();
        // nextToken(): every token, CDATA, entity references and comments included.
        int NextToken();
        // nextText(): on a START_TAG, the element's text (an END_TAG must follow it).
        std::string NextText();

        int GetEventType() const { return m_Type; }
        int GetDepth() const { return int(m_Stack.size()); }
        // The tag's name; an ENTITY_REF's entity name.
        const std::string& GetName() const { return m_Name; }
        const std::string& GetNamespace() const { return m_Empty; }
        // TEXT, CDSECT, COMMENT and the like: the content; ENTITY_REF: the replacement text.
        const std::string& GetText() const { return m_Text; }
        bool IsEmptyElementTag() const { return m_Degenerated; }

        int GetAttributeCount() const { return m_Type == START_TAG ? int(m_Attributes.size()) : -1; }
        const std::string& GetAttributeName(int index) const { return m_Attributes.at(std::size_t(index)).first; }
        const std::string& GetAttributeValue(int index) const { return m_Attributes.at(std::size_t(index)).second; }
        const std::string& GetAttributeNamespace(int) const { return m_Empty; }
        // getAttributeValue(null, name).
        std::optional<std::string> GetAttributeValue(const std::string& name) const;

    private:
        struct Token {
            int type = START_DOCUMENT;
            std::string name, text;
            std::vector<std::pair<std::string, std::string>> attributes;
            bool degenerated = false;
            bool whitespace = false;
        };
        Token ReadToken();
        Token ReadTag();
        std::string ReadName();
        std::string ReadUntil(const char* terminator, const char* what);
        std::string ResolveEntity(const std::string& name) const;
        void SkipWhitespace();
        void Apply(Token&& token);
        [[noreturn]] void Fail(const std::string& message) const;
        bool LookingAt(const char* s) const;

        std::string m_Input;
        std::size_t m_Pos = 0;
        int m_Type = START_DOCUMENT;
        std::string m_Name, m_Text;
        std::vector<std::pair<std::string, std::string>> m_Attributes;
        std::vector<std::string> m_Stack;
        bool m_Degenerated = false;
        std::optional<Token> m_PendingEnd;   // the END_TAG of <x/>
        std::optional<Token> m_Lookahead;    // a tag read by Next() past merged text
        const std::string m_Empty;
    };

    // KXmlSerializer writing UTF-8: tags close lazily (an element ended right after it starts is
    // written "<x />"), text escapes &, < and >, attribute values also the quote and \t\n\r.
    class XmlSerializer {
    public:
        void StartTag(const std::string& ns, const std::string& name);
        void Attribute(const std::string& ns, const std::string& name, const std::string& value);
        void EndTag(const std::string& ns, const std::string& name);
        void Text(const std::string& text);
        void Cdsect(const std::string& text);
        void EntityRef(const std::string& name);
        void IgnorableWhitespace(const std::string& text);
        void ProcessingInstruction(const std::string& text);
        void Comment(const std::string& text);
        void Docdecl(const std::string& text);
        void StartDocument(bool standalone);
        void EndDocument();
        const std::string& Output();

    private:
        void Check();
        void WriteEscaped(const std::string& s, int quote);
        std::string m_Out;
        bool m_Pending = false;
        std::vector<std::string> m_Stack;
    };

}
