#pragma once

// Colour spaces (ui-graphics colorspace/: ColorSpace.kt, Rgb.kt, Xyz.kt, Lab.kt, Oklab.kt,
// Connector.kt, Adaptation.kt, Illuminant.kt, WhitePoint.kt, TransferParameters.kt,
// RenderIntent.kt, ColorModel.kt, ColorSpaces.kt): named RGB spaces with their primaries, white
// point and transfer functions, CIE XYZ and L*a*b*, Oklab, and connectors that convert colours
// between any two (through XYZ D50, adapting white points with Bradford).
//
// kiln's Color holds sRGB: ColorFromSpace converts a colour given in another space to it, and
// ConvertColor gives a colour's components in another space. Matrices are column-major 3x3, as
// Compose's float arrays.

#include "kiln/ui/Geometry.h"

#include <array>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::gfx::colorspace {

    using Vec3 = std::array<float, 3>;
    using Mat3 = std::array<float, 9>;

    // WhitePoint: chromaticity (x, y).
    struct WhitePoint {
        float x = 0, y = 0;
        static WhitePoint FromXyz(float x, float y, float z) { const float s = x + y + z; return { x / s, y / s }; }
        Vec3 ToXyz() const { return { x / y, 1.0f, (1.0f - x - y) / y }; }
        bool operator==(const WhitePoint&) const = default;
    };

    namespace Illuminant {
        inline constexpr WhitePoint A{ 0.44757f, 0.40745f }, B{ 0.34842f, 0.35161f }, C{ 0.31006f, 0.31616f }, D50{ 0.34567f, 0.35850f },
            D55{ 0.33242f, 0.34743f }, D60{ 0.32168f, 0.33767f }, D65{ 0.31271f, 0.32902f }, D75{ 0.29902f, 0.31485f }, E{ 0.33333f, 0.33333f };
        inline constexpr Vec3 D50Xyz{ 0.964212f, 1.0f, 0.825188f };
    }

    // TransferParameters: the ICC parametric curve y = (a x + b)^gamma + e for x >= d, else
    // c x + f; gamma -2 is SMPTE ST 2084 (PQ), -3 is HLG.
    struct TransferParameters {
        double gamma = 1, a = 1, b = 0, c = 0, d = 0, e = 0, f = 0;
        bool IsPQish() const { return gamma == -2.0; }
        bool IsHLGish() const { return gamma == -3.0; }
        bool operator==(const TransferParameters&) const = default;
    };

    enum class ColorModel : std::uint8_t { Rgb, Xyz, Lab, Cmyk };
    inline int ComponentCount(ColorModel m) { return m == ColorModel::Cmyk ? 4 : 3; }

    // Adaptation: a chromatic adaptation transform (cone response matrix).
    struct Adaptation {
        Mat3 transform;
        static const Adaptation& Bradford();
        static const Adaptation& VonKries();
        static const Adaptation& Ciecat02();
    };

    enum class RenderIntent : std::uint8_t { Perceptual, Relative, Saturation, Absolute };

    class ColorSpace {
    public:
        ColorSpace(std::string name, ColorModel model, int id = -1) : m_Name(std::move(name)), m_Model(model), m_Id(id) {}
        virtual ~ColorSpace() = default;
        const std::string& Name() const { return m_Name; }
        ColorModel Model() const { return m_Model; }
        int ComponentCount() const { return colorspace::ComponentCount(m_Model); }
        int Id() const { return m_Id; }
        virtual bool IsWideGamut() const = 0;
        virtual bool IsSrgb() const { return false; }
        virtual float GetMinValue(int component) const = 0;
        virtual float GetMaxValue(int component) const = 0;
        virtual Vec3 ToXyz(Vec3 v) const = 0;
        virtual Vec3 FromXyz(Vec3 v) const = 0;
        Vec3 ToXyz(float a, float b, float c) const { return ToXyz(Vec3{ a, b, c }); }
        Vec3 FromXyz(float x, float y, float z) const { return FromXyz(Vec3{ x, y, z }); }
        bool operator==(const ColorSpace& o) const { return m_Id == o.m_Id && m_Name == o.m_Name && m_Model == o.m_Model; }

    private:
        std::string m_Name;
        ColorModel m_Model;
        int m_Id;
    };
    using ColorSpacePtr = std::shared_ptr<const ColorSpace>;

    // Rgb: an RGB space from its primaries (xy, 6 values, or XYZ, 9), white point and transfer
    // functions (parametric, a gamma, or functions with a value range).
    class Rgb final : public ColorSpace {
    public:
        using Function = std::function<double(double)>;
        Rgb(std::string name, std::vector<float> primaries, WhitePoint whitePoint, TransferParameters function, int id = -1);
        Rgb(std::string name, std::vector<float> primaries, WhitePoint whitePoint, double gamma, float min = 0, float max = 1, int id = -1);
        Rgb(std::string name, std::vector<float> primaries, WhitePoint whitePoint, Function oetf, Function eotf, float min, float max,
            std::optional<TransferParameters> function = std::nullopt, int id = -1);
        // From a matrix to XYZ (its primaries and white point follow).
        Rgb(std::string name, const Mat3& toXyz, TransferParameters function);
        Rgb(std::string name, const Mat3& toXyz, double gamma);
        Rgb(std::string name, const Mat3& toXyz, Function oetf, Function eotf);
        // The same space adapted to another white point (adapt).
        Rgb(const Rgb& space, const Mat3& transform, WhitePoint whitePoint);

        const WhitePoint& GetWhitePoint() const { return m_WhitePoint; }
        const std::array<float, 6>& Primaries() const { return m_Primaries; }
        const Mat3& Transform() const { return m_Transform; }
        const Mat3& InverseTransform() const { return m_Inverse; }
        const std::optional<TransferParameters>& GetTransferParameters() const { return m_Params; }
        // oetf / eotf, with the value range applied.
        double Oetf(double x) const { return std::clamp(m_Oetf(x), double(m_Min), double(m_Max)); }
        double Eotf(double x) const { return m_Eotf(std::clamp(x, double(m_Min), double(m_Max))); }
        Vec3 ToLinear(Vec3 v) const { for (float& c : v) c = float(Eotf(c)); return v; }
        Vec3 FromLinear(Vec3 v) const { for (float& c : v) c = float(Oetf(c)); return v; }

        bool IsWideGamut() const override { return m_WideGamut; }
        bool IsSrgb() const override { return m_IsSrgb; }
        float GetMinValue(int) const override { return m_Min; }
        float GetMaxValue(int) const override { return m_Max; }
        Vec3 ToXyz(Vec3 v) const override;
        Vec3 FromXyz(Vec3 v) const override;
        using ColorSpace::FromXyz;
        using ColorSpace::ToXyz;

    private:
        void Init(const std::vector<float>& primaries, const std::optional<Mat3>& transform);
        WhitePoint m_WhitePoint;
        Function m_Oetf, m_Eotf;
        float m_Min = 0, m_Max = 1;
        std::optional<TransferParameters> m_Params;
        std::array<float, 6> m_Primaries{};
        Mat3 m_Transform{}, m_Inverse{};
        bool m_WideGamut = false, m_IsSrgb = false;
    };

    // adapt: an RGB space with another white point (others unchanged).
    ColorSpacePtr Adapt(const ColorSpacePtr& space, WhitePoint whitePoint, const Adaptation& adaptation = Adaptation::Bradford());

    // Connector: converts colours from one space to another.
    class Connector {
    public:
        Connector(ColorSpacePtr source, ColorSpacePtr destination, RenderIntent intent);
        const ColorSpacePtr& Source() const { return m_Source; }
        const ColorSpacePtr& Destination() const { return m_Destination; }
        RenderIntent GetRenderIntent() const { return m_Intent; }
        Vec3 Transform(Vec3 v) const;
        Vec3 Transform(float a, float b, float c) const { return Transform(Vec3{ a, b, c }); }

    private:
        ColorSpacePtr m_Source, m_Destination, m_TransformSource, m_TransformDestination;
        RenderIntent m_Intent;
        bool m_Identity = false, m_Rgb = false;
        std::optional<Vec3> m_Scale;   // Absolute between RGB and XYZ / Lab
        Mat3 m_RgbTransform{};
    };

    // connect(destination, intent).
    Connector Connect(const ColorSpacePtr& source, const ColorSpacePtr& destination = nullptr, RenderIntent intent = RenderIntent::Perceptual);

    // ColorSpaces: the named spaces.
    struct ColorSpaces {
        static const ColorSpacePtr& Srgb();
        static const ColorSpacePtr& LinearSrgb();
        static const ColorSpacePtr& ExtendedSrgb();
        static const ColorSpacePtr& LinearExtendedSrgb();
        static const ColorSpacePtr& Bt709();
        static const ColorSpacePtr& Bt2020();
        static const ColorSpacePtr& DciP3();
        static const ColorSpacePtr& DisplayP3();
        static const ColorSpacePtr& Ntsc1953();
        static const ColorSpacePtr& SmpteC();
        static const ColorSpacePtr& AdobeRgb();
        static const ColorSpacePtr& ProPhotoRgb();
        static const ColorSpacePtr& Aces();
        static const ColorSpacePtr& Acescg();
        static const ColorSpacePtr& CieXyz();
        static const ColorSpacePtr& CieLab();
        static const ColorSpacePtr& Bt2020Hlg();
        static const ColorSpacePtr& Bt2020Pq();
        static const ColorSpacePtr& Oklab();
        // match: the named RGB space with this XYZ D50 matrix and transfer function, or null.
        static ColorSpacePtr Match(const Mat3& toXyzD50, const TransferParameters& function);
        static const std::vector<ColorSpacePtr>& All();
    };

    // Color(r, g, b, alpha, colorSpace), held as sRGB (clamped to its range).
    ui::Color ColorFromSpace(float r, float g, float b, float alpha, const ColorSpacePtr& space);
    // Color.convert(colorSpace): the components in `space`, then alpha.
    std::array<float, 4> ConvertColor(ui::Color color, const ColorSpacePtr& space);

}
