/*
 * Copyright 2011 Google Inc. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#pragma once

// The parts of Skia the vendored shadow tessellator and polygon utilities use, each as Skia
// defines it (include/core/SkPoint.h, src/core/SkPoint.cpp, SkPointPriv.h, SkFloatingPoint.h,
// SkColorData.h, SkMatrix.cpp, SkRect.cpp, SkPath.cpp's Iter), over plain data. kiln fills
// SkPath from its own path type.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#define SkASSERT(x) ((void)0)
#define SkASSERTF(x, ...) ((void)0)
#define SkDEBUGCODE(...)
#define SkDEBUGFAIL(msg) ((void)0)
#define SK_ABORT(...) std::abort()
#define SK_RESTRICT __restrict
#define SK_SPI
#define SK_ONLY_STACK_ALLOCATABLE \
    static void* operator new(size_t) = delete; \
    static void operator delete(void*) = delete
static inline void* sk_malloc_throw(size_t size) { void* p = std::malloc(size ? size : 1); if (!p) std::abort(); return p; }
static inline void sk_free(void* p) { std::free(p); }
static inline bool SkToBool(bool b) { return b; }
template <typename T> static inline bool SkToBool(const T& x) { return x != 0; }

using SkScalar = float;
using U8CPU = unsigned;
using SkColor = uint32_t;
using SkPMColor = uint32_t;
using SkAlpha = uint8_t;

template <typename T> using SkSpan = std::span<T>;

constexpr float SK_Scalar1 = 1.0f;
constexpr float SK_ScalarHalf = 0.5f;
constexpr float SK_ScalarNearlyZero = 1.0f / (1 << 12);
constexpr float SK_ScalarSqrt2 = 1.41421356f;
constexpr float SK_ScalarPI = 3.14159265f;
constexpr float SK_ScalarMax = 3.402823466e+38f;
constexpr float SK_ScalarMin = -SK_ScalarMax;
constexpr float SK_ScalarInfinity = std::numeric_limits<float>::infinity();
constexpr SkColor SK_ColorTRANSPARENT = 0x00000000;
constexpr SkColor SK_ColorBLACK = 0xFF000000;

// --- SkFloatingPoint.h / SkScalar.h ------------------------------------------------------------

static constexpr int sk_float_sgn(float x) { return (0.0f < x) - (x < 0.0f); }
template <typename T, typename... Pack> static inline bool SkIsFinite(T x, Pack... values) {
    T prod = x - x;
    prod = (prod * ... * values);
    return prod == prod;
}
template <typename T> static inline bool SkIsFinite(const T array[], int count) {
    T x = array[0];
    T prod = x - x;
    for (int i = 1; i < count; ++i) prod *= array[i];
    return prod == prod;
}
static inline bool SkIsNaN(float x) { return x != x; }
static constexpr float sk_double_to_float(double x) { return static_cast<float>(x); }
static constexpr float sk_float_midpoint(float a, float b) { return static_cast<float>(0.5 * (static_cast<double>(a) + b)); }
static constexpr float sk_ieee_float_divide(float numer, float denom) { return numer / denom; }
static constexpr double sk_ieee_double_divide(double numer, double denom) { return numer / denom; }
static inline float sk_float_round2int_no_saturate(float x) { return std::floor(x + 0.5f); }

static inline bool SkScalarNearlyZero(SkScalar x, SkScalar tolerance = SK_ScalarNearlyZero) { return std::fabs(x) <= tolerance; }
static inline bool SkScalarNearlyEqual(SkScalar x, SkScalar y, SkScalar tolerance = SK_ScalarNearlyZero) { return std::fabs(x - y) <= tolerance; }
static inline SkScalar SkScalarInvert(SkScalar x) { return 1.0f / x; }
static inline SkScalar SkScalarSqrt(SkScalar x) { return std::sqrt(x); }
static inline SkScalar SkScalarAbs(SkScalar x) { return std::fabs(x); }
static inline SkScalar SkScalarSin(SkScalar x) { return std::sin(x); }
static inline SkScalar SkScalarCos(SkScalar x) { return std::cos(x); }
static inline SkScalar SkScalarATan2(SkScalar y, SkScalar x) { return std::atan2(y, x); }
static inline SkScalar SkScalarRoundToScalar(SkScalar x) { return std::floor(x + 0.5f); }
static inline int SkScalarRoundToInt(SkScalar x) { return int(std::floor(x + 0.5f)); }
static inline int SkScalarCeilToInt(SkScalar x) { return int(std::ceil(x)); }
static inline SkScalar SkScalarSignAsScalar(SkScalar x) { return x < 0 ? -1.0f : (x > 0 ? 1.0f : 0.0f); }
template <typename T> static constexpr T SkTAbs(T value) { return value < 0 ? -value : value; }
template <typename T> static constexpr const T& SkTPin(const T& x, const T& lo, const T& hi) { return std::max(lo, std::min(x, hi)); }
template <typename D, typename S> constexpr D SkTo(S s) { return static_cast<D>(s); }
template <typename S> constexpr int SkToInt(S s) { return static_cast<int>(s); }
template <typename S> constexpr uint8_t SkToU8(S s) { return static_cast<uint8_t>(s); }
template <typename S> constexpr uint16_t SkToU16(S s) { return static_cast<uint16_t>(s); }

// --- SkColor ------------------------------------------------------------------------------------

static constexpr SkColor SkColorSetARGB(U8CPU a, U8CPU r, U8CPU g, U8CPU b) { return (a << 24) | (r << 16) | (g << 8) | b; }
static constexpr U8CPU SkColorGetA(SkColor c) { return (c >> 24) & 0xFF; }

// SkColorData.h SkPMLerp (SkFastFourByteInterp256_64)
static inline uint64_t SkSplay(uint32_t color) {
    static constexpr uint32_t kMask = 0x00FF00FF;
    uint64_t agrb = (color >> 8) & kMask;
    agrb <<= 32;
    agrb |= color & kMask;
    return agrb;
}
static inline uint32_t SkUnsplay(uint64_t agrb) {
    static constexpr uint32_t kMask = 0xFF00FF00;
    return SkPMColor(((agrb & kMask) >> 8) | ((agrb >> 32) & kMask));
}
static inline SkPMColor SkPMLerp(SkPMColor src, SkPMColor dst, unsigned scale) {
    return SkUnsplay(SkSplay(src) * scale + (256 - scale) * SkSplay(dst));
}

// --- SkPoint ------------------------------------------------------------------------------------

struct SkPoint;
using SkVector = SkPoint;

struct SkPoint {
    float fX = 0, fY = 0;

    static constexpr SkPoint Make(float x, float y) { return { x, y }; }
    constexpr float x() const { return fX; }
    constexpr float y() const { return fY; }
    bool isZero() const { return (0 == fX) & (0 == fY); }
    void set(float x, float y) { fX = x; fY = y; }
    void offset(float dx, float dy) { fX += dx; fY += dy; }
    float length() const { return SkPoint::Length(fX, fY); }
    float distanceToOrigin() const { return this->length(); }
    bool normalize() { return this->setLength(fX, fY, 1); }
    bool setNormalize(float x, float y) { return this->setLength(x, y, 1); }
    bool setLength(float length) { return this->setLength(fX, fY, length); }
    bool setLength(float x, float y, float length) {
        // set_point_length<false> (SkPoint.cpp)
        double xx = x, yy = y;
        double dmag = std::sqrt(xx * xx + yy * yy);
        double dscale = sk_ieee_double_divide(length, dmag);
        x *= dscale;
        y *= dscale;
        if (!SkIsFinite(x, y) || (x == 0 && y == 0)) {
            this->set(0, 0);
            return false;
        }
        this->set(x, y);
        return true;
    }
    void scale(float scale, SkPoint* dst) const { dst->set(fX * scale, fY * scale); }
    void scale(float value) { this->scale(value, this); }
    void negate() { fX = -fX; fY = -fY; }
    SkPoint operator-() const { return { -fX, -fY }; }
    void operator+=(const SkVector& v) { fX += v.fX; fY += v.fY; }
    void operator-=(const SkVector& v) { fX -= v.fX; fY -= v.fY; }
    SkPoint operator*(float scale) const { return { fX * scale, fY * scale }; }
    SkPoint& operator*=(float scale) { fX *= scale; fY *= scale; return *this; }
    bool isFinite() const { return SkIsFinite(fX, fY); }
    bool equals(float x, float y) const { return fX == x && fY == y; }
    friend bool operator==(const SkPoint& a, const SkPoint& b) { return a.fX == b.fX && a.fY == b.fY; }
    friend bool operator!=(const SkPoint& a, const SkPoint& b) { return a.fX != b.fX || a.fY != b.fY; }
    friend SkVector operator-(const SkPoint& a, const SkPoint& b) { return { a.fX - b.fX, a.fY - b.fY }; }
    friend SkPoint operator+(const SkPoint& a, const SkVector& b) { return { a.fX + b.fX, a.fY + b.fY }; }
    static float Length(float dx, float dy) {
        float mag2 = dx * dx + dy * dy;
        if (SkIsFinite(mag2)) return std::sqrt(mag2);
        double xx = dx, yy = dy;
        return sk_double_to_float(std::sqrt(xx * xx + yy * yy));
    }
    static float Distance(const SkPoint& a, const SkPoint& b) { return Length(a.fX - b.fX, a.fY - b.fY); }
    static float DotProduct(const SkVector& a, const SkVector& b) { return a.fX * b.fX + a.fY * b.fY; }
    static float CrossProduct(const SkVector& a, const SkVector& b) { return a.fX * b.fY - a.fY * b.fX; }
    float cross(const SkVector& vec) const { return CrossProduct(*this, vec); }
    float dot(const SkVector& vec) const { return DotProduct(*this, vec); }
};

class SkPointPriv {
public:
    enum Side { kLeft_Side = -1, kOn_Side = 0, kRight_Side = 1 };
    static bool CanNormalize(SkScalar dx, SkScalar dy) { return SkIsFinite(dx, dy) && (dx || dy); }
    static SkScalar DistanceToSqd(const SkPoint& pt, const SkPoint& a) {
        SkScalar dx = pt.fX - a.fX;
        SkScalar dy = pt.fY - a.fY;
        return dx * dx + dy * dy;
    }
    static bool EqualsWithinTolerance(const SkPoint& p1, const SkPoint& p2) { return EqualsWithinTolerance(p1, p2, SK_ScalarNearlyZero); }
    static bool EqualsWithinTolerance(const SkPoint& pt, const SkPoint& p, SkScalar tol) {
        return SkScalarNearlyZero(pt.fX - p.fX, tol) && SkScalarNearlyZero(pt.fY - p.fY, tol);
    }
    static SkScalar LengthSqd(const SkPoint& pt) { return SkPoint::DotProduct(pt, pt); }
    static SkScalar DistanceToLineSegmentBetweenSqd(const SkPoint& pt, const SkPoint& a, const SkPoint& b) {
        SkVector u = b - a;
        SkVector v = pt - a;
        float uLengthSqd = LengthSqd(u);
        float uDotV = SkPoint::DotProduct(u, v);
        if (uDotV <= 0) return LengthSqd(v);
        if (uDotV > uLengthSqd) return DistanceToSqd(b, pt);
        float det = u.cross(v);
        float temp = sk_ieee_float_divide(det, uLengthSqd);
        temp *= det;
        if (!SkIsFinite(temp)) return LengthSqd(v);
        return temp;
    }
    static SkPoint MakeOrthog(const SkPoint& vec, Side side = kLeft_Side) {
        return (side == kRight_Side) ? SkPoint{ -vec.fY, vec.fX } : SkPoint{ vec.fY, -vec.fX };
    }
};

struct SkPoint3 {
    SkScalar fX = 0, fY = 0, fZ = 0;
    static SkPoint3 Make(SkScalar x, SkScalar y, SkScalar z) { return { x, y, z }; }
    void set(SkScalar x, SkScalar y, SkScalar z) { fX = x; fY = y; fZ = z; }
    bool isFinite() const { return SkIsFinite(fX, fY, fZ); }
    SkPoint3 cross(const SkPoint3& v) const { return { fY * v.fZ - fZ * v.fY, fZ * v.fX - fX * v.fZ, fX * v.fY - fY * v.fX }; }
    SkPoint3 operator-() const { return { -fX, -fY, -fZ }; }
    friend SkPoint3 operator-(const SkPoint3& a, const SkPoint3& b) { return { a.fX - b.fX, a.fY - b.fY, a.fZ - b.fZ }; }
};
using SkVector3 = SkPoint3;

// --- SkRect -------------------------------------------------------------------------------------

struct SkRect {
    SkScalar fLeft = 0, fTop = 0, fRight = 0, fBottom = 0;
    static constexpr SkRect MakeEmpty() { return SkRect{ 0, 0, 0, 0 }; }
    static constexpr SkRect MakeLTRB(SkScalar l, SkScalar t, SkScalar r, SkScalar b) { return SkRect{ l, t, r, b }; }
    bool isEmpty() const { return !(fLeft < fRight && fTop < fBottom); }
    bool isFinite() const { return SkIsFinite(fLeft, fTop, fRight, fBottom); }
    SkScalar width() const { return fRight - fLeft; }
    SkScalar height() const { return fBottom - fTop; }
    SkScalar centerX() const { return sk_float_midpoint(fLeft, fRight); }
    SkScalar centerY() const { return sk_float_midpoint(fTop, fBottom); }
    void setEmpty() { *this = MakeEmpty(); }
    void setLTRB(SkScalar l, SkScalar t, SkScalar r, SkScalar b) { fLeft = l; fTop = t; fRight = r; fBottom = b; }
    // SkRect::setBoundsCheck (SkRect.cpp), scalar form.
    bool setBoundsCheck(SkSpan<const SkPoint> pts) {
        if (pts.empty()) {
            this->setEmpty();
            return true;
        }
        float l = pts[0].fX, t = pts[0].fY, r = l, b = t;
        float accum = 0 * l * t;
        for (size_t i = 1; i < pts.size(); ++i) {
            const float x = pts[i].fX, y = pts[i].fY;
            accum *= x * y;
            l = std::min(l, x); t = std::min(t, y);
            r = std::max(r, x); b = std::max(b, y);
        }
        if (accum == 0) {
            this->setLTRB(l, t, r, b);
            return true;
        }
        this->setEmpty();
        return false;
    }
    void setBoundsNoCheck(SkSpan<const SkPoint> pts) { if (!this->setBoundsCheck(pts)) this->setLTRB(NAN, NAN, NAN, NAN); }
    bool contains(SkScalar x, SkScalar y) const { return x >= fLeft && x < fRight && y >= fTop && y < fBottom; }
    std::array<SkPoint, 4> toQuad() const { return { SkPoint{ fLeft, fTop }, SkPoint{ fRight, fTop }, SkPoint{ fRight, fBottom }, SkPoint{ fLeft, fBottom } }; }
};

class SkRectPriv {
public:
    static constexpr float HalfWidth(const SkRect& r) { return sk_float_midpoint(-r.fLeft, r.fRight); }
    static constexpr float HalfHeight(const SkRect& r) { return sk_float_midpoint(-r.fTop, r.fBottom); }
};

// skvx::float4 (only the min / max / indexing compute_triangle_bounds uses)
namespace skvx {
    struct float4 {
        float v[4];
        float4() : v{ 0, 0, 0, 0 } {}
        float4(float a, float b, float c, float d) : v{ a, b, c, d } {}
        float operator[](int i) const { return v[i]; }
    };
    inline float4 min(const float4& a, const float4& b) { return { std::min(a[0], b[0]), std::min(a[1], b[1]), std::min(a[2], b[2]), std::min(a[3], b[3]) }; }
    inline float4 max(const float4& a, const float4& b) { return { std::max(a[0], b[0]), std::max(a[1], b[1]), std::max(a[2], b[2]), std::max(a[3], b[3]) }; }
}

// --- SkMatrix (what the tessellator uses) --------------------------------------------------------

class SkMatrix {
public:
    enum { kMScaleX, kMSkewX, kMTransX, kMSkewY, kMScaleY, kMTransY, kMPersp0, kMPersp1, kMPersp2 };
    SkMatrix() = default;
    SkScalar operator[](int i) const { return fMat[i]; }
    SkScalar get(int i) const { return fMat[i]; }
    SkScalar getTranslateX() const { return fMat[kMTransX]; }
    SkScalar getTranslateY() const { return fMat[kMTransY]; }
    bool hasPerspective() const { return fMat[kMPersp0] != 0 || fMat[kMPersp1] != 0 || fMat[kMPersp2] != 1; }
    bool isScaleTranslate() const { return !hasPerspective() && fMat[kMSkewX] == 0 && fMat[kMSkewY] == 0; }
    SkMatrix& setAll(SkScalar scaleX, SkScalar skewX, SkScalar transX, SkScalar skewY, SkScalar scaleY, SkScalar transY,
                     SkScalar persp0, SkScalar persp1, SkScalar persp2) {
        fMat = { scaleX, skewX, transX, skewY, scaleY, transY, persp0, persp1, persp2 };
        return *this;
    }
    SkMatrix& setScaleTranslate(SkScalar sx, SkScalar sy, SkScalar tx, SkScalar ty) { return setAll(sx, 0, tx, 0, sy, ty, 0, 0, 1); }
    bool isIdentity() const { return fMat == std::array<SkScalar, 9>{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }; }
    // SkMatrix::setConcat (SkMatrix.cpp): scale-translate products directly, perspective ones in
    // float, affine ones through double (muladdmul).
    static SkMatrix Concat(const SkMatrix& a, const SkMatrix& b) {
        if (a.isIdentity()) return b;
        if (b.isIdentity()) return a;
        auto onlyScaleTranslate = [](const SkMatrix& m) { return m.isScaleTranslate(); };
        SkMatrix tmp;
        const auto& A = a.fMat;
        const auto& B = b.fMat;
        if (onlyScaleTranslate(a) && onlyScaleTranslate(b)) {
            tmp.setScaleTranslate(A[kMScaleX] * B[kMScaleX], A[kMScaleY] * B[kMScaleY], A[kMScaleX] * B[kMTransX] + A[kMTransX],
                                  A[kMScaleY] * B[kMTransY] + A[kMTransY]);
            return tmp;
        }
        if (a.hasPerspective() || b.hasPerspective()) {
            auto rowcol3 = [](const float row[], const float col[]) { return row[0] * col[0] + row[1] * col[3] + row[2] * col[6]; };
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) tmp.fMat[size_t(r * 3 + c)] = rowcol3(&A[size_t(r * 3)], &B[size_t(c)]);
            return tmp;
        }
        auto muladdmul = [](float p, float q, float r, float t) { return sk_double_to_float((double)p * q + (double)r * t); };
        tmp.fMat[kMScaleX] = muladdmul(A[kMScaleX], B[kMScaleX], A[kMSkewX], B[kMSkewY]);
        tmp.fMat[kMSkewX] = muladdmul(A[kMScaleX], B[kMSkewX], A[kMSkewX], B[kMScaleY]);
        tmp.fMat[kMTransX] = muladdmul(A[kMScaleX], B[kMTransX], A[kMSkewX], B[kMTransY]) + A[kMTransX];
        tmp.fMat[kMSkewY] = muladdmul(A[kMSkewY], B[kMScaleX], A[kMScaleY], B[kMSkewY]);
        tmp.fMat[kMScaleY] = muladdmul(A[kMSkewY], B[kMSkewX], A[kMScaleY], B[kMScaleY]);
        tmp.fMat[kMTransY] = muladdmul(A[kMSkewY], B[kMTransX], A[kMScaleY], B[kMTransY]) + A[kMTransY];
        tmp.fMat[kMPersp0] = 0;
        tmp.fMat[kMPersp1] = 0;
        tmp.fMat[kMPersp2] = 1;
        return tmp;
    }
    SkMatrix& preConcat(const SkMatrix& other) { *this = Concat(*this, other); return *this; }
    // mapPoints: Persp_pts for a perspective matrix, the affine form otherwise.
    void mapPoints(SkSpan<SkPoint> dst, SkSpan<const SkPoint> src) const {
        for (size_t i = 0; i < src.size(); ++i) dst[i] = this->mapPoint(src[i]);
    }
    void mapPoints(SkSpan<SkPoint> pts) const {
        for (SkPoint& p : pts) p = this->mapPoint(p);
    }
    SkPoint mapPoint(SkPoint p) const {
        const float sx = p.fX, sy = p.fY;
        float x = sx * fMat[kMScaleX] + sy * fMat[kMSkewX] + fMat[kMTransX];
        float y = sx * fMat[kMSkewY] + sy * fMat[kMScaleY] + fMat[kMTransY];
        if (hasPerspective()) {
            float z = sx * fMat[kMPersp0] + sy * fMat[kMPersp1] + fMat[kMPersp2];
            if (z) z = 1 / z;
            return { x * z, y * z };
        }
        return { x, y };
    }
    // mapRect: the bounds of the mapped corners.
    SkRect mapRect(const SkRect& src) const {
        std::array<SkPoint, 4> q = src.toQuad();
        this->mapPoints(q);
        SkRect r;
        r.setBoundsNoCheck(q);
        return r;
    }
    void mapRectToQuad(SkPoint dst[4], const SkRect& rect) const {
        std::array<SkPoint, 4> q = rect.toQuad();
        for (int i = 0; i < 4; ++i) dst[i] = this->mapPoint(q[i]);
    }

    std::array<SkScalar, 9> fMat{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };
};

// --- SkTDArray (the members the vendored code calls) ---------------------------------------------

template <typename T> class SkTDArray {
public:
    SkTDArray() = default;
    int size() const { return int(fData.size()); }
    bool empty() const { return fData.empty(); }
    T* begin() { return fData.data(); }
    const T* begin() const { return fData.data(); }
    T* end() { return fData.data() + fData.size(); }
    const T* end() const { return fData.data() + fData.size(); }
    T* data() { return fData.data(); }
    const T* data() const { return fData.data(); }
    T& operator[](int i) { return fData[size_t(i)]; }
    const T& operator[](int i) const { return fData[size_t(i)]; }
    T& back() { return fData.back(); }
    const T& back() const { return fData.back(); }
    void reserve(int n) { fData.reserve(size_t(std::max(n, 0))); }
    void resize(int n) {
        // Skia's array relocates with memcpy, so it also holds types C++ cannot move (the
        // triangulator's grid of intrusive lists). Those are only ever sized once, from empty.
        if constexpr (std::is_move_constructible_v<T>) {
            fData.resize(size_t(n));
        } else {
            SkASSERT(fData.empty());
            fData = std::vector<T>(size_t(n));
        }
    }
    void clear() { fData.clear(); }
    void reset() { fData.clear(); }
    void push_back(const T& v) { fData.push_back(v); }
    void pop_back() { fData.pop_back(); }
    T* append() { fData.emplace_back(); return &fData.back(); }
    T* append(int count) {
        const size_t at = fData.size();
        fData.resize(at + size_t(count));
        return fData.data() + at;
    }
    T* append(int count, const T* src) {
        T* dst = append(count);
        if (src) std::copy(src, src + count, dst);
        return dst;
    }
    void removeShuffle(int index) {
        fData[size_t(index)] = fData.back();
        fData.pop_back();
    }
    void remove(int index, int count = 1) { fData.erase(fData.begin() + index, fData.begin() + index + count); }
    T* insert(int index) { return &*fData.insert(fData.begin() + index, T{}); }
    void swap(SkTDArray& o) { fData.swap(o.fData); }
    operator SkSpan<const T>() const { return { fData.data(), fData.size() }; }

private:
    std::vector<T> fData;
};

namespace skia_private {
    // AutoSTMalloc / STArray: heap storage of N items (the small-size optimisation is a speed matter).
    template <int N, typename T> class AutoSTMalloc {
    public:
        explicit AutoSTMalloc(size_t count) : fData(count) {}
        T* get() { return fData.data(); }
        operator T*() { return fData.data(); }
        T& operator[](int i) { return fData[size_t(i)]; }
    private:
        std::vector<T> fData;
    };
    template <int N, typename T> class AutoSTArray {
    public:
        explicit AutoSTArray(int count) : fData(size_t(std::max(count, 0))) {}
        T* get() { return fData.data(); }
        T& operator[](int i) { return fData[size_t(i)]; }
        int size() const { return int(fData.size()); }
    private:
        std::vector<T> fData;
    };
    template <int N, typename T> class STArray {
    public:
        explicit STArray(int reserveCount = N) { fData.reserve(size_t(std::max(reserveCount, 0))); }
        void push_back(const T& v) { fData.push_back(v); }
        T& push_back() { fData.emplace_back(); return fData.back(); }
        T* push_back_n(int n) { const size_t at = fData.size(); fData.resize(at + size_t(n)); return fData.data() + at; }
        int size() const { return int(fData.size()); }
        bool empty() const { return fData.empty(); }
        T& operator[](int i) { return fData[size_t(i)]; }
        const T& operator[](int i) const { return fData[size_t(i)]; }
        T* begin() { return fData.data(); }
        T* end() { return fData.data() + fData.size(); }
        T& back() { return fData.back(); }
        void pop_back() { fData.pop_back(); }
        void removeShuffle(int index) { fData[size_t(index)] = fData.back(); fData.pop_back(); }
    private:
        std::vector<T> fData;
    };
}

// --- SkPath: the contours, with SkPath::Iter's walk (forceClose) --------------------------------

enum class SkPathVerb : uint8_t { kMove, kLine, kQuad, kConic, kCubic, kClose };

class SkPath {
public:
    std::vector<SkPathVerb> fVerbs;
    std::vector<SkPoint> fPoints;
    std::vector<float> fConicWeights;
    SkRect fBounds;

    const SkRect& getBounds() const { return fBounds; }
    int countPoints() const { return int(fPoints.size()); }

    struct IterRec {
        SkPathVerb fVerb;
        SkSpan<const SkPoint> fPoints;
        float fConicWeight;
        float conicWeight() const { return fConicWeight; }
    };

    // SkPath::Iter (SkPath.cpp)
    class Iter {
    public:
        Iter(const SkPath& path, bool forceClose) {
            fPts = path.fPoints.data();
            fVerbs = path.fVerbs.data();
            fVerbStop = path.fVerbs.data() + path.fVerbs.size();
            fConicWeights = path.fConicWeights.data();
            fForceClose = forceClose;
        }
        std::optional<IterRec> next() {
            int verb = this->nextLegacy(fStorage.data());
            if (verb < 0) return {};
            static constexpr uint8_t kCounts[] = { 1, 2, 3, 3, 4, 0 };
            const SkPathVerb v = static_cast<SkPathVerb>(verb);
            return IterRec{ v, { fStorage.data(), kCounts[verb] }, v == SkPathVerb::kConic ? fConicWeight : 1 };
        }

    private:
        SkPathVerb autoClose(SkPoint pts[2]) {
            if (fLastPt != fMoveTo) {
                if (SkIsNaN(fLastPt.fX) || SkIsNaN(fLastPt.fY) || SkIsNaN(fMoveTo.fX) || SkIsNaN(fMoveTo.fY)) return SkPathVerb::kClose;
                pts[0] = fLastPt;
                pts[1] = fMoveTo;
                fLastPt = fMoveTo;
                return SkPathVerb::kLine;
            }
            pts[0] = fMoveTo;
            return SkPathVerb::kClose;
        }
        int nextLegacy(SkPoint pts[4]) {   // -1: done
            if (fVerbs == fVerbStop) {
                if (fNeedClose) {
                    if (SkPathVerb::kLine == this->autoClose(pts)) return int(SkPathVerb::kLine);
                    fNeedClose = false;
                    return int(SkPathVerb::kClose);
                }
                return -1;
            }
            SkPathVerb verb = *fVerbs++;
            const SkPoint* srcPts = fPts;
            switch (verb) {
                case SkPathVerb::kMove:
                    if (fNeedClose) {
                        fVerbs--;
                        verb = this->autoClose(pts);
                        if (verb == SkPathVerb::kClose) fNeedClose = false;
                        return int(verb);
                    }
                    if (fVerbs == fVerbStop) return -1;   // a trailing moveTo
                    fMoveTo = *srcPts;
                    pts[0] = *srcPts;
                    srcPts += 1;
                    fLastPt = fMoveTo;
                    fNeedClose = fForceClose;
                    break;
                case SkPathVerb::kLine:
                    pts[0] = fLastPt;
                    pts[1] = srcPts[0];
                    fLastPt = srcPts[0];
                    srcPts += 1;
                    break;
                case SkPathVerb::kConic:
                    fConicWeight = *fConicWeights++;
                    [[fallthrough]];
                case SkPathVerb::kQuad:
                    pts[0] = fLastPt;
                    std::memcpy(&pts[1], srcPts, 2 * sizeof(SkPoint));
                    fLastPt = srcPts[1];
                    srcPts += 2;
                    break;
                case SkPathVerb::kCubic:
                    pts[0] = fLastPt;
                    std::memcpy(&pts[1], srcPts, 3 * sizeof(SkPoint));
                    fLastPt = srcPts[2];
                    srcPts += 3;
                    break;
                case SkPathVerb::kClose:
                    verb = this->autoClose(pts);
                    if (verb == SkPathVerb::kLine) fVerbs--;
                    else fNeedClose = false;
                    fLastPt = fMoveTo;
                    break;
            }
            fPts = srcPts;
            return int(verb);
        }
        const SkPoint* fPts = nullptr;
        const SkPathVerb* fVerbs = nullptr;
        const SkPathVerb* fVerbStop = nullptr;
        const float* fConicWeights = nullptr;
        float fConicWeight = 1;
        SkPoint fMoveTo, fLastPt;
        bool fForceClose = false, fNeedClose = false;
        std::array<SkPoint, 4> fStorage;
    };
};

// --- SkVertices: the tessellator's triangle mesh --------------------------------------------------

class SkVertices {
public:
    enum VertexMode { kTriangles_VertexMode };
    std::vector<SkPoint> positions;
    std::vector<SkColor> colors;
    std::vector<uint16_t> indices;

    static std::shared_ptr<SkVertices> MakeCopy(VertexMode, int vertexCount, const SkPoint* pos,
                                                const SkPoint* /*texs*/, const SkColor* colors,
                                                int indexCount, const uint16_t* idx) {
        auto v = std::make_shared<SkVertices>();
        if (vertexCount > 0) v->positions.assign(pos, pos + vertexCount);
        if (colors) v->colors.assign(colors, colors + vertexCount);
        if (idx) v->indices.assign(idx, idx + indexCount);
        return v;
    }
};
template <typename T> using sk_sp = std::shared_ptr<T>;
