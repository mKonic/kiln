#include "kiln/graphics/ColorSpace.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <mutex>

namespace kiln::gfx::colorspace {

    namespace {
        Mat3 Inverse3x3(const Mat3& m) {
            const float a = m[0], b = m[3], c = m[6], d = m[1], e = m[4], f = m[7], g = m[2], h = m[5], i = m[8];
            const float xA = e * i - f * h, xB = f * g - d * i, xC = d * h - e * g;
            const float det = a * xA + b * xB + c * xC;
            return { xA / det, xB / det, xC / det, (c * h - b * i) / det, (a * i - c * g) / det, (b * g - a * h) / det,
                     (b * f - c * e) / det, (c * d - a * f) / det, (a * e - b * d) / det };
        }
        Mat3 Mul3x3(const Mat3& l, const Mat3& r) {
            return { l[0] * r[0] + l[3] * r[1] + l[6] * r[2], l[1] * r[0] + l[4] * r[1] + l[7] * r[2], l[2] * r[0] + l[5] * r[1] + l[8] * r[2],
                     l[0] * r[3] + l[3] * r[4] + l[6] * r[5], l[1] * r[3] + l[4] * r[4] + l[7] * r[5], l[2] * r[3] + l[5] * r[4] + l[8] * r[5],
                     l[0] * r[6] + l[3] * r[7] + l[6] * r[8], l[1] * r[6] + l[4] * r[7] + l[7] * r[8], l[2] * r[6] + l[5] * r[7] + l[8] * r[8] };
        }
        Vec3 Mul3(const Mat3& l, Vec3 v) {
            return { l[0] * v[0] + l[3] * v[1] + l[6] * v[2], l[1] * v[0] + l[4] * v[1] + l[7] * v[2], l[2] * v[0] + l[5] * v[1] + l[8] * v[2] };
        }
        Mat3 Mul3x3Diag(const Vec3& d, const Mat3& r) {
            return { d[0] * r[0], d[1] * r[1], d[2] * r[2], d[0] * r[3], d[1] * r[4], d[2] * r[5], d[0] * r[6], d[1] * r[7], d[2] * r[8] };
        }
        Mat3 ChromaticAdaptation(const Mat3& matrix, const Vec3& srcWhite, const Vec3& dstWhite) {
            const Vec3 src = Mul3(matrix, srcWhite), dst = Mul3(matrix, dstWhite);
            return Mul3x3(Inverse3x3(matrix), Mul3x3Diag({ dst[0] / src[0], dst[1] / src[1], dst[2] / src[2] }, matrix));
        }
        bool Compare(WhitePoint a, WhitePoint b) { return std::abs(a.x - b.x) < 1e-3f && std::abs(a.y - b.y) < 1e-3f; }
        template<std::size_t N> bool Compare(const std::array<float, N>& a, const std::array<float, N>& b) {
            for (std::size_t i = 0; i < N; ++i) if (a[i] != b[i] && std::abs(a[i] - b[i]) > 1e-3f) return false;
            return true;
        }
        bool Compare(const TransferParameters& a, const std::optional<TransferParameters>& b) {
            return b && std::abs(a.a - b->a) < 1e-3 && std::abs(a.b - b->b) < 1e-3 && std::abs(a.c - b->c) < 1e-3 && std::abs(a.d - b->d) < 2e-3 &&
                   std::abs(a.e - b->e) < 1e-3 && std::abs(a.f - b->f) < 1e-3 && std::abs(a.gamma - b->gamma) < 1e-3;
        }

