#include "kiln/text/Html.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <regex>
#include <string>
#include <vector>

// The parse is TagSoup's, as far as Android's Html relies on it: tag and attribute names in
// lower case, bare attributes valued by their name, void elements closed at once, <foo/> closed
// at once, a block element closing an open <p>, an <li> closing the previous one (or opening an
// implied <ul>), end tags closing what is open inside them and stray end tags ignored, script and
// style content as text, and everything still open closed at the end.

namespace kiln::text {

    namespace {

        // --- UTF-8 / UTF-16 ------------------------------------------------------------------

        char32_t DecodeUtf8(std::string_view s, std::size_t& i) {
            const unsigned char c = static_cast<unsigned char>(s[i]);
            int n = c < 0x80 ? 0 : (c >> 5) == 0x6 ? 1 : (c >> 4) == 0xE ? 2 : (c >> 3) == 0x1E ? 3 : -1;
            if (n == 0) {
                ++i;
                return c;
            }
            if (n < 0 || i + std::size_t(n) >= s.size()) {
                ++i;
                return 0xFFFD;
            }
            char32_t cp = n == 0 ? c : n == 1 ? (c & 0x1F) : n == 2 ? (c & 0x0F) : (c & 0x07);
            for (int k = 1; k <= n; ++k) {
                const unsigned char d = static_cast<unsigned char>(s[i + std::size_t(k)]);
                if ((d >> 6) != 0x2) {
                    ++i;
                    return 0xFFFD;
                }
                cp = (cp << 6) | (d & 0x3F);
            }
            i += std::size_t(n) + 1;
            return cp;
        }

        void AppendUtf16(std::u16string& out, char32_t cp) {
            if (cp >= 0x10000) {
                cp -= 0x10000;
                out.push_back(char16_t(0xD800 + (cp >> 10)));
                out.push_back(char16_t(0xDC00 + (cp & 0x3FF)));
            } else {
                out.push_back(char16_t(cp));
            }
        }

