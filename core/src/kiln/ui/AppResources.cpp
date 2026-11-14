#include "kiln/ui/AppResources.h"

#include <cmath>
#include <charconv>
#include <cstdlib>
#include <mutex>
#include <stdexcept>

namespace kiln::ui {

    namespace {
        struct Registry {
            std::mutex mutex;
            std::shared_ptr<const AppResources> host, generated;
        };
        Registry& R() {
            static Registry* r = new Registry;   // read from static destructors too
            return *r;
        }

        // The configuration content reads (subscribing to it), or the default outside content.
        Configuration CurrentConfiguration() {
            auto* c = rt::Composition::Current();
            return c ? c->Read(LocalConfiguration) : LocalConfiguration.Default();
        }
        float CurrentDensity(float* fontScale) {
            auto* c = rt::Composition::Current();
            const Density d = c ? c->Read(LocalDensity) : LocalDensity.Default();
            if (fontScale) *fontScale = d.fontScale;
            return d.density;
        }
        [[noreturn]] void NotFound(const char* type, const char* name) {
            throw std::out_of_range(std::string("kiln: the app has no ") + type + " resource '" + (name ? name : "") + "'");
        }
        const AppResources& Require() {
            auto r = GetAppResources();
            if (!r) throw std::logic_error("kiln: the app has no resources (no res/ in kiln_add_app)");
            // The registry keeps it alive for the process.
            return *r;
        }

        std::string_view Language(std::string_view tag) {
            const std::size_t dash = tag.find_first_of("-_");
            return dash == std::string_view::npos ? tag : tag.substr(0, dash);
        }
        bool In(std::string_view x, std::initializer_list<std::string_view> set) {
            for (std::string_view s : set) if (s == x) return true;
            return false;
        }
    }

    std::string AppResources::Format(std::string_view pattern, std::span<const text::FormatArg> args, const Configuration& config) const {
        return text::FormatJava(pattern, args, config.locales.empty() ? std::string_view() : std::string_view(config.locales.front()));
    }

    void SetAppResources(std::shared_ptr<const AppResources> resources) {
        std::lock_guard lock(R().mutex);
        R().host = std::move(resources);
    }
    void SetDefaultAppResources(std::shared_ptr<const AppResources> resources) {
        std::lock_guard lock(R().mutex);
        R().generated = std::move(resources);
    }
    std::shared_ptr<const AppResources> GetAppResources() {
        std::lock_guard lock(R().mutex);
        return R().host ? R().host : R().generated;
    }

    std::string StringResource(StringRes id) {
        const Configuration config = CurrentConfiguration();
        auto v = Require().GetString(id.name, config);
        if (!v) NotFound("string", id.name);
        return *v;
    }
    std::string StringResource(StringRes id, std::span<const text::FormatArg> formatArgs) {
        const Configuration config = CurrentConfiguration();
        const AppResources& r = Require();
        auto v = r.GetString(id.name, config);
        if (!v) NotFound("string", id.name);
        return r.Format(*v, formatArgs, config);
    }
    std::string PluralStringResource(PluralsRes id, int count) {
        const Configuration config = CurrentConfiguration();
        auto v = Require().GetQuantityString(id.name, count, config);
        if (!v) NotFound("plurals", id.name);
        return *v;
    }
    std::string PluralStringResource(PluralsRes id, int count, std::span<const text::FormatArg> formatArgs) {
        const Configuration config = CurrentConfiguration();
        const AppResources& r = Require();
        auto v = r.GetQuantityString(id.name, count, config);
        if (!v) NotFound("plurals", id.name);
        return r.Format(*v, formatArgs, config);
    }
    std::vector<std::string> StringArrayResource(StringArrayRes id) {
        auto v = Require().GetStringArray(id.name, CurrentConfiguration());
        if (!v) NotFound("string-array", id.name);
        return *v;
    }
    std::vector<int> IntegerArrayResource(IntArrayRes id) {
        auto v = Require().GetIntArray(id.name, CurrentConfiguration());
        if (!v) NotFound("integer-array", id.name);
        return *v;
    }
    Color ColorResource(ColorRes id) {
        auto v = Require().GetColor(id.name, CurrentConfiguration());
        if (!v) NotFound("color", id.name);
        return *v;
    }
    Dp DimensionResource(DimenRes id) {
        const Configuration config = CurrentConfiguration();
        float fontScale = 1;
        const float density = CurrentDensity(&fontScale);
        auto v = Require().GetDimension(id.name, config, density, fontScale);
        if (!v) NotFound("dimen", id.name);
        return Dp(*v / density);   // dimensionResource: getDimension(px) / density
    }
    int IntegerResource(IntegerRes id) {
        auto v = Require().GetInteger(id.name, CurrentConfiguration());
        if (!v) NotFound("integer", id.name);
        return *v;
    }
    bool BooleanResource(BoolRes id) {
        auto v = Require().GetBoolean(id.name, CurrentConfiguration());
        if (!v) NotFound("bool", id.name);
        return *v;
    }
    DrawableResource ReadDrawableResource(DrawableRes id) {
        auto v = Require().GetDrawable(id.name, CurrentConfiguration());
        if (!v) NotFound("drawable", id.name);
        return std::move(*v);
    }