        double RcpResponse(double x, double a, double b, double c, double d, double g) { return x >= d * c ? (std::pow(x, 1.0 / g) - b) / a : x / c; }
        double Response(double x, double a, double b, double c, double d, double g) { return x >= d ? std::pow(a * x + b, g) : c * x; }
        double RcpResponse(double x, double a, double b, double c, double d, double e, double f, double g) {
            return x >= d * c ? (std::pow(x - e, 1.0 / g) - b) / a : (x - f) / c;
        }
        double Response(double x, double a, double b, double c, double d, double e, double f, double g) {
            return x >= d ? std::pow(a * x + b, g) + e : c * x + f;
        }
        double HlgOetf(const TransferParameters& p, double x) {
            const double sign = x < 0 ? -1.0 : 1.0;
            double absX = x * sign;
            const double R = 1.0 / p.a, G = 1.0 / p.b, a = 1.0 / p.c, b = p.d, c = p.e, K = p.f + 1.0;
            absX /= K;
            return sign * (absX <= 1 ? R * std::pow(absX, G) : a * std::log(absX - b) + c);
        }
        double HlgEotf(const TransferParameters& p, double x) {
            const double sign = x < 0 ? -1.0 : 1.0;
            const double absX = x * sign;
            const double R = p.a, G = p.b, a = p.c, b = p.d, c = p.e, K = p.f + 1.0;
            return K * sign * (absX * R <= 1 ? std::pow(absX * R, G) : std::exp((absX - c) * a) + b);
        }
        double PqOetf(const TransferParameters& p, double x) {
            const double sign = x < 0 ? -1.0 : 1.0;
            const double absX = x * sign;
            const double a = -p.a, b = p.d, c = 1.0 / p.f, d = p.b, e = -p.e, f = 1.0 / p.c;
            const double tmp = std::max(a + b * std::pow(absX, c), 0.0);
            return sign * std::pow(tmp / (d + e * std::pow(absX, c)), f);
        }
        double PqEotf(const TransferParameters& p, double x) {
            const double sign = x < 0 ? -1.0 : 1.0;
            const double absX = x * sign;
            const double tmp = std::max(p.a + p.b * std::pow(absX, p.c), 0.0);
            return sign * std::pow(tmp / (p.d + p.e * std::pow(absX, p.c)), p.f);
        }
        Rgb::Function GenerateOetf(const TransferParameters& f) {
            if (f.IsHLGish()) return [f](double x) { return HlgOetf(f, x); };
            if (f.IsPQish()) return [f](double x) { return PqOetf(f, x); };
            if (f.e == 0 && f.f == 0) return [f](double x) { return RcpResponse(x, f.a, f.b, f.c, f.d, f.gamma); };
            return [f](double x) { return RcpResponse(x, f.a, f.b, f.c, f.d, f.e, f.f, f.gamma); };
        }
        Rgb::Function GenerateEotf(const TransferParameters& f) {
            if (f.IsHLGish()) return [f](double x) { return HlgEotf(f, x); };
            if (f.IsPQish()) return [f](double x) { return PqEotf(f, x); };
            if (f.e == 0 && f.f == 0) return [f](double x) { return Response(x, f.a, f.b, f.c, f.d, f.gamma); };
            return [f](double x) { return Response(x, f.a, f.b, f.c, f.d, f.e, f.f, f.gamma); };
        }
        Rgb::Function GammaFunction(double gamma, bool inverse) {
            if (gamma == 1.0) return [](double x) { return x; };
            const double e = inverse ? 1.0 / gamma : gamma;
            return [e](double x) { return std::pow(x < 0.0 ? 0.0 : x, e); };
        }

