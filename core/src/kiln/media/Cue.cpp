#include "kiln/media/Cue.h"

#include <algorithm>
#include <stdexcept>

namespace kiln::media {

    CuesWithTiming::CuesWithTiming(std::vector<Cue> c, std::int64_t start, std::int64_t duration)
        : cues(std::move(c)), startTimeUs(start), durationUs(duration),
          endTimeUs(duration == kTimeUnset || start == kTimeUnset ? kTimeUnset : start + duration) {}

    std::vector<CuesWithTiming> SubtitleParser::ParseAll(std::span<const std::uint8_t> data) {
        std::vector<CuesWithTiming> all;
        Parse(data, SubtitleOutputOptions::AllCues(), [&](CuesWithTiming c) { all.push_back(std::move(c)); });
        return all;
    }

    bool MergingCuesResolver::AddCues(CuesWithTiming cues, std::int64_t positionUs) {
        if (cues.startTimeUs == kTimeUnset || cues.durationUs == kTimeUnset) throw std::invalid_argument("cues need a start and duration");
        const bool shown = cues.startTimeUs <= positionUs && positionUs < cues.endTimeUs;
        for (std::size_t i = m_List.size(); i-- > 0;)
            if (cues.startTimeUs >= m_List[i].startTimeUs) {
                m_List.insert(m_List.begin() + std::ptrdiff_t(i + 1), std::move(cues));
                return shown;
            }
        m_List.insert(m_List.begin(), std::move(cues));
        return shown;
    }

    std::vector<Cue> MergingCuesResolver::GetCuesAtTimeUs(std::int64_t timeUs) const {
        if (m_List.empty() || timeUs < m_List[0].startTimeUs) return {};
        std::vector<const CuesWithTiming*> visible;
        for (const auto& c : m_List) {
            if (timeUs >= c.startTimeUs && timeUs < c.endTimeUs) visible.push_back(&c);
            if (timeUs < c.startTimeUs) break;
        }
        // CUES_DISPLAY_PRIORITY_COMPARATOR: by start time, then the longer first.
        std::stable_sort(visible.begin(), visible.end(), [](const CuesWithTiming* a, const CuesWithTiming* b) {
            return a->startTimeUs != b->startTimeUs ? a->startTimeUs < b->startTimeUs : a->durationUs > b->durationUs;
        });
        std::vector<Cue> result;
        for (const auto* c : visible) result.insert(result.end(), c->cues.begin(), c->cues.end());
        return result;
    }

    void MergingCuesResolver::DiscardCuesBeforeTimeUs(std::int64_t timeUs) {
        for (std::size_t i = 0; i < m_List.size(); ++i) {
            const std::int64_t start = m_List[i].startTimeUs;
            if (timeUs > start && timeUs > m_List[i].endTimeUs) {
                m_List.erase(m_List.begin() + std::ptrdiff_t(i));
                --i;
            } else if (timeUs < start) {
                break;
            }
        }
    }

    std::int64_t MergingCuesResolver::GetPreviousCueChangeTimeUs(std::int64_t timeUs) const {
        if (m_List.empty() || timeUs < m_List[0].startTimeUs) return kTimeUnset;
        std::int64_t result = m_List[0].startTimeUs;
        for (const auto& c : m_List) {
            if (c.endTimeUs <= timeUs) result = std::max(result, c.endTimeUs);
            else if (c.startTimeUs <= timeUs) result = std::max(result, c.startTimeUs);
            else break;
        }
        return result;
    }

    std::int64_t MergingCuesResolver::GetNextCueChangeTimeUs(std::int64_t timeUs) const {
        std::int64_t result = kTimeUnset;
        for (const auto& c : m_List) {
            if (timeUs < c.startTimeUs) {
                result = result == kTimeUnset ? c.startTimeUs : std::min(result, c.startTimeUs);
                break;
            }
            if (timeUs < c.endTimeUs) result = result == kTimeUnset ? c.endTimeUs : std::min(result, c.endTimeUs);
        }
        return result != kTimeUnset ? result : kTimeEndOfSource;
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
