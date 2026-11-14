#include "kiln/media/Webvtt.h"

#include "kiln/text/TextStyle.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <stdexcept>
#include <string_view>
#include <unordered_map>

namespace kiln::media {

    namespace {
        // --- Java's parsing, as media3 relies on it --------------------------------------------

        bool IsJavaSpace(char16_t c) { return c == ' ' || c == '\t' || c == '\n' || c == 0x0B || c == '\f' || c == '\r'; }

        // Long.parseLong / Integer.parseInt: an optional sign and decimal digits.
        std::int64_t JavaParseLong(std::string_view s, std::int64_t min = INT64_MIN, std::int64_t max = INT64_MAX) {
            std::size_t i = 0;
            bool negative = false;
            if (!s.empty() && (s[0] == '-' || s[0] == '+')) { negative = s[0] == '-'; i = 1; }
            if (i == s.size()) throw std::invalid_argument("not a number");
            unsigned long long v = 0;
            for (; i < s.size(); ++i) {
                if (s[i] < '0' || s[i] > '9') throw std::invalid_argument("not a number");
                v = v * 10 + unsigned(s[i] - '0');
                if (v > 9223372036854775808ULL) throw std::invalid_argument("out of range");
            }
            const long double value = negative ? -static_cast<long double>(v) : static_cast<long double>(v);
            if (value < static_cast<long double>(min) || value > static_cast<long double>(max)) throw std::invalid_argument("out of range");
            return negative ? static_cast<std::int64_t>(0 - v) : static_cast<std::int64_t>(v);
        }
        int JavaParseInt(std::string_view s) { return int(JavaParseLong(s, INT32_MIN, INT32_MAX)); }

        // Float.parseFloat: trimmed; a signed decimal with an optional exponent and f / d suffix,
        // NaN or Infinity.
        float JavaParseFloat(std::string_view raw) {
            const std::string_view s = JavaTrim(raw);
            std::size_t i = 0;
            if (i < s.size() && (s[i] == '+' || s[i] == '-')) ++i;
            const std::string_view body = s.substr(i);
            if (body == "NaN") return NAN;
            if (body == "Infinity") return s[0] == '-' ? -INFINITY : INFINITY;
            std::size_t digits = 0;
            std::size_t j = i;
            while (j < s.size() && s[j] >= '0' && s[j] <= '9') { ++j; ++digits; }
            if (j < s.size() && s[j] == '.') {
                ++j;
                while (j < s.size() && s[j] >= '0' && s[j] <= '9') { ++j; ++digits; }
            }
            if (digits == 0) throw std::invalid_argument("not a float");
            if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                ++j;
                if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
                std::size_t e = 0;
                while (j < s.size() && s[j] >= '0' && s[j] <= '9') { ++j; ++e; }
                if (e == 0) throw std::invalid_argument("not a float");
            }
            const std::size_t numberEnd = j;
            if (j < s.size() && (s[j] == 'f' || s[j] == 'F' || s[j] == 'd' || s[j] == 'D')) ++j;
            if (j != s.size()) throw std::invalid_argument("not a float");
            return std::strtof(std::string(s.substr(0, numberEnd)).c_str(), nullptr);
        }

        // Util.split(value, regex) for a one-character separator: String.split with limit -1.
        std::vector<std::string> SplitAll(std::string_view s, char sep) {
            std::vector<std::string> parts;
            std::size_t from = 0;
            for (;;) {
                const std::size_t at = s.find(sep, from);
                if (at == std::string_view::npos) { parts.emplace_back(s.substr(from)); break; }
                parts.emplace_back(s.substr(from, at - from));
                from = at + 1;
            }
            return parts;
        }

        std::string AsciiLower(std::string_view s) {
            std::string out(s);
            for (char& c : out) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
            return out;
        }

        // Java's String.hashCode, for HashSet<String>'s iteration order.
        std::int32_t JavaHash(std::u16string_view s) {
            std::uint32_t h = 0;
            for (char16_t c : s) h = 31 * h + c;
            return std::int32_t(h);
        }

