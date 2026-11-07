#include "kiln/material3/internal/colorUtil/ColorUtil.h"

#include "kiln/material3/internal/colorUtil/Tables.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3::colorutil {

    namespace {
        using namespace tables;
        constexpr double kPi = 3.141592653589793;   // Math.PI
        constexpr float kPiF = float(kPi);           // PI.toFloat()

        // kotlin.math on Float: computed in double, rounded to float.
        float PowF(float a, float b) { return float(std::pow(double(a), double(b))); }
        float SqrtF(float a) { return float(std::sqrt(double(a))); }
        float CosF(float a) { return float(std::cos(double(a))); }
        float SinF(float a) { return float(std::sin(double(a))); }
        float LnF(float a) { return float(std::log(double(a))); }
        float Atan2F(float y, float x) { return float(std::atan2(double(y), double(x))); }
        float SignF(float a) { return a > 0 ? 1.0f : a < 0 ? -1.0f : a; }   // Math.signum
        // Math.round(double) / Math.round(float): floor(x + 0.5).
        long long JavaRound(double v) { return (long long)std::floor(v + 0.5); }

        int Red(std::int32_t argb) { return (argb >> 16) & 0xff; }
        int Green(std::int32_t argb) { return (argb >> 8) & 0xff; }
        int Blue(std::int32_t argb) { return argb & 0xff; }

        int ClampInt(int min, int max, int input) { return input < min ? min : input > max ? max : input; }
        std::int32_t ArgbFromRgb(int red, int green, int blue) {
            return std::int32_t((255u << 24) | (std::uint32_t(red & 255) << 16) | (std::uint32_t(green & 255) << 8) | std::uint32_t(blue & 255));
        }
        int Delinearized(double rgbComponent) {
            const double normalized = rgbComponent / 100.0;
            const double d = normalized <= 0.0031308 ? normalized * 12.92 : 1.055 * std::pow(normalized, 1.0 / 2.4) - 0.055;
            return ClampInt(0, 255, int(JavaRound(d * 255.0)));
        }
        std::int32_t ArgbFromXyz(double x, double y, double z) {
            const auto& m = XYZ_TO_SRGB;
            const double linearR = m[0][0] * x + m[0][1] * y + m[0][2] * z;
            const double linearG = m[1][0] * x + m[1][1] * y + m[1][2] * z;
            const double linearB = m[2][0] * x + m[2][1] * y + m[2][2] * z;
            return ArgbFromRgb(Delinearized(linearR), Delinearized(linearG), Delinearized(linearB));
        }
        float Linearized(int rgbComponent) {
            const float normalized = float(rgbComponent) / 255.0f;
            return normalized <= 0.04045f ? normalized / 12.92f * 100.0f : PowF((normalized + 0.055f) / 1.055f, 2.4f) * 100.0f;
        }
        float YFromInt(std::int32_t argb) {
            const float r = Linearized(Red(argb)), g = Linearized(Green(argb)), b = Linearized(Blue(argb));
            const auto& m = SRGB_TO_XYZ;
            return float((r * m[1][0]) + (g * m[1][1]) + (b * m[1][2]));
        }
        void XyzFromInt(std::int32_t argb, float out[3]) {
            const float r = Linearized(Red(argb)), g = Linearized(Green(argb)), b = Linearized(Blue(argb));
            const auto& m = SRGB_TO_XYZ;
            out[0] = float((r * m[0][0]) + (g * m[0][1]) + (b * m[0][2]));
            out[1] = float((r * m[1][0]) + (g * m[1][1]) + (b * m[1][2]));
            out[2] = float((r * m[2][0]) + (g * m[2][1]) + (b * m[2][2]));
        }
        float LstarFromY(float y) {
            float yPrime = y / 100.0f;
            const float e = 216.0f / 24389.0f;
            if (yPrime <= e) return (24389.0f / 27.0f) * yPrime;
            return 116.0f * float(std::cbrt(double(yPrime))) - 16.0f;
        }
        // androidx.core.graphics.ColorUtils.XYZToColor
        std::int32_t XyzToColor(double x, double y, double z) {
            double r = (x * 3.2406 + y * -1.5372 + z * -0.4986) / 100;
            double g = (x * -0.9689 + y * 1.8758 + z * 0.0415) / 100;
            double b = (x * 0.0557 + y * -0.2040 + z * 1.0570) / 100;
            r = r > 0.0031308 ? 1.055 * std::pow(r, 1 / 2.4) - 0.055 : 12.92 * r;
            g = g > 0.0031308 ? 1.055 * std::pow(g, 1 / 2.4) - 0.055 : 12.92 * g;
            b = b > 0.0031308 ? 1.055 * std::pow(b, 1 / 2.4) - 0.055 : 12.92 * b;
            auto constrain = [](int v) { return v < 0 ? 0 : std::min(v, 255); };
            return ArgbFromRgb(constrain(int(JavaRound(r * 255))), constrain(int(JavaRound(g * 255))), constrain(int(JavaRound(b * 255))));
        }
    }

    // --- CamUtils -------------------------------------------------------------------------------

    namespace CamUtils {
        int Signum(double num) { return num < 0 ? -1 : num == 0.0 ? 0 : 1; }

        std::int32_t ArgbFromLstar(double lstar) {
            const double fy = (lstar + 16.0) / 116.0;
            const double kappa = 24389.0 / 27.0;
            const double epsilon = 216.0 / 24389.0;
            const bool lExceedsEpsilonKappa = lstar > 8.0;
            const double y = lExceedsEpsilonKappa ? fy * fy * fy : lstar / kappa;
            const bool cubeExceedEpsilon = fy * fy * fy > epsilon;
            const double x = cubeExceedEpsilon ? fy * fy * fy : lstar / kappa;
            const double z = cubeExceedEpsilon ? fy * fy * fy : lstar / kappa;
            return ArgbFromXyz(x * WHITE_POINT_D65[0], y * WHITE_POINT_D65[1], z * WHITE_POINT_D65[2]);
        }

        std::int32_t ArgbFromLinrgbComponents(double r, double g, double b) {
            return ArgbFromRgb(Delinearized(r), Delinearized(g), Delinearized(b));
        }

        std::int32_t IntFromLstar(float lstar) {
            if (lstar < 1) return std::int32_t(0xFF000000u);   // -0x1000000
            if (lstar > 99) return -1;
            const float fy = (lstar + 16.0f) / 116.0f;
            const float fz = fy;
            const float kappa = 24389.0f / 27.0f;
            const float epsilon = 216.0f / 24389.0f;
            const bool lExceedsEpsilonKappa = lstar > 8.0f;
            const float yT = lExceedsEpsilonKappa ? fy * fy * fy : lstar / kappa;
            const bool cubeExceedEpsilon = (fy * fy * fy) > epsilon;
            const float xT = cubeExceedEpsilon ? fy * fy * fy : (116.0f * fy - 16.0f) / kappa;
            const float zT = cubeExceedEpsilon ? fz * fz * fz : (116.0f * fy - 16.0f) / kappa;
            return XyzToColor(double(xT * WHITE_POINT_D65[0]), double(yT * WHITE_POINT_D65[1]), double(zT * WHITE_POINT_D65[2]));
        }

        float LstarFromInt(std::int32_t argb) { return LstarFromY(YFromInt(argb)); }

        double YFromLstar(double lstar) {
            const double ke = 8.0;
            return lstar > ke ? std::pow((lstar + 16.0) / 116.0, 3.0) * 100.0 : lstar / (24389.0 / 27.0) * 100.0;
        }
    }

    // --- Frame ----------------------------------------------------------------------------------

    namespace {
        float Lerp(float start, float stop, float amount) { return (1.0f - amount) * start + amount * stop; }
    }

    Frame Frame::Make(const float wp[3], float adaptingLuminance, float backgroundLstar, float surround, bool discountingIlluminant) {
        const auto& m = XYZ_TO_CAM16RGB;
        const float rW = (wp[0] * m[0][0]) + (wp[1] * m[0][1]) + (wp[2] * m[0][2]);
        const float gW = (wp[0] * m[1][0]) + (wp[1] * m[1][1]) + (wp[2] * m[1][2]);
        const float bW = (wp[0] * m[2][0]) + (wp[1] * m[2][1]) + (wp[2] * m[2][2]);
        const float f = 0.8f + (surround / 10.0f);
        const float c = (f >= 0.9) ? Lerp(0.59f, 0.69f, ((f - 0.9f) * 10.0f)) : Lerp(0.525f, 0.59f, ((f - 0.8f) * 10.0f));
        float d = discountingIlluminant
                      ? 1.0f
                      : f * (1.0f - ((1.0f / 3.6f) * float(std::exp(double((-adaptingLuminance - 42.0f) / 92.0f)))));
        d = (d > 1.0) ? 1.0f : (d < 0.0) ? 0.0f : d;
        const float nc = f;
        Frame fr{};
        fr.rgbD[0] = d * (100.0f / rW) + 1.0f - d;
        fr.rgbD[1] = d * (100.0f / gW) + 1.0f - d;
        fr.rgbD[2] = d * (100.0f / bW) + 1.0f - d;
        const float k = 1.0f / (5.0f * adaptingLuminance + 1.0f);
        const float k4 = k * k * k * k;
        const float k4F = 1.0f - k4;
        const float fl = (k4 * adaptingLuminance) + (0.1f * k4F * k4F * float(std::cbrt(5.0 * double(adaptingLuminance))));
        const float n = float(CamUtils::YFromLstar(double(backgroundLstar))) / wp[1];
        const float z = 1.48f + SqrtF(n);
        const float nbb = 0.725f / PowF(n, 0.2f);
        const float rgbAFactors[3] = { PowF(fl * fr.rgbD[0] * rW / 100.0f, 0.42f), PowF(fl * fr.rgbD[1] * gW / 100.0f, 0.42f),
                                       PowF(fl * fr.rgbD[2] * bW / 100.0f, 0.42f) };
        const float rgbA[3] = { (400.0f * rgbAFactors[0]) / (rgbAFactors[0] + 27.13f), (400.0f * rgbAFactors[1]) / (rgbAFactors[1] + 27.13f),
                                (400.0f * rgbAFactors[2]) / (rgbAFactors[2] + 27.13f) };
        const float aw = ((2.0f * rgbA[0]) + rgbA[1] + (0.05f * rgbA[2])) * nbb;
        fr.n = n;
        fr.aw = aw;
        fr.nbb = nbb;
        fr.ncb = nbb;
        fr.c = c;
        fr.nc = nc;
        fr.fl = fl;
        fr.flRoot = PowF(fl, 0.25f);
        fr.z = z;
        return fr;
    }

    const Frame& Frame::Default() {
        static const Frame frame = Make(WHITE_POINT_D65, float(200.0 / kPi * CamUtils::YFromLstar(50.0) / 100.0), 50.0f, 2.0f, false);
        return frame;
    }

    // --- Cam ------------------------------------------------------------------------------------

    float Cam::Distance(const Cam& o) const {
        const float dJ = jstar - o.jstar, dA = astar - o.astar, dB = bstar - o.bstar;
        const double dEPrime = std::sqrt(double(dJ * dJ + dA * dA + dB * dB));
        const double dE = 1.41 * std::pow(dEPrime, 0.63);
        return float(dE);
    }

    Cam Cam::FromInt(std::int32_t argb) {
        // fromIntInFrame(argb, Frame.Default)
        const Frame& frame = Frame::Default();
        float xyz[3];
        XyzFromInt(argb, xyz);
        const auto& m = XYZ_TO_CAM16RGB;
        const float rT = (xyz[0] * m[0][0]) + (xyz[1] * m[0][1]) + (xyz[2] * m[0][2]);
        const float gT = (xyz[0] * m[1][0]) + (xyz[1] * m[1][1]) + (xyz[2] * m[1][2]);
        const float bT = (xyz[0] * m[2][0]) + (xyz[1] * m[2][1]) + (xyz[2] * m[2][2]);
        const float rD = frame.rgbD[0] * rT, gD = frame.rgbD[1] * gT, bD = frame.rgbD[2] * bT;
        const float rAF = PowF(frame.fl * std::fabs(rD) / 100.0f, 0.42f);
        const float gAF = PowF(frame.fl * std::fabs(gD) / 100.0f, 0.42f);
        const float bAF = PowF(frame.fl * std::fabs(bD) / 100.0f, 0.42f);
        const float rA = (SignF(rD) * 400.0f * rAF / (rAF + 27.13f));
        const float gA = (SignF(gD) * 400.0f * gAF / (gAF + 27.13f));
        const float bA = (SignF(bD) * 400.0f * bAF / (bAF + 27.13f));
        const float a = (11.0f * rA + -12.0f * gA + bA) / 11.0f;
        const float b = (rA + gA - 2.0f * bA) / 9.0f;
        const float u = (20.0f * rA + 20.0f * gA + 21.0f * bA) / 20.0f;
        const float p2 = (40.0f * rA + 20.0f * gA + bA) / 20.0f;
        const float atan2 = Atan2F(b, a);
        const float atanDegrees = atan2 * 180.0f / kPiF;
        const float hue = atanDegrees < 0 ? atanDegrees + 360.0f : atanDegrees >= 360 ? atanDegrees - 360.0f : atanDegrees;
        const float hueRadians = hue * kPiF / 180.0f;
        const float ac = p2 * frame.nbb;
        const float j = 100.0f * PowF(ac / frame.aw, frame.c * frame.z);
        const float huePrime = (hue < 20.14) ? hue + 360 : hue;
        const float eHue = 0.25f * (CosF(huePrime * kPiF / 180.0f + 2.0f) + 3.8f);
        const float p1 = 50000.0f / 13.0f * eHue * frame.nc * frame.ncb;
        const float t = p1 * SqrtF(a * a + b * b) / (u + 0.305f);
        const float alpha = PowF(t, 0.9f) * PowF(1.64f - PowF(0.29f, frame.n), 0.73f);
        const float c = alpha * SqrtF(j / 100.0f);
        const float mm = c * frame.flRoot;
        const float s = 50.0f * SqrtF((alpha * frame.c) / (frame.aw + 4.0f));
        const float jstar = (1.0f + 100.0f * 0.007f) * j / (1.0f + 0.007f * j);
        const float mstar = 1.0f / 0.0228f * LnF(1.0f + 0.0228f * mm);
        const float astar = mstar * CosF(hueRadians);
        const float bstar = mstar * SinF(hueRadians);
        return Cam{ hue, c, j, mm, s, jstar, astar, bstar };
    }

    std::int32_t Cam::GetInt(float hue, float chroma, float lstar) {
        // getInt(hue, chroma, lstar, Frame.Default): the default frame goes to the HCT solver
        // (material3 never calls it with another frame).
        return HctSolver::SolveToInt(double(hue), double(chroma), double(lstar));
    }

    // --- HCTSolver ------------------------------------------------------------------------------

    namespace {
        struct V3 { double v[3]; double& operator[](int i) { return v[i]; } double operator[](int i) const { return v[i]; } };

        double SanitizeRadians(double angle) { return std::fmod(angle + kPi * 8, kPi * 2); }
        double TrueDelinearized(double rgbComponent) {
            const double normalized = rgbComponent / 100.0;
            const double d = normalized <= 0.0031308 ? normalized * 12.92 : 1.055 * std::pow(normalized, 1.0 / 2.4) - 0.055;
            return d * 255;
        }
        double ChromaticAdaptation(double component) {
            const double af = std::pow(std::fabs(component), 0.42);
            return CamUtils::Signum(component) * 400.0 * af / (af + 27.13);
        }
        double HueOf(const V3& linrgb) {
            const auto& m = SCALED_DISCOUNT_FROM_LINRGB;
            const double rD = linrgb[0] * m[0][0] + linrgb[1] * m[0][1] + linrgb[2] * m[0][2];
            const double gD = linrgb[0] * m[1][0] + linrgb[1] * m[1][1] + linrgb[2] * m[1][2];
            const double bD = linrgb[0] * m[2][0] + linrgb[1] * m[2][1] + linrgb[2] * m[2][2];
            const double rA = ChromaticAdaptation(rD), gA = ChromaticAdaptation(gD), bA = ChromaticAdaptation(bD);
            const double a = (11.0 * rA + -12.0 * gA + bA) / 11.0;
            const double b = (rA + gA - 2.0 * bA) / 9.0;
            return std::atan2(b, a);
        }
        bool AreInCyclicOrder(double a, double b, double c) {
            const double deltaAB = SanitizeRadians(b - a);
            const double deltaAC = SanitizeRadians(c - a);
            return deltaAB < deltaAC;
        }
        double Intercept(double source, double mid, double target) {
            if (target == source) return target;
            return (mid - source) / (target - source);
        }
        V3 LerpPoint(const V3& s, double t, const V3& e) {
            return V3{ { s[0] + (e[0] - s[0]) * t, s[1] + (e[1] - s[1]) * t, s[2] + (e[2] - s[2]) * t } };
        }
        V3 SetCoordinate(const V3& source, double coordinate, const V3& target, int axis) {
            return LerpPoint(source, Intercept(source[axis], coordinate, target[axis]), target);
        }
        bool IsBounded(double x) { return 0.0 <= x && x <= 100.0; }
        V3 NthVertex(double y, int n) {
            const double kR = Y_FROM_LINRGB[0], kG = Y_FROM_LINRGB[1], kB = Y_FROM_LINRGB[2];
            const double coordA = n % 4 <= 1 ? 0.0 : 100.0;
            const double coordB = n % 2 == 0 ? 0.0 : 100.0;
            if (n < 4) {
                const double r = (y - coordA * kG - coordB * kB) / kR;
                return IsBounded(r) ? V3{ { r, coordA, coordB } } : V3{ { -1.0, -1.0, -1.0 } };
            } else if (n < 8) {
                const double g = (y - coordB * kR - coordA * kB) / kG;
                return IsBounded(g) ? V3{ { coordB, g, coordA } } : V3{ { -1.0, -1.0, -1.0 } };
            }
            const double b = (y - coordA * kR - coordB * kG) / kB;
            return IsBounded(b) ? V3{ { coordA, coordB, b } } : V3{ { -1.0, -1.0, -1.0 } };
        }
        void BisectToSegment(double y, double targetHue, V3& left, V3& right) {
            left = V3{ { -1.0, -1.0, -1.0 } };
            right = left;
            double leftHue = 0.0, rightHue = 0.0;
            bool initialized = false, uncut = true;
            for (int n = 0; n <= 11; ++n) {
                const V3 mid = NthVertex(y, n);
                if (mid[0] < 0) continue;
                const double midHue = HueOf(mid);
                if (!initialized) {
                    left = mid;
                    right = mid;
                    leftHue = midHue;
                    rightHue = midHue;
                    initialized = true;
                    continue;
                }
                if (uncut || AreInCyclicOrder(leftHue, midHue, rightHue)) {
                    uncut = false;
                    if (AreInCyclicOrder(leftHue, targetHue, midHue)) {
                        right = mid;
                        rightHue = midHue;
                    } else {
                        left = mid;
                        leftHue = midHue;
                    }
                }
            }
        }
        int CriticalPlaneBelow(double x) { return int(std::floor(x - 0.5)); }
        int CriticalPlaneAbove(double x) { return int(std::ceil(x - 0.5)); }
        std::int32_t BisectToLimit(double y, double targetHue) {
            V3 left, right;
            BisectToSegment(y, targetHue, left, right);
            double leftHue = HueOf(left);
            for (int axis = 0; axis <= 2; ++axis) {
                if (left[axis] != right[axis]) {
                    int lPlane, rPlane;
                    if (left[axis] < right[axis]) {
                        lPlane = CriticalPlaneBelow(TrueDelinearized(left[axis]));
                        rPlane = CriticalPlaneAbove(TrueDelinearized(right[axis]));
                    } else {
                        lPlane = CriticalPlaneAbove(TrueDelinearized(left[axis]));
                        rPlane = CriticalPlaneBelow(TrueDelinearized(right[axis]));
                    }
                    for (int i = 0; i <= 7; ++i) {
                        if (std::fabs(double(rPlane - lPlane)) <= 1) break;
                        const int mPlane = int(std::floor((lPlane + rPlane) / 2.0));
                        const double midPlaneCoordinate = CRITICAL_PLANES[mPlane];
                        const V3 mid = SetCoordinate(left, midPlaneCoordinate, right, axis);
                        const double midHue = HueOf(mid);
                        if (AreInCyclicOrder(leftHue, targetHue, midHue)) {
                            right = mid;
                            rPlane = mPlane;
                        } else {
                            left = mid;
                            leftHue = midHue;
                            lPlane = mPlane;
                        }
                    }
                }
            }
            return CamUtils::ArgbFromLinrgbComponents((left[0] + right[0]) / 2, (left[1] + right[1]) / 2, (left[2] + right[2]) / 2);
        }
        double InverseChromaticAdaptation(double adapted) {
            const double adaptedAbs = std::fabs(adapted);
            const double base = std::max(0.0, 27.13 * adaptedAbs / (400.0 - adaptedAbs));
            return CamUtils::Signum(adapted) * std::pow(base, 1.0 / 0.42);
        }
        std::int32_t FindResultByJ(double hueRadians, double chroma, double y) {
            double j = std::sqrt(y) * 11.0;
            const Frame& vc = Frame::Default();
            const double tInnerCoeff = 1 / std::pow(1.64 - std::pow(0.29, double(vc.n)), 0.73);
            const double eHue = 0.25 * (std::cos(hueRadians + 2.0) + 3.8);
            const double p1 = (eHue * (50000.0 / 13.0) * vc.nc * vc.ncb);
            const double hSin = std::sin(hueRadians), hCos = std::cos(hueRadians);
            for (int iterationRound = 0; iterationRound <= 4; ++iterationRound) {
                const double jNormalized = j / 100.0;
                const double alpha = (chroma == 0.0 || j == 0.0) ? 0.0 : chroma / std::sqrt(jNormalized);
                const double t = std::pow(alpha * tInnerCoeff, 1.0 / 0.9);
                const double acExponent = 1.0 / vc.c / vc.z;
                const double ac = vc.aw * std::pow(jNormalized, acExponent);
                const double p2 = ac / vc.nbb;
                const double gamma = 23.0 * (p2 + 0.305) * t / (23.0 * p1 + 11 * t * hCos + 108.0 * t * hSin);
                const double a = gamma * hCos, b = gamma * hSin;
                const double rA = (460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0;
                const double gA = (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0;
                const double bA = (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0;
                const double rCScaled = InverseChromaticAdaptation(rA);
                const double gCScaled = InverseChromaticAdaptation(gA);
                const double bCScaled = InverseChromaticAdaptation(bA);
                const auto& m = LINRGB_FROM_SCALED_DISCOUNT;
                const double linrgbR = rCScaled * m[0][0] + gCScaled * m[0][1] + bCScaled * m[0][2];
                const double linrgbG = rCScaled * m[1][0] + gCScaled * m[1][1] + bCScaled * m[1][2];
                const double linrgbB = rCScaled * m[2][0] + gCScaled * m[2][1] + bCScaled * m[2][2];
                if (linrgbR < 0 || linrgbG < 0 || linrgbB < 0) return 0;
                const double fnj = Y_FROM_LINRGB[0] * linrgbR + Y_FROM_LINRGB[1] * linrgbG + Y_FROM_LINRGB[2] * linrgbB;
                if (fnj <= 0) return 0;
                if (iterationRound == 4 || std::fabs(fnj - y) < 0.002) {
                    if (linrgbR > 100.01 || linrgbG > 100.01 || linrgbB > 100.01) return 0;
                    return CamUtils::ArgbFromLinrgbComponents(linrgbR, linrgbG, linrgbB);
                }
                j -= (fnj - y) * j / (2 * fnj);
            }
            return 0;
        }
        double SanitizeDegreesDouble(double degrees) {
            double d = std::fmod(degrees, 360.0);
            if (d < 0) d += 360.0;
            return d;
        }
    }

    namespace HctSolver {
        std::int32_t SolveToInt(double hueDegrees, double chroma, double lstar) {
            if (chroma < 0.0001 || lstar < 0.0001 || lstar > 99.9999) return CamUtils::ArgbFromLstar(lstar);
            const double hueDegreesPrime = SanitizeDegreesDouble(hueDegrees);
            const double hueRadians = hueDegreesPrime * 0.017453292519943295;   // Math.toRadians
            const double y = CamUtils::YFromLstar(lstar);
            const std::int32_t exactAnswer = FindResultByJ(hueRadians, chroma, y);
            if (exactAnswer != 0) return exactAnswer;
            return BisectToLimit(y, hueRadians);
        }
    }

    std::uint32_t SetLuminance(std::uint32_t argb, float newLuminance) {
        if ((newLuminance < 0.0001) || (newLuminance > 99.9999)) return std::uint32_t(CamUtils::ArgbFromLstar(double(newLuminance)));
        const Cam baseCam = Cam::FromInt(std::int32_t(argb));
        return std::uint32_t(Cam::GetInt(baseCam.hue, baseCam.chroma, newLuminance));
    }

}
