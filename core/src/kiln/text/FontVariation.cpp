// Ported from androidx.compose.ui.text.font.FontVariation.
#include "kiln/text/TextStyle.h"

#include <format>
#include <stdexcept>

namespace kiln::text::FontVariation {

    namespace {
        void Require(bool ok, const std::string& message) {
            if (!ok) throw std::invalid_argument(message);
        }
        void ValidateUniqueAxes(const std::vector<Setting>& settings) {
            for (std::size_t i = 0; i < settings.size(); ++i)
                for (std::size_t j = i + 1; j < settings.size(); ++j)
                    Require(settings[i].axisName != settings[j].axisName, "'" + settings[i].axisName + "' must be unique");
        }
        std::vector<Setting> MergeLists(const std::vector<Setting>& base, const std::vector<Setting>& overrides) {
            std::vector<Setting> result = base;
            for (const Setting& o : overrides) {
                auto it = std::find_if(result.begin(), result.end(), [&](const Setting& s) { return s.axisName == o.axisName; });
                if (it != result.end()) *it = o;
                else result.push_back(o);
            }
            return result;
        }
    }

    float Setting::ToVariationValue(const ui::Density* density) const {
        if (!needsDensity) return value;
        // An sp size looks the same on every density; only the font scale changes it.
        Require(density != nullptr, "density must not be null");
        return value * density->fontScale;
    }

    Setting MakeSetting(std::string name, float value) {
        Require(name.size() == 4, "Name must be exactly four characters. Actual: '" + name + "'");
        return { std::move(name), value };
    }
    Setting Weight(int value) {
        Require(value >= 1 && value <= 1000, std::format("'wght' value must be in [1, 1000]. Actual: {}", value));
        return { "wght", float(value) };
    }
    Setting Italic(float value) {
        Require(value >= 0 && value <= 1, std::format("'ital' must be in 0.0f..1.0f. Actual: {}", value));
        return { "ital", value };
    }
    Setting Slant(float value) {
        Require(value >= -90 && value <= 90, std::format("'slnt' must be in -90f..90f. Actual: {}", value));
        return { "slnt", value };
    }
    Setting Width(float value) {
        Require(value > 0, std::format("'wdth' must be strictly > 0.0f. Actual: {}", value));
        return { "wdth", value };
    }
    Setting Grade(int value) {
        Require(value >= -1000 && value <= 1000, "'GRAD' must be in -1000..1000");
        return { "GRAD", float(value) };
    }
    Setting OpticalSizing(TextUnit textSize) {
        Require(textSize.IsSp(), "'opsz' must be provided in sp units");
        return { "opsz", textSize.value, true };
    }

    Settings::Settings(std::vector<Setting> list) : settings(std::move(list)) { ValidateUniqueAxes(settings); }

    bool Settings::NeedsDensity() const {
        return std::any_of(settings.begin(), settings.end(), [](const Setting& s) { return s.needsDensity; });
    }

    Settings Settings::Merge(const Settings& other) const {
        if (other.settings.empty()) return *this;
        if (settings.empty()) return other;
        return Settings(MergeLists(settings, other.settings));
    }

    Settings Settings::Merge(const std::vector<Setting>& overrides) const {
        if (overrides.empty()) return *this;
        ValidateUniqueAxes(overrides);
        if (settings.empty()) return Settings(overrides);
        return Settings(MergeLists(settings, overrides));
    }

    std::string Settings::ToString(const ui::Density* density) const {
        std::string out;
        for (const Setting& s : settings) {
            if (!out.empty()) out += ", ";
            out += std::format("'{}' {}", s.axisName, s.ToVariationValue(density));
        }
        return out;
    }

    Settings MakeSettings(FontWeight weight, FontStyle style, std::vector<Setting> settings) {
        std::vector<Setting> all{ Weight(weight.weight), Italic(style == FontStyle::Italic ? 1.0f : 0.0f) };
        all.insert(all.end(), settings.begin(), settings.end());
        return Settings(std::move(all));
    }

}
