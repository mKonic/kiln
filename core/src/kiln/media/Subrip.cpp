#include "kiln/media/Subrip.h"

#include "kiln/text/Html.h"

#include <regex>
#include <stdexcept>

namespace kiln::media {

    namespace {
        // Fractional positions for use when alignment tags are present.
        constexpr float kStartFraction = 0.08f;
        constexpr float kEndFraction = 1 - kStartFraction;
        constexpr float kMidFraction = 0.5f;

        // Some SRT files don't include hours or milliseconds in the timecode: optional groups.
        const std::string kTimecode = R"((?:(\d+):)?(\d+):(\d+)(?:,(\d{3}))?)";
        const std::regex& TimingLine() {
            static const std::regex r(R"(\s*()" + kTimecode + R"()\s*-->\s*()" + kTimecode + R"()\s*)");
            return r;
        }
        const std::regex& TagPattern() {
            static const std::regex r(R"(\{\\.*?\})");
            return r;
        }
        const std::regex& AlignmentTag() {
            static const std::regex r(R"(\{\\an[1-9]\})");
            return r;
        }

        // Integer.parseInt: an optional sign and decimal digits that fit an int.
        bool IsJavaInt(std::string_view s) {
            std::size_t i = 0;
            bool negative = false;
            if (!s.empty() && (s[0] == '-' || s[0] == '+')) { negative = s[0] == '-'; i = 1; }
            if (i == s.size()) return false;
            long long v = 0;
            for (; i < s.size(); ++i) {
                if (s[i] < '0' || s[i] > '9') return false;
                v = v * 10 + (s[i] - '0');
                if (v > 2147483648LL) return false;
            }
            return negative || v <= 2147483647LL;
        }

        std::int64_t ParseTimecode(const std::smatch& m, int groupOffset) {
            std::int64_t ms = m[groupOffset + 1].matched ? std::stoll(m[groupOffset + 1].str()) * 60 * 60 * 1000 : 0;
            ms += std::stoll(m[groupOffset + 2].str()) * 60 * 1000;
            ms += std::stoll(m[groupOffset + 3].str()) * 1000;
            if (m[groupOffset + 4].matched) ms += std::stoll(m[groupOffset + 4].str());
            return ms * 1000;
        }

        // Trims and removes {\...} tags from the line, collecting them.
        std::string ProcessLine(std::string_view rawLine, std::vector<std::string>& tags) {
            const std::string line(JavaTrim(rawLine));
            std::string processed;
            std::size_t from = 0;
            for (auto it = std::sregex_iterator(line.begin(), line.end(), TagPattern()); it != std::sregex_iterator(); ++it) {
                tags.push_back(it->str());
                processed.append(line, from, std::size_t(it->position()) - from);
                from = std::size_t(it->position() + it->length());
            }
            processed.append(line, from);
            return processed;
        }

        Cue BuildCue(text::AnnotatedString text, const std::string* alignmentTag) {
            Cue cue;
            cue.text = std::move(text);
            if (!alignmentTag) return cue;
            const char n = (*alignmentTag)[4];   // {\anN}
            // Horizontal: 1 4 7 left, 3 6 9 right, else middle.
            cue.positionAnchor = n == '1' || n == '4' || n == '7' ? Cue::kAnchorTypeStart
                                 : n == '3' || n == '6' || n == '9' ? Cue::kAnchorTypeEnd
                                                                    : Cue::kAnchorTypeMiddle;
            // Vertical: 1 2 3 bottom, 7 8 9 top, else middle.
            cue.lineAnchor = n == '1' || n == '2' || n == '3' ? Cue::kAnchorTypeEnd
                             : n == '7' || n == '8' || n == '9' ? Cue::kAnchorTypeStart
                                                                : Cue::kAnchorTypeMiddle;
            cue.position = SubripParser::FractionalPositionForAnchorType(cue.positionAnchor);
            cue.line = SubripParser::FractionalPositionForAnchorType(cue.lineAnchor);
            cue.lineType = Cue::kLineTypeFraction;
            return cue;
        }
    }

    float SubripParser::FractionalPositionForAnchorType(int anchorType) {
        switch (anchorType) {
            case Cue::kAnchorTypeStart: return kStartFraction;
            case Cue::kAnchorTypeMiddle: return kMidFraction;
            case Cue::kAnchorTypeEnd: return kEndFraction;
            default: throw std::invalid_argument("anchor type unset");
        }
    }

    void SubripParser::Parse(std::span<const std::uint8_t> data, const SubtitleOutputOptions& options,
                             const kiln::Function<void(CuesWithTiming)>& output) {
        SubtitleLineReader reader(data);
        const bool keepBefore = options.startTimeUs != kTimeUnset && options.outputAllCues;
        std::vector<CuesWithTiming> before;
        std::string line;
        std::vector<std::string> tags;
        while (reader.ReadLine(line)) {
            if (line.empty()) continue;   // blank lines
            if (!IsJavaInt(line)) continue;   // an invalid index
            if (!reader.ReadLine(line)) break;   // unexpected end
            std::smatch m;
            if (!std::regex_match(line, m, TimingLine())) continue;   // invalid timing
            const std::int64_t startUs = ParseTimecode(m, 1), endUs = ParseTimecode(m, 6);

            // The text and its tags; lines joined with <br>.
            std::string html;
            tags.clear();
            while (reader.ReadLine(line) && !line.empty()) {
                if (!html.empty()) html += "<br>";
                html += ProcessLine(line, tags);
            }
            const std::string* alignment = nullptr;
            for (const auto& tag : tags)
                if (std::regex_match(tag, AlignmentTag())) {
                    alignment = &tag;   // later ones are ignored
                    break;
                }
            CuesWithTiming cues({ BuildCue(text::FromHtml(html), alignment) }, startUs, endUs - startUs);
            if (options.startTimeUs == kTimeUnset || endUs >= options.startTimeUs) output(std::move(cues));
            else if (keepBefore) before.push_back(std::move(cues));
        }
        for (auto& c : before) output(std::move(c));
    }

}
