// ui-graphics API: Matrix (MatrixTest), ColorMatrix (ColorMatrixTest), Path convexity and
// operators, Brush.intrinsicSize / applyTo and the Canvas over a DrawScope.
#include "Check.h"

#include "kiln/graphics/ColorFilter.h"
#include "kiln/graphics/Matrix4.h"
#include "kiln/graphics/Paint.h"
#include "kiln/ui/Canvas.h"

#include <cmath>

using namespace kiln;
using gfx::Matrix4;
using ui::Offset;
using ui::Rect;

TEST(Matrix4MapsRotatesAndInverts) {
    Matrix4 m;
    CHECK(m.IsIdentity());
    m.Translate(5);
    m.Reset();
    CHECK(m.IsIdentity());
    // resetToPivotedTransform equals translate(-pivot) * (translate, rotations, scale) * translate(pivot).
    const float px = 64, py = 32, tx = 200, ty = 300, tz = 1, rx = 15, ry = 25, rz = 35, sx = 1.2f, sy = 1.4f, sz = 1.1f;
    m.values.fill(2);
    m.ResetToPivotedTransform();
    CHECK(m.IsIdentity());
    m.ResetToPivotedTransform(px, py, tx, ty, tz, rx, ry, rz, sx, sy, sz);
    Matrix4 m2, inner, last;
    m2.Translate(-px, -py);
    inner.Translate(tx, ty, tz);
    inner.RotateX(rx);
    inner.RotateY(ry);
    inner.RotateZ(rz);
    inner.Scale(sx, sy, sz);
    m2 *= inner;
    last.Translate(px, py);
    m2 *= last;
    for (int i = 0; i < 16; ++i) CHECK_NEAR(m.values[std::size_t(i)], m2.values[std::size_t(i)], 1e-3);
    // mapPoint
    Matrix4 r;
    r.RotateZ(45);
    CHECK_NEAR(r.Map(Offset{ 10, 0 }).x, 7.071, 0.02);
    CHECK_NEAR(r.Map(Offset{ 10, 0 }).y, 7.071, 0.02);
    r.Reset();
    r.RotateX(45);
    CHECK_NEAR(r.Map(Offset{ 0, 10 }).y, 7.071, 0.02);
    r.Reset();
    r.RotateY(45);
    CHECK_NEAR(r.Map(Offset{ 10, 0 }).x, 7.071, 0.02);
    // mapPointInfinite
    r.Reset();
    r.Set(3, 3, 0);
    CHECK_EQ(r.Map(Offset{ 1, 1 }), (Offset{ 0, 0 }));
    // mapRect
    r.Reset();
    r.RotateZ(90);
    r.Scale(2, 2);
    const Rect m90 = r.Map(Rect{ 0, 0, 10, 10 });
    CHECK_NEAR(m90.left, -20, 1e-4);
    CHECK_NEAR(m90.right, 0, 1e-4);
    CHECK_NEAR(m90.bottom, 20, 1e-4);
    // invert
    Matrix4 inv = m;
    inv.Invert();
    inv *= m;   // inv * m: identity
    for (int i = 0; i < 16; ++i) CHECK_NEAR(inv.values[std::size_t(i)], Matrix4{}.values[std::size_t(i)], 1e-3);
    // The 2D part round-trips.
    const gfx::Matrix a{ 1, 2, 3, 4, 5, 6 };
    CHECK(Matrix4::FromAffine(a).ToAffine().e == 5 && Matrix4::FromAffine(a).ToAffine().c == 3);
}

TEST(ColorMatrixSetters) {
    gfx::ColorMatrix c;
    c.SetToRotateRed(180);
    CHECK_NEAR(c.values[6], -1, 1e-5);
    CHECK_NEAR(c.values[12], -1, 1e-5);
    CHECK_NEAR(c.values[7], 0, 1e-5);
    c.SetToRotateGreen(180);
    CHECK_NEAR(c.values[0], -1, 1e-5);
    CHECK_NEAR(c.values[12], -1, 1e-5);
    c.SetToRotateBlue(180);
    CHECK_NEAR(c.values[0], -1, 1e-5);
    CHECK_NEAR(c.values[6], -1, 1e-5);
    c.SetToSaturation(0.5f);
    CHECK_NEAR(c.Get(0, 0), 0.213f * 0.5f + 0.5f, 1e-6);
    CHECK_NEAR(c.Get(1, 1), 0.715f * 0.5f + 0.5f, 1e-6);
    c.SetToScale(2, 3, 4, 5);
    CHECK(c.Get(0, 0) == 2 && c.Get(1, 1) == 3 && c.Get(2, 2) == 4 && c.Get(3, 3) == 5);
    // Scale then translate: the translation column is carried through.
    gfx::ColorMatrix t;
    t.Set(0, 4, 10);
    gfx::ColorMatrix s;
    s.SetToScale(2, 1, 1, 1);
    s *= t;
    CHECK_EQ(s.Get(0, 4), 20.0f);
    CHECK(gfx::ColorFilter::ColorMatrix(s).matrix == s.values);
}

