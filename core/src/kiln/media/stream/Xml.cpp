#include "kiln/media/stream/Xml.h"

#include <cstring>

namespace kiln::media::stream {

    namespace {
        bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
        bool IsNameChar(char c) {
            const auto u = static_cast<unsigned char>(c);
            return u >= 0x80 || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == ':';
        }
        void AppendUtf8(std::string& out, std::uint32_t cp) {
            if (cp < 0x80) {
                out += char(cp);
            } else if (cp < 0x800) {
                out += char(0xC0 | (cp >> 6));
                out += char(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += char(0xE0 | (cp >> 12));
                out += char(0x80 | ((cp >> 6) & 0x3F));
                out += char(0x80 | (cp & 0x3F));
            } else {
                out += char(0xF0 | (cp >> 18));
                out += char(0x80 | ((cp >> 12) & 0x3F));
                out += char(0x80 | ((cp >> 6) & 0x3F));
                out += char(0x80 | (cp & 0x3F));
            }
        }
    }

    XmlPullParser::XmlPullParser(std::string input) : m_Input(std::move(input)) {
        if (m_Input.compare(0, 3, "\xEF\xBB\xBF") == 0) m_Pos = 3;
        // The XML declaration belongs to START_DOCUMENT; KXmlParser never reports it.
        if (LookingAt("<?xml") && m_Pos + 5 < m_Input.size() && IsSpace(m_Input[m_Pos + 5])) ReadUntil("?>", "XML declaration");
    }

    bool XmlPullParser::LookingAt(const char* s) const { return m_Input.compare(m_Pos, std::strlen(s), s) == 0; }

    void XmlPullParser::Fail(const std::string& message) const {
        throw XmlPullParserException(message + " (position " + std::to_string(m_Pos) + ")");
    }

    void XmlPullParser::SkipWhitespace() {
        while (m_Pos < m_Input.size() && IsSpace(m_Input[m_Pos])) m_Pos++;
    }

    std::string XmlPullParser::ReadName() {
        const std::size_t start = m_Pos;
        while (m_Pos < m_Input.size() && IsNameChar(m_Input[m_Pos])) m_Pos++;
        if (m_Pos == start) Fail("name expected");
        return m_Input.substr(start, m_Pos - start);
    }

    std::string XmlPullParser::ReadUntil(const char* terminator, const char* what) {
        const std::size_t end = m_Input.find(terminator, m_Pos);
        if (end == std::string::npos) Fail(std::string("unterminated ") + what);
        std::string s = m_Input.substr(m_Pos, end - m_Pos);
        m_Pos = end + std::strlen(terminator);
        return s;
    }

    std::string XmlPullParser::ResolveEntity(const std::string& name) const {
        if (name == "lt") return "<";
        if (name == "gt") return ">";
        if (name == "amp") return "&";
        if (name == "apos") return "'";
        if (name == "quot") return "\"";
        if (name.size() > 1 && name[0] == '#') {
            std::uint32_t cp = 0;
            try {
                cp = name[1] == 'x' ? std::uint32_t(std::stoul(name.substr(2), nullptr, 16)) : std::uint32_t(std::stoul(name.substr(1), nullptr, 10));
            } catch (const std::exception&) {
                Fail("invalid character reference &" + name + ";");
            }
            std::string out;
            AppendUtf8(out, cp);
            return out;
        }
        Fail("unresolved entity &" + name + ";");
    }

    XmlPullParser::Token XmlPullParser::ReadTag() {
        Token t;
        t.type = START_TAG;
        m_Pos++;   // <
        t.name = ReadName();
        while (true) {
            SkipWhitespace();
            if (m_Pos >= m_Input.size()) Fail("unterminated start tag <" + t.name);
            if (m_Input[m_Pos] == '>') {
                m_Pos++;
                break;
            }
            if (LookingAt("/>")) {
                m_Pos += 2;
                t.degenerated = true;
                break;
            }
            std::string attributeName = ReadName();
            SkipWhitespace();
            if (m_Pos >= m_Input.size() || m_Input[m_Pos] != '=') Fail("= expected after attribute " + attributeName);
            m_Pos++;
            SkipWhitespace();
            if (m_Pos >= m_Input.size() || (m_Input[m_Pos] != '"' && m_Input[m_Pos] != '\'')) Fail("quote expected for attribute " + attributeName);
            const char quote = m_Input[m_Pos++];
            std::string value;
            while (true) {
                if (m_Pos >= m_Input.size()) Fail("unterminated attribute value");
                const char c = m_Input[m_Pos];
                if (c == quote) {
                    m_Pos++;
                    break;
                }
                if (c == '<') Fail("< in attribute value");
                if (c == '&') {
                    m_Pos++;
                    value += ResolveEntity(ReadUntil(";", "entity"));
                } else {
                    value += c;
                    m_Pos++;
                }
            }
            for (const auto& [n, v] : t.attributes)
                if (n == attributeName) Fail("duplicate attribute " + attributeName);
            t.attributes.emplace_back(std::move(attributeName), std::move(value));
        }
        return t;
    }

