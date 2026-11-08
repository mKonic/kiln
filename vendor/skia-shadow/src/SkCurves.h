/*
 * Copyright 2011 Google Inc. All rights reserved.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

// Curve helpers the shadow tessellator uses, extracted from Skia: GrPathUtils' point generators
// (src/gpu/ganesh/geometry/GrPathUtils.{h,cpp}), the Wang's formula pieces they call
// (src/gpu/tessellate/WangsFormula.h) and SkConic's conversion to quads with SkAutoConicToQuads
// (src/core/SkGeometry.{h,cpp}). Vector math (skvx) is written out per component in the same
// order.
#pragma once

#include "skia_compat.h"

namespace GrPathUtils {
    // Will not return more than this many points for a quadratic or cubic.
    static const int kMaxPointsPerCurve = 1 << 10;
    uint32_t quadraticPointCount(const SkPoint points[], SkScalar tol);
    uint32_t generateQuadraticPoints(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2, SkScalar tolSqd, SkPoint** points,
                                     uint32_t pointsLeft);
    uint32_t cubicPointCount(const SkPoint points[], SkScalar tol);
    uint32_t generateCubicPoints(const SkPoint& p0, const SkPoint& p1, const SkPoint& p2, const SkPoint& p3, SkScalar tolSqd,
                                 SkPoint** points, uint32_t pointsLeft);
}

struct SkConic {
    SkConic() {}
    SkPoint fPts[3];
    SkScalar fW;
    void set(const SkPoint pts[3], SkScalar w) {
        std::memcpy(fPts, pts, 3 * sizeof(SkPoint));
        fW = w;
    }
    void chop(SkConic dst[2]) const;
    static constexpr int kMaxConicToQuadPOW2 = 5;
    int computeQuadPOW2(SkScalar tol) const;
    [[nodiscard]] int chopIntoQuadsPOW2(SkPoint pts[], int pow2) const;
    static SkScalar TransformW(const SkPoint[3], SkScalar w, const SkMatrix&);
};

class SkAutoConicToQuads {
public:
    SkAutoConicToQuads() : fQuadCount(0) {}
    const SkPoint* computeQuads(const SkConic& conic, SkScalar tol) {
        int pow2 = conic.computeQuadPOW2(tol);
        fQuadCount = 1 << pow2;
        fStorage.assign(size_t(1 + 2 * fQuadCount), SkPoint{});
        SkPoint* pts = fStorage.data();
        fQuadCount = conic.chopIntoQuadsPOW2(pts, pow2);
        return pts;
    }
    const SkPoint* computeQuads(SkSpan<const SkPoint> pts, SkScalar weight, SkScalar tol) {
        if (weight <= 0) {
            fQuadCount = 1;
            fStorage.assign(3, SkPoint{});
            SkPoint* outPts = fStorage.data();
            outPts[0] = pts[0];
            outPts[1] = { (pts[0].fX + pts[2].fX) * 0.5f, (pts[0].fY + pts[2].fY) * 0.5f };
            outPts[2] = pts[2];
            return outPts;
        }
        SkConic conic;
        conic.set(pts.data(), weight);
        return computeQuads(conic, tol);
    }
    const SkPoint* computeQuads(const SkPoint pts[3], SkScalar weight, SkScalar tol) { return this->computeQuads({ pts, 3 }, weight, tol); }
    int countQuads() const { return fQuadCount; }

private:
    std::vector<SkPoint> fStorage;
    int fQuadCount;
};