        std::array<float, 6> ComputePrimaries(const Mat3& toXyz) {
            const Vec3 r = Mul3(toXyz, { 1, 0, 0 }), g = Mul3(toXyz, { 0, 1, 0 }), b = Mul3(toXyz, { 0, 0, 1 });
            const float rs = r[0] + r[1] + r[2], gs = g[0] + g[1] + g[2], bs = b[0] + b[1] + b[2];
            return { r[0] / rs, r[1] / rs, g[0] / gs, g[1] / gs, b[0] / bs, b[1] / bs };
        }
        WhitePoint ComputeWhitePoint(const Mat3& toXyz) {
            const Vec3 w = Mul3(toXyz, { 1, 1, 1 });
            const float s = w[0] + w[1] + w[2];
            return { w[0] / s, w[1] / s };
        }
        Mat3 ComputeXyzMatrix(const std::array<float, 6>& p, WhitePoint w) {
            const float rx = p[0], ry = p[1], gx = p[2], gy = p[3], bx = p[4], by = p[5], wx = w.x, wy = w.y;
            const float oneRxRy = (1 - rx) / ry, oneGxGy = (1 - gx) / gy, oneBxBy = (1 - bx) / by, oneWxWy = (1 - wx) / wy;
            const float rxRy = rx / ry, gxGy = gx / gy, bxBy = bx / by, wxWy = wx / wy;
            const float bY = ((oneWxWy - oneRxRy) * (gxGy - rxRy) - (wxWy - rxRy) * (oneGxGy - oneRxRy)) /
                             ((oneBxBy - oneRxRy) * (gxGy - rxRy) - (bxBy - rxRy) * (oneGxGy - oneRxRy));
            const float gY = (wxWy - rxRy - bY * (bxBy - rxRy)) / (gxGy - rxRy);
            const float rY = 1.0f - gY - bY;
            const float rYRy = rY / ry, gYGy = gY / gy, bYBy = bY / by;
            return { rYRy * rx, rY, rYRy * (1.0f - rx - ry), gYGy * gx, gY, gYGy * (1.0f - gx - gy), bYBy * bx, bY, bYBy * (1.0f - bx - by) };
        }
        float Area(const std::array<float, 6>& p) {
            const float det = p[0] * p[3] + p[1] * p[4] + p[2] * p[5] - p[3] * p[4] - p[1] * p[2] - p[0] * p[5];
            return std::abs(0.5f * det);
        }
        float Cross(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }
        bool Contains(const std::array<float, 6>& p1, const std::array<float, 6>& p2) {
            const std::array<float, 6> p0{ p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2], p1[3] - p2[3], p1[4] - p2[4], p1[5] - p2[5] };
            if (Cross(p0[0], p0[1], p2[0] - p2[4], p2[1] - p2[5]) < 0 || Cross(p2[0] - p2[2], p2[1] - p2[3], p0[0], p0[1]) < 0) return false;
            if (Cross(p0[2], p0[3], p2[2] - p2[0], p2[3] - p2[1]) < 0 || Cross(p2[2] - p2[4], p2[3] - p2[5], p0[2], p0[3]) < 0) return false;
            return !(Cross(p0[4], p0[5], p2[4] - p2[2], p2[5] - p2[3]) < 0 || Cross(p2[4] - p2[0], p2[5] - p2[1], p0[4], p0[5]) < 0);
        }
        constexpr std::array<float, 6> kSrgbPrimaries{ 0.640f, 0.330f, 0.300f, 0.600f, 0.150f, 0.060f };
        constexpr std::array<float, 6> kNtsc1953Primaries{ 0.67f, 0.33f, 0.21f, 0.71f, 0.14f, 0.08f };
        constexpr std::array<float, 6> kBt2020Primaries{ 0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f };
        const TransferParameters kSrgbParams{ 2.4, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045 };
        const TransferParameters kHlgParams{ -3.0, 2.0, 2.0, 1 / 0.17883277, 0.28466892, 0.55991073, -0.685490157 };
        const TransferParameters kPqParams{ -2.0, -1.555223, 1.860454, 32 / 2523.0, 2413 / 128.0, -2392 / 128.0, 8192 / 1305.0 };
        std::vector<float> V(const std::array<float, 6>& a) { return { a.begin(), a.end() }; }

        class Xyz final : public ColorSpace {
        public:
            Xyz(std::string name, int id) : ColorSpace(std::move(name), ColorModel::Xyz, id) {}
            bool IsWideGamut() const override { return true; }
            float GetMinValue(int) const override { return -2; }
            float GetMaxValue(int) const override { return 2; }
            Vec3 ToXyz(Vec3 v) const override { for (float& c : v) c = std::clamp(c, -2.0f, 2.0f); return v; }
            Vec3 FromXyz(Vec3 v) const override { for (float& c : v) c = std::clamp(c, -2.0f, 2.0f); return v; }
        };