        // Color.rgb / Color.argb (no clamping, as Android's).
        std::uint32_t Argb(int a, int r, int g, int b) { return (std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b); }
        std::uint32_t Rgb(int r, int g, int b) { return 0xFF000000u | (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | std::uint32_t(b); }

        const std::unordered_map<std::string, std::uint32_t>& CssColors() {
            static const std::unordered_map<std::string, std::uint32_t> colors = {
                { "aliceblue", 0xFFF0F8FFu },
                { "antiquewhite", 0xFFFAEBD7u },
                { "aqua", 0xFF00FFFFu },
                { "aquamarine", 0xFF7FFFD4u },
                { "azure", 0xFFF0FFFFu },
                { "beige", 0xFFF5F5DCu },
                { "bisque", 0xFFFFE4C4u },
                { "black", 0xFF000000u },
                { "blanchedalmond", 0xFFFFEBCDu },
                { "blue", 0xFF0000FFu },
                { "blueviolet", 0xFF8A2BE2u },
                { "brown", 0xFFA52A2Au },
                { "burlywood", 0xFFDEB887u },
                { "cadetblue", 0xFF5F9EA0u },
                { "chartreuse", 0xFF7FFF00u },
                { "chocolate", 0xFFD2691Eu },
                { "coral", 0xFFFF7F50u },
                { "cornflowerblue", 0xFF6495EDu },
                { "cornsilk", 0xFFFFF8DCu },
                { "crimson", 0xFFDC143Cu },
                { "cyan", 0xFF00FFFFu },
                { "darkblue", 0xFF00008Bu },
                { "darkcyan", 0xFF008B8Bu },
                { "darkgoldenrod", 0xFFB8860Bu },
                { "darkgray", 0xFFA9A9A9u },
                { "darkgreen", 0xFF006400u },
                { "darkgrey", 0xFFA9A9A9u },
                { "darkkhaki", 0xFFBDB76Bu },
                { "darkmagenta", 0xFF8B008Bu },
                { "darkolivegreen", 0xFF556B2Fu },
                { "darkorange", 0xFFFF8C00u },
                { "darkorchid", 0xFF9932CCu },
                { "darkred", 0xFF8B0000u },
                { "darksalmon", 0xFFE9967Au },
                { "darkseagreen", 0xFF8FBC8Fu },
                { "darkslateblue", 0xFF483D8Bu },
                { "darkslategray", 0xFF2F4F4Fu },
                { "darkslategrey", 0xFF2F4F4Fu },
                { "darkturquoise", 0xFF00CED1u },
                { "darkviolet", 0xFF9400D3u },
                { "deeppink", 0xFFFF1493u },
                { "deepskyblue", 0xFF00BFFFu },
                { "dimgray", 0xFF696969u },
                { "dimgrey", 0xFF696969u },
                { "dodgerblue", 0xFF1E90FFu },
                { "firebrick", 0xFFB22222u },
                { "floralwhite", 0xFFFFFAF0u },
                { "forestgreen", 0xFF228B22u },
                { "fuchsia", 0xFFFF00FFu },
                { "gainsboro", 0xFFDCDCDCu },
                { "ghostwhite", 0xFFF8F8FFu },
                { "gold", 0xFFFFD700u },
                { "goldenrod", 0xFFDAA520u },
                { "gray", 0xFF808080u },
                { "green", 0xFF008000u },
                { "greenyellow", 0xFFADFF2Fu },
                { "grey", 0xFF808080u },
                { "honeydew", 0xFFF0FFF0u },
                { "hotpink", 0xFFFF69B4u },
                { "indianred", 0xFFCD5C5Cu },
                { "indigo", 0xFF4B0082u },
                { "ivory", 0xFFFFFFF0u },
                { "khaki", 0xFFF0E68Cu },
                { "lavender", 0xFFE6E6FAu },
                { "lavenderblush", 0xFFFFF0F5u },
                { "lawngreen", 0xFF7CFC00u },
                { "lemonchiffon", 0xFFFFFACDu },
                { "lightblue", 0xFFADD8E6u },
                { "lightcoral", 0xFFF08080u },
                { "lightcyan", 0xFFE0FFFFu },
                { "lightgoldenrodyellow", 0xFFFAFAD2u },
                { "lightgray", 0xFFD3D3D3u },
                { "lightgreen", 0xFF90EE90u },
                { "lightgrey", 0xFFD3D3D3u },
                { "lightpink", 0xFFFFB6C1u },
                { "lightsalmon", 0xFFFFA07Au },
                { "lightseagreen", 0xFF20B2AAu },
                { "lightskyblue", 0xFF87CEFAu },
                { "lightslategray", 0xFF778899u },
                { "lightslategrey", 0xFF778899u },
                { "lightsteelblue", 0xFFB0C4DEu },
                { "lightyellow", 0xFFFFFFE0u },
                { "lime", 0xFF00FF00u },
                { "limegreen", 0xFF32CD32u },
                { "linen", 0xFFFAF0E6u },
                { "magenta", 0xFFFF00FFu },
                { "maroon", 0xFF800000u },
                { "mediumaquamarine", 0xFF66CDAAu },
                { "mediumblue", 0xFF0000CDu },
                { "mediumorchid", 0xFFBA55D3u },
                { "mediumpurple", 0xFF9370DBu },
                { "mediumseagreen", 0xFF3CB371u },
                { "mediumslateblue", 0xFF7B68EEu },
                { "mediumspringgreen", 0xFF00FA9Au },
                { "mediumturquoise", 0xFF48D1CCu },
                { "mediumvioletred", 0xFFC71585u },
                { "midnightblue", 0xFF191970u },
                { "mintcream", 0xFFF5FFFAu },
                { "mistyrose", 0xFFFFE4E1u },
                { "moccasin", 0xFFFFE4B5u },
                { "navajowhite", 0xFFFFDEADu },
                { "navy", 0xFF000080u },
                { "oldlace", 0xFFFDF5E6u },
                { "olive", 0xFF808000u },
                { "olivedrab", 0xFF6B8E23u },
                { "orange", 0xFFFFA500u },
                { "orangered", 0xFFFF4500u },
                { "orchid", 0xFFDA70D6u },
                { "palegoldenrod", 0xFFEEE8AAu },
                { "palegreen", 0xFF98FB98u },
                { "paleturquoise", 0xFFAFEEEEu },
                { "palevioletred", 0xFFDB7093u },
                { "papayawhip", 0xFFFFEFD5u },
                { "peachpuff", 0xFFFFDAB9u },
                { "peru", 0xFFCD853Fu },
                { "pink", 0xFFFFC0CBu },
                { "plum", 0xFFDDA0DDu },
                { "powderblue", 0xFFB0E0E6u },
                { "purple", 0xFF800080u },
                { "rebeccapurple", 0xFF663399u },
                { "red", 0xFFFF0000u },
                { "rosybrown", 0xFFBC8F8Fu },
                { "royalblue", 0xFF4169E1u },
                { "saddlebrown", 0xFF8B4513u },
                { "salmon", 0xFFFA8072u },
                { "sandybrown", 0xFFF4A460u },
                { "seagreen", 0xFF2E8B57u },
                { "seashell", 0xFFFFF5EEu },
                { "sienna", 0xFFA0522Du },
                { "silver", 0xFFC0C0C0u },
                { "skyblue", 0xFF87CEEBu },
                { "slateblue", 0xFF6A5ACDu },
                { "slategray", 0xFF708090u },
                { "slategrey", 0xFF708090u },
                { "snow", 0xFFFFFAFAu },
                { "springgreen", 0xFF00FF7Fu },
                { "steelblue", 0xFF4682B4u },
                { "tan", 0xFFD2B48Cu },
                { "teal", 0xFF008080u },
                { "thistle", 0xFFD8BFD8u },
                { "tomato", 0xFFFF6347u },
                { "transparent", 0x00000000u },
                { "turquoise", 0xFF40E0D0u },
                { "violet", 0xFFEE82EEu },
                { "wheat", 0xFFF5DEB3u },
                { "white", 0xFFFFFFFFu },
                { "whitesmoke", 0xFFF5F5F5u },
                { "yellow", 0xFFFFFF00u },
                { "yellowgreen", 0xFF9ACD32u },
            };
            return colors;
        }

        // Digits only (\d{1,3} and friends), count in [min, max].
        bool AllDigits(std::string_view s, std::size_t min, std::size_t max) {
            if (s.size() < min || s.size() > max) return false;
            for (char c : s) if (c < '0' || c > '9') return false;
            return true;
        }
    }

    std::uint32_t ParseCssColor(std::string_view raw) {
        if (raw.empty()) throw std::invalid_argument("empty colour");
        std::string e;
        for (char c : raw) if (c != ' ') e += c;
        if (e.empty()) throw std::invalid_argument("empty colour");
        if (e[0] == '#') {
            // Parsed as a long so #80000000 and up don't fail.
            const std::string_view hex = std::string_view(e).substr(1);
            if (hex.empty()) throw std::invalid_argument("bad colour");
            std::size_t i = 0;
            bool negative = false;
            if (hex[0] == '-' || hex[0] == '+') { negative = hex[0] == '-'; i = 1; }
            if (i == hex.size()) throw std::invalid_argument("bad colour");
            std::uint64_t v = 0;
            for (; i < hex.size(); ++i) {
                const char c = hex[i];
                int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (d < 0) throw std::invalid_argument("bad colour");
                v = v * 16 + std::uint64_t(d);
                if (v > 0x7FFFFFFFFFFFFFFFULL) throw std::invalid_argument("bad colour");
            }
            std::uint32_t color = std::uint32_t(negative ? 0 - v : v);
            if (e.size() == 7) color |= 0xFF000000u;
            else if (e.size() == 9) color = ((color & 0xFF) << 24) | (color >> 8);   // #RRGGBBAA to AARRGGBB
            else throw std::invalid_argument("bad colour");
            return color;
        }
        auto inside = [&](std::string_view prefix) -> std::optional<std::vector<std::string>> {
            const std::string_view v(e);
            if (!v.starts_with(prefix) || v.size() < prefix.size() + 1 || v.back() != ')') return std::nullopt;
            return SplitAll(v.substr(prefix.size(), v.size() - prefix.size() - 1), ',');
        };
        if (e.starts_with("rgba")) {
            // ^rgba\((\d{1,3}),(\d{1,3}),(\d{1,3}),(\d*\.?\d*?)\)$, the alpha 0 to 1.
            if (auto p = inside("rgba("); p && p->size() == 4 && AllDigits((*p)[0], 1, 3) && AllDigits((*p)[1], 1, 3) && AllDigits((*p)[2], 1, 3)) {
                const std::string& a = (*p)[3];
                const std::size_t dot = a.find('.');
                const bool alphaOk = dot == std::string::npos ? AllDigits(a, 0, a.size())
                                                              : AllDigits(std::string_view(a).substr(0, dot), 0, dot) && AllDigits(std::string_view(a).substr(dot + 1), 0, a.size());
                if (alphaOk)
                    return Argb(int(255 * JavaParseFloat(a)), std::stoi((*p)[0]), std::stoi((*p)[1]), std::stoi((*p)[2]));
            }
        } else if (e.starts_with("rgb")) {
            if (auto p = inside("rgb("); p && p->size() == 3 && AllDigits((*p)[0], 1, 3) && AllDigits((*p)[1], 1, 3) && AllDigits((*p)[2], 1, 3))
                return Rgb(std::stoi((*p)[0]), std::stoi((*p)[1]), std::stoi((*p)[2]));
        } else if (auto it = CssColors().find(AsciiLower(e)); it != CssColors().end()) {
            return it->second;
        }
        throw std::invalid_argument("bad colour");
    }

