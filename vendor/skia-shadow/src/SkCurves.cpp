/*
 * Copyright 2011 Google Inc. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// See SkCurves.h for where each function comes from (Skia, BSD-style licence in ../LICENSE).
#include "SkCurves.h"

// --- WangsFormula.h ------------------------------------------------------------------------------

namespace {
    template <int Degree> constexpr float length_term_p2(float precision) {
        return ((Degree * Degree) * ((Degree - 1) * (Degree - 1)) / 64.f) * (precision * precision);
    }
    inline int nextlog2(float x) {
        if (x <= 1) return 0;
        uint32_t bits;
        std::memcpy(&bits, &x, sizeof bits);   // SkFloat2Bits
        static constexpr uint32_t kDigitsAfterBinaryPoint = std::numeric_limits<float>::digits - 1;
        bits += (1u << kDigitsAfterBinaryPoint) - 1u;
        const int exp = ((bits >> kDigitsAfterBinaryPoint) & 0b1111'1111) - 127;
        return exp > 0 ? exp : 0;
    }
    inline int nextlog16(float x) { return (nextlog2(x) + 3) >> 2; }
    inline float quadratic_p4(float precision, const SkPoint pts[]) {
        // v = -2*p1 + p0 + p2
        const float vx = -2 * pts[1].fX + pts[0].fX + pts[2].fX;
        const float vy = -2 * pts[1].fY + pts[0].fY + pts[2].fY;
        return (vx * vx + vy * vy) * length_term_p2<2>(precision);
    }
    inline float cubic_p4(float precision, const SkPoint pts[]) {
        // v = -2*p12 + p01 + p23
        const float v0 = -2 * pts[1].fX + pts[0].fX + pts[2].fX;
        const float v1 = -2 * pts[1].fY + pts[0].fY + pts[2].fY;
        const float v2 = -2 * pts[2].fX + pts[1].fX + pts[3].fX;
        const float v3 = -2 * pts[2].fY + pts[1].fY + pts[3].fY;
        return std::max(v0 * v0 + v1 * v1, v2 * v2 + v3 * v3) * length_term_p2<3>(precision);
    }
    inline int quadratic_log2(float precision, const SkPoint pts[]) { return nextlog16(quadratic_p4(precision, pts)); }
    inline int cubic_log2(float precision, const SkPoint pts[]) { return nextlog16(cubic_p4(precision, pts)); }
}

// --- GrPathUtils.cpp -----------------------------------------------------------------------------

static const SkScalar kMinCurveTol = 0.0001f;

static float tolerance_to_wangs_precision(float srcTol) {
    SkASSERT(srcTol >= kMinCurveTol);
    return 1.f / srcTol;
}

static uint32_t max_bezier_vertices(uint32_t chopCount) {
    static constexpr uint32_t kMaxChopsPerCurve = 10;
    static_assert((1 << kMaxChopsPerCurve) == GrPathUtils::kMaxPointsPerCurve);
    return 1 << std::min(chopCount, kMaxChopsPerCurve);
}

uint32_t GrPathUtils::quadraticPointCount(const SkPoint points[], SkScalar tol) {
    return max_bezier_vertices(quadratic_log2(tolerance_to_wangs_precision(tol), points));
}

uint32_t GrPathUtils::generateQuadraticPoints(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2, SkScalar tolSqd, SkPoint** points,
                                              uint32_t pointsLeft) {
    if (pointsLeft < 2 || (SkPointPriv::DistanceToLineSegmentBetweenSqd(p1, p0, p2)) < tolSqd) {
        (*points)[0] = p2;
        *points += 1;
        return 1;
    }
    SkPoint q[] = {
        { sk_float_midpoint(p0.fX, p1.fX), sk_float_midpoint(p0.fY, p1.fY) },
        { sk_float_midpoint(p1.fX, p2.fX), sk_float_midpoint(p1.fY, p2.fY) },
    };
    SkPoint r = { sk_float_midpoint(q[0].fX, q[1].fX), sk_float_midpoint(q[0].fY, q[1].fY) };
    pointsLeft >>= 1;
    uint32_t a = generateQuadraticPoints(p0, q[0], r, tolSqd, points, pointsLeft);
    uint32_t b = generateQuadraticPoints(r, q[1], p2, tolSqd, points, pointsLeft);
    return a + b;
}

uint32_t GrPathUtils::cubicPointCount(const SkPoint points[], SkScalar tol) {
    return max_bezier_vertices(cubic_log2(tolerance_to_wangs_precision(tol), points));
}

uint32_t GrPathUtils::generateCubicPoints(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2, const SkPoint& p3, SkScalar tolSqd,
                                          SkPoint** points, uint32_t pointsLeft) {
    if (pointsLeft < 2 ||
        (SkPointPriv::DistanceToLineSegmentBetweenSqd(p1, p0, p3) < tolSqd && SkPointPriv::DistanceToLineSegmentBetweenSqd(p2, p0, p3) < tolSqd)) {
        (*points)[0] = p3;
        *points += 1;
        return 1;
    }
    SkPoint q[] = { { sk_float_midpoint(p0.fX, p1.fX), sk_float_midpoint(p0.fY, p1.fY) },
                    { sk_float_midpoint(p1.fX, p2.fX), sk_float_midpoint(p1.fY, p2.fY) },
                    { sk_float_midpoint(p2.fX, p3.fX), sk_float_midpoint(p2.fY, p3.fY) } };
    SkPoint r[] = { { sk_float_midpoint(q[0].fX, q[1].fX), sk_float_midpoint(q[0].fY, q[1].fY) },
                    { sk_float_midpoint(q[1].fX, q[2].fX), sk_float_midpoint(q[1].fY, q[2].fY) } };
    SkPoint s = { sk_float_midpoint(r[0].fX, r[1].fX), sk_float_midpoint(r[0].fY, r[1].fY) };
    pointsLeft >>= 1;
    uint32_t a = generateCubicPoints(p0, q[0], r[0], s, tolSqd, points, pointsLeft);
    uint32_t b = generateCubicPoints(s, r[1], q[2], p3, tolSqd, points, pointsLeft);
    return a + b;
}

// --- SkGeometry.cpp: SkConic ---------------------------------------------------------------------

static SkScalar subdivide_w_value(SkScalar w) { return SkScalarSqrt(SK_ScalarHalf + w * SK_ScalarHalf); }

void SkConic::chop(SkConic* SK_RESTRICT dst) const {
    const float scale = SkScalarInvert(SK_Scalar1 + fW);
    // t0 = p0 * scale, t1 = p1 * (w * scale), t2 = p2 * scale
    const float t0x = fPts[0].fX * scale, t0y = fPts[0].fY * scale;
    const float ws = fW * scale;
    const float t1x = fPts[1].fX * ws, t1y = fPts[1].fY * ws;
    const float t2x = fPts[2].fX * scale, t2y = fPts[2].fY * scale;
    const SkPoint p1 = { t0x + t1x, t0y + t1y };
    const SkPoint p3 = { t1x + t2x, t1y + t2y };
    const SkPoint p2 = { 0.5f * t0x + t1x + 0.5f * t2x, 0.5f * t0y + t1y + 0.5f * t2y };
    dst[0].fPts[0] = fPts[0];
    dst[0].fPts[1] = p1;
    dst[0].fPts[2] = p2;
    dst[1].fPts[0] = p2;
    dst[1].fPts[1] = p3;
    dst[1].fPts[2] = fPts[2];
    dst[0].fW = dst[1].fW = subdivide_w_value(fW);
}

static inline bool bad_conic_w(float w) { return w < 0 || !SkIsFinite(w); }

static bool are_finite(const SkPoint pts[], int count) { return SkIsFinite(&pts[0].fX, count << 1); }

int SkConic::computeQuadPOW2(SkScalar tol) const {
    if (tol < 0 || !SkIsFinite(tol) || !are_finite(fPts, 3) || bad_conic_w(fW)) return 0;
    // AS_QUAD_ERROR_SETUP
    SkScalar a = fW - 1;
    SkScalar k = a / (4 * (2 + a));
    SkScalar x = k * (fPts[0].fX - 2 * fPts[1].fX + fPts[2].fX);
    SkScalar y = k * (fPts[0].fY - 2 * fPts[1].fY + fPts[2].fY);
    SkScalar error = SkScalarSqrt(x * x + y * y);
    int pow2;
    for (pow2 = 0; pow2 < kMaxConicToQuadPOW2; ++pow2) {
        if (error <= tol) break;
        error *= 0.25f;
    }
    return pow2;
}

static bool between(SkScalar a, SkScalar b, SkScalar c) { return (a - b) * (c - b) <= 0; }

static SkPoint* subdivide(const SkConic& src, SkPoint pts[], int level) {
    if (0 == level) {
        std::memcpy(pts, &src.fPts[1], 2 * sizeof(SkPoint));
        return pts + 2;
    }
    SkConic dst[2];
    src.chop(dst);
    const SkScalar startY = src.fPts[0].fY;
    SkScalar endY = src.fPts[2].fY;
    if (between(startY, src.fPts[1].fY, endY)) {
        SkScalar midY = dst[0].fPts[2].fY;
        if (!between(startY, midY, endY)) {
            SkScalar closerY = SkTAbs(midY - startY) < SkTAbs(midY - endY) ? startY : endY;
            dst[0].fPts[2].fY = dst[1].fPts[0].fY = closerY;
        }
        if (!between(startY, dst[0].fPts[1].fY, dst[0].fPts[2].fY)) dst[0].fPts[1].fY = startY;
        if (!between(dst[1].fPts[0].fY, dst[1].fPts[1].fY, endY)) dst[1].fPts[1].fY = endY;
    }
    --level;
    pts = subdivide(dst[0], pts, level);
    return subdivide(dst[1], pts, level);
}

int SkConic::chopIntoQuadsPOW2(SkPoint pts[], int pow2) const {
    if (bad_conic_w(fW)) pow2 = 0;
    *pts = fPts[0];
    if (pow2 == kMaxConicToQuadPOW2) {   // an extreme weight that would make many quads
        SkConic dst[2];
        this->chop(dst);
        if (SkPointPriv::EqualsWithinTolerance(dst[0].fPts[1], dst[0].fPts[2]) &&
            SkPointPriv::EqualsWithinTolerance(dst[1].fPts[0], dst[1].fPts[1])) {
            pts[1] = pts[2] = pts[3] = dst[0].fPts[1];   // ctrl == end: lines
            pts[4] = dst[1].fPts[2];
            pow2 = 1;
            goto commonFinitePtCheck;
        }
    }
    subdivide(*this, pts + 1, pow2);
commonFinitePtCheck:
    const int quadCount = 1 << pow2;
    const int ptCount = 2 * quadCount + 1;
    if (!are_finite(pts, ptCount)) {
        for (int i = 1; i < ptCount - 1; ++i) pts[i] = fPts[1];
    }
    return 1 << pow2;
}

SkScalar SkConic::TransformW(const SkPoint pts[3], SkScalar w, const SkMatrix& matrix) {
    if (!matrix.hasPerspective()) return w;
    // ratquad_mapTo3D, then mapHomogeneousPoints
    const SkPoint3 src[3] = { { pts[0].fX * 1, pts[0].fY * 1, 1 }, { pts[1].fX * w, pts[1].fY * w, w }, { pts[2].fX * 1, pts[2].fY * 1, 1 } };
    double z[3];
    const auto& m = matrix.fMat;
    for (int i = 0; i < 3; ++i) z[i] = m[6] * src[i].fX + m[7] * src[i].fY + m[8] * src[i].fZ;
    return sk_double_to_float(std::sqrt(sk_ieee_double_divide(z[1] * z[1], z[0] * z[2])));
}
