#include "kiln/text/DateFormat.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <ctime>

namespace kiln::text {

    // --- civil dates ----------------------------------------------------------------------------

    std::int64_t DaysFromCivil(int y, int m, int d) {
        y -= m <= 2;
        const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = unsigned(y - era * 400);
        const unsigned doy = (153 * unsigned(m > 2 ? m - 3 : m + 9) + 2) / 5 + unsigned(d) - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + std::int64_t(doe) - 719468;
    }

    CivilDate CivilFromDays(std::int64_t z) {
        z += 719468;
        const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = unsigned(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const std::int64_t y = std::int64_t(yoe) + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        const unsigned d = doy - (153 * mp + 2) / 5 + 1;
        const unsigned m = mp < 10 ? mp + 3 : mp - 9;
        return { int(y + (m <= 2)), int(m), int(d) };
    }

    CivilDate CivilFromUtcMillis(std::int64_t millis) {
        std::int64_t days = millis / MillisPerDay;
        if (millis % MillisPerDay < 0) --days;
        return CivilFromDays(days);
    }

    bool IsLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

    int DaysInMonth(int y, int m) {
        static constexpr int days[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        return m == 2 && IsLeapYear(y) ? 29 : days[m - 1];
    }

    int IsoDayOfWeek(const CivilDate& d) {
        const std::int64_t days = DaysFromCivil(d.year, d.month, d.day);
        // 1970-01-01 was a Thursday (4).
        const std::int64_t w = ((days % 7) + 7 + 3) % 7;
        return int(w) + 1;
    }

    // --- the built-in (English) backend ---------------------------------------------------------

    namespace {
        constexpr std::array<const char*, 12> kMonths{ "January", "February", "March", "April", "May", "June", "July",
                                                       "August", "September", "October", "November", "December" };
        constexpr std::array<const char*, 7> kDays{ "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday" };

        std::string Padded(int v, int width) {
            std::string s = std::to_string(v < 0 ? -v : v);
            while (int(s.size()) < width) s.insert(s.begin(), '0');
            return v < 0 ? "-" + s : s;
        }

        // DateTimeFormatter pattern letters for a date: y/u, M/L, d, E/c, quoted literals.
        std::string Format(const CivilDate& d, std::string_view pattern) {
            std::string out;
            for (std::size_t i = 0; i < pattern.size();) {
                const char c = pattern[i];
                if (c == '\'') {
                    std::size_t j = i + 1;
                    if (j < pattern.size() && pattern[j] == '\'') { out += '\''; i += 2; continue; }
                    while (j < pattern.size() && pattern[j] != '\'') out += pattern[j++];
                    i = j + 1;
                    continue;
                }
                if (!std::isalpha(static_cast<unsigned char>(c))) { out += c; ++i; continue; }
                std::size_t n = 1;
                while (i + n < pattern.size() && pattern[i + n] == c) ++n;
                const char* month = kMonths[std::size_t(d.month - 1)];
                const char* day = kDays[std::size_t(IsoDayOfWeek(d) - 1)];
                switch (c) {
                    case 'y': case 'u':
                        out += n == 2 ? Padded(d.year % 100, 2) : Padded(d.year, int(n));
                        break;
                    case 'M': case 'L':
                        if (n <= 2) out += Padded(d.month, int(n));
                        else if (n == 3) out += std::string(month).substr(0, 3);
                        else if (n == 4) out += month;
                        else out += month[0];
                        break;
                    case 'd': out += Padded(d.day, int(n)); break;
                    case 'E': case 'c': case 'e':
                        if (n <= 3) out += std::string(day).substr(0, 3);
                        else if (n == 4) out += day;
                        else out += day[0];
                        break;
                    default: out.append(n, c); break;
                }
                i += n;
            }
            return out;
        }

        // DateTimePatternGenerator for en-US, for the fields a skeleton names.
        std::string PatternForSkeleton(std::string_view skeleton) {
            int y = 0, m = 0, d = 0, e = 0;
            for (char c : skeleton) {
                if (c == 'y' || c == 'Y' || c == 'u') ++y;
                else if (c == 'M' || c == 'L') ++m;
                else if (c == 'd') ++d;
                else if (c == 'E' || c == 'c') ++e;
            }
            if (y == 2 && !m && !d && !e) return "yy";
            const std::string weekday = e >= 4 ? "EEEE" : e ? "EEE" : "";
            if (m <= 2 && m > 0) {   // numeric months
                std::string p = d ? "M/d" : "M";
                if (y) p += "/y";
                return e ? weekday + ", " + p : p;
            }
            const std::string month = m >= 4 ? "MMMM" : m == 3 ? "MMM" : "";
            std::string p;
            if (!month.empty() && d) p = month + " d";
            else if (!month.empty()) p = y || e ? month : (m >= 4 ? "LLLL" : "LLL");
            else if (d) p = "d";
            if (y) p += p.empty() ? "y" : (d ? ", y" : " y");
            if (e) p = p.empty() ? (e >= 4 ? "cccc" : "ccc") : weekday + ", " + p;
            return p;
        }

        bool UsStyleWeek(std::string_view locale) {
            // Regions whose weeks start on Sunday (CLDR weekData), the common ones.
            static constexpr std::array<std::string_view, 24> sunday{ "US", "CA", "MX", "BR", "JP", "KR", "TW", "HK", "IL", "PH", "IN", "ZA",
                                                                     "AU", "AR", "CO", "PE", "VE", "SA", "PK", "TH", "ID", "KE", "NZ", "SG" };
            const auto dash = locale.find_first_of("-_");
            if (dash == std::string_view::npos) return locale.empty() || locale == "en" || locale == "ja" || locale == "ko" || locale == "he";
            std::string_view rest = locale.substr(dash + 1);
            // Skip a script subtag.
            if (rest.size() == 4 && std::isupper(static_cast<unsigned char>(rest[0])) && rest.find_first_of("-_") == std::string_view::npos) return false;
            if (rest.size() > 4 && rest[4] == '-') rest = rest.substr(5);
            const std::string_view region = rest.substr(0, rest.find_first_of("-_"));
            for (auto r : sunday) if (r == region) return true;
            return false;
        }

        class BuiltIn final : public DateFormatBackend {
        public:
            std::string FormatSkeleton(std::int64_t ms, std::string_view skeleton, std::string_view) override {
                return Format(CivilFromUtcMillis(ms), PatternForSkeleton(skeleton));
            }
            std::string FormatPattern(std::int64_t ms, std::string_view pattern, std::string_view) override {
                return Format(CivilFromUtcMillis(ms), pattern);
            }
            std::optional<CivilDate> Parse(std::string_view text, std::string_view pattern, std::string_view) override {
                // Numeric fields of the pattern's widths and literal delimiters (ResolverStyle.SMART:
                // a day past the month's end is the month's last).
                int y = -1, m = -1, d = -1;
                std::size_t t = 0;
                for (std::size_t i = 0; i < pattern.size();) {
                    const char c = pattern[i];
                    std::size_t n = 1;
                    while (i + n < pattern.size() && pattern[i + n] == c) ++n;
                    if (c == 'y' || c == 'u' || c == 'M' || c == 'd') {
                        // A single letter takes digits greedily (up to the field's maximum).
                        const std::size_t max = n > 1 ? n : (c == 'y' || c == 'u' ? 9 : 2);
                        std::size_t len = 0;
                        while (t + len < text.size() && len < max && std::isdigit(static_cast<unsigned char>(text[t + len]))) ++len;
                        if (len == 0 || (n > 1 && len != n)) return std::nullopt;
                        const int v = std::stoi(std::string(text.substr(t, len)));
                        t += len;
                        if (c == 'M') m = v;
                        else if (c == 'd') d = v;
                        else y = n == 2 ? 2000 + v : v;
                        i += n;
                    } else {
                        for (std::size_t k = 0; k < n; ++k, ++t)
                            if (t >= text.size() || text[t] != c) return std::nullopt;
                        i += n;
                    }
                }
                if (t != text.size() || y < 0 || m < 1 || m > 12 || d < 1 || d > 31) return std::nullopt;
                return CivilDate{ y, m, std::min(d, DaysInMonth(y, m)) };
            }
            std::string ShortDatePattern(std::string_view) override { return "M/d/yy"; }
            int FirstDayOfWeek(std::string_view locale) override { return UsStyleWeek(locale) ? 7 : 1; }
            std::vector<std::pair<std::string, std::string>> WeekdayNames(std::string_view) override {
                std::vector<std::pair<std::string, std::string>> v;
                for (const char* day : kDays) v.emplace_back(day, std::string(1, day[0]));
                return v;
            }
            CivilDate Today() override {
                const std::time_t now = std::time(nullptr);
                std::tm local{};
                localtime_r(&now, &local);
                return { local.tm_year + 1900, local.tm_mon + 1, local.tm_mday };
            }
        };

        DateFormatBackend* g_Backend = nullptr;
    }

    DateFormatBackend& BuiltInDateFormat() {
        static BuiltIn builtIn;
        return builtIn;
    }

    DateFormatBackend& DateFormat() { return g_Backend ? *g_Backend : BuiltInDateFormat(); }

    void SetDateFormat(DateFormatBackend* backend) { g_Backend = backend; }

}