        class Lab final : public ColorSpace {
        public:
            Lab(std::string name, int id) : ColorSpace(std::move(name), ColorModel::Lab, id) {}
            bool IsWideGamut() const override { return true; }
            float GetMinValue(int c) const override { return c == 0 ? 0.0f : -128.0f; }
            float GetMaxValue(int c) const override { return c == 0 ? 100.0f : 128.0f; }
            Vec3 ToXyz(Vec3 v) const override {
                v[0] = std::clamp(v[0], 0.0f, 100.0f);
                v[1] = std::clamp(v[1], -128.0f, 128.0f);
                v[2] = std::clamp(v[2], -128.0f, 128.0f);
                const float fy = (v[0] + 16.0f) / 116.0f, fx = fy + v[1] * 0.002f, fz = fy - v[2] * 0.005f;
                auto f = [](float t) { return t > kD ? t * t * t : (1.0f / kB) * (t - kC); };
                return { f(fx) * Illuminant::D50Xyz[0], f(fy) * Illuminant::D50Xyz[1], f(fz) * Illuminant::D50Xyz[2] };
            }
            Vec3 FromXyz(Vec3 v) const override {
                auto f = [](float t) { return t > kA ? std::cbrt(t) : kB * t + kC; };
                const float fx = f(v[0] / Illuminant::D50Xyz[0]), fy = f(v[1] / Illuminant::D50Xyz[1]), fz = f(v[2] / Illuminant::D50Xyz[2]);
                return { std::clamp(116.0f * fy - 16.0f, 0.0f, 100.0f), std::clamp(500.0f * (fx - fy), -128.0f, 128.0f),
                         std::clamp(200.0f * (fy - fz), -128.0f, 128.0f) };
            }
        private:
            static constexpr float kA = 216.0f / 24389.0f, kB = 841.0f / 108.0f, kC = 4.0f / 29.0f, kD = 6.0f / 29.0f;
        };

