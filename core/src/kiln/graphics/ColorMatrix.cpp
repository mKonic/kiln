// Ported from androidx.compose.ui.graphics.ColorMatrix.
#include "kiln/graphics/ColorFilter.h"

#include <cmath>
#include <numbers>

namespace kiln::gfx {

    ColorMatrix& ColorMatrix::operator*=(const ColorMatrix& m) {
        ColorMatrix r;
        for (int row = 0; row < 4; ++row) {
            for (int col = 0; col < 4; ++col)
                r.Set(row, col, Get(row, 0) * m.Get(0, col) + Get(row, 1) * m.Get(1, col) + Get(row, 2) * m.Get(2, col) + Get(row, 3) * m.Get(3, col));
            r.Set(row, 4, Get(row, 0) * m.Get(0, 4) + Get(row, 1) * m.Get(1, 4) + Get(row, 2) * m.Get(2, 4) + Get(row, 3) * m.Get(3, 4) + Get(row, 4));
        }
        values = r.values;
        return *this;
    }

    void ColorMatrix::SetToSaturation(float sat) {
        Reset();
        const float inv = 1 - sat, r = 0.213f * inv, g = 0.715f * inv, b = 0.072f * inv;
        Set(0, 0, r + sat); Set(0, 1, g); Set(0, 2, b);
        Set(1, 0, r); Set(1, 1, g + sat); Set(1, 2, b);
        Set(2, 0, r); Set(2, 1, g); Set(2, 2, b + sat);
    }

    void ColorMatrix::SetToScale(float redScale, float greenScale, float blueScale, float alphaScale) {
        Reset();
        Set(0, 0, redScale);
        Set(1, 1, greenScale);
        Set(2, 2, blueScale);
        Set(3, 3, alphaScale);
    }

    namespace {
        void CosSin(float degrees, float& c, float& s) {
            const double r = double(degrees) * std::numbers::pi / 180.0;
            c = float(std::cos(r));
            s = float(std::sin(r));
        }
    }

    void ColorMatrix::SetToRotateRed(float degrees) {
        Reset();
        float c, s;
        CosSin(degrees, c, s);
        Set(1, 1, c); Set(1, 2, s); Set(2, 1, -s); Set(2, 2, c);
    }

    void ColorMatrix::SetToRotateGreen(float degrees) {
        Reset();
        float c, s;
        CosSin(degrees, c, s);
        Set(0, 0, c); Set(0, 2, -s); Set(2, 0, s); Set(2, 2, c);
    }

    void ColorMatrix::SetToRotateBlue(float degrees) {
        Reset();
        float c, s;
        CosSin(degrees, c, s);
        Set(0, 0, c); Set(0, 1, s); Set(1, 0, -s); Set(1, 1, c);
    }

    void ColorMatrix::ConvertRgbToYuv() {
        Reset();
        Set(0, 0, 0.299f); Set(0, 1, 0.587f); Set(0, 2, 0.114f);
        Set(1, 0, -0.16874f); Set(1, 1, -0.33126f); Set(1, 2, 0.5f);
        Set(2, 0, 0.5f); Set(2, 1, -0.41869f); Set(2, 2, -0.08131f);
    }

    void ColorMatrix::ConvertYuvToRgb() {
        Reset();
        Set(0, 2, 1.402f);
        Set(1, 0, 1); Set(1, 1, -0.34414f); Set(1, 2, -0.71414f);
        Set(2, 0, 1); Set(2, 1, 1.772f); Set(2, 2, 0);
    }

}
