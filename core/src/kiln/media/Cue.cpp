#include "kiln/media/Cue.h"

namespace kiln::media {

    CuesWithTiming::CuesWithTiming(std::vector<Cue> c, std::int64_t start, std::int64_t duration)
        : cues(std::move(c)), startTimeUs(start), durationUs(duration),
          endTimeUs(duration == kTimeUnset || start == kTimeUnset ? kTimeUnset : start + duration) {}

    std::vector<CuesWithTiming> SubtitleParser::ParseAll(std::span<const std::uint8_t> data) {
        std::vector<CuesWithTiming> all;
        Parse(data, SubtitleOutputOptions::AllCues(), [&](CuesWithTiming c) { all.push_back(std::move(c)); });
        return all;
    }

    namespace {
        void AppendUtf8(std::string& out, char32_t c) {
            if (c < 0x80) out += char(c);
            else if (c < 0x800) { out += char(0xC0 | (c >> 6)); out += char(0x80 | (c & 0x3F)); }
            else if (c < 0x10000) { out += char(0xE0 | (c >> 12)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
            else { out += char(0xF0 | (c >> 18)); out += char(0x80 | ((c >> 12) & 0x3F)); out += char(0x80 | ((c >> 6) & 0x3F)); out += char(0x80 | (c & 0x3F)); }
        }
        // UTF-16 to UTF-8; unpaired surrogates become U+FFFD (as Java's decoder replaces them).
        std::string FromUtf16(std::span<const std::uint8_t> d, bool bigEndian) {
            std::string out;
            out.reserve(d.size() / 2);
            auto unit = [&](std::size_t i) -> char16_t {
                return bigEndian ? char16_t(d[i] << 8 | d[i + 1]) : char16_t(d[i + 1] << 8 | d[i]);
            };
            for (std::size_t i = 0; i + 1 < d.size(); i += 2) {
                char16_t u = unit(i);
                if (u >= 0xD800 && u <= 0xDBFF && i + 3 < d.size()) {
                    const char16_t low = unit(i + 2);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        AppendUtf8(out, 0x10000 + ((char32_t(u) - 0xD800) << 10) + (low - 0xDC00));
                        i += 2;
                        continue;
                    }
                }
                AppendUtf8(out, u >= 0xD800 && u <= 0xDFFF ? 0xFFFD : u);
            }
            return out;
        }
    }

    // ParsableByteArray.readUtfCharsetFromBom, then the bytes as that charset.
    SubtitleLineReader::SubtitleLineReader(std::span<const std::uint8_t> d) {
        if (d.size() >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) m_Text.assign(d.begin() + 3, d.end());
        else if (d.size() >= 2 && d[0] == 0xFE && d[1] == 0xFF) m_Text = FromUtf16(d.subspan(2), true);
        else if (d.size() >= 2 && d[0] == 0xFF && d[1] == 0xFE) m_Text = FromUtf16(d.subspan(2), false);
        else m_Text.assign(d.begin(), d.end());
    }

    bool SubtitleLineReader::PeekLine(std::string& line) const {
        if (m_Pos >= m_Text.size()) return false;
        std::size_t end = m_Pos;
        while (end < m_Text.size() && m_Text[end] != '\n' && m_Text[end] != '\r') ++end;
        line.assign(m_Text, m_Pos, end - m_Pos);
        return true;
    }

    bool SubtitleLineReader::ReadLine(std::string& line) {
        if (!PeekLine(line)) return false;
        m_Pos += line.size();
        // The terminator: \r, \n or \r\n.
        if (m_Pos < m_Text.size() && m_Text[m_Pos] == '\r') ++m_Pos;
        if (m_Pos < m_Text.size() && m_Text[m_Pos] == '\n') ++m_Pos;
        return true;
    }

    namespace detail {
        std::u16string Utf8To16(std::string_view s) {
            std::u16string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size();) {
                const unsigned char c = static_cast<unsigned char>(s[i]);
                char32_t cp = 0xFFFD;
                std::size_t n = 1;
                if (c < 0x80) cp = c;
                else if ((c >> 5) == 0x6 && i + 1 < s.size()) { cp = ((c & 0x1F) << 6) | (s[i + 1] & 0x3F); n = 2; }
                else if ((c >> 4) == 0xE && i + 2 < s.size()) { cp = ((c & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); n = 3; }
                else if ((c >> 3) == 0x1E && i + 3 < s.size()) {
                    cp = ((c & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
                    n = 4;
                }
                i += n;
                if (cp >= 0x10000) {
                    cp -= 0x10000;
                    out += char16_t(0xD800 + (cp >> 10));
                    out += char16_t(0xDC00 + (cp & 0x3FF));
                } else {
                    out += char16_t(cp);
                }
            }
            return out;
        }
        std::string Utf16To8(std::u16string_view s) {
            std::string out;
            out.reserve(s.size());
            for (std::size_t i = 0; i < s.size(); ++i) {
                char32_t c = s[i];
                if (c >= 0xD800 && c <= 0xDBFF && i + 1 < s.size() && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
                    c = 0x10000 + ((c - 0xD800) << 10) + (s[i + 1] - 0xDC00);
                    ++i;
                } else if (c >= 0xD800 && c <= 0xDFFF) {
                    c = 0xFFFD;
                }
                AppendUtf8(out, c);
            }
            return out;
        }
    }

    std::string_view JavaTrim(std::string_view s) {
        std::size_t b = 0, e = s.size();
        while (b < e && static_cast<unsigned char>(s[b]) <= ' ') ++b;
        while (e > b && static_cast<unsigned char>(s[e - 1]) <= ' ') --e;
        return s.substr(b, e - b);
    }

}