        class OklabSpace final : public ColorSpace {
        public:
            OklabSpace(std::string name, int id) : ColorSpace(std::move(name), ColorModel::Lab, id) {
                m_M1 = Mul3x3({ 0.8189330101f, 0.0329845436f, 0.0482003018f, 0.3618667424f, 0.9293118715f, 0.2643662691f, -0.1288597137f, 0.0361456387f,
                                0.6338517070f },
                              ChromaticAdaptation(Adaptation::Bradford().transform, Illuminant::D50.ToXyz(), Illuminant::D65.ToXyz()));
                m_M2 = { 0.2104542553f, 1.9779984951f, 0.0259040371f, 0.7936177850f, -2.4285922050f, 0.7827717662f, -0.0040720468f, 0.4505937099f, -0.8086757660f };
                m_InvM1 = Inverse3x3(m_M1);
                m_InvM2 = Inverse3x3(m_M2);
            }
            bool IsWideGamut() const override { return true; }
            float GetMinValue(int c) const override { return c == 0 ? 0.0f : -0.5f; }
            float GetMaxValue(int c) const override { return c == 0 ? 1.0f : 0.5f; }
            Vec3 ToXyz(Vec3 v) const override {
                v[0] = std::clamp(v[0], 0.0f, 1.0f);
                v[1] = std::clamp(v[1], -0.5f, 0.5f);
                v[2] = std::clamp(v[2], -0.5f, 0.5f);
                v = Mul3(m_InvM2, v);
                for (float& c : v) c = c * c * c;
                return Mul3(m_InvM1, v);
            }
            Vec3 FromXyz(Vec3 v) const override {
                v = Mul3(m_M1, v);
                for (float& c : v) c = std::cbrt(c);
                return Mul3(m_M2, v);
            }
        private:
            Mat3 m_M1, m_M2, m_InvM1, m_InvM2;
        };
    }

    const Adaptation& Adaptation::Bradford() {
        static const Adaptation a{ { 0.8951f, -0.7502f, 0.0389f, 0.2664f, 1.7135f, -0.0685f, -0.1614f, 0.0367f, 1.0296f } };
        return a;
    }
    const Adaptation& Adaptation::VonKries() {
        static const Adaptation a{ { 0.40024f, -0.22630f, 0.00000f, 0.70760f, 1.16532f, 0.00000f, -0.08081f, 0.04570f, 0.91822f } };
        return a;
    }
    const Adaptation& Adaptation::Ciecat02() {
        static const Adaptation a{ { 0.7328f, -0.7036f, 0.0030f, 0.4296f, 1.6975f, 0.0136f, -0.1624f, 0.0061f, 0.9834f } };
        return a;
    }

    // --- Rgb -------------------------------------------------------------------------------

    Rgb::Rgb(std::string name, std::vector<float> primaries, WhitePoint whitePoint, TransferParameters f, int id)
        : ColorSpace(std::move(name), ColorModel::Rgb, id), m_WhitePoint(whitePoint), m_Oetf(GenerateOetf(f)), m_Eotf(GenerateEotf(f)), m_Params(f) {
        Init(primaries, std::nullopt);
    }
    Rgb::Rgb(std::string name, std::vector<float> primaries, WhitePoint whitePoint, double gamma, float min, float max, int id)
        : ColorSpace(std::move(name), ColorModel::Rgb, id), m_WhitePoint(whitePoint), m_Oetf(GammaFunction(gamma, true)), m_Eotf(GammaFunction(gamma, false)),
          m_Min(min), m_Max(max), m_Params(TransferParameters{ gamma, 1.0, 0.0, 0.0, 0.0 }) {
        Init(primaries, std::nullopt);
    }
    Rgb::Rgb(std::string name, std::vector<float> primaries, WhitePoint whitePoint, Function oetf, Function eotf, float min, float max,
             std::optional<TransferParameters> f, int id)
        : ColorSpace(std::move(name), ColorModel::Rgb, id), m_WhitePoint(whitePoint), m_Oetf(std::move(oetf)), m_Eotf(std::move(eotf)), m_Min(min),
          m_Max(max), m_Params(f) {
        Init(primaries, std::nullopt);
    }
    Rgb::Rgb(std::string name, const Mat3& toXyz, TransferParameters f) : Rgb(std::move(name), V(ComputePrimaries(toXyz)), ComputeWhitePoint(toXyz), f) {}
    Rgb::Rgb(std::string name, const Mat3& toXyz, double gamma) : Rgb(std::move(name), V(ComputePrimaries(toXyz)), ComputeWhitePoint(toXyz), gamma) {}
    Rgb::Rgb(std::string name, const Mat3& toXyz, Function oetf, Function eotf)
        : Rgb(std::move(name), V(ComputePrimaries(toXyz)), ComputeWhitePoint(toXyz), std::move(oetf), std::move(eotf), 0.0f, 1.0f) {}
    Rgb::Rgb(const Rgb& s, const Mat3& transform, WhitePoint whitePoint)
        : ColorSpace(s.Name(), ColorModel::Rgb, -1), m_WhitePoint(whitePoint), m_Oetf(s.m_Oetf), m_Eotf(s.m_Eotf), m_Min(s.m_Min), m_Max(s.m_Max),
          m_Params(s.m_Params) {
        Init(V(s.m_Primaries), transform);
    }

    void Rgb::Init(const std::vector<float>& p, const std::optional<Mat3>& transform) {
        if (p.size() == 9) {
            float s = p[0] + p[1] + p[2];
            m_Primaries[0] = p[0] / s; m_Primaries[1] = p[1] / s;
            s = p[3] + p[4] + p[5];
            m_Primaries[2] = p[3] / s; m_Primaries[3] = p[4] / s;
            s = p[6] + p[7] + p[8];
            m_Primaries[4] = p[6] / s; m_Primaries[5] = p[7] / s;
        } else {
            for (std::size_t i = 0; i < 6 && i < p.size(); ++i) m_Primaries[i] = p[i];
        }
        m_Transform = transform ? *transform : ComputeXyzMatrix(m_Primaries, m_WhitePoint);
        m_Inverse = Inverse3x3(m_Transform);
        m_WideGamut = (Area(m_Primaries) / Area(kNtsc1953Primaries) > 0.9f && Contains(m_Primaries, kSrgbPrimaries)) || (m_Min < 0.0f && m_Max > 1.0f);
        // isSrgb: sRGB's primaries, white point, range and curves (to 1e-3 at every 8-bit step).
        if (Id() == 0) { m_IsSrgb = true; return; }
        m_IsSrgb = false;
        if (!Compare(m_Primaries, kSrgbPrimaries) || !Compare(m_WhitePoint, Illuminant::D65) || m_Min != 0.0f || m_Max != 1.0f) return;
        const Function so = GenerateOetf(kSrgbParams), se = GenerateEotf(kSrgbParams);
        for (double x = 0.0; x <= 1.0; x += 1 / 255.0)
            if (std::abs(m_Oetf(x) - so(x)) > 1e-3 || std::abs(m_Eotf(x) - se(x)) > 1e-3) return;
        m_IsSrgb = true;
    }

    Vec3 Rgb::ToXyz(Vec3 v) const { return Mul3(m_Transform, ToLinear(v)); }
    Vec3 Rgb::FromXyz(Vec3 v) const { return FromLinear(Mul3(m_Inverse, v)); }

    ColorSpacePtr Adapt(const ColorSpacePtr& space, WhitePoint whitePoint, const Adaptation& adaptation) {
        const auto* rgb = space && space->Model() == ColorModel::Rgb ? dynamic_cast<const Rgb*>(space.get()) : nullptr;
        if (!rgb || Compare(rgb->GetWhitePoint(), whitePoint)) return space;
        const Mat3 adapt = ChromaticAdaptation(adaptation.transform, rgb->GetWhitePoint().ToXyz(), whitePoint.ToXyz());
        return std::make_shared<Rgb>(*rgb, Mul3x3(adapt, rgb->Transform()), whitePoint);
    }

    // --- Connector -------------------------------------------------------------------------

    Connector::Connector(ColorSpacePtr source, ColorSpacePtr destination, RenderIntent intent)
        : m_Source(std::move(source)), m_Destination(std::move(destination)), m_Intent(intent) {
        if (m_Source == m_Destination) { m_Identity = true; return; }
        const auto* srcRgb = m_Source->Model() == ColorModel::Rgb ? dynamic_cast<const Rgb*>(m_Source.get()) : nullptr;
        const auto* dstRgb = m_Destination->Model() == ColorModel::Rgb ? dynamic_cast<const Rgb*>(m_Destination.get()) : nullptr;
        if (srcRgb && dstRgb) {
            // RgbConnector: one matrix between the linear spaces, white points adapted through D50.
            m_Rgb = true;
            if (Compare(srcRgb->GetWhitePoint(), dstRgb->GetWhitePoint())) {
                m_RgbTransform = Mul3x3(dstRgb->InverseTransform(), srcRgb->Transform());
            } else {
                Mat3 transform = srcRgb->Transform(), inverse = dstRgb->InverseTransform();
                const Vec3 srcXyz = srcRgb->GetWhitePoint().ToXyz(), dstXyz = dstRgb->GetWhitePoint().ToXyz();
                if (!Compare(srcRgb->GetWhitePoint(), Illuminant::D50))
                    transform = Mul3x3(ChromaticAdaptation(Adaptation::Bradford().transform, srcXyz, Illuminant::D50Xyz), srcRgb->Transform());
                if (!Compare(dstRgb->GetWhitePoint(), Illuminant::D50))
                    inverse = Inverse3x3(Mul3x3(ChromaticAdaptation(Adaptation::Bradford().transform, dstXyz, Illuminant::D50Xyz), dstRgb->Transform()));
                if (intent == RenderIntent::Absolute) transform = Mul3x3Diag({ srcXyz[0] / dstXyz[0], srcXyz[1] / dstXyz[1], srcXyz[2] / dstXyz[2] }, transform);
                m_RgbTransform = Mul3x3(inverse, transform);
            }
            return;
        }
        m_TransformSource = srcRgb ? Adapt(m_Source, Illuminant::D50) : m_Source;
        m_TransformDestination = dstRgb ? Adapt(m_Destination, Illuminant::D50) : m_Destination;
        if (intent == RenderIntent::Absolute && (srcRgb || dstRgb)) {
            const Rgb* rgb = srcRgb ? srcRgb : dstRgb;
            const Vec3 srcXyz = srcRgb ? rgb->GetWhitePoint().ToXyz() : Illuminant::D50Xyz;
            const Vec3 dstXyz = dstRgb ? rgb->GetWhitePoint().ToXyz() : Illuminant::D50Xyz;
            m_Scale = Vec3{ srcXyz[0] / dstXyz[0], srcXyz[1] / dstXyz[1], srcXyz[2] / dstXyz[2] };
        }
    }

    Vec3 Connector::Transform(Vec3 v) const {
        if (m_Identity) return v;
        if (m_Rgb) {
            const auto& src = static_cast<const Rgb&>(*m_Source);
            const auto& dst = static_cast<const Rgb&>(*m_Destination);
            return dst.FromLinear(Mul3(m_RgbTransform, src.ToLinear(v)));
        }
        Vec3 xyz = m_TransformSource->ToXyz(v);
        if (m_Scale) for (int i = 0; i < 3; ++i) xyz[std::size_t(i)] *= (*m_Scale)[std::size_t(i)];
        return m_TransformDestination->FromXyz(xyz);
    }

    Connector Connect(const ColorSpacePtr& source, const ColorSpacePtr& destination, RenderIntent intent) {
        return Connector(source, destination ? destination : ColorSpaces::Srgb(), intent);
    }

    // --- ColorSpaces -----------------------------------------------------------------------