    // --- WebvttCssStyle --------------------------------------------------------------------------

    int WebvttCssStyle::GetSpecificityScore(const std::string* id, const std::string& tag, const std::set<std::string>& classes,
                                            const std::string& voice) const {
        if (targetId.empty() && targetTag.empty() && targetClasses.empty() && targetVoice.empty())
            return tag.empty() ? 1 : 0;   // universal: only the whole cue
        int score = 0;
        auto update = [&](const std::string& target, const std::string* actual, int add) {
            if (target.empty() || score == -1) return;
            score = actual && target == *actual ? score + add : -1;
        };
        update(targetId, id, 0x40000000);
        update(targetTag, &tag, 2);
        update(targetVoice, &voice, 4);
        if (score == -1 || !std::includes(classes.begin(), classes.end(), targetClasses.begin(), targetClasses.end())) return 0;
        return score + int(targetClasses.size()) * 4;
    }

    int WebvttCssStyle::GetStyle() const {
        if (bold == kUnspecified && italic == kUnspecified) return kUnspecified;
        return (bold == 1 ? kStyleBold : kStyleNormal) | (italic == 1 ? kStyleItalic : kStyleNormal);
    }

    // --- WebvttCssParser -------------------------------------------------------------------------

    namespace {
        // ParsableByteArray over the block.
        struct CssInput {
            std::string_view data;
            std::size_t pos = 0;
            std::size_t Left() const { return data.size() - pos; }
        };