        std::string ToUtf8(const std::u16string& s) {
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                char32_t cp = s[i];
                if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] < 0xE000)
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (s[++i] - 0xDC00);
                else if (cp >= 0xD800 && cp < 0xE000)
                    cp = 0xFFFD;
                if (cp < 0x80) {
                    out.push_back(char(cp));
                } else if (cp < 0x800) {
                    out.push_back(char(0xC0 | (cp >> 6)));
                    out.push_back(char(0x80 | (cp & 0x3F)));
                } else if (cp < 0x10000) {
                    out.push_back(char(0xE0 | (cp >> 12)));
                    out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(char(0x80 | (cp & 0x3F)));
                } else {
                    out.push_back(char(0xF0 | (cp >> 18)));
                    out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
                    out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
                    out.push_back(char(0x80 | (cp & 0x3F)));
                }
            }
            return out;
        }

        std::string Lower(std::string_view s) {
            std::string r(s);
            for (char& c : r) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            return r;
        }

        bool EqualsIgnoreCase(std::string_view a, std::string_view b) { return a.size() == b.size() && Lower(a) == Lower(b); }

        // --- entities (TagSoup's HTML schema: the HTML 4 set and apos) ------------------------

        struct Entity {
            const char* name;
            char32_t codePoint;
        };
        constexpr Entity kEntities[] = {
        { "AElig", 198 }, { "Aacute", 193 }, { "Acirc", 194 }, { "Agrave", 192 }, { "Alpha", 913 }, { "Aring", 197 },
        { "Atilde", 195 }, { "Auml", 196 }, { "Beta", 914 }, { "Ccedil", 199 }, { "Chi", 935 }, { "Dagger", 8225 },
        { "Delta", 916 }, { "ETH", 208 }, { "Eacute", 201 }, { "Ecirc", 202 }, { "Egrave", 200 }, { "Epsilon", 917 },
        { "Eta", 919 }, { "Euml", 203 }, { "Gamma", 915 }, { "Iacute", 205 }, { "Icirc", 206 }, { "Igrave", 204 },
        { "Iota", 921 }, { "Iuml", 207 }, { "Kappa", 922 }, { "Lambda", 923 }, { "Mu", 924 }, { "Ntilde", 209 },
        { "Nu", 925 }, { "OElig", 338 }, { "Oacute", 211 }, { "Ocirc", 212 }, { "Ograve", 210 }, { "Omega", 937 },
        { "Omicron", 927 }, { "Oslash", 216 }, { "Otilde", 213 }, { "Ouml", 214 }, { "Phi", 934 }, { "Pi", 928 },
        { "Prime", 8243 }, { "Psi", 936 }, { "Rho", 929 }, { "Scaron", 352 }, { "Sigma", 931 }, { "THORN", 222 },
        { "Tau", 932 }, { "Theta", 920 }, { "Uacute", 218 }, { "Ucirc", 219 }, { "Ugrave", 217 }, { "Upsilon", 933 },
        { "Uuml", 220 }, { "Xi", 926 }, { "Yacute", 221 }, { "Yuml", 376 }, { "Zeta", 918 }, { "aacute", 225 },
        { "acirc", 226 }, { "acute", 180 }, { "aelig", 230 }, { "agrave", 224 }, { "alefsym", 8501 }, { "alpha", 945 },
        { "amp", 38 }, { "and", 8743 }, { "ang", 8736 }, { "apos", 39 }, { "aring", 229 }, { "asymp", 8776 },
        { "atilde", 227 }, { "auml", 228 }, { "bdquo", 8222 }, { "beta", 946 }, { "brvbar", 166 }, { "bull", 8226 },
        { "cap", 8745 }, { "ccedil", 231 }, { "cedil", 184 }, { "cent", 162 }, { "chi", 967 }, { "circ", 710 },
        { "clubs", 9827 }, { "cong", 8773 }, { "copy", 169 }, { "crarr", 8629 }, { "cup", 8746 }, { "curren", 164 },
        { "dArr", 8659 }, { "dagger", 8224 }, { "darr", 8595 }, { "deg", 176 }, { "delta", 948 }, { "diams", 9830 },
        { "divide", 247 }, { "eacute", 233 }, { "ecirc", 234 }, { "egrave", 232 }, { "empty", 8709 }, { "emsp", 8195 },
        { "ensp", 8194 }, { "epsilon", 949 }, { "equiv", 8801 }, { "eta", 951 }, { "eth", 240 }, { "euml", 235 },
        { "euro", 8364 }, { "exist", 8707 }, { "fnof", 402 }, { "forall", 8704 }, { "frac12", 189 }, { "frac14", 188 },
        { "frac34", 190 }, { "frasl", 8260 }, { "gamma", 947 }, { "ge", 8805 }, { "gt", 62 }, { "hArr", 8660 },
        { "harr", 8596 }, { "hearts", 9829 }, { "hellip", 8230 }, { "iacute", 237 }, { "icirc", 238 },
        { "iexcl", 161 }, { "igrave", 236 }, { "image", 8465 }, { "infin", 8734 }, { "int", 8747 }, { "iota", 953 },
        { "iquest", 191 }, { "isin", 8712 }, { "iuml", 239 }, { "kappa", 954 }, { "lArr", 8656 }, { "lambda", 955 },
        { "lang", 9001 }, { "laquo", 171 }, { "larr", 8592 }, { "lceil", 8968 }, { "ldquo", 8220 }, { "le", 8804 },
        { "lfloor", 8970 }, { "lowast", 8727 }, { "loz", 9674 }, { "lrm", 8206 }, { "lsaquo", 8249 },
        { "lsquo", 8216 }, { "lt", 60 }, { "macr", 175 }, { "mdash", 8212 }, { "micro", 181 }, { "middot", 183 },
        { "minus", 8722 }, { "mu", 956 }, { "nabla", 8711 }, { "nbsp", 160 }, { "ndash", 8211 }, { "ne", 8800 },
        { "ni", 8715 }, { "not", 172 }, { "notin", 8713 }, { "nsub", 8836 }, { "ntilde", 241 }, { "nu", 957 },
        { "oacute", 243 }, { "ocirc", 244 }, { "oelig", 339 }, { "ograve", 242 }, { "oline", 8254 }, { "omega", 969 },
        { "omicron", 959 }, { "oplus", 8853 }, { "or", 8744 }, { "ordf", 170 }, { "ordm", 186 }, { "oslash", 248 },
        { "otilde", 245 }, { "otimes", 8855 }, { "ouml", 246 }, { "para", 182 }, { "part", 8706 }, { "permil", 8240 },
        { "perp", 8869 }, { "phi", 966 }, { "pi", 960 }, { "piv", 982 }, { "plusmn", 177 }, { "pound", 163 },
        { "prime", 8242 }, { "prod", 8719 }, { "prop", 8733 }, { "psi", 968 }, { "quot", 34 }, { "rArr", 8658 },
        { "radic", 8730 }, { "rang", 9002 }, { "raquo", 187 }, { "rarr", 8594 }, { "rceil", 8969 }, { "rdquo", 8221 },
        { "real", 8476 }, { "reg", 174 }, { "rfloor", 8971 }, { "rho", 961 }, { "rlm", 8207 }, { "rsaquo", 8250 },
        { "rsquo", 8217 }, { "sbquo", 8218 }, { "scaron", 353 }, { "sdot", 8901 }, { "sect", 167 }, { "shy", 173 },
        { "sigma", 963 }, { "sigmaf", 962 }, { "sim", 8764 }, { "spades", 9824 }, { "sub", 8834 }, { "sube", 8838 },
        { "sum", 8721 }, { "sup", 8835 }, { "sup1", 185 }, { "sup2", 178 }, { "sup3", 179 }, { "supe", 8839 },
        { "szlig", 223 }, { "tau", 964 }, { "there4", 8756 }, { "theta", 952 }, { "thetasym", 977 },
        { "thinsp", 8201 }, { "thorn", 254 }, { "tilde", 732 }, { "times", 215 }, { "trade", 8482 }, { "uArr", 8657 },
        { "uacute", 250 }, { "uarr", 8593 }, { "ucirc", 251 }, { "ugrave", 249 }, { "uml", 168 }, { "upsih", 978 },
        { "upsilon", 965 }, { "uuml", 252 }, { "weierp", 8472 }, { "xi", 958 }, { "yacute", 253 }, { "yen", 165 },
        { "yuml", 255 }, { "zeta", 950 }, { "zwj", 8205 }, { "zwnj", 8204 },
        };

        std::optional<char32_t> NamedEntity(std::string_view name) {
            const auto* end = std::end(kEntities);
            const auto* it = std::lower_bound(std::begin(kEntities), end, name, [](const Entity& e, std::string_view n) { return std::string_view(e.name) < n; });
            if (it != end && std::string_view(it->name) == name) return it->codePoint;
            return std::nullopt;
        }

        // Text with character references decoded, as UTF-16.
        void DecodeText(std::string_view s, std::u16string& out) {
            std::size_t i = 0;
            while (i < s.size()) {
                if (s[i] == '&') {
                    std::size_t j = i + 1;
                    std::optional<char32_t> cp;
                    if (j < s.size() && s[j] == '#') {
                        ++j;
                        const bool hex = j < s.size() && (s[j] == 'x' || s[j] == 'X');
                        if (hex) ++j;
                        const std::size_t digits = j;
                        std::uint32_t v = 0;
                        while (j < s.size() && (hex ? std::isxdigit(static_cast<unsigned char>(s[j])) : std::isdigit(static_cast<unsigned char>(s[j])))) {
                            if (v <= 0x10FFFF) v = v * (hex ? 16 : 10) + std::uint32_t(std::isdigit(static_cast<unsigned char>(s[j])) ? s[j] - '0' : (s[j] | 0x20) - 'a' + 10);
                            ++j;
                        }
                        if (j > digits) {
                            cp = v == 0 || v > 0x10FFFF || (v >= 0xD800 && v < 0xE000) ? char32_t(0xFFFD) : char32_t(v);
                            if (j < s.size() && s[j] == ';') ++j;
                        }
                    } else {
                        while (j < s.size() && std::isalnum(static_cast<unsigned char>(s[j]))) ++j;
                        if (j < s.size() && s[j] == ';') {
                            cp = NamedEntity(s.substr(i + 1, j - i - 1));
                            if (cp) ++j;
                        }
                    }
                    if (cp) {
                        AppendUtf16(out, *cp);
                        i = j;
                        continue;
                    }
                }
                AppendUtf16(out, DecodeUtf8(s, i));
            }
        }

        struct Attributes {
            std::vector<std::pair<std::string, std::string>> list;   // names lower-cased, values decoded (UTF-8)
            const std::string* Get(std::string_view name) const {
                for (const auto& [k, v] : list) if (k == name) return &v;
                return nullptr;
            }
        };

        // --- HtmlToSpannedConverter (FROM_HTML_MODE_COMPACT) with Compose's content handler ---

        enum class MarkKind : std::uint8_t {
            Bold, Italic, Underline, Strikethrough, Big, Small, Monospace, Blockquote, Super, Sub, Font, Href, Foreground,
            Background, Heading, Newline, Alignment
        };
        struct Mark {
            MarkKind kind;
            int where = 0;
            std::uint32_t color = 0;
            int number = 0;              // Heading level, Newline count
            TextAlign align = TextAlign::Unspecified;
            std::optional<std::string> text;   // Font face, Href
        };

        // The spans Compose turns into styles, in the order they were set.
        struct Span {
            enum class Kind : std::uint8_t { Alignment, Annotation, Background, Bullet, Foreground, RelativeSize, Strikethrough, Style, Subscript, Superscript, Typeface, Underline, Url };
            Kind kind;
            int start = 0, end = 0;
            std::uint32_t color = 0;
            float size = 1;
            bool bold = false, italic = false;
            int level = 0;
            TextAlign align = TextAlign::Unspecified;
            std::string key, value;
        };

        constexpr float kHeadingSizes[] = { 1.5f, 1.4f, 1.3f, 1.2f, 1.1f, 1.0f };

        // Html.getHtmlColor (no FROM_HTML_OPTION_USE_CSS_COLORS): a name for Color.parseColor,
        // otherwise XmlUtils.convertValueToInt; -1 (also what white comes to) is no colour.
        std::optional<std::uint32_t> HtmlColor(const std::string& color) {
            if (color.empty()) return std::nullopt;
            std::int64_t value = -1;
            if (std::isalpha(static_cast<unsigned char>(color[0]))) {
                static constexpr std::pair<const char*, std::uint32_t> names[] = {
                    { "black", 0xFF000000u }, { "darkgray", 0xFF444444u }, { "gray", 0xFF888888u }, { "lightgray", 0xFFCCCCCCu },
                    { "white", 0xFFFFFFFFu }, { "red", 0xFFFF0000u }, { "green", 0xFF00FF00u }, { "blue", 0xFF0000FFu },
                    { "yellow", 0xFFFFFF00u }, { "cyan", 0xFF00FFFFu }, { "magenta", 0xFFFF00FFu }, { "aqua", 0xFF00FFFFu },
                    { "fuchsia", 0xFFFF00FFu }, { "darkgrey", 0xFF444444u }, { "grey", 0xFF888888u }, { "lightgrey", 0xFFCCCCCCu },
                    { "lime", 0xFF00FF00u }, { "maroon", 0xFF800000u }, { "navy", 0xFF000080u }, { "olive", 0xFF808000u },
                    { "purple", 0xFF800080u }, { "silver", 0xFFC0C0C0u }, { "teal", 0xFF008080u },
                };
                const std::string lower = Lower(color);
                for (const auto& [n, c] : names) if (lower == n) value = std::int32_t(c);
                if (value == -1) return std::nullopt;
            } else {
                // XmlUtils.convertValueToInt: [-] then 0x.. (hex), 0.. (octal), #.. (hex) or
                // decimal; Integer.parseInt of the rest (an optional sign, no overflow).
                std::size_t i = 0;
                int sign = 1, base = 10;
                if (color[0] == '-') {
                    sign = -1;
                    ++i;
                }
                if (i < color.size() && color[i] == '0') {
                    if (i == color.size() - 1) return std::nullopt;   // 0: no alpha-less colour either way
                    if (color[i + 1] == 'x' || color[i + 1] == 'X') {
                        i += 2;
                        base = 16;
                    } else {
                        ++i;
                        base = 8;
                    }
                } else if (i < color.size() && color[i] == '#') {
                    ++i;
                    base = 16;
                }
                int sign2 = 1;
                if (i < color.size() && (color[i] == '+' || color[i] == '-')) {
                    sign2 = color[i] == '-' ? -1 : 1;
                    ++i;
                }
                if (i >= color.size()) return std::nullopt;
                std::int64_t v = 0;
                for (; i < color.size(); ++i) {
                    const char c = color[i];
                    int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'z' ? c - 'a' + 10 : c >= 'A' && c <= 'Z' ? c - 'A' + 10 : 99;
                    if (d >= base) return std::nullopt;
                    v = v * base + d;
                    if (v > 0x80000000LL) return std::nullopt;
                }
                v *= sign2;
                if (v > 0x7FFFFFFFLL || v < -0x80000000LL) return std::nullopt;
                value = v * sign;
                if (value == 0x80000000LL) value = -0x80000000LL;   // Integer.MIN_VALUE * -1
            }
            if (value == -1) return std::nullopt;
            return std::uint32_t(value) | 0xFF000000u;
        }

        // Pattern "(?:\\s+|\\A)<property>\\s*:\\s*(\\S*)\\b" of a style attribute.
        std::optional<std::string> CssValue(const std::string& style, const char* property) {
            const std::regex pattern(std::string("(?:\\s+|^)") + property + "\\s*:\\s*(\\S*)\\b");
            std::smatch m;
            if (!std::regex_search(style, m, pattern)) return std::nullopt;
            return m[1].str();
        }

        class Converter {
        public:
            std::u16string text;
            std::vector<Span> spans;

            void Start(const std::string& tag, const Attributes& a) {
                // Compose's AnnotationContentHandler first: annotation, ul and li.
                if (tag == "annotation") return AnnotationStart(a);
                if (tag == "ul") {
                    CommitBullet();
                    ++m_BulletIndentation;
                    return;
                }
                if (tag == "li") {
                    CommitBullet();
                    m_Bullet = { m_BulletIndentation, Length() };
                    return;
                }
                if (tag == "br") return;
                if (tag == "p") {
                    StartBlock(a, 1);
                    StartCss(a);
                } else if (tag == "div") {
                    StartBlock(a, 1);
                } else if (tag == "span") {
                    StartCss(a);
                } else if (tag == "strong" || tag == "b") {
                    Open(MarkKind::Bold);
                } else if (tag == "em" || tag == "cite" || tag == "dfn" || tag == "i") {
                    Open(MarkKind::Italic);
                } else if (tag == "big") {
                    Open(MarkKind::Big);
                } else if (tag == "small") {
                    Open(MarkKind::Small);
                } else if (tag == "font") {
                    StartFont(a);
                } else if (tag == "blockquote") {
                    StartBlock(a, 1);
                    Open(MarkKind::Blockquote);
                } else if (tag == "tt") {
                    Open(MarkKind::Monospace);
                } else if (tag == "a") {
                    Mark m{ MarkKind::Href, Length() };
                    if (const auto* href = a.Get("href")) m.text = *href;
                    m_Marks.push_back(std::move(m));
                } else if (tag == "u") {
                    Open(MarkKind::Underline);
                } else if (tag == "del" || tag == "s" || tag == "strike") {
                    Open(MarkKind::Strikethrough);
                } else if (tag == "sup") {
                    Open(MarkKind::Super);
                } else if (tag == "sub") {
                    Open(MarkKind::Sub);
                } else if (IsHeading(tag)) {
                    StartBlock(a, 1);
                    Mark m{ MarkKind::Heading, Length() };
                    m.number = tag[1] - '1';
                    m_Marks.push_back(m);
                } else if (tag == "img") {
                    // ImageSpan (no image getter): the object replacement character.
                    text.push_back(u'\uFFFC');
                }
            }

            void End(const std::string& tag) {
                if (tag == "annotation") return AnnotationEnd();
                if (tag == "ul") {
                    CommitBullet();
                    --m_BulletIndentation;
                    return;
                }
                if (tag == "li") return CommitBullet();
                if (tag == "br") {
                    text.push_back(u'\n');
                } else if (tag == "p") {
                    EndCss();
                    EndBlock();
                } else if (tag == "div") {
                    EndBlock();
                } else if (tag == "span") {
                    EndCss();
                } else if (tag == "strong" || tag == "b") {
                    Close(MarkKind::Bold, Style(true, false));
                } else if (tag == "em" || tag == "cite" || tag == "dfn" || tag == "i") {
                    Close(MarkKind::Italic, Style(false, true));
                } else if (tag == "big") {
                    Close(MarkKind::Big, RelativeSize(1.25f));
                } else if (tag == "small") {
                    Close(MarkKind::Small, RelativeSize(0.8f));
                } else if (tag == "font") {
                    EndFont();
                } else if (tag == "blockquote") {
                    EndBlock();
                    Close(MarkKind::Blockquote, std::nullopt);   // QuoteSpan: nothing in Compose
                } else if (tag == "tt") {
                    Close(MarkKind::Monospace, Typeface("monospace"));
                } else if (tag == "a") {
                    const int h = Last(MarkKind::Href);
                    if (h >= 0 && m_Marks[std::size_t(h)].text) {
                        Span s{ Span::Kind::Url };
                        s.value = *m_Marks[std::size_t(h)].text;
                        SetFromMark(h, { s });
                    }
                } else if (tag == "u") {
                    Close(MarkKind::Underline, Span{ Span::Kind::Underline });
                } else if (tag == "del" || tag == "s" || tag == "strike") {
                    Close(MarkKind::Strikethrough, Span{ Span::Kind::Strikethrough });
                } else if (tag == "sup") {
                    Close(MarkKind::Super, Span{ Span::Kind::Superscript });
                } else if (tag == "sub") {
                    Close(MarkKind::Sub, Span{ Span::Kind::Subscript });
                } else if (IsHeading(tag)) {
                    // The size and weight end before the line break after the heading.
                    const int h = Last(MarkKind::Heading);
                    if (h >= 0) SetFromMark(h, { RelativeSize(kHeadingSizes[m_Marks[std::size_t(h)].number]), Style(true, false) });
                    EndBlock();
                }
            }

            void Characters(const std::u16string& chunk) {
                // Spaces and line feeds collapse to one space, none after a line break or at the start.
                std::u16string sb;
                for (const char16_t c : chunk) {
                    if (c == u' ' || c == u'\n') {
                        const char16_t pred = !sb.empty() ? sb.back() : !text.empty() ? text.back() : u'\n';
                        if (pred != u' ' && pred != u'\n') sb.push_back(u' ');
                    } else {
                        sb.push_back(c);
                    }
                }
                text += sb;
            }

            // convert(): paragraph spans lose a blank last line, and go when empty.
            void Finish() {
                for (std::size_t i = 0; i < spans.size();) {
                    Span& s = spans[i];
                    if (s.kind == Span::Kind::Alignment) {
                        if (s.end - 2 >= 0 && text[std::size_t(s.end - 1)] == u'\n' && text[std::size_t(s.end - 2)] == u'\n') --s.end;
                        if (s.end == s.start) {
                            spans.erase(spans.begin() + std::ptrdiff_t(i));
                            continue;
                        }
                    }
                    ++i;
                }
            }

        private:
            std::vector<Mark> m_Marks;
            struct PendingAnnotation {
                std::string key, value;
                int start;
            };
            std::vector<PendingAnnotation> m_Annotations;
            int m_BulletIndentation = 0;
            std::optional<std::pair<int, int>> m_Bullet;   // level, start

            int Length() const { return int(text.size()); }
            static bool IsHeading(const std::string& t) { return t.size() == 2 && t[0] == 'h' && t[1] >= '1' && t[1] <= '6'; }
            static Span Style(bool bold, bool italic) {
                Span s{ Span::Kind::Style };
                s.bold = bold;
                s.italic = italic;
                return s;
            }
            static Span RelativeSize(float size) {
                Span s{ Span::Kind::RelativeSize };
                s.size = size;
                return s;
            }
            static Span Typeface(std::string family) {
                Span s{ Span::Kind::Typeface };
                s.value = std::move(family);
                return s;
            }

            void Open(MarkKind kind) { m_Marks.push_back({ kind, Length() }); }
            int Last(MarkKind kind) const {
                for (int i = int(m_Marks.size()) - 1; i >= 0; --i)
                    if (m_Marks[std::size_t(i)].kind == kind) return i;
                return -1;
            }
            // setSpanFromMark: the spans from the mark to here, when that is not empty.
            void SetFromMark(int index, std::initializer_list<Span> replacements) {
                const int where = m_Marks[std::size_t(index)].where;
                m_Marks.erase(m_Marks.begin() + index);
                if (where == Length()) return;
                for (Span s : replacements) {
                    s.start = where;
                    s.end = Length();
                    spans.push_back(std::move(s));
                }
            }
            void Close(MarkKind kind, std::optional<Span> replacement) {
                const int i = Last(kind);
                if (i < 0) return;
                if (replacement) SetFromMark(i, { *replacement });
                else SetFromMark(i, {});
            }

            void AppendNewlines(int minimum) {
                if (text.empty()) return;
                int existing = 0;
                for (auto it = text.rbegin(); it != text.rend() && *it == u'\n'; ++it) ++existing;
                for (int j = existing; j < minimum; ++j) text.push_back(u'\n');
            }
            void StartBlock(const Attributes& a, int margin) {
                if (margin > 0) {
                    AppendNewlines(margin);
                    Mark m{ MarkKind::Newline, Length() };
                    m.number = margin;
                    m_Marks.push_back(m);
                }
                if (const auto* style = a.Get("style")) {
                    if (const auto align = CssValue(*style, "text-align")) {
                        TextAlign t = TextAlign::Unspecified;
                        if (EqualsIgnoreCase(*align, "start")) t = TextAlign::Start;
                        else if (EqualsIgnoreCase(*align, "center")) t = TextAlign::Center;
                        else if (EqualsIgnoreCase(*align, "end")) t = TextAlign::End;
                        if (t != TextAlign::Unspecified) {
                            Mark m{ MarkKind::Alignment, Length() };
                            m.align = t;
                            m_Marks.push_back(m);
                        }
                    }
                }
            }
            void EndBlock() {
                const int n = Last(MarkKind::Newline);
                if (n >= 0) {
                    AppendNewlines(m_Marks[std::size_t(n)].number);
                    m_Marks.erase(m_Marks.begin() + n);
                }
                const int a = Last(MarkKind::Alignment);
                if (a >= 0) {
                    Span s{ Span::Kind::Alignment };
                    s.align = m_Marks[std::size_t(a)].align;
                    SetFromMark(a, { s });
                }
            }
            void StartCss(const Attributes& a) {
                const auto* style = a.Get("style");
                if (!style) return;
                if (const auto v = CssValue(*style, "color")) {
                    if (const auto c = HtmlColor(*v)) {
                        Mark m{ MarkKind::Foreground, Length() };
                        m.color = *c;
                        m_Marks.push_back(m);
                    }
                }
                if (const auto v = CssValue(*style, "background(?:-color)?")) {
                    if (const auto c = HtmlColor(*v)) {
                        Mark m{ MarkKind::Background, Length() };
                        m.color = *c;
                        m_Marks.push_back(m);
                    }
                }
                if (const auto v = CssValue(*style, "text-decoration"))
                    if (EqualsIgnoreCase(*v, "line-through")) Open(MarkKind::Strikethrough);
            }
            void EndCss() {
                Close(MarkKind::Strikethrough, Span{ Span::Kind::Strikethrough });
                if (const int b = Last(MarkKind::Background); b >= 0) {
                    Span s{ Span::Kind::Background };
                    s.color = m_Marks[std::size_t(b)].color;
                    SetFromMark(b, { s });
                }
                if (const int f = Last(MarkKind::Foreground); f >= 0) {
                    Span s{ Span::Kind::Foreground };
                    s.color = m_Marks[std::size_t(f)].color;
                    SetFromMark(f, { s });
                }
            }
            void StartFont(const Attributes& a) {
                if (const auto* color = a.Get("color"); color && !color->empty()) {
                    if (const auto c = HtmlColor(*color)) {
                        Mark m{ MarkKind::Foreground, Length() };
                        m.color = *c;
                        m_Marks.push_back(m);
                    }
                }
                if (const auto* face = a.Get("face"); face && !face->empty()) {
                    Mark m{ MarkKind::Font, Length() };
                    m.text = *face;
                    m_Marks.push_back(std::move(m));
                }
            }
            void EndFont() {
                if (const int f = Last(MarkKind::Font); f >= 0) SetFromMark(f, { Typeface(*m_Marks[std::size_t(f)].text) });
                if (const int c = Last(MarkKind::Foreground); c >= 0) {
                    Span s{ Span::Kind::Foreground };
                    s.color = m_Marks[std::size_t(c)].color;
                    SetFromMark(c, { s });
                }
            }

            // handleAnnotationStart / End: each key=value pair with both set becomes a string
            // annotation over the text up to the next closing </annotation>, when there is any.
            void AnnotationStart(const Attributes& a) {
                for (const auto& [k, v] : a.list)
                    if (!k.empty() && !v.empty()) m_Annotations.push_back({ k, v, Length() });
            }
            void AnnotationEnd() {
                for (auto& p : m_Annotations) {
                    if (p.start == Length()) continue;
                    Span s{ Span::Kind::Annotation };
                    s.start = p.start;
                    s.end = Length();
                    s.key = std::move(p.key);
                    s.value = std::move(p.value);
                    spans.push_back(std::move(s));
                }
                m_Annotations.clear();
            }
            void CommitBullet() {
                if (!m_Bullet) return;
                Span s{ Span::Kind::Bullet };
                s.level = m_Bullet->first;
                s.start = m_Bullet->second;
                s.end = Length();
                spans.push_back(s);
                m_Bullet.reset();
            }
        };

        // --- the lenient parse -----------------------------------------------------------------

        bool IsVoid(const std::string& t) {
            static const char* const names[] = { "area", "base", "basefont", "br", "col", "frame", "hr", "img", "input", "isindex", "link", "meta", "param", "wbr" };
            for (const char* n : names) if (t == n) return true;
            return false;
        }
        // Block elements an open <p> cannot hold (TagSoup closes the paragraph first).
        bool ClosesParagraph(const std::string& t) {
            static const char* const names[] = { "address", "blockquote", "center", "dir", "div", "dl", "fieldset", "form", "h1", "h2", "h3",
                                                 "h4", "h5", "h6", "hr", "li", "menu", "ol", "p", "pre", "table", "ul" };
            for (const char* n : names) if (t == n) return true;
            return false;
        }
        bool IsList(const std::string& t) { return t == "ul" || t == "ol" || t == "menu" || t == "dir"; }
        bool HoldsParagraph(const std::string& t) {
            return t == "div" || t == "li" || t == "blockquote" || t == "td" || t == "th" || t == "dd" || t == "center" || t == "form" || IsList(t);
        }
        bool IsNameChar(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == ':' || c == '.'; }

        class Parser {
        public:
            explicit Parser(Converter& c) : m_Converter(c) {}

            void Parse(std::string_view s) {
                std::size_t i = 0, textStart = 0;
                auto flush = [&](std::size_t end) {
                    if (end > textStart) Text(s.substr(textStart, end - textStart));
                };
                while (i < s.size()) {
                    if (s[i] != '<' || i + 1 >= s.size()) {
                        ++i;
                        continue;
                    }
                    const char n = s[i + 1];
                    if (s.compare(i, 4, "<!--") == 0) {
                        flush(i);
                        const std::size_t e = s.find("-->", i + 4);
                        i = textStart = e == std::string_view::npos ? s.size() : e + 3;
                    } else if (n == '!' || n == '?') {
                        flush(i);
                        const std::size_t e = s.find('>', i + 2);
                        i = textStart = e == std::string_view::npos ? s.size() : e + 1;
                    } else if (n == '/' && i + 2 < s.size() && std::isalpha(static_cast<unsigned char>(s[i + 2]))) {
                        flush(i);
                        std::size_t j = i + 2;
                        while (j < s.size() && IsNameChar(s[j])) ++j;
                        const std::string name = Lower(s.substr(i + 2, j - i - 2));
                        const std::size_t e = s.find('>', j);
                        i = textStart = e == std::string_view::npos ? s.size() : e + 1;
                        EndTag(name);
                    } else if (std::isalpha(static_cast<unsigned char>(n))) {
                        flush(i);
                        std::size_t j = i + 1;
                        while (j < s.size() && IsNameChar(s[j])) ++j;
                        const std::string name = Lower(s.substr(i + 1, j - i - 1));
                        Attributes attrs;
                        bool selfClosing = false;
                        j = ParseAttributes(s, j, attrs, selfClosing);
                        i = textStart = j;
                        StartTag(name, attrs, selfClosing);
                        if (!selfClosing && (name == "script" || name == "style")) {
                            // CDATA content: text up to the matching end tag.
                            std::size_t e = j;
                            while (true) {
                                e = s.find("</", e);
                                if (e == std::string_view::npos || EqualsIgnoreCase(s.substr(e + 2, name.size()), name)) break;
                                e += 2;
                            }
                            const std::size_t stop = e == std::string_view::npos ? s.size() : e;
                            if (stop > j) {
                                std::u16string raw;
                                for (std::size_t k = j; k < stop;) AppendUtf16(raw, DecodeUtf8(s, k));
                                m_Converter.Characters(raw);
                            }
                            i = textStart = stop;
                        }
                    } else {
                        ++i;   // a literal '<'
                    }
                }
                flush(s.size());
                while (!m_Stack.empty()) Pop();
            }

        private:
            Converter& m_Converter;
            std::vector<std::string> m_Stack;

            void Text(std::string_view raw) {
                std::u16string decoded;
                DecodeText(raw, decoded);
                m_Converter.Characters(decoded);
            }

            static std::size_t ParseAttributes(std::string_view s, std::size_t j, Attributes& attrs, bool& selfClosing) {
                while (j < s.size()) {
                    while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
                    if (j >= s.size()) break;
                    if (s[j] == '>') return j + 1;
                    if (s[j] == '/') {
                        ++j;
                        while (j < s.size() && std::isspace(static_cast<unsigned char>(s[j]))) ++j;
                        if (j < s.size() && s[j] == '>') {
                            selfClosing = true;
                            return j + 1;
                        }
                        continue;
                    }
                    const std::size_t nameStart = j;
                    while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j])) && s[j] != '=' && s[j] != '>' && s[j] != '/') ++j;
                    if (j == nameStart) {
                        ++j;
                        continue;
                    }
                    const std::string name = Lower(s.substr(nameStart, j - nameStart));
                    std::size_t k = j;
                    while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
                    std::string value = name;   // a bare attribute: its name (TagSoup)
                    if (k < s.size() && s[k] == '=') {
                        ++k;
                        while (k < s.size() && std::isspace(static_cast<unsigned char>(s[k]))) ++k;
                        std::size_t vs = k, ve;
                        if (k < s.size() && (s[k] == '"' || s[k] == '\'')) {
                            const char q = s[k];
                            vs = k + 1;
                            ve = s.find(q, vs);
                            if (ve == std::string_view::npos) ve = s.size();
                            k = ve < s.size() ? ve + 1 : ve;
                        } else {
                            while (k < s.size() && !std::isspace(static_cast<unsigned char>(s[k])) && s[k] != '>') ++k;
                            ve = k;
                        }
                        std::u16string decoded;
                        DecodeText(s.substr(vs, ve - vs), decoded);
                        value = ToUtf8(decoded);
                        j = k;
                    }
                    attrs.list.emplace_back(name, std::move(value));
                }
                return j;
            }

            void Pop() {
                const std::string name = std::move(m_Stack.back());
                m_Stack.pop_back();
                m_Converter.End(name);
            }
            void PopThrough(std::size_t index) {
                while (m_Stack.size() > index) Pop();
            }

            void StartTag(const std::string& name, const Attributes& attrs, bool selfClosing) {
                if (ClosesParagraph(name)) {
                    for (std::size_t k = m_Stack.size(); k-- > 0;) {
                        if (m_Stack[k] == "p") {
                            PopThrough(k);
                            break;
                        }
                        if (HoldsParagraph(m_Stack[k])) break;
                    }
                }
                if (name == "li") {
                    // The previous item closes; an item outside a list gets an implied <ul>.
                    std::optional<std::size_t> holder;
                    for (std::size_t k = m_Stack.size(); k-- > 0;) {
                        if (m_Stack[k] == "li") {
                            PopThrough(k);
                            holder = k;
                            break;
                        }
                        if (IsList(m_Stack[k])) {
                            PopThrough(k + 1);
                            holder = k;
                            break;
                        }
                    }
                    if (!holder) {
                        m_Converter.Start("ul", {});
                        m_Stack.push_back("ul");
                    }
                }
                m_Converter.Start(name, attrs);
                if (selfClosing || IsVoid(name)) m_Converter.End(name);
                else m_Stack.push_back(name);
            }

            void EndTag(const std::string& name) {
                if (IsVoid(name)) return;
                for (std::size_t k = m_Stack.size(); k-- > 0;) {
                    if (m_Stack[k] == name) {
                        PopThrough(k);
                        return;
                    }
                }
            }
        };

    }

    AnnotatedString FromHtml(std::string_view html, const TextLinkStyles& linkStyles, LinkInteractionListener listener) {
        Converter c;
        Parser(c).Parse(html);
        c.Finish();
        AnnotatedStringBuilder b;
        b.Append(ToUtf8(c.text));
        for (const Span& s : c.spans) {
            SpanStyle style;
            switch (s.kind) {
                case Span::Kind::Alignment: {
                    ParagraphStyle p;
                    p.textAlign = s.align;
                    b.AddStyle(p, s.start, s.end);
                    continue;
                }
                case Span::Kind::Annotation: b.AddStringAnnotation(s.key, s.value, s.start, s.end); continue;
                case Span::Kind::Bullet:
                    b.AddBullet(Bullet::Default(), Em(Bullet::DefaultIndentation().value * float(s.level)), s.start, s.end);
                    continue;
                case Span::Kind::Url: b.AddLink(LinkAnnotation::Url(s.value, linkStyles, listener), s.start, s.end); continue;
                case Span::Kind::Background: style.background = Color(s.color); break;
                case Span::Kind::Foreground: style.color = Color(s.color); break;
                case Span::Kind::RelativeSize: style.fontSize = Em(s.size); break;
                case Span::Kind::Strikethrough: style.textDecoration = TextDecoration::LineThrough; break;
                case Span::Kind::Underline: style.textDecoration = TextDecoration::Underline; break;
                case Span::Kind::Subscript: style.baselineShift = BaselineShift::Subscript(); break;
                case Span::Kind::Superscript: style.baselineShift = BaselineShift::Superscript(); break;
                case Span::Kind::Style:
                    if (s.bold) style.fontWeight = FontWeight::Bold();
                    if (s.italic) style.fontStyle = FontStyle::Italic;
                    break;
                case Span::Kind::Typeface:
                    // TypefaceSpan.toSpanStyle: the generic names, else an installed family by name.
                    if (s.value == "cursive") style.fontFamily = FontFamily::Cursive();
                    else if (s.value == "monospace") style.fontFamily = FontFamily::Monospace();
                    else if (s.value == "sans-serif") style.fontFamily = FontFamily::SansSerif();
                    else if (s.value == "serif") style.fontFamily = FontFamily::Serif();
                    else style.fontFamily = FontFamily::Of({ Font::Device(s.value) });
                    break;
            }
            b.AddStyle(style, s.start, s.end);
        }
        return b.ToAnnotatedString();
    }

}