#define KILN_SPACE(fn, ...) const ColorSpacePtr& ColorSpaces::fn() { static const ColorSpacePtr s = __VA_ARGS__; return s; }
    KILN_SPACE(Srgb, std::make_shared<Rgb>("sRGB IEC61966-2.1", V(kSrgbPrimaries), Illuminant::D65, kSrgbParams, 0))
    KILN_SPACE(LinearSrgb, std::make_shared<Rgb>("sRGB IEC61966-2.1 (Linear)", V(kSrgbPrimaries), Illuminant::D65, 1.0, 0.0f, 1.0f, 1))
    KILN_SPACE(ExtendedSrgb, std::make_shared<Rgb>(
        "scRGB-nl IEC 61966-2-2:2003", V(kSrgbPrimaries), Illuminant::D65,
        [](double x) { return std::copysign(RcpResponse(std::abs(x), 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045, 2.4), x); },
        [](double x) { return std::copysign(Response(std::abs(x), 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045, 2.4), x); }, -0.799f, 2.399f, kSrgbParams, 2))
    KILN_SPACE(LinearExtendedSrgb, std::make_shared<Rgb>("scRGB IEC 61966-2-2:2003", V(kSrgbPrimaries), Illuminant::D65, 1.0, -0.5f, 7.499f, 3))
    KILN_SPACE(Bt709, std::make_shared<Rgb>("Rec. ITU-R BT.709-5", V(kSrgbPrimaries), Illuminant::D65,
                                            TransferParameters{ 1 / 0.45, 1 / 1.099, 0.099 / 1.099, 1 / 4.5, 0.081 }, 4))
    KILN_SPACE(Bt2020, std::make_shared<Rgb>("Rec. ITU-R BT.2020-1", V(kBt2020Primaries), Illuminant::D65,
                                             TransferParameters{ 1 / 0.45, 1 / 1.0993, 0.0993 / 1.0993, 1 / 4.5, 0.08145 }, 5))
    KILN_SPACE(DciP3, std::make_shared<Rgb>("SMPTE RP 431-2-2007 DCI (P3)", std::vector<float>{ 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f },
                                            WhitePoint{ 0.314f, 0.351f }, 2.6, 0.0f, 1.0f, 6))
    KILN_SPACE(DisplayP3, std::make_shared<Rgb>("Display P3", std::vector<float>{ 0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f }, Illuminant::D65,
                                                kSrgbParams, 7))
    KILN_SPACE(Ntsc1953, std::make_shared<Rgb>("NTSC (1953)", V(kNtsc1953Primaries), Illuminant::C,
                                               TransferParameters{ 1 / 0.45, 1 / 1.099, 0.099 / 1.099, 1 / 4.5, 0.081 }, 8))
    KILN_SPACE(SmpteC, std::make_shared<Rgb>("SMPTE-C RGB", std::vector<float>{ 0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f }, Illuminant::D65,
                                             TransferParameters{ 1 / 0.45, 1 / 1.099, 0.099 / 1.099, 1 / 4.5, 0.081 }, 9))
    KILN_SPACE(AdobeRgb, std::make_shared<Rgb>("Adobe RGB (1998)", std::vector<float>{ 0.64f, 0.33f, 0.21f, 0.71f, 0.15f, 0.06f }, Illuminant::D65,
                                               2.2, 0.0f, 1.0f, 10))
    KILN_SPACE(ProPhotoRgb, std::make_shared<Rgb>("ROMM RGB ISO 22028-2:2013", std::vector<float>{ 0.7347f, 0.2653f, 0.1596f, 0.8404f, 0.0366f, 0.0001f },
                                                  Illuminant::D50, TransferParameters{ 1.8, 1.0, 0.0, 1 / 16.0, 0.031248 }, 11))
    KILN_SPACE(Aces, std::make_shared<Rgb>("SMPTE ST 2065-1:2012 ACES", std::vector<float>{ 0.73470f, 0.26530f, 0.0f, 1.0f, 0.00010f, -0.0770f },
                                           Illuminant::D60, 1.0, -65504.0f, 65504.0f, 12))
    KILN_SPACE(Acescg, std::make_shared<Rgb>("Academy S-2014-004 ACEScg", std::vector<float>{ 0.713f, 0.293f, 0.165f, 0.830f, 0.128f, 0.044f },
                                             Illuminant::D60, 1.0, -65504.0f, 65504.0f, 13))
    KILN_SPACE(CieXyz, std::make_shared<Xyz>("Generic XYZ", 14))
    KILN_SPACE(CieLab, std::make_shared<Lab>("Generic L*a*b*", 15))
    KILN_SPACE(Bt2020Hlg, std::make_shared<Rgb>("Hybrid Log Gamma encoding", V(kBt2020Primaries), Illuminant::D65,
                                                [](double x) { return HlgOetf(kHlgParams, x); }, [](double x) { return HlgEotf(kHlgParams, x); }, 0.0f,
                                                1.0f, kHlgParams, 17))
    KILN_SPACE(Bt2020Pq, std::make_shared<Rgb>("Perceptual Quantizer encoding", V(kBt2020Primaries), Illuminant::D65,
                                               [](double x) { return PqOetf(kPqParams, x); }, [](double x) { return PqEotf(kPqParams, x); }, 0.0f, 1.0f,
                                               kPqParams, 18))
    KILN_SPACE(Oklab, std::make_shared<OklabSpace>("Oklab", 19))