TEST(PathConvexityAndOperators) {
    gfx::Path empty;
    CHECK(empty.IsConvex());
    gfx::Path rect;
    rect.AddRect({ 0, 0, 10, 10 });
    CHECK(rect.IsConvex());
    gfx::Path oval;
    oval.AddOval({ 0, 0, 10, 20 });
    CHECK(oval.IsConvex());
    gfx::Path arrow;   // a concave chevron
    arrow.MoveTo(0, 0);
    arrow.LineTo(10, 5);
    arrow.LineTo(0, 10);
    arrow.LineTo(4, 5);
    arrow.Close();
    CHECK(!arrow.IsConvex());
    gfx::Path star;    // turns one way but winds twice
    for (int i = 0; i < 5; ++i) {
        const double a = i * 4 * 3.14159265358979 / 5;
        if (i == 0) star.MoveTo(float(std::cos(a) * 10), float(std::sin(a) * 10));
        else star.LineTo(float(std::cos(a) * 10), float(std::sin(a) * 10));
    }
    star.Close();
    CHECK(!star.IsConvex());
    gfx::Path two = rect;
    two.AddRect({ 20, 20, 30, 30 });
    CHECK(!two.IsConvex());
    gfx::Path other;
    other.AddRect({ 5, 0, 15, 10 });
    const Rect u = (rect + other).Bounds(), d = (rect - other).Bounds();
    CHECK_NEAR(u.right, 15, 1e-3);
    CHECK_NEAR(d.right, 5, 1e-3);
}

TEST(BrushIntrinsicSizeAndApplyTo) {
    gfx::Brush lin = gfx::Brush::LinearGradient({ ui::Color::Red(), ui::Color::Blue() }, { 0, 0 }, { 30, 40 });
    CHECK_EQ(lin.IntrinsicSize(), (ui::Size{ 30, 40 }));
    const ui::Size inf = gfx::Brush::LinearGradient({ ui::Color::Red(), ui::Color::Blue() }).IntrinsicSize();
    CHECK(std::isnan(inf.width) && std::isnan(inf.height));
    gfx::Paint p;
    gfx::Brush::SolidColor(ui::Color(0xFF00FF00u)).ApplyTo({ 10, 10 }, p, 0.5f);
    CHECK(!p.shader && p.alpha == 1 && p.color == ui::Color(0xFF00FF00u).WithAlpha(0.5f));
    gfx::Brush::LinearGradient({ ui::Color::Red(), ui::Color::Blue() }).ApplyTo({ 100, 50 }, p, 0.25f);
    CHECK(p.shader && p.alpha == 0.25f && p.color == ui::Color::Black());
    CHECK(p.shader->end == (Offset{ 100, 50 }));   // the infinite end resolved against the size
    gfx::Brush::LinearGradient({ ui::Color::Red(), ui::Color::Blue() }).ApplyTo({ 0, 0 }, p, 1);
    CHECK(!p.shader);
}

TEST(CanvasSavesAndRestoresTheScope) {
    ui::DrawScope scope({}, ui::LayoutDirection::Ltr, {});
    {
        ui::Canvas canvas(scope);
        canvas.Save();
        canvas.Translate(10, 20);
        CHECK_EQ(scope.CurrentTransform().e, 10.0f);
        canvas.WithSave([&] {
            canvas.Scale(2, 2);
            CHECK_EQ(scope.CurrentTransform().a, 2.0f);
        });
        CHECK_EQ(scope.CurrentTransform().a, 1.0f);
        canvas.Concat(Matrix4::FromAffine(gfx::Matrix::Translate(5, 0)));
        CHECK_EQ(scope.CurrentTransform().e, 15.0f);
        gfx::Paint paint{ .color = ui::Color::Red(), .strokeWidth = 4, .style = gfx::PaintingStyle::Stroke };
        canvas.DrawCircle({ 5, 5 }, 4, paint);
        canvas.DrawRawPoints(ui::PointMode::Points, std::vector<float>{ 1, 2, 3, 4 }, paint);
    }   // the canvas restores what is still saved
    CHECK_EQ(scope.CurrentTransform().e, 0.0f);
}