    XmlPullParser::Token XmlPullParser::ReadToken() {
        Token t;
        if (m_PendingEnd) {
            t = std::move(*m_PendingEnd);
            m_PendingEnd.reset();
            return t;
        }
        if (m_Pos >= m_Input.size()) {
            if (!m_Stack.empty()) Fail("unexpected end of document in <" + m_Stack.back() + ">");
            t.type = END_DOCUMENT;
            return t;
        }
        if (m_Input[m_Pos] == '<') {
            if (LookingAt("<!--")) {
                m_Pos += 4;
                t.type = COMMENT;
                t.text = ReadUntil("-->", "comment");
            } else if (LookingAt("<![CDATA[")) {
                m_Pos += 9;
                t.type = CDSECT;
                t.text = ReadUntil("]]>", "CDATA section");
            } else if (LookingAt("<!DOCTYPE")) {
                m_Pos += 9;
                t.type = DOCDECL;
                const std::size_t start = m_Pos;
                int depth = 0;
                while (m_Pos < m_Input.size() && !(m_Input[m_Pos] == '>' && depth == 0)) {
                    if (m_Input[m_Pos] == '[') depth++;
                    else if (m_Input[m_Pos] == ']') depth--;
                    m_Pos++;
                }
                if (m_Pos >= m_Input.size()) Fail("unterminated DOCTYPE");
                t.text = m_Input.substr(start, m_Pos - start);
                m_Pos++;
            } else if (LookingAt("<?")) {
                m_Pos += 2;
                t.type = PROCESSING_INSTRUCTION;
                t.text = ReadUntil("?>", "processing instruction");
            } else if (LookingAt("</")) {
                m_Pos += 2;
                t.type = END_TAG;
                t.name = ReadName();
                SkipWhitespace();
                if (m_Pos >= m_Input.size() || m_Input[m_Pos] != '>') Fail("> expected in </" + t.name);
                m_Pos++;
                if (m_Stack.empty() || m_Stack.back() != t.name) Fail("</" + t.name + "> does not match the open element");
            } else {
                t = ReadTag();
                if (t.degenerated) {
                    Token end;
                    end.type = END_TAG;
                    end.name = t.name;
                    m_PendingEnd = std::move(end);
                }
            }
            return t;
        }
        if (m_Input[m_Pos] == '&') {
            m_Pos++;
            t.type = ENTITY_REF;
            t.name = ReadUntil(";", "entity");
            t.text = ResolveEntity(t.name);
            if (m_Stack.empty()) Fail("entity reference outside the root element");
            return t;
        }
        const std::size_t start = m_Pos;
        while (m_Pos < m_Input.size() && m_Input[m_Pos] != '<' && m_Input[m_Pos] != '&') m_Pos++;
        t.type = TEXT;
        t.text = m_Input.substr(start, m_Pos - start);
        t.whitespace = true;
        for (char c : t.text)
            if (!IsSpace(c)) t.whitespace = false;
        if (m_Stack.empty()) {
            if (!t.whitespace) Fail("text outside the root element");
            t.type = IGNORABLE_WHITESPACE;
        }
        return t;
    }

    void XmlPullParser::Apply(Token&& t) {
        m_Type = t.type;
        m_Text = std::move(t.text);
        m_Degenerated = t.degenerated;
        if (t.type == START_TAG) {
            m_Name = std::move(t.name);
            m_Attributes = std::move(t.attributes);
            m_Stack.push_back(m_Name);
        } else if (t.type == END_TAG) {
            m_Name = std::move(t.name);
            m_Attributes.clear();
            m_Stack.pop_back();
        } else if (t.type == ENTITY_REF) {
            m_Name = std::move(t.name);
            m_Attributes.clear();
        } else {
            m_Name.clear();
            m_Attributes.clear();
        }
    }

    int XmlPullParser::NextToken() {
        Apply(m_Lookahead ? std::exchange(m_Lookahead, std::nullopt).value() : ReadToken());
        return m_Type;
    }