#undef KILN_SPACE

    const std::vector<ColorSpacePtr>& ColorSpaces::All() {
        static const std::vector<ColorSpacePtr> all{ Srgb(), LinearSrgb(), ExtendedSrgb(), LinearExtendedSrgb(), Bt709(), Bt2020(), DciP3(),
                                                     DisplayP3(), Ntsc1953(), SmpteC(), AdobeRgb(), ProPhotoRgb(), Aces(), Acescg(), CieXyz(),
                                                     CieLab(), Bt2020Hlg(), Bt2020Pq(), Oklab() };
        return all;
    }

    ColorSpacePtr ColorSpaces::Match(const Mat3& toXyzD50, const TransferParameters& function) {
        for (const ColorSpacePtr& s : All()) {
            if (s->Model() != ColorModel::Rgb) continue;
            const auto adapted = Adapt(s, Illuminant::D50);
            const auto& rgb = static_cast<const Rgb&>(*adapted);
            if (Compare(toXyzD50, rgb.Transform()) && Compare(function, rgb.GetTransferParameters())) return s;
        }
        return nullptr;
    }

    // --- Colours -----------------------------------------------------------------------------

    ui::Color ColorFromSpace(float r, float g, float b, float alpha, const ColorSpacePtr& space) {
        Vec3 v{ r, g, b };
        if (space && !space->IsSrgb()) v = Connect(space, ColorSpaces::Srgb()).Transform(v);
        auto byte = [](float c) { return std::uint32_t(std::lround(std::clamp(c, 0.0f, 1.0f) * 255.0f)); };
        return ui::Color((byte(alpha) << 24) | (byte(v[0]) << 16) | (byte(v[1]) << 8) | byte(v[2]));
    }

    std::array<float, 4> ConvertColor(ui::Color c, const ColorSpacePtr& space) {
        const float a = ((c.argb >> 24) & 0xff) / 255.0f;
        Vec3 v{ ((c.argb >> 16) & 0xff) / 255.0f, ((c.argb >> 8) & 0xff) / 255.0f, (c.argb & 0xff) / 255.0f };
        if (space && !space->IsSrgb()) v = Connect(ColorSpaces::Srgb(), space).Transform(v);
        return { v[0], v[1], v[2], a };
    }

}
