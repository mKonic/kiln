// Ported from androidx.compose.ui.graphics.Matrix.
#include "kiln/graphics/Matrix4.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kiln::gfx {

    namespace {
        float PerspectiveScale(const Matrix4& m, float x, float y) {
            const float inverseZ = 1.0f / (m(0, 3) * x + m(1, 3) * y + m(3, 3));
            return std::isfinite(inverseZ) ? inverseZ : 0.0f;
        }
        float Dot(const Matrix4& a, int row, const Matrix4& b, int column) {
            return a(row, 0) * b(0, column) + a(row, 1) * b(1, column) + a(row, 2) * b(2, column) + a(row, 3) * b(3, column);
        }
        constexpr double kRadians = std::numbers::pi / 180.0;
    }

    ui::Offset Matrix4::Map(ui::Offset p) const {
        const float pZ = PerspectiveScale(*this, p.x, p.y);
        return { pZ * (Get(0, 0) * p.x + Get(1, 0) * p.y + Get(3, 0)), pZ * (Get(0, 1) * p.x + Get(1, 1) * p.y + Get(3, 1)) };
    }

    ui::Rect Matrix4::Map(const ui::Rect& r) const {
        const ui::Offset p0 = Map(ui::Offset{ r.left, r.top }), p1 = Map(ui::Offset{ r.left, r.bottom }), p2 = Map(ui::Offset{ r.right, r.top }), p3 = Map(ui::Offset{ r.right, r.bottom });
        return { std::min({ p0.x, p1.x, p2.x, p3.x }), std::min({ p0.y, p1.y, p2.y, p3.y }),
                 std::max({ p0.x, p1.x, p2.x, p3.x }), std::max({ p0.y, p1.y, p2.y, p3.y }) };
    }

    Matrix4& Matrix4::operator*=(const Matrix4& m) {
        Matrix4 r;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 4; ++col) r.Set(row, col, Dot(*this, row, m, col));
        values = r.values;
        return *this;
    }

    void Matrix4::Invert() {
        const float a00 = Get(0, 0), a01 = Get(0, 1), a02 = Get(0, 2), a03 = Get(0, 3);
        const float a10 = Get(1, 0), a11 = Get(1, 1), a12 = Get(1, 2), a13 = Get(1, 3);
        const float a20 = Get(2, 0), a21 = Get(2, 1), a22 = Get(2, 2), a23 = Get(2, 3);
        const float a30 = Get(3, 0), a31 = Get(3, 1), a32 = Get(3, 2), a33 = Get(3, 3);
        const float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10;
        const float b03 = a01 * a12 - a02 * a11, b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12;
        const float b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30, b08 = a20 * a33 - a23 * a30;
        const float b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
        const float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
        if (det == 0.0f) return;
        const float inv = 1.0f / det;
        Set(0, 0, (a11 * b11 - a12 * b10 + a13 * b09) * inv);
        Set(0, 1, (-a01 * b11 + a02 * b10 - a03 * b09) * inv);
        Set(0, 2, (a31 * b05 - a32 * b04 + a33 * b03) * inv);
        Set(0, 3, (-a21 * b05 + a22 * b04 - a23 * b03) * inv);
        Set(1, 0, (-a10 * b11 + a12 * b08 - a13 * b07) * inv);
        Set(1, 1, (a00 * b11 - a02 * b08 + a03 * b07) * inv);
        Set(1, 2, (-a30 * b05 + a32 * b02 - a33 * b01) * inv);
        Set(1, 3, (a20 * b05 - a22 * b02 + a23 * b01) * inv);
        Set(2, 0, (a10 * b10 - a11 * b08 + a13 * b06) * inv);
        Set(2, 1, (-a00 * b10 + a01 * b08 - a03 * b06) * inv);
        Set(2, 2, (a30 * b04 - a31 * b02 + a33 * b00) * inv);
        Set(2, 3, (-a20 * b04 + a21 * b02 - a23 * b00) * inv);
        Set(3, 0, (-a10 * b09 + a11 * b07 - a12 * b06) * inv);
        Set(3, 1, (a00 * b09 - a01 * b07 + a02 * b06) * inv);
        Set(3, 2, (-a30 * b03 + a31 * b01 - a32 * b00) * inv);
        Set(3, 3, (a20 * b03 - a21 * b01 + a22 * b00) * inv);
    }

    void Matrix4::RotateX(float degrees) {
        const double r = degrees * kRadians;
        const float s = float(std::sin(r)), c = float(std::cos(r));
        for (int row = 0; row < 4; ++row) {
            const float a1 = Get(row, 1), a2 = Get(row, 2);
            Set(row, 1, a1 * c - a2 * s);
            Set(row, 2, a1 * s + a2 * c);
        }
    }

    void Matrix4::RotateY(float degrees) {
        const double r = degrees * kRadians;
        const float s = float(std::sin(r)), c = float(std::cos(r));
        for (int row = 0; row < 4; ++row) {
            const float a0 = Get(row, 0), a2 = Get(row, 2);
            Set(row, 0, a0 * c + a2 * s);
            Set(row, 2, -a0 * s + a2 * c);
        }
    }

    void Matrix4::RotateZ(float degrees) {
        const double r = degrees * kRadians;
        const float s = float(std::sin(r)), c = float(std::cos(r));
        for (int col = 0; col < 4; ++col) {
            const float a0 = Get(0, col), a1 = Get(1, col);
            Set(0, col, c * a0 + s * a1);
            Set(1, col, -s * a0 + c * a1);
        }
    }

    void Matrix4::Scale(float x, float y, float z) {
        for (int col = 0; col < 4; ++col) {
            Set(0, col, Get(0, col) * x);
            Set(1, col, Get(1, col) * y);
            Set(2, col, Get(2, col) * z);
        }
    }

    void Matrix4::Translate(float x, float y, float z) {
        for (int col = 0; col < 4; ++col)
            Set(3, col, Get(0, col) * x + Get(1, col) * y + Get(2, col) * z + Get(3, col));
    }

    void Matrix4::ResetToPivotedTransform(float pivotX, float pivotY, float translationX, float translationY, float translationZ,
                                          float rotationX, float rotationY, float rotationZ, float scaleX, float scaleY, float scaleZ) {
        // X
        const double rx = rotationX * kRadians;
        const float rsx = float(std::sin(rx)), rcx = float(std::cos(rx));
        float v11 = rcx, v12 = rsx, v21 = -rsx, v22 = rcx;
        const float v31 = translationY * rcx - translationZ * rsx;
        float v32 = translationY * rsx + translationZ * rcx;
        // Y
        const double ry = rotationY * kRadians;
        const float rsy = float(std::sin(ry)), rcy = float(std::cos(ry));
        float v00 = rcy, v02 = -rsy;
        float v10 = v12 * rsy;
        v12 *= rcy;
        float v20 = v22 * rsy;
        v22 *= rcy;
        const float v30 = translationX * rcy + v32 * rsy;
        v32 = -translationX * rsy + v32 * rcy;
        // Z
        const double rz = rotationZ * kRadians;
        const float rsz = float(std::sin(rz)), rcz = float(std::cos(rz));
        const float a10 = v10;
        v10 = -rsz * v00 + rcz * v10;
        v00 = rcz * v00 + rsz * a10;
        float v01 = rsz * v11;
        v11 *= rcz;
        const float a12 = v12;
        v12 = -rsz * v02 + rcz * a12;
        v02 = rcz * v02 + rsz * a12;
        v00 *= scaleX; v01 *= scaleX; v02 *= scaleX;
        v10 *= scaleY; v11 *= scaleY; v12 *= scaleY;
        v20 *= scaleZ; v21 *= scaleZ; v22 *= scaleZ;
        values = { v00, v01, v02, 0,
                   v10, v11, v12, 0,
                   v20, v21, v22, 0,
                   -pivotX * v00 - pivotY * v10 + v30 + pivotX, -pivotX * v01 - pivotY * v11 + v31 + pivotY, -pivotX * v02 - pivotY * v12 + v32, 1 };
    }

}
