#pragma once

// App resources (ui res/StringResources.android.kt, PrimitiveResources.android.kt,
// ColorResources.android.kt; PainterResources in foundation/PainterResource.h): the app's own
// strings, plurals, arrays, colours, dimensions, integers, booleans and drawables, named by the
// header tools/gen-app-res writes from the app's res/ (R::string::app_name and so on, so a
// missing name is a compile error).
//
// On Android they are the app's Android resources, so locale fallback, plural rules and
// qualifiers (values-night, values-sw600dp) are the platform's. In a desktop window they come
// from tables the same tool writes from the same res/ folder.
//
// Reading one inside content subscribes to LocalConfiguration, so a locale, night mode or
// density change reads it again (LocalResources is replaced on configuration changes in
// Compose). A name the app has no value for throws, as Resources.NotFoundException does.

#include "kiln/text/LocaleFormat.h"
#include "kiln/ui/Configuration.h"
#include "kiln/ui/Geometry.h"
#include "kiln/ui/Locals.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kiln::ui {

    // Resource names by type (the generated R header's constants).
    struct StringRes { const char* name; };
    struct PluralsRes { const char* name; };
    struct StringArrayRes { const char* name; };
    struct IntArrayRes { const char* name; };
    struct ColorRes { const char* name; };
    struct DimenRes { const char* name; };
    struct IntegerRes { const char* name; };
    struct BoolRes { const char* name; };
    struct DrawableRes { const char* name; };

    // A vector drawable as resolved elements: each start tag (vector, group, clip-path, path)
    // with its attributes, and the end of each group. Values are resolved: colours "#AARRGGBB",
    // dimensions "<n>px" or "<n>dp", references and theme attributes looked up, gradients
    // "gradient:<type>;<startX>;<startY>;<endX>;<endY>;<centerX>;<centerY>;<radius>;<tile>;
    // <argb>@<offset>,..." (type linear / radial / sweep, tile clamp / repeat / mirror).
    struct VectorElement {
        std::string tag;
        bool end = false;
        std::vector<std::pair<std::string, std::string>> attrs;
        std::optional<std::string_view> Attr(std::string_view name) const {
            for (const auto& [k, v] : attrs) if (k == name) return std::string_view(v);
            return std::nullopt;
        }
    };
    // A drawable: an encoded bitmap (PNG, JPEG, WebP) or a vector drawable.
    struct DrawableResource {
        std::vector<std::uint8_t> bitmap;
        std::vector<VectorElement> vector;
        bool IsVector() const { return !vector.empty(); }
    };

    class AppResources {
    public:
        virtual ~AppResources() = default;
        // Values for the configuration, nullopt where the app has none. Strings are raw (format
        // specifiers left in; escapes and quotes resolved as Resources.getString does).
        virtual std::optional<std::string> GetString(std::string_view name, const Configuration& config) const = 0;
        virtual std::optional<std::string> GetQuantityString(std::string_view name, int quantity, const Configuration& config) const = 0;
        virtual std::optional<std::vector<std::string>> GetStringArray(std::string_view name, const Configuration& config) const = 0;
        virtual std::optional<std::vector<int>> GetIntArray(std::string_view name, const Configuration& config) const = 0;
        virtual std::optional<Color> GetColor(std::string_view name, const Configuration& config) const = 0;
        // In pixels for the density (and the font scale for sp), as Resources.getDimension.
        virtual std::optional<float> GetDimension(std::string_view name, const Configuration& config, float density, float fontScale) const = 0;
        virtual std::optional<int> GetInteger(std::string_view name, const Configuration& config) const = 0;
        virtual std::optional<bool> GetBoolean(std::string_view name, const Configuration& config) const = 0;
        virtual std::optional<DrawableResource> GetDrawable(std::string_view name, const Configuration& config) const = 0;
        // String.format(locale, pattern, args) for the configuration's first locale. The base
        // is kiln's FormatJava; the Android resources use the platform's String.format.
        virtual std::string Format(std::string_view pattern, std::span<const text::FormatArg> args, const Configuration& config) const;
    };

    // The app's resources: what the host sets (Android) or else the generated tables (desktop).
    void SetAppResources(std::shared_ptr<const AppResources> resources);
    // The generated tables register themselves here; used when the host sets none.
    void SetDefaultAppResources(std::shared_ptr<const AppResources> resources);
    std::shared_ptr<const AppResources> GetAppResources();

    // stringResource / pluralStringResource / stringArrayResource / integerArrayResource /
    // colorResource / dimensionResource / integerResource / booleanResource. Outside content
    // they read the default configuration.
    std::string StringResource(StringRes id);
    std::string StringResource(StringRes id, std::span<const text::FormatArg> formatArgs);
    template<typename... A>
        requires(sizeof...(A) > 0)
    std::string StringResource(StringRes id, A&&... formatArgs) {
        const text::FormatArg args[] = { text::FormatArg(std::forward<A>(formatArgs))... };
        return StringResource(id, std::span<const text::FormatArg>(args));
    }
    // count picks the plural form; formatArgs fill it (often count again, as in Compose).
    std::string PluralStringResource(PluralsRes id, int count);
    std::string PluralStringResource(PluralsRes id, int count, std::span<const text::FormatArg> formatArgs);
    template<typename... A>
        requires(sizeof...(A) > 0)
    std::string PluralStringResource(PluralsRes id, int count, A&&... formatArgs) {
        const text::FormatArg args[] = { text::FormatArg(std::forward<A>(formatArgs))... };
        return PluralStringResource(id, count, std::span<const text::FormatArg>(args));
    }
    std::vector<std::string> StringArrayResource(StringArrayRes id);
    std::vector<int> IntegerArrayResource(IntArrayRes id);
    Color ColorResource(ColorRes id);
    Dp DimensionResource(DimenRes id);
    int IntegerResource(IntegerRes id);
    bool BooleanResource(BoolRes id);
    // The drawable's data (foundation's PainterResource draws it).
    DrawableResource ReadDrawableResource(DrawableRes id);

    // CLDR plural categories, and the category of an integer count for a language (the
    // desktop tables' plural choice; Android uses the platform's rules).
    enum class PluralCategory : std::uint8_t { Zero, One, Two, Few, Many, Other };
    PluralCategory PluralCategoryOf(std::string_view languageTag, int count);
    std::string_view PluralCategoryName(PluralCategory c);

    // Generated tables (tools/gen-app-res): strings by locale tag ("" the default).
    class TableAppResources final : public AppResources {
    public:
        struct Entry { const char* type; const char* name; const char* tag; const char* quantity; const char* value; };
        struct ArrayEntry { const char* type; const char* name; const char* tag; std::vector<std::string> values; };
        struct DrawableEntry { const char* name; const char* tag; const char* path; std::vector<VectorElement> vector; };
        TableAppResources(std::span<const Entry> values, std::vector<ArrayEntry> arrays, std::vector<DrawableEntry> drawables)
            : m_Values(values), m_Arrays(std::move(arrays)), m_Drawables(std::move(drawables)) {}
        std::optional<std::string> GetString(std::string_view name, const Configuration& config) const override;
        std::optional<std::string> GetQuantityString(std::string_view name, int quantity, const Configuration& config) const override;
        std::optional<std::vector<std::string>> GetStringArray(std::string_view name, const Configuration& config) const override;
        std::optional<std::vector<int>> GetIntArray(std::string_view name, const Configuration& config) const override;
        std::optional<Color> GetColor(std::string_view name, const Configuration& config) const override;
        std::optional<float> GetDimension(std::string_view name, const Configuration& config, float density, float fontScale) const override;
        std::optional<int> GetInteger(std::string_view name, const Configuration& config) const override;
        std::optional<bool> GetBoolean(std::string_view name, const Configuration& config) const override;
        std::optional<DrawableResource> GetDrawable(std::string_view name, const Configuration& config) const override;

    private:
        const char* Find(std::string_view type, std::string_view name, std::string_view quantity, const Configuration& config) const;
        std::span<const Entry> m_Values;
        std::vector<ArrayEntry> m_Arrays;
        std::vector<DrawableEntry> m_Drawables;
    };

    // Parses "#RGB", "#ARGB", "#RRGGBB" or "#AARRGGBB".
    std::optional<Color> ParseResourceColor(std::string_view value);
    // A dimension value ("16dp", "14sp", "2px", "1in", "1mm", "1pt") in pixels.
    std::optional<float> ParseResourceDimension(std::string_view value, float density, float fontScale);

}
