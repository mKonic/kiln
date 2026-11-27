#pragma once

// Compose's 4x4 Matrix (androidx.compose.ui.graphics.Matrix): row-major values, row 3 the
// translation, column 3 the perspective. Points map with the perspective divide. gfx::Matrix
// is the 2D affine transform the renderer uses; ToAffine / FromAffine convert.

#include "kiln/graphics/Path.h"

#include <array>

namespace kiln::gfx {

    struct Matrix4 {
        // Indices into values (Matrix.ScaleX ...).
        static constexpr int ScaleX = 0, SkewY = 1, Perspective0 = 3, SkewX = 4, ScaleY = 5, Perspective1 = 7,
                             ScaleZ = 10, TranslateX = 12, TranslateY = 13, TranslateZ = 14, Perspective2 = 15;

        std::array<float, 16> values{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

        float Get(int row, int column) const { return values[std::size_t(row * 4 + column)]; }
        void Set(int row, int column, float v) { values[std::size_t(row * 4 + column)] = v; }
        float operator()(int row, int column) const { return Get(row, column); }
        bool operator==(const Matrix4&) const = default;

        // map: a point through the matrix, divided by its w (0 when w is 0 or not finite).
        ui::Offset Map(ui::Offset p) const;
        // map(rect): the bounds of the mapped corners.
        ui::Rect Map(const ui::Rect& r) const;
        // this = this * m (timesAssign).
        Matrix4& operator*=(const Matrix4& m);
        // Inverts in place; a singular matrix is left unchanged.
        void Invert();
        void Reset() { *this = Matrix4{}; }
        void SetFrom(const Matrix4& m) { values = m.values; }
        bool IsIdentity() const { return *this == Matrix4{}; }
        // Pre-concatenated rotations (degrees), scale and translation, as Compose's.
        void RotateX(float degrees);
        void RotateY(float degrees);
        void RotateZ(float degrees);
        void Scale(float x = 1, float y = 1, float z = 1);
        void Translate(float x = 0, float y = 0, float z = 0);
        // resetToPivotedTransform: a layer's transform (RenderNode's order) about a pivot.
        void ResetToPivotedTransform(float pivotX = 0, float pivotY = 0, float translationX = 0, float translationY = 0,
                                     float translationZ = 0, float rotationX = 0, float rotationY = 0, float rotationZ = 0,
                                     float scaleX = 1, float scaleY = 1, float scaleZ = 1);

        // The 2D part (setFrom(android.graphics.Matrix) and back); z and perspective dropped.
        Matrix ToAffine() const { return { values[0], values[1], values[4], values[5], values[12], values[13] }; }
        static Matrix4 FromAffine(const Matrix& m) {
            Matrix4 r;
            r.values[0] = m.a; r.values[1] = m.b; r.values[4] = m.c; r.values[5] = m.d; r.values[12] = m.e; r.values[13] = m.f;
            return r;
        }
    };

}