    // CLDR plural rules (plurals.xml) for integer counts.
    PluralCategory PluralCategoryOf(std::string_view tag, int count) {
        using P = PluralCategory;
        const std::string_view lang = Language(tag);
        const long n = std::labs(long(count));
        const long m10 = n % 10, m100 = n % 100;
        if (In(lang, { "ja", "zh", "ko", "vi", "th", "id", "in", "ms", "my", "km", "lo", "yo", "ig", "jv", "su", "bo", "dz" })) return P::Other;
        if (In(lang, { "ru", "uk", "be" })) {
            if (m10 == 1 && m100 != 11) return P::One;
            if (m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14)) return P::Few;
            return P::Many;
        }
        if (lang == "pl") {
            if (n == 1) return P::One;
            if (m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14)) return P::Few;
            return P::Many;
        }
        if (In(lang, { "cs", "sk" })) return n == 1 ? P::One : (n >= 2 && n <= 4) ? P::Few : P::Other;
        if (In(lang, { "hr", "sr", "bs", "sh" })) {
            if (m10 == 1 && m100 != 11) return P::One;
            if (m10 >= 2 && m10 <= 4 && !(m100 >= 12 && m100 <= 14)) return P::Few;
            return P::Other;
        }
        if (lang == "lt") {
            if (m10 == 1 && !(m100 >= 11 && m100 <= 19)) return P::One;
            if (m10 >= 2 && !(m100 >= 11 && m100 <= 19)) return P::Few;
            return P::Other;
        }
        if (lang == "lv") {
            if (m10 == 0 || (m100 >= 11 && m100 <= 19)) return P::Zero;
            if (m10 == 1 && m100 != 11) return P::One;
            return P::Other;
        }
        if (In(lang, { "ro", "mo" })) {
            if (n == 1) return P::One;
            if (n == 0 || (m100 >= 2 && m100 <= 19)) return P::Few;
            return P::Other;
        }
        if (lang == "sl") {
            if (m100 == 1) return P::One;
            if (m100 == 2) return P::Two;
            if (m100 == 3 || m100 == 4) return P::Few;
            return P::Other;
        }
        if (lang == "ar") {
            if (n == 0) return P::Zero;
            if (n == 1) return P::One;
            if (n == 2) return P::Two;
            if (m100 >= 3 && m100 <= 10) return P::Few;
            if (m100 >= 11 && m100 <= 99) return P::Many;
            return P::Other;
        }
        if (In(lang, { "he", "iw" })) return n == 1 ? P::One : n == 2 ? P::Two : P::Other;
        if (lang == "ga") return n == 1 ? P::One : n == 2 ? P::Two : (n >= 3 && n <= 6) ? P::Few : (n >= 7 && n <= 10) ? P::Many : P::Other;
        if (lang == "cy") return n == 0 ? P::Zero : n == 1 ? P::One : n == 2 ? P::Two : n == 3 ? P::Few : n == 6 ? P::Many : P::Other;
        if (lang == "mt") {
            if (n == 1) return P::One;
            if (n == 2) return P::Two;
            if (n == 0 || (m100 >= 3 && m100 <= 10)) return P::Few;
            if (m100 >= 11 && m100 <= 19) return P::Many;
            return P::Other;
        }
        // i = 0 or n = 1 (French, Portuguese, Hindi, Bengali, Persian, Amharic, Zulu, Armenian...).
        if (In(lang, { "fr", "pt", "hi", "bn", "fa", "am", "zu", "hy", "kab", "gu", "kn", "mr", "as" })) return n <= 1 ? P::One : (lang == "fr" && n != 0 && n % 1000000 == 0) ? P::Many : P::Other;
        if (In(lang, { "es", "it", "ca" }) && n != 0 && n % 1000000 == 0) return P::Many;
        return n == 1 ? P::One : P::Other;
    }

    std::string_view PluralCategoryName(PluralCategory c) {
        switch (c) {
            case PluralCategory::Zero: return "zero";
            case PluralCategory::One: return "one";
            case PluralCategory::Two: return "two";
            case PluralCategory::Few: return "few";
            case PluralCategory::Many: return "many";
            case PluralCategory::Other: break;
        }
        return "other";
    }

    std::optional<Color> ParseResourceColor(std::string_view v) {
        if (v.empty() || v[0] != '#') return std::nullopt;
        v.remove_prefix(1);
        std::uint32_t x = 0;
        auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), x, 16);
        if (ec != std::errc() || p != v.data() + v.size()) return std::nullopt;
        auto nib = [](std::uint32_t h) { return h * 17; };
        switch (v.size()) {
            case 3: return Color(0xff000000u | nib((x >> 8) & 0xf) << 16 | nib((x >> 4) & 0xf) << 8 | nib(x & 0xf));
            case 4: return Color(nib((x >> 12) & 0xf) << 24 | nib((x >> 8) & 0xf) << 16 | nib((x >> 4) & 0xf) << 8 | nib(x & 0xf));
            case 6: return Color(0xff000000u | x);
            case 8: return Color(x);
            default: return std::nullopt;
        }
    }

    std::optional<float> ParseResourceDimension(std::string_view v, float density, float fontScale) {
        std::size_t i = 0;
        while (i < v.size() && (std::isdigit(static_cast<unsigned char>(v[i])) || v[i] == '.' || v[i] == '-' || v[i] == '+')) ++i;
        if (i == 0) return std::nullopt;
        const float n = std::strtof(std::string(v.substr(0, i)).c_str(), nullptr);
        const std::string_view unit = v.substr(i);
        // TypedValue.applyDimension (xdpi taken as the density's dpi).
        if (unit == "px") return n;
        if (unit == "dp" || unit == "dip") return n * density;
        if (unit == "sp") return n * density * fontScale;
        if (unit == "pt") return n * density * 160.0f / 72.0f;
        if (unit == "in") return n * density * 160.0f;
        if (unit == "mm") return n * density * 160.0f / 25.4f;
        return std::nullopt;
    }

    // --- Generated tables -----------------------------------------------------------------------

    const char* TableAppResources::Find(std::string_view type, std::string_view name, std::string_view quantity, const Configuration& config) const {
        // The tags to try: each candidate of the first locale (Android resolves one locale of
        // the list; LocaleList's first is the common case), "-night" variants first at night.
        std::vector<std::string> tags = config.locales.empty() ? std::vector<std::string>{ "" } : text::ResourceLocaleCandidates(config.locales.front());
        if (tags.empty() || !tags.back().empty()) tags.push_back("");
        const bool night = config.IsNightModeActive();
        for (const std::string& tag : tags)
            for (int pass = night ? 0 : 1; pass < 2; ++pass) {
                const std::string want = pass == 0 ? (tag.empty() ? std::string("night") : tag + "-night") : tag;
                for (const Entry& e : m_Values)
                    if (want == e.tag && type == e.type && name == e.name && quantity == (e.quantity ? e.quantity : "")) return e.value;
            }
        return nullptr;
    }

    std::optional<std::string> TableAppResources::GetString(std::string_view name, const Configuration& config) const {
        if (const char* v = Find("string", name, "", config)) return std::string(v);
        return std::nullopt;
    }
    std::optional<std::string> TableAppResources::GetQuantityString(std::string_view name, int quantity, const Configuration& config) const {
        const std::string_view lang = config.locales.empty() ? std::string_view("en") : std::string_view(config.locales.front());
        if (const char* v = Find("plurals", name, PluralCategoryName(PluralCategoryOf(lang, quantity)), config)) return std::string(v);
        if (const char* v = Find("plurals", name, "other", config)) return std::string(v);   // as Resources falls back
        return std::nullopt;
    }
    std::optional<std::vector<std::string>> TableAppResources::GetStringArray(std::string_view name, const Configuration& config) const {
        std::vector<std::string> tags = config.locales.empty() ? std::vector<std::string>{ "" } : text::ResourceLocaleCandidates(config.locales.front());
        if (tags.empty() || !tags.back().empty()) tags.push_back("");
        for (const std::string& tag : tags)
            for (const ArrayEntry& a : m_Arrays)
                if (tag == a.tag && name == a.name && std::string_view(a.type) == "string-array") return a.values;
        return std::nullopt;
    }
    std::optional<std::vector<int>> TableAppResources::GetIntArray(std::string_view name, const Configuration&) const {
        for (const ArrayEntry& a : m_Arrays)
            if (name == a.name && std::string_view(a.type) == "integer-array") {
                std::vector<int> out;
                for (const auto& s : a.values) out.push_back(std::atoi(s.c_str()));
                return out;
            }
        return std::nullopt;
    }
    std::optional<Color> TableAppResources::GetColor(std::string_view name, const Configuration& config) const {
        if (const char* v = Find("color", name, "", config)) return ParseResourceColor(v);
        return std::nullopt;
    }
    std::optional<float> TableAppResources::GetDimension(std::string_view name, const Configuration& config, float density, float fontScale) const {
        if (const char* v = Find("dimen", name, "", config)) return ParseResourceDimension(v, density, fontScale);
        return std::nullopt;
    }
    std::optional<int> TableAppResources::GetInteger(std::string_view name, const Configuration& config) const {
        if (const char* v = Find("integer", name, "", config)) return std::atoi(v);
        return std::nullopt;
    }
    std::optional<bool> TableAppResources::GetBoolean(std::string_view name, const Configuration& config) const {
        if (const char* v = Find("bool", name, "", config)) return std::string_view(v) == "true";
        return std::nullopt;
    }
    std::optional<DrawableResource> TableAppResources::GetDrawable(std::string_view name, const Configuration& config) const {
        const bool night = config.IsNightModeActive();
        const DrawableEntry* best = nullptr;
        for (const DrawableEntry& d : m_Drawables) {
            if (name != d.name) continue;
            const std::string_view tag = d.tag;
            if (night && tag == "night") { best = &d; break; }
            if (tag.empty() && !best) best = &d;
        }
        if (!best) return std::nullopt;
        DrawableResource out;
        if (!best->vector.empty()) {
            out.vector = best->vector;
            return out;
        }
        // A bitmap: read from the source res/ folder (desktop builds run from the checkout).
        if (FILE* f = std::fopen(best->path, "rb")) {
            std::uint8_t buf[65536];
            std::size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.bitmap.insert(out.bitmap.end(), buf, buf + n);
            std::fclose(f);
        }
        if (out.bitmap.empty()) return std::nullopt;
        return out;
    }

}
