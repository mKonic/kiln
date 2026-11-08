/*
 * Copyright 2017 Google LLC
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkDrawShadowInfo.h"

#include "skia_compat.h"   // was: include/core/SkMatrix.h and the other Skia headers

#include <array>

namespace SkDrawShadowMetrics {

static SkScalar compute_z(SkScalar x, SkScalar y, const SkPoint3& params) {
    return x*params.fX + y*params.fY + params.fZ;
}

bool GetSpotShadowTransform(const SkPoint3& lightPos, SkScalar lightRadius,
                            const SkMatrix& ctm, const SkPoint3& zPlaneParams,
                            const SkRect& pathBounds, bool directional,
                            SkMatrix* shadowTransform, SkScalar* radius) {
    auto heightFunc = [zPlaneParams] (SkScalar x, SkScalar y) {
        return zPlaneParams.fX*x + zPlaneParams.fY*y + zPlaneParams.fZ;
    };
    SkScalar occluderHeight = heightFunc(pathBounds.centerX(), pathBounds.centerY());

    // TODO: have directional lights support tilt via the zPlaneParams
    if (!ctm.hasPerspective() || directional) {
        SkScalar scale;
        SkVector translate;
        if (directional) {
            SkDrawShadowMetrics::GetDirectionalParams(occluderHeight, lightPos.fX, lightPos.fY,
                                                      lightPos.fZ, lightRadius, radius,
                                                      &scale, &translate);
        } else {
            SkDrawShadowMetrics::GetSpotParams(occluderHeight, lightPos.fX, lightPos.fY,
                                               lightPos.fZ, lightRadius, radius,
                                               &scale, &translate);
        }
        shadowTransform->setScaleTranslate(scale, scale, translate.fX, translate.fY);
        shadowTransform->preConcat(ctm);
    } else {
        if (SkScalarNearlyZero(pathBounds.width()) || SkScalarNearlyZero(pathBounds.height())) {
            return false;
        }

        // get rotated quad in 3D
        SkPoint pts[4];
        ctm.mapRectToQuad(pts, pathBounds);

        std::array<SkPoint3, 4> pts3D;
        SkScalar z = heightFunc(pathBounds.fLeft, pathBounds.fTop);
        pts3D[0].set(pts[0].fX, pts[0].fY, z);
        z = heightFunc(pathBounds.fRight, pathBounds.fTop);
        pts3D[1].set(pts[1].fX, pts[1].fY, z);
        z = heightFunc(pathBounds.fRight, pathBounds.fBottom);
        pts3D[2].set(pts[2].fX, pts[2].fY, z);
        z = heightFunc(pathBounds.fLeft, pathBounds.fBottom);
        pts3D[3].set(pts[3].fX, pts[3].fY, z);

        // project from light through corners to z=0 plane
        for (int i = 0; i < 4; ++i) {
            SkScalar dz = lightPos.fZ - pts3D[i].fZ;
            // light shouldn't be below or at a corner's z-location
            if (dz <= SK_ScalarNearlyZero) {
                return false;
            }
            SkScalar zRatio = pts3D[i].fZ / dz;
            pts3D[i].fX -= (lightPos.fX - pts3D[i].fX)*zRatio;
            pts3D[i].fY -= (lightPos.fY - pts3D[i].fY)*zRatio;
            pts3D[i].fZ = SK_Scalar1;
        }

        // Generate matrix that projects from [-1,1]x[-1,1] square to projected quad
        SkPoint3 h0, h1, h2;
        // Compute homogenous crossing point between top and bottom edges (gives new x-axis).
        h0 = (pts3D[1].cross(pts3D[0])).cross(pts3D[2].cross(pts3D[3]));
        // Compute homogenous crossing point between left and right edges (gives new y-axis).
        h1 = (pts3D[0].cross(pts3D[3])).cross(pts3D[1].cross(pts3D[2]));
        // Compute homogenous crossing point between diagonals (gives new origin).
        h2 = (pts3D[0].cross(pts3D[2])).cross(pts3D[1].cross(pts3D[3]));
        // If h2 is a vector (z=0 in 2D homogeneous space), that means that at least
        // two of the quad corners are coincident and we don't have a realistic projection
        if (SkScalarNearlyZero(h2.fZ)) {
            return false;
        }
        // In some cases the crossing points are in the wrong direction
        // to map (-1,-1) to pts3D[0], so we need to correct for that.
        // Want h0 to be to the right of the left edge.
        SkVector3 v = pts3D[3] - pts3D[0];
        SkVector3 w = h0 - pts3D[0];
        SkScalar perpDot = v.fX*w.fY - v.fY*w.fX;
        if (perpDot > 0) {
            h0 = -h0;
        }
        // Want h1 to be above the bottom edge.
        v = pts3D[1] - pts3D[0];
        perpDot = v.fX*w.fY - v.fY*w.fX;
        if (perpDot < 0) {
            h1 = -h1;
        }
        shadowTransform->setAll(h0.fX / h2.fZ, h1.fX / h2.fZ, h2.fX / h2.fZ,
                               h0.fY / h2.fZ, h1.fY / h2.fZ, h2.fY / h2.fZ,
                               h0.fZ / h2.fZ, h1.fZ / h2.fZ, 1);
        // generate matrix that transforms from bounds to [-1,1]x[-1,1] square
        SkMatrix toHomogeneous;
        SkScalar xScale = 2/(pathBounds.fRight - pathBounds.fLeft);
        SkScalar yScale = 2/(pathBounds.fBottom - pathBounds.fTop);
        toHomogeneous.setAll(xScale, 0, -xScale*pathBounds.fLeft - 1,
                             0, yScale, -yScale*pathBounds.fTop - 1,
                             0, 0, 1);
        shadowTransform->preConcat(toHomogeneous);

        *radius = SkDrawShadowMetrics::SpotBlurRadius(occluderHeight, lightPos.fZ, lightRadius);
    }

    return true;
}

// GetLocalBounds left out (kiln does not use the cached shadow path).

}  // namespace SkDrawShadowMetrics