        bool MaybeSkipWhitespace(CssInput& in) {
            switch (in.data[in.pos]) {
                case '\t': case '\r': case '\n': case '\f': case ' ': ++in.pos; return true;
                default: return false;
            }
        }
        bool MaybeSkipComment(CssInput& in) {
            std::size_t position = in.pos, limit = in.data.size();
            if (position + 2 <= limit && in.data[position++] == '/' && in.data[position++] == '*') {
                while (position + 1 < limit) {
                    const char skipped = in.data[position++];
                    if (skipped == '*' && in.data[position] == '/') {
                        ++position;
                        limit = position;
                    }
                }
                in.pos = limit;
                return true;
            }
            return false;
        }
        void SkipWhitespaceAndComments(CssInput& in) {
            bool skipping = true;
            while (in.Left() > 0 && skipping) skipping = MaybeSkipWhitespace(in) || MaybeSkipComment(in);
        }
        std::string ParseIdentifier(CssInput& in) {
            std::string id;
            while (in.pos < in.data.size()) {
                const char c = in.data[in.pos];
                if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '#' || c == '-' || c == '.' || c == '_') {
                    id += c;
                    ++in.pos;
                } else {
                    break;
                }
            }
            return id;
        }
        std::optional<std::string> ParseNextToken(CssInput& in) {
            SkipWhitespaceAndComments(in);
            if (in.Left() == 0) return std::nullopt;
            std::string id = ParseIdentifier(in);
            if (!id.empty()) return id;
            return std::string(1, in.data[in.pos++]);   // a delimiter
        }
        std::optional<std::string> ParsePropertyValue(CssInput& in) {
            std::string expression;
            for (;;) {
                const std::size_t position = in.pos;
                auto token = ParseNextToken(in);
                if (!token) return std::nullopt;   // syntax error
                if (*token == "}" || *token == ";") {
                    in.pos = position;
                    return expression;
                }
                expression += *token;
            }
        }
        std::string ReadCueTarget(CssInput& in) {
            std::size_t position = in.pos;
            bool end = false;
            while (position < in.data.size() && !end) end = in.data[position++] == ')';
            --position;   // gives ')' back
            std::string target(JavaTrim(in.data.substr(in.pos, position - in.pos)));
            in.pos = position;
            return target;
        }
        std::optional<std::string> ParseSelector(CssInput& in) {
            SkipWhitespaceAndComments(in);
            if (in.Left() < 5) return std::nullopt;
            if (in.data.substr(in.pos, 5) != "::cue") return std::nullopt;
            in.pos += 5;
            const std::size_t position = in.pos;
            auto token = ParseNextToken(in);
            if (!token) return std::nullopt;
            if (*token == "{") {
                in.pos = position;
                return std::string();
            }
            std::optional<std::string> target;
            if (*token == "(") target = ReadCueTarget(in);
            token = ParseNextToken(in);
            if (!token || *token != ")") return std::nullopt;
            return target;   // nullopt when there was no "(": the rule is dropped (media3 returns null)
        }
        void ParseFontSize(const std::string& value, WebvttCssStyle& style) {
            // ^((?:[0-9]*\.)?[0-9]+)(px|em|%)$
            const std::string v = AsciiLower(value);
            std::string_view number(v), unit;
            if (number.ends_with("px")) { unit = "px"; number.remove_suffix(2); }
            else if (number.ends_with("em")) { unit = "em"; number.remove_suffix(2); }
            else if (number.ends_with("%")) { unit = "%"; number.remove_suffix(1); }
            else return;   // invalid font-size
            const std::size_t dot = number.find('.');
            const bool ok = dot == std::string_view::npos ? AllDigits(number, 1, number.size())
                                                         : AllDigits(number.substr(0, dot), 0, dot) && AllDigits(number.substr(dot + 1), 1, number.size());
            if (!ok) return;
            style.fontSizeUnit = unit == "px" ? WebvttCssStyle::kFontSizeUnitPixel : unit == "em" ? WebvttCssStyle::kFontSizeUnitEm : WebvttCssStyle::kFontSizeUnitPercent;
            style.fontSize = JavaParseFloat(number);
        }
        void ParseStyleDeclaration(CssInput& in, WebvttCssStyle& style) {
            SkipWhitespaceAndComments(in);
            const std::string property = ParseIdentifier(in);
            if (property.empty()) return;
            auto colon = ParseNextToken(in);
            if (!colon || *colon != ":") return;
            SkipWhitespaceAndComments(in);
            auto value = ParsePropertyValue(in);
            if (!value || value->empty()) return;
            const std::size_t position = in.pos;
            auto token = ParseNextToken(in);
            if (token && *token == ";") {
                // well formed
            } else if (token && *token == "}") {
                in.pos = position;   // well formed; the closing bracket goes back
            } else {
                return;
            }
            const std::string& v = *value;
            if (property == "color") style.fontColor = ParseCssColor(v);
            else if (property == "background-color") style.backgroundColor = ParseCssColor(v);
            else if (property == "ruby-position") {
                if (v == "over") style.rubyPosition = kTextAnnotationPositionBefore;
                else if (v == "under") style.rubyPosition = kTextAnnotationPositionAfter;
            } else if (property == "text-combine-upright") style.combineUpright = v == "all" || v.starts_with("digits");
            else if (property == "text-decoration") { if (v == "underline") style.underline = 1; }
            else if (property == "font-family") style.fontFamily = AsciiLower(v);
            else if (property == "font-weight") { if (v == "bold") style.bold = 1; }
            else if (property == "font-style") { if (v == "italic") style.italic = 1; }
            else if (property == "font-size") ParseFontSize(v, style);
        }
        void ApplySelectorToStyle(WebvttCssStyle& style, std::string selector) {
            if (selector.empty()) return;   // universal
            if (const std::size_t voiceStart = selector.find('['); voiceStart != std::string::npos) {
                // \[voice="([^"]*)"\]
                const std::string_view v = std::string_view(selector).substr(voiceStart);
                constexpr std::string_view open = "[voice=\"";
                if (v.starts_with(open) && v.ends_with("\"]") && v.size() >= open.size() + 2) {
                    const std::string_view name = v.substr(open.size(), v.size() - open.size() - 2);
                    if (name.find('"') == std::string_view::npos) style.targetVoice = std::string(name);
                }
                selector.resize(voiceStart);
            }
            const auto classes = SplitAll(selector, '.');
            const std::string& tagAndId = classes[0];
            if (const std::size_t hash = tagAndId.find('#'); hash != std::string::npos) {
                style.targetTag = tagAndId.substr(0, hash);
                style.targetId = tagAndId.substr(hash + 1);
            } else {
                style.targetTag = tagAndId;
            }
            if (classes.size() > 1) style.targetClasses = std::set<std::string>(classes.begin() + 1, classes.end());
        }
    }

    std::vector<WebvttCssStyle> ParseWebvttCssBlock(std::string_view block) {
        CssInput in{ block };
        std::vector<WebvttCssStyle> styles;
        for (;;) {
            auto selector = ParseSelector(in);
            if (!selector) break;
            auto open = ParseNextToken(in);
            if (!open || *open != "{") return styles;
            WebvttCssStyle style;
            ApplySelectorToStyle(style, *selector);
            std::optional<std::string> token;
            bool blockEnd = false;
            while (!blockEnd) {
                const std::size_t position = in.pos;
                token = ParseNextToken(in);
                blockEnd = !token || *token == "}";
                if (!blockEnd) {
                    in.pos = position;
                    ParseStyleDeclaration(in, style);
                }
            }
            if (token && *token == "}") styles.push_back(std::move(style));
        }
        return styles;
    }

    // --- WebvttCueParser ---------------------------------------------------------------------------

    namespace {
        // SpannableStringBuilder with the spans media3 sets (UTF-16, so positions are Java's).
        struct Span {
            enum class Kind { Style, Underline, Strikethrough, Foreground, Background, Typeface, AbsoluteSize, RelativeSize, TextCombine, Ruby, Voice };
            Kind kind;
            int start = 0, end = 0;
            int style = 0;                 // Style: bold 1 | italic 2
            std::uint32_t color = 0;
            float size = 0;                // AbsoluteSize (px, dip), RelativeSize
            std::string value;             // Typeface family, ruby text, voice
            int position = kTextAnnotationPositionUnknown;   // Ruby
        };
        struct Spannable {
            std::u16string text;
            std::vector<Span> spans;
            int Length() const { return int(text.size()); }
            void SetSpan(Span s, int start, int end) {
                s.start = start;
                s.end = end;
                spans.push_back(std::move(s));
            }
            // SpanUtil.addOrReplaceSpan: spans of the same kind on exactly this range go first.
            void AddOrReplaceSpan(Span s, int start, int end) {
                std::erase_if(spans, [&](const Span& o) { return o.kind == s.kind && o.start == start && o.end == end; });
                SetSpan(std::move(s), start, end);
            }
            // delete(start, end): spans after it move back, those inside shrink to it.
            void Delete(int start, int end) {
                const int n = end - start;
                text.erase(std::size_t(start), std::size_t(n));
                auto adjust = [&](int p) { return p <= start ? p : p >= end ? p - n : start; };
                for (Span& s : spans) {
                    s.start = adjust(s.start);
                    s.end = adjust(s.end);
                }
            }
        };

        struct StartTag {
            std::string name;
            int position = 0;
            std::string voice;
            std::vector<std::string> classes;   // in HashSet iteration order
            std::set<std::string> classSet;
        };
        struct Element {
            StartTag startTag;
            int endPosition = 0;
        };

        // HashSet<String>'s iteration order: by bucket (the spread hash of a table sized for the
        // set), and in insertion order within a bucket.
        std::vector<std::string> HashSetOrder(const std::vector<std::string>& inserted) {
            std::vector<std::string> unique;
            for (const auto& s : inserted)
                if (std::find(unique.begin(), unique.end(), s) == unique.end()) unique.push_back(s);
            std::uint32_t capacity = 16;
            while (double(unique.size()) > capacity * 0.75) capacity *= 2;
            auto bucket = [&](const std::string& s) {
                const std::uint32_t h = std::uint32_t(JavaHash(detail::Utf8To16(s)));
                return (h ^ (h >> 16)) & (capacity - 1);
            };
            std::stable_sort(unique.begin(), unique.end(), [&](const std::string& a, const std::string& b) { return bucket(a) < bucket(b); });
            return unique;
        }

        StartTag BuildStartTag(std::string_view fullTagExpression, int position) {
            std::string_view expr = JavaTrim(fullTagExpression);
            StartTag tag;
            tag.position = position;
            if (const std::size_t voiceStart = expr.find(' '); voiceStart != std::string_view::npos) {
                tag.voice = std::string(JavaTrim(expr.substr(voiceStart)));
                expr = expr.substr(0, voiceStart);
            }
            auto parts = SplitAll(expr, '.');
            tag.name = parts[0];
            std::vector<std::string> classes(parts.begin() + 1, parts.end());
            tag.classes = HashSetOrder(classes);
            tag.classSet = std::set<std::string>(classes.begin(), classes.end());
            return tag;
        }

        // getTagName: up to the first space or dot.
        std::string GetTagName(std::string_view tagExpression) {
            const std::string_view t = JavaTrim(tagExpression);
            const std::size_t end = t.find_first_of(" .");
            return std::string(t.substr(0, end));
        }

        bool IsSupportedTag(const std::string& name) {
            return name == "b" || name == "c" || name == "i" || name == "lang" || name == "ruby" || name == "rt" || name == "u" || name == "v";
        }

        struct StyleMatch {
            int score;
            const WebvttCssStyle* style;
        };
        std::vector<StyleMatch> GetApplicableStyles(const std::vector<WebvttCssStyle>& declared, const std::string* id, const StartTag& tag) {
            std::vector<StyleMatch> matches;
            for (const auto& style : declared) {
                const int score = style.GetSpecificityScore(id, tag.name, tag.classSet, tag.voice);
                if (score > 0) matches.push_back({ score, &style });
            }
            std::stable_sort(matches.begin(), matches.end(), [](const StyleMatch& a, const StyleMatch& b) { return a.score < b.score; });
            return matches;
        }

        void ApplyStyleToText(Spannable& text, const WebvttCssStyle& style, int start, int end) {
            if (style.GetStyle() != WebvttCssStyle::kUnspecified) text.AddOrReplaceSpan({ .kind = Span::Kind::Style, .style = style.GetStyle() }, start, end);
            if (style.linethrough == 1) text.SetSpan({ .kind = Span::Kind::Strikethrough }, start, end);
            if (style.underline == 1) text.SetSpan({ .kind = Span::Kind::Underline }, start, end);
            if (style.fontColor) text.AddOrReplaceSpan({ .kind = Span::Kind::Foreground, .color = *style.fontColor }, start, end);
            if (style.backgroundColor) text.AddOrReplaceSpan({ .kind = Span::Kind::Background, .color = *style.backgroundColor }, start, end);
            if (style.fontFamily) text.AddOrReplaceSpan({ .kind = Span::Kind::Typeface, .value = *style.fontFamily }, start, end);
            switch (style.fontSizeUnit) {
                case WebvttCssStyle::kFontSizeUnitPixel:
                    text.AddOrReplaceSpan({ .kind = Span::Kind::AbsoluteSize, .size = float(int(style.fontSize)) }, start, end);
                    break;
                case WebvttCssStyle::kFontSizeUnitEm:
                    text.AddOrReplaceSpan({ .kind = Span::Kind::RelativeSize, .size = style.fontSize }, start, end);
                    break;
                case WebvttCssStyle::kFontSizeUnitPercent:
                    text.AddOrReplaceSpan({ .kind = Span::Kind::RelativeSize, .size = style.fontSize / 100 }, start, end);
                    break;
                default: break;
            }
            if (style.combineUpright) text.SetSpan({ .kind = Span::Kind::TextCombine }, start, end);
        }

        int GetRubyPosition(const std::vector<WebvttCssStyle>& styles, const std::string* id, const StartTag& tag) {
            for (const auto& m : GetApplicableStyles(styles, id, tag))
                if (m.style->rubyPosition != kTextAnnotationPositionUnknown) return m.style->rubyPosition;
            return kTextAnnotationPositionUnknown;
        }

        void ApplyRubySpans(Spannable& text, const std::string* id, const StartTag& startTag, const std::vector<Element>& nested,
                            const std::vector<WebvttCssStyle>& styles) {
            const int rubyTagPosition = GetRubyPosition(styles, id, startTag);
            std::vector<Element> sorted = nested;
            std::stable_sort(sorted.begin(), sorted.end(), [](const Element& a, const Element& b) { return a.startTag.position < b.startTag.position; });
            int deleted = 0;
            int lastRubyTextEnd = startTag.position;
            for (const Element& rt : sorted) {
                if (rt.startTag.name != "rt") continue;
                // The <rt>'s ruby-position, else the <ruby>'s, else over.
                int position = GetRubyPosition(styles, id, rt.startTag);
                if (position == kTextAnnotationPositionUnknown) position = rubyTagPosition;
                if (position == kTextAnnotationPositionUnknown) position = kTextAnnotationPositionBefore;
                const int start = rt.startTag.position - deleted, end = rt.endPosition - deleted;
                const std::u16string rubyText = text.text.substr(std::size_t(start), std::size_t(end - start));
                text.Delete(start, end);
                text.SetSpan({ .kind = Span::Kind::Ruby, .value = detail::Utf16To8(rubyText), .position = position }, lastRubyTextEnd, start);
                deleted += int(rubyText.size());
                lastRubyTextEnd = start;   // the ruby text is gone: new start == old end
            }
        }

        // WebVTT's default text and background colour classes.
        std::optional<std::uint32_t> DefaultTextColor(const std::string& c) {
            if (c == "white") return Rgb(255, 255, 255);
            if (c == "lime") return Rgb(0, 255, 0);
            if (c == "cyan") return Rgb(0, 255, 255);
            if (c == "red") return Rgb(255, 0, 0);
            if (c == "yellow") return Rgb(255, 255, 0);
            if (c == "magenta") return Rgb(255, 0, 255);
            if (c == "blue") return Rgb(0, 0, 255);
            if (c == "black") return Rgb(0, 0, 0);
            return std::nullopt;
        }
        std::optional<std::uint32_t> DefaultBackgroundColor(const std::string& c) {
            if (!c.starts_with("bg_")) return std::nullopt;
            return DefaultTextColor(c.substr(3));
        }

        void ApplySpansForTag(const std::string* id, const StartTag& startTag, const std::vector<Element>& nested, Spannable& text,
                              const std::vector<WebvttCssStyle>& styles) {
            const int start = startTag.position, end = text.Length();
            const std::string& name = startTag.name;
            if (name == "b") text.SetSpan({ .kind = Span::Kind::Style, .style = WebvttCssStyle::kStyleBold }, start, end);
            else if (name == "i") text.SetSpan({ .kind = Span::Kind::Style, .style = WebvttCssStyle::kStyleItalic }, start, end);
            else if (name == "ruby") ApplyRubySpans(text, id, startTag, nested, styles);
            else if (name == "u") text.SetSpan({ .kind = Span::Kind::Underline }, start, end);
            else if (name == "c") {
                for (const auto& c : startTag.classes) {
                    if (auto color = DefaultTextColor(c)) text.SetSpan({ .kind = Span::Kind::Foreground, .color = *color }, start, end);
                    else if (auto bg = DefaultBackgroundColor(c)) text.SetSpan({ .kind = Span::Kind::Background, .color = *bg }, start, end);
                }
            } else if (name == "v") text.SetSpan({ .kind = Span::Kind::Voice, .value = startTag.voice }, start, end);
            else if (name == "lang" || name.empty()) {
            } else {
                return;
            }
            for (const auto& m : GetApplicableStyles(styles, id, startTag)) ApplyStyleToText(text, *m.style, start, end);
        }

        void ApplyEntity(std::u16string_view entity, Spannable& text) {
            if (entity == u"lt") text.text += u'<';
            else if (entity == u"gt") text.text += u'>';
            else if (entity == u"nbsp") text.text += u' ';
            else if (entity == u"amp") text.text += u'&';
            // others are ignored
        }

        // The spans as an AnnotatedString, drawn as Android draws them.
        text::AnnotatedString ToAnnotatedString(const Spannable& s) {
            text::AnnotatedStringBuilder b;
            b.Append(detail::Utf16To8(s.text));
            std::vector<std::pair<int, int>> underlines, strikes;
            for (const Span& span : s.spans) {
                if (span.start >= span.end) continue;   // emptied (by ruby text moving out)
                text::SpanStyle style;
                switch (span.kind) {
                    case Span::Kind::Style:
                        if (span.style & WebvttCssStyle::kStyleBold) style.fontWeight = text::FontWeight::Bold();
                        if (span.style & WebvttCssStyle::kStyleItalic) style.fontStyle = text::FontStyle::Italic;
                        break;
                    case Span::Kind::Underline:
                        style.textDecoration = text::TextDecoration::Underline;
                        underlines.emplace_back(span.start, span.end);
                        break;
                    case Span::Kind::Strikethrough:
                        style.textDecoration = text::TextDecoration::LineThrough;
                        strikes.emplace_back(span.start, span.end);
                        break;
                    case Span::Kind::Foreground: style.color = ui::Color(span.color); break;
                    case Span::Kind::Background: style.background = ui::Color(span.color); break;
                    case Span::Kind::Typeface:
                        // TypefaceSpan: the generic names, else an installed family by name.
                        if (span.value == "cursive") style.fontFamily = text::FontFamily::Cursive();
                        else if (span.value == "monospace") style.fontFamily = text::FontFamily::Monospace();
                        else if (span.value == "sans-serif") style.fontFamily = text::FontFamily::SansSerif();
                        else if (span.value == "serif") style.fontFamily = text::FontFamily::Serif();
                        else style.fontFamily = text::FontFamily::Of({ text::Font::Device(span.value) });
                        break;
                    case Span::Kind::AbsoluteSize: style.fontSize = text::Sp(span.size); break;
                    case Span::Kind::RelativeSize: style.fontSize = text::Em(span.size); break;
                    case Span::Kind::TextCombine: b.AddStringAnnotation(kTextCombineUprightAnnotation, "", span.start, span.end); continue;
                    case Span::Kind::Voice: b.AddStringAnnotation(kVoiceAnnotation, span.value, span.start, span.end); continue;
                    case Span::Kind::Ruby:
                        b.AddStringAnnotation(kRubyAnnotation, span.value, span.start, span.end);
                        b.AddStringAnnotation(kRubyPositionAnnotation, span.position == kTextAnnotationPositionAfter ? "under" : "over", span.start, span.end);
                        continue;
                }
                b.AddStyle(style, span.start, span.end);
            }
            // Android draws an underline and a strikethrough together; a SpanStyle has one.
            for (auto [us, ue] : underlines)
                for (auto [ss, se] : strikes) {
                    const int start = std::max(us, ss), end = std::min(ue, se);
                    if (start < end) b.AddStyle(text::SpanStyle{ .textDecoration = text::TextDecoration::Both }, start, end);
                }
            return b.ToAnnotatedString();
        }
    }

    namespace webvtt {

        std::int64_t ParseTimestampUs(std::string_view timestamp) {
            std::int64_t value = 0;
            const std::size_t dot = timestamp.find('.');
            const std::string_view whole = timestamp.substr(0, dot);
            for (const auto& part : SplitAll(whole, ':')) value = std::int64_t(std::uint64_t(value) * 60 + std::uint64_t(JavaParseLong(part)));
            value = std::int64_t(std::uint64_t(value) * 1000);
            if (dot != std::string_view::npos) {
                const std::string_view millis = JavaTrim(timestamp.substr(dot + 1));
                if (millis.size() != 3) throw std::invalid_argument("expected 3 decimal places");
                value += JavaParseLong(millis);
            }
            return std::int64_t(std::uint64_t(value) * 1000);
        }

        text::AnnotatedString ParseCueText(const std::string* id, std::string_view markupUtf8, const std::vector<WebvttCssStyle>& styles) {
            const std::u16string markup = detail::Utf8To16(markupUtf8);
            const int length = int(markup.size());
            Spannable text;
            std::vector<StartTag> stack;   // back is the top
            std::vector<Element> nested;
            int pos = 0;
            while (pos < length) {
                const char16_t curr = markup[std::size_t(pos)];
                if (curr == u'<') {
                    if (pos + 1 >= length) {
                        ++pos;
                        continue;
                    }
                    const int lt = pos;
                    const bool closing = markup[std::size_t(lt + 1)] == u'/';
                    const std::size_t gt = markup.find(u'>', std::size_t(lt + 1));
                    pos = gt == std::u16string::npos ? length : int(gt) + 1;
                    const bool isVoid = markup[std::size_t(pos - 2)] == u'/';
                    const int exprStart = lt + (closing ? 2 : 1), exprEnd = isVoid ? pos - 2 : pos - 1;
                    if (exprStart > exprEnd) continue;   // cut off at the end (media3's substring throws)
                    const std::string expr = detail::Utf16To8(std::u16string_view(markup).substr(std::size_t(exprStart), std::size_t(exprEnd - exprStart)));
                    if (JavaTrim(expr).empty()) continue;
                    const std::string name = GetTagName(expr);
                    if (!IsSupportedTag(name)) continue;
                    if (closing) {
                        for (;;) {
                            if (stack.empty()) break;
                            StartTag tag = std::move(stack.back());
                            stack.pop_back();
                            ApplySpansForTag(id, tag, nested, text, styles);
                            if (!stack.empty()) nested.push_back({ tag, text.Length() });
                            else nested.clear();
                            if (tag.name == name) break;
                        }
                    } else if (!isVoid) {
                        stack.push_back(BuildStartTag(expr, text.Length()));
                    }
                } else if (curr == u'&') {
                    const std::size_t semi = markup.find(u';', std::size_t(pos + 1));
                    const std::size_t space = markup.find(u' ', std::size_t(pos + 1));
                    const std::size_t end = semi == std::u16string::npos ? space : space == std::u16string::npos ? semi : std::min(semi, space);
                    if (end != std::u16string::npos) {
                        ApplyEntity(std::u16string_view(markup).substr(std::size_t(pos + 1), end - std::size_t(pos + 1)), text);
                        if (end == space) text.text += u' ';
                        pos = int(end) + 1;
                    } else {
                        text.text += curr;
                        ++pos;
                    }
                } else {
                    text.text += curr;
                    ++pos;
                }
            }
            // Unclosed tags, then the whole cue.
            while (!stack.empty()) {
                StartTag tag = std::move(stack.back());
                stack.pop_back();
                ApplySpansForTag(id, tag, nested, text, styles);
            }
            ApplySpansForTag(id, StartTag{}, {}, text, styles);
            return ToAnnotatedString(text);
        }

    }

    namespace {
        constexpr float kDefaultPosition = 0.5f;
        enum TextAlignment { kAlignStart = 1, kAlignCenter = 2, kAlignEnd = 3, kAlignLeft = 4, kAlignRight = 5 };

        struct CueInfoBuilder {
            std::int64_t startTimeUs = 0, endTimeUs = 0;
            text::AnnotatedString text;
            int textAlignment = kAlignCenter;
            float line = Cue::kDimenUnset;
            int lineType = Cue::kLineTypeNumber;   // snap-to-lines
            int lineAnchor = Cue::kAnchorTypeStart;
            float position = Cue::kDimenUnset;
            int positionAnchor = Cue::kTypeUnset;
            float size = 1.0f;
            int verticalType = Cue::kTypeUnset;

            static float ComputeLine(float line, int lineType) {
                if (line != Cue::kDimenUnset && lineType == Cue::kLineTypeFraction && (line < 0.0f || line > 1.0f)) return 1.0f;
                if (line != Cue::kDimenUnset) return line;
                if (lineType == Cue::kLineTypeFraction) return 1.0f;
                return Cue::kDimenUnset;   // stacked by WebvttSubtitle.getCues
            }
            static float DerivePosition(int alignment) {
                return alignment == kAlignLeft ? 0.0f : alignment == kAlignRight ? 1.0f : kDefaultPosition;
            }
            static int DerivePositionAnchor(int alignment) {
                switch (alignment) {
                    case kAlignLeft: case kAlignStart: return Cue::kAnchorTypeStart;
                    case kAlignRight: case kAlignEnd: return Cue::kAnchorTypeEnd;
                    default: return Cue::kAnchorTypeMiddle;
                }
            }
            static std::optional<Cue::Alignment> ConvertTextAlignment(int alignment) {
                switch (alignment) {
                    case kAlignStart: case kAlignLeft: return Cue::Alignment::Normal;
                    case kAlignCenter: return Cue::Alignment::Center;
                    case kAlignEnd: case kAlignRight: return Cue::Alignment::Opposite;
                    default: return std::nullopt;
                }
            }
            static float DeriveMaxSize(int anchor, float position) {
                switch (anchor) {
                    case Cue::kAnchorTypeStart: return 1.0f - position;
                    case Cue::kAnchorTypeEnd: return position;
                    case Cue::kAnchorTypeMiddle: return position <= 0.5f ? position * 2 : (1.0f - position) * 2;
                    default: throw std::logic_error("position anchor unset");
                }
            }
            Cue ToCue() const {
                const float pos = position != Cue::kDimenUnset ? position : DerivePosition(textAlignment);
                const int anchor = positionAnchor != Cue::kTypeUnset ? positionAnchor : DerivePositionAnchor(textAlignment);
                Cue cue;
                cue.text = text;
                cue.textAlignment = ConvertTextAlignment(textAlignment);
                cue.line = ComputeLine(line, lineType);
                cue.lineType = lineType;
                cue.lineAnchor = lineAnchor;
                cue.position = pos;
                cue.positionAnchor = anchor;
                cue.size = std::min(size, DeriveMaxSize(anchor, pos));
                cue.verticalType = verticalType;
                return cue;
            }
        };

        float ParsePercentage(std::string_view s) {
            if (!s.ends_with('%')) throw std::invalid_argument("percentages must end with %");
            return JavaParseFloat(s.substr(0, s.size() - 1)) / 100;
        }
        int ParseAnchor(std::string_view s, bool position) {
            if (s == "start" || (position && s == "line-left")) return Cue::kAnchorTypeStart;
            if (s == "center" || s == "middle") return Cue::kAnchorTypeMiddle;
            if (s == "end" || (position && s == "line-right")) return Cue::kAnchorTypeEnd;
            return Cue::kTypeUnset;
        }

        void ParseCueSettingsList(std::string_view list, CueInfoBuilder& b) {
            // (\S+?):(\S+), found run by run of non-space characters.
            std::size_t i = 0;
            while (i < list.size()) {
                while (i < list.size() && IsJavaSpace(char16_t(static_cast<unsigned char>(list[i])))) ++i;
                std::size_t end = i;
                while (end < list.size() && !IsJavaSpace(char16_t(static_cast<unsigned char>(list[end])))) ++end;
                const std::string_view run = list.substr(i, end - i);
                i = end;
                std::size_t colon = run.find(':', 1);
                while (colon != std::string_view::npos && colon + 1 >= run.size()) colon = std::string_view::npos;
                if (colon == std::string_view::npos) continue;
                const std::string_view name = run.substr(0, colon), value = run.substr(colon + 1);
                try {
                    if (name == "line") {
                        std::string_view s = value;
                        if (const std::size_t comma = s.find(','); comma != std::string_view::npos) {
                            b.lineAnchor = ParseAnchor(s.substr(comma + 1), false);
                            s = s.substr(0, comma);
                        }
                        if (s.ends_with('%')) {
                            b.line = ParsePercentage(s);
                            b.lineType = Cue::kLineTypeFraction;
                        } else {
                            b.line = float(JavaParseInt(s));
                            b.lineType = Cue::kLineTypeNumber;
                        }
                    } else if (name == "align") {
                        b.textAlignment = value == "start" ? kAlignStart : value == "left" ? kAlignLeft : value == "center" || value == "middle" ? kAlignCenter
                                          : value == "end" ? kAlignEnd : value == "right" ? kAlignRight : kAlignCenter;
                    } else if (name == "position") {
                        std::string_view s = value;
                        if (const std::size_t comma = s.find(','); comma != std::string_view::npos) {
                            b.positionAnchor = ParseAnchor(s.substr(comma + 1), true);
                            s = s.substr(0, comma);
                        }
                        b.position = ParsePercentage(s);
                    } else if (name == "size") {
                        b.size = ParsePercentage(value);
                    } else if (name == "vertical") {
                        b.verticalType = value == "rl" ? Cue::kVerticalTypeRl : value == "lr" ? Cue::kVerticalTypeLr : Cue::kTypeUnset;
                    }
                } catch (const std::invalid_argument&) {
                    // a bad setting is skipped
                }
            }
        }

        // CUE_HEADER_PATTERN: ^(\S+)\s+-->\s+(\S+)((?:.|\f)*+)?$
        struct CueHeader { std::string start, end, settings; };
        std::optional<CueHeader> MatchCueHeader(std::string_view line) {
            auto space = [](char c) { return IsJavaSpace(char16_t(static_cast<unsigned char>(c))); };
            std::size_t i = 0;
            while (i < line.size() && !space(line[i])) ++i;
            if (i == 0) return std::nullopt;
            CueHeader h;
            h.start = std::string(line.substr(0, i));
            std::size_t j = i;
            while (j < line.size() && space(line[j])) ++j;
            if (j == i || line.substr(j, 3) != "-->") return std::nullopt;
            j += 3;
            std::size_t k = j;
            while (k < line.size() && space(line[k])) ++k;
            if (k == j) return std::nullopt;
            std::size_t e = k;
            while (e < line.size() && !space(line[e])) ++e;
            if (e == k) return std::nullopt;
            h.end = std::string(line.substr(k, e - k));
            h.settings = std::string(line.substr(e));
            return h;
        }

        std::optional<WebvttCueInfo> ParseCueBody(const std::string* id, const CueHeader& header, SubtitleLineReader& in,
                                                  const std::vector<WebvttCssStyle>& styles) {
            CueInfoBuilder b;
            try {
                b.startTimeUs = webvtt::ParseTimestampUs(header.start);
                b.endTimeUs = webvtt::ParseTimestampUs(header.end);
            } catch (const std::invalid_argument&) {
                return std::nullopt;   // a bad header
            }
            ParseCueSettingsList(header.settings, b);
            std::string markup, line;
            while (in.ReadLine(line) && !line.empty()) {
                if (!markup.empty()) markup += '\n';
                markup += JavaTrim(line);
            }
            b.text = webvtt::ParseCueText(id, markup, styles);
            return WebvttCueInfo{ b.ToCue(), b.startTimeUs, b.endTimeUs };
        }

        std::optional<WebvttCueInfo> ParseCue(SubtitleLineReader& in, const std::vector<WebvttCssStyle>& styles) {
            std::string first;
            if (!in.ReadLine(first)) return std::nullopt;
            if (auto h = MatchCueHeader(first)) return ParseCueBody(nullptr, *h, in, styles);   // no id
            std::string second;
            if (!in.ReadLine(second)) return std::nullopt;
            if (auto h = MatchCueHeader(second)) {
                const std::string id(JavaTrim(first));
                return ParseCueBody(&id, *h, in, styles);
            }
            return std::nullopt;
        }
    }

    namespace webvtt {
        Cue ParseCueSettingsList(std::string_view settings) {
            CueInfoBuilder b;
            media::ParseCueSettingsList(settings, b);
            return b.ToCue();
        }
        Cue NewCueForText(text::AnnotatedString text) {
            CueInfoBuilder b;
            b.text = std::move(text);
            return b.ToCue();
        }
    }

    // --- WebvttSubtitle / LegacySubtitleUtil ----------------------------------------------------------

    WebvttSubtitle::WebvttSubtitle(std::vector<WebvttCueInfo> infos) : m_Infos(std::move(infos)) {
        for (const auto& i : m_Infos) {
            m_SortedTimes.push_back(i.startTimeUs);
            m_SortedTimes.push_back(i.endTimeUs);
        }
        std::sort(m_SortedTimes.begin(), m_SortedTimes.end());
    }

    int WebvttSubtitle::GetNextEventTimeIndex(std::int64_t timeUs) const {
        const auto it = std::upper_bound(m_SortedTimes.begin(), m_SortedTimes.end(), timeUs);
        return it == m_SortedTimes.end() ? kIndexUnset : int(it - m_SortedTimes.begin());
    }

    std::vector<Cue> WebvttSubtitle::GetCues(std::int64_t timeUs) const {
        std::vector<Cue> current;
        std::vector<const WebvttCueInfo*> unsetLine;
        for (const auto& info : m_Infos) {
            if (info.startTimeUs <= timeUs && timeUs < info.endTimeUs) {
                if (info.cue.line == Cue::kDimenUnset) unsetLine.push_back(&info);
                else current.push_back(info.cue);
            }
        }
        // Steps 4 - 10 of the cue's computed line: stacked up from the bottom by start time.
        std::stable_sort(unsetLine.begin(), unsetLine.end(), [](const WebvttCueInfo* a, const WebvttCueInfo* b) { return a->startTimeUs < b->startTimeUs; });
        for (std::size_t i = 0; i < unsetLine.size(); ++i) {
            Cue cue = unsetLine[i]->cue;
            cue.line = float(-1 - int(i));
            cue.lineType = Cue::kLineTypeNumber;
            current.push_back(std::move(cue));
        }
        return current;
    }

    void WebvttSubtitle::ToCuesWithTiming(const SubtitleOutputOptions& options, const kiln::Function<void(CuesWithTiming)>& output) const {
        const int count = GetEventTimeCount();
        auto startIndex = [&]() {
            if (options.startTimeUs == kTimeUnset) return 0;
            int next = GetNextEventTimeIndex(options.startTimeUs);
            if (next == kIndexUnset) next = count;
            if (next > 0 && GetEventTime(next - 1) == options.startTimeUs) --next;
            return next;
        }();
        auto outputEvent = [&](int index) {
            const std::int64_t start = GetEventTime(index);
            std::vector<Cue> cues = GetCues(start);
            if (cues.empty()) return;   // already in the previous one's duration
            if (index == count - 1) throw std::logic_error("the last cue list must be empty");
            const std::int64_t duration = GetEventTime(index + 1) - start;
            if (duration > 0) output(CuesWithTiming(std::move(cues), start, duration));
        };
        bool startedInMiddleOfCue = false;
        if (options.startTimeUs != kTimeUnset && startIndex < count) {
            std::vector<Cue> cuesAtStart = GetCues(options.startTimeUs);
            const std::int64_t firstEvent = GetEventTime(startIndex);
            if (!cuesAtStart.empty() && options.startTimeUs < firstEvent) {
                output(CuesWithTiming(std::move(cuesAtStart), options.startTimeUs, firstEvent - options.startTimeUs));
                startedInMiddleOfCue = true;
            }
        }
        for (int i = startIndex; i < count; ++i) outputEvent(i);
        if (options.outputAllCues) {
            const int endIndex = startedInMiddleOfCue ? startIndex - 1 : startIndex;
            for (int i = 0; i < endIndex; ++i) outputEvent(i);
            if (startedInMiddleOfCue)
                output(CuesWithTiming(GetCues(options.startTimeUs), GetEventTime(endIndex), options.startTimeUs - GetEventTime(endIndex)));
        }
    }

    // --- WebvttParser ------------------------------------------------------------------------------

    void WebvttParser::Parse(std::span<const std::uint8_t> data, const SubtitleOutputOptions& options,
                             const kiln::Function<void(CuesWithTiming)>& output) {
        SubtitleLineReader in(data);
        std::string line;
        // The header's first line, then the rest of the header block.
        if (!in.ReadLine(line) || !line.starts_with("WEBVTT")) throw std::invalid_argument("expected WEBVTT");
        while (in.ReadLine(line) && !line.empty()) {}

        std::vector<WebvttCssStyle> styles;
        std::vector<WebvttCueInfo> infos;
        for (;;) {
            // getNextEvent: what the next line starts, without taking it.
            if (!in.PeekLine(line)) break;
            if (line == "STYLE") {
                if (!infos.empty()) throw std::invalid_argument("a style block was found after the first cue");
                in.ReadLine(line);   // the STYLE header
                const std::size_t start = in.Position();
                while (in.ReadLine(line) && !line.empty()) {}   // the block ends at an empty line
                auto block = ParseWebvttCssBlock(std::string_view(in.Text()).substr(start, in.Position() - start));
                styles.insert(styles.end(), block.begin(), block.end());
            } else if (line.starts_with("NOTE")) {
                while (in.ReadLine(line) && !line.empty()) {}
            } else if (auto info = ParseCue(in, styles)) {
                infos.push_back(std::move(*info));
            }
        }
        WebvttSubtitle(std::move(infos)).ToCuesWithTiming(options, output);
    }

}