    int XmlPullParser::Next() {
        std::string text;
        bool haveText = false;
        while (true) {
            Token t = m_Lookahead ? std::exchange(m_Lookahead, std::nullopt).value() : ReadToken();
            switch (t.type) {
                case TEXT:
                case CDSECT:
                case ENTITY_REF:
                    text += t.text;
                    haveText = true;
                    continue;
                case COMMENT:
                case PROCESSING_INSTRUCTION:
                case DOCDECL:
                case IGNORABLE_WHITESPACE:
                    continue;
                default:
                    break;
            }
            if (haveText) {
                m_Lookahead = std::move(t);
                Token textToken;
                textToken.type = TEXT;
                textToken.text = std::move(text);
                Apply(std::move(textToken));
                return m_Type;
            }
            Apply(std::move(t));
            return m_Type;
        }
    }

    std::string XmlPullParser::NextText() {
        if (m_Type != START_TAG) Fail("nextText() on a non-START_TAG event");
        std::string result;
        int type = Next();
        if (type == TEXT) {
            result = m_Text;
            type = Next();
        }
        if (type != END_TAG) Fail("END_TAG expected after the text of an element");
        return result;
    }

    std::optional<std::string> XmlPullParser::GetAttributeValue(const std::string& name) const {
        for (const auto& [n, v] : m_Attributes)
            if (n == name) return v;
        return std::nullopt;
    }

    // --- XmlSerializer -------------------------------------------------------------------------

    void XmlSerializer::Check() {
        if (m_Pending) {
            m_Out += '>';
            m_Pending = false;
        }
    }

    void XmlSerializer::WriteEscaped(const std::string& s, int quote) {
        for (char c : s) {
            switch (c) {
                case '&': m_Out += "&amp;"; break;
                case '>': m_Out += "&gt;"; break;
                case '<': m_Out += "&lt;"; break;
                case '"':
                case '\'':
                    if (c == quote) {
                        m_Out += c == '"' ? "&quot;" : "&apos;";
                        break;
                    }
                    m_Out += c;
                    break;
                case '\n':
                case '\r':
                case '\t':
                    if (quote == -1) m_Out += c;
                    else m_Out += "&#" + std::to_string(int(c)) + ";";
                    break;
                default:
                    if (static_cast<unsigned char>(c) < 0x20) throw std::invalid_argument("illegal character in XML text");
                    m_Out += c;   // UTF-8 output: non-ASCII written as is
            }
        }
    }

    void XmlSerializer::StartTag(const std::string&, const std::string& name) {
        Check();
        m_Out += '<';
        m_Out += name;
        m_Pending = true;
        m_Stack.push_back(name);
    }

    void XmlSerializer::Attribute(const std::string&, const std::string& name, const std::string& value) {
        if (!m_Pending) throw std::logic_error("illegal position for attribute");
        m_Out += ' ';
        m_Out += name;
        m_Out += "=\"";
        WriteEscaped(value, '"');
        m_Out += '"';
    }

    void XmlSerializer::EndTag(const std::string&, const std::string& name) {
        if (m_Stack.empty() || m_Stack.back() != name) throw std::invalid_argument("</" + name + "> does not match start");
        m_Stack.pop_back();
        if (m_Pending) {
            m_Out += " />";
            m_Pending = false;
        } else {
            m_Out += "</" + name + ">";
        }
    }

    void XmlSerializer::Text(const std::string& text) {
        Check();
        WriteEscaped(text, -1);
    }

    void XmlSerializer::Cdsect(const std::string& text) {
        Check();
        std::string s = text;
        for (std::size_t i = s.find("]]>"); i != std::string::npos; i = s.find("]]>", i + 15)) s.replace(i, 3, "]]]]><![CDATA[>");
        m_Out += "<![CDATA[" + s + "]]>";
    }

    void XmlSerializer::EntityRef(const std::string& name) {
        Check();
        m_Out += "&" + name + ";";
    }

    void XmlSerializer::IgnorableWhitespace(const std::string& text) {
        Check();
        m_Out += text;
    }

    void XmlSerializer::ProcessingInstruction(const std::string& text) {
        Check();
        m_Out += "<?" + text + "?>";
    }

    void XmlSerializer::Comment(const std::string& text) {
        Check();
        m_Out += "<!--" + text + "-->";
    }

    void XmlSerializer::Docdecl(const std::string& text) {
        m_Out += "<!DOCTYPE" + text + ">";
    }

    void XmlSerializer::StartDocument(bool standalone) {
        m_Out += "<?xml version='1.0' encoding='UTF-8' standalone='";
        m_Out += standalone ? "yes" : "no";
        m_Out += "' ?>";
    }

    void XmlSerializer::EndDocument() {
        while (!m_Stack.empty()) EndTag("", m_Stack.back());
    }

    const std::string& XmlSerializer::Output() {
        Check();
        return m_Out;
    }

}
