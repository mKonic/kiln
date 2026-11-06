#include "kiln/foundation/PolygonShape.h"

#include "kiln/graphics/shapes/Shapes.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace kiln::foundation {

    namespace shp = gfx::shapes;

    namespace {
        bool Specified(Offset o) { return !std::isnan(o.x) && !std::isnan(o.y); }
        bool SameOffset(Offset a, Offset b) { return (std::isnan(a.x) && std::isnan(b.x)) || (a.x == b.x && a.y == b.y); }

        Size VertexBounds(const std::vector<Offset>& v) {
            float minX = std::numeric_limits<float>::infinity(), minY = minX, maxX = -minX, maxY = -minX;
            for (const Offset& p : v) { minX = std::min(minX, p.x); minY = std::min(minY, p.y); maxX = std::max(maxX, p.x); maxY = std::max(maxY, p.y); }
            return { maxX - minX, maxY - minY };
        }

        // toRoundedPolygonRounding: dp at the density, fractions of the reference's min side.
        shp::CornerRounding ToRounding(const PolygonCornerRounding& r, Size reference, const ui::Density& d) {
            if (r.value == 0) return shp::CornerRounding::Unrounded();
            const float px = r.unit == PolygonCornerRounding::Unit::Dp ? r.value * d.density : r.value * reference.MinDimension();
            return { px, r.smoothing };
        }
        std::vector<shp::CornerRounding> ToRoundings(const std::vector<PolygonCornerRounding>& v, Size reference, const ui::Density& d) {
            std::vector<shp::CornerRounding> out;
            for (const auto& r : v) out.push_back(ToRounding(r, reference, d));
            return out;
        }

        shp::RoundedPolygon ToRoundedPolygon(const PolygonShapeGeometry& g, const ui::Density& d) {
            const Size reference = g.roundingReference.value_or(VertexBounds(g.vertices));
            std::vector<float> xy;
            for (const Offset& v : g.vertices) { xy.push_back(v.x); xy.push_back(v.y); }
            std::vector<shp::CornerRounding> perVertex;
            if (g.perVertexRounding) perVertex = ToRoundings(*g.perVertexRounding, reference, d);
            return shp::RoundedPolygon::FromVertices(xy, ToRounding(g.rounding, reference, d), g.perVertexRounding ? &perVertex : nullptr,
                                                     Specified(g.center) ? g.center.x : shp::RoundedPolygon::kAutoCenter,
                                                     Specified(g.center) ? g.center.y : shp::RoundedPolygon::kAutoCenter);
        }

        std::vector<Offset> RegularVertices(int n, float radius, Offset center) {
            std::vector<Offset> out;
            for (int i = 0; i < n; ++i) {
                const float angle = 3.14159265358979323846f / float(n) * 2 * float(i);
                out.push_back({ center.x + radius * std::cos(angle), center.y + radius * std::sin(angle) });
            }
            return out;
        }

        // The polygon's bounds with a matrix applied to its anchors and controls.
        std::array<float, 4> TransformedBounds(const shp::RoundedPolygon& p, const gfx::Matrix* m) {
            if (!m) return p.CalculateBounds(true);
            float minX = std::numeric_limits<float>::max(), minY = minX, maxX = -minX, maxY = -minX;
            for (const shp::Cubic& c : p.Cubics())
                for (int i = 0; i < 3; ++i) {
                    const Offset q = m->Map({ c.points[std::size_t(i * 2)], c.points[std::size_t(i * 2 + 1)] });
                    minX = std::min(minX, q.x); minY = std::min(minY, q.y); maxX = std::max(maxX, q.x); maxY = std::max(maxY, q.y);
                }
            return { minX, minY, maxX, maxY };
        }

        bool Identity(const gfx::Matrix& m) { return m.a == 1 && m.b == 0 && m.c == 0 && m.d == 1 && m.e == 0 && m.f == 0; }
    }

    bool PolygonShapeGeometry::operator==(const PolygonShapeGeometry& o) const {
        if (vertices.size() != o.vertices.size()) return false;
        for (std::size_t i = 0; i < vertices.size(); ++i) if (!(vertices[i] == o.vertices[i])) return false;
        return SameOffset(center, o.center) && rounding == o.rounding && perVertexRounding == o.perVertexRounding &&
               roundingReference == o.roundingReference;
    }

    // --- PolygonShapeScope -------------------------------------------------------------------

    PolygonShapeGeometry PolygonShapeScope::Polygon(int n, float radius, Offset center, PolygonCornerRounding rounding) const {
        if (std::isnan(radius)) radius = size.MinDimension() / 2;
        if (!Specified(center)) center = { size.width / 2, size.height / 2 };
        return { RegularVertices(std::max(3, n), radius, center), center, rounding, std::nullopt, Size{ radius, radius } };
    }
    PolygonShapeGeometry PolygonShapeScope::Polygon(int n, std::vector<PolygonCornerRounding> perVertex, float radius, Offset center) const {
        if (std::isnan(radius)) radius = size.MinDimension() / 2;
        if (!Specified(center)) center = { size.width / 2, size.height / 2 };
        return { RegularVertices(std::max(3, n), radius, center), center, {}, std::move(perVertex), Size{ radius, radius } };
    }
    PolygonShapeGeometry PolygonShapeScope::Polygon(std::vector<Offset> vertices, Offset center, PolygonCornerRounding rounding) const {
        return { std::move(vertices), center, rounding, std::nullopt, std::nullopt };
    }
    PolygonShapeGeometry PolygonShapeScope::Polygon(std::vector<Offset> vertices, std::vector<PolygonCornerRounding> perVertex, Offset center) const {
        return { std::move(vertices), center, {}, std::move(perVertex), std::nullopt };
    }

    // --- PolygonShapeTransformScope ----------------------------------------------------------

    gfx::Matrix PolygonShapeTransformScope::Combine(const gfx::Matrix& after, const gfx::Matrix& before) {
        // after(before(p))
        return { after.a * before.a + after.c * before.b, after.b * before.a + after.d * before.b,
                 after.a * before.c + after.c * before.d, after.b * before.c + after.d * before.d,
                 after.a * before.e + after.c * before.f + after.e, after.b * before.e + after.d * before.f + after.f };
    }

    void PolygonShapeTransformScope::Reset(const shp::RoundedPolygon& base, Size s, ui::LayoutDirection dir, ui::Density d) {
        m_Base = &base;
        size = s;
        layoutDirection = dir;
        density = d;
        m_CenterX = base.CenterX();
        m_CenterY = base.CenterY();
        matrix = {};
        hasMatrix = false;
    }

    void PolygonShapeTransformScope::Rotate(float degrees, Offset pivot) {
        if (degrees == 0) return;
        const float px = Specified(pivot) ? pivot.x : m_CenterX, py = Specified(pivot) ? pivot.y : m_CenterY;
        const double radians = double(degrees) * (3.14159265358979323846 / 180.0);
        const float c = float(std::cos(radians)), s = float(std::sin(radians));
        // Rotate about the pivot: translate, rotate, translate back.
        Then({ c, s, -s, c, px - c * px + s * py, py - s * px - c * py });
    }

    void PolygonShapeTransformScope::Translate(float x, float y) {
        if (x == 0 && y == 0) return;
        Then(gfx::Matrix::Translate(x, y));
    }

    void PolygonShapeTransformScope::Scale(float sx, float sy, Offset pivot) {
        if (std::isnan(sy)) sy = sx;
        if (sx == 1 && sy == 1) return;
        const float px = Specified(pivot) ? pivot.x : m_CenterX, py = Specified(pivot) ? pivot.y : m_CenterY;
        Then({ sx, 0, 0, sy, px - px * sx, py - py * sy });
    }

    void PolygonShapeTransformScope::Transform(const gfx::Matrix& m) {
        if (Identity(m)) return;
        // About the centre.
        const gfx::Matrix about = Combine(gfx::Matrix::Translate(m_CenterX, m_CenterY), Combine(m, gfx::Matrix::Translate(-m_CenterX, -m_CenterY)));
        Then(about);
    }

    void PolygonShapeTransformScope::ScaleToFit(ContentScale contentScale, Alignment alignment) {
        if (!m_Base) return;
        const gfx::Matrix* current = hasMatrix && !Identity(matrix) ? &matrix : nullptr;
        const auto b = TransformedBounds(*m_Base, current);
        const float width = b[2] - b[0], height = b[3] - b[1];
        if (width <= 0 || height <= 0) return;
        const ScaleFactor factor = contentScale.ComputeScaleFactor({ width, height }, size);
        // computeAlignedOffset (bias alignment, unrounded).
        const float h = layoutDirection == ui::LayoutDirection::Ltr ? alignment.horizontal : -alignment.horizontal;
        const float posX = (size.width - width * factor.x) / 2 * (1 + h);
        const float posY = (size.height - height * factor.y) / 2 * (1 + alignment.vertical);
        const float offX = posX - b[0] * factor.x, offY = posY - b[1] * factor.y;
        if (factor.x == 1 && factor.y == 1 && offX == 0 && offY == 0) return;
        Then({ factor.x, 0, 0, factor.y, offX, offY });
        m_CenterX = m_CenterX * factor.x + offX;
        m_CenterY = m_CenterY * factor.y + offY;
    }

    // --- PolygonShape ------------------------------------------------------------------------

    struct PolygonShape::Transformation {
        TransformBlock block;
        PolygonShapeTransformScope scope;
        std::shared_ptr<const shp::RoundedPolygon> base, transformed;
        gfx::Matrix matrix;

        std::shared_ptr<const shp::RoundedPolygon> Resolve(const std::shared_ptr<const shp::RoundedPolygon>& polygon, Size size,
                                                           ui::LayoutDirection dir, const ui::Density& d) {
            scope.Reset(*polygon, size, dir, d);
            if (block) block(scope);
            if (!scope.hasMatrix || Identity(scope.matrix)) return polygon;
            const gfx::Matrix& m = scope.matrix;
            if (transformed && polygon == base && m.a == matrix.a && m.b == matrix.b && m.c == matrix.c && m.d == matrix.d &&
                m.e == matrix.e && m.f == matrix.f)
                return transformed;
            base = polygon;
            matrix = m;
            transformed = std::make_shared<shp::RoundedPolygon>(polygon->Transformed([m](float x, float y) {
                const Offset p = m.Map({ x, y });
                return shp::Point{ p.x, p.y };
            }));
            return transformed;
        }
    };

    void PolygonShape::Transform(TransformBlock block) {
        if (!m_Transform) m_Transform = std::make_shared<Transformation>();
        m_Transform->block = std::move(block);
        m_Transform->transformed.reset();
        m_Outline.reset();
    }

    std::shared_ptr<PolygonShape> PolygonShape::Copy() const {
        auto copy = CopyBase();
        if (m_Transform) copy->Transform(m_Transform->block);
        return copy;
    }

    std::shared_ptr<const shp::RoundedPolygon> PolygonShape::ResolveBase(Size size, ui::LayoutDirection dir, const ui::Density& d) const {
        const int version = ContentVersion(size, dir, d);
        if (m_Base && size == m_BaseSize && dir == m_BaseDir && d.density == m_BaseDensity.density &&
            d.fontScale == m_BaseDensity.fontScale && version == m_BaseVersion)
            return m_Base;
        m_Base = std::make_shared<shp::RoundedPolygon>(BuildPolygon(size, dir, d));
        m_BaseSize = size;
        m_BaseDir = dir;
        m_BaseDensity = d;
        m_BaseVersion = version;
        return m_Base;
    }

    std::shared_ptr<const shp::RoundedPolygon> PolygonShape::ResolvePolygon(Size size, ui::LayoutDirection dir, const ui::Density& d) const {
        auto base = ResolveBase(size, dir, d);
        if (!m_Transform) return base;
        return m_Transform->Resolve(base, size, dir, d);
    }

    gfx::Outline PolygonShape::CreateOutline(Size size, ui::LayoutDirection dir, const ui::Density& d) const {
        auto polygon = ResolvePolygon(size, dir, d);
        if (m_Outline && polygon == m_OutlinePolygon) return *m_Outline;
        m_OutlinePolygon = polygon;
        m_Outline = gfx::Outline{ polygon->ToPath() };
        return *m_Outline;
    }

    namespace {
        class BuilderPolygonShape final : public PolygonShape {
        public:
            explicit BuilderPolygonShape(Builder b) : m_Builder(std::move(b)) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override { return std::make_shared<BuilderPolygonShape>(m_Builder); }
            PolygonShapeGeometry Evaluate(Size size, ui::LayoutDirection dir, const ui::Density& d) const {
                PolygonShapeScope scope;
                scope.size = size;
                scope.layoutDirection = dir;
                scope.density = d;
                return m_Builder(scope);
            }
            int ContentVersion(Size size, ui::LayoutDirection dir, const ui::Density& d) const override {
                PolygonShapeGeometry g = Evaluate(size, dir, d);
                if (!m_Last || !(g == *m_Last)) { m_Last = std::move(g); ++m_Version; }
                return m_Version;
            }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection dir, const ui::Density& d) const override {
                if (!m_Last) m_Last = Evaluate(size, dir, d);
                return ToRoundedPolygon(*m_Last, d);
            }
        private:
            Builder m_Builder;
            mutable std::optional<PolygonShapeGeometry> m_Last;
            mutable int m_Version = 0;
        };

        class PolyRegularShape final : public PolygonShape {
        public:
            PolyRegularShape(int n, PolygonCornerRounding r) : m_N(n), m_Rounding(r) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override { return std::make_shared<PolyRegularShape>(m_N, m_Rounding); }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection, const ui::Density& d) const override {
                const float radius = std::min(size.width, size.height) / 2;
                return shp::RoundedPolygon::Regular(m_N, radius, size.width / 2, size.height / 2, ToRounding(m_Rounding, { radius, radius }, d));
            }
        private:
            int m_N;
            PolygonCornerRounding m_Rounding;
        };

        class PolyStarShape final : public PolygonShape {
        public:
            PolyStarShape(int n, float ratio, PolygonCornerRounding outer, PolygonCornerRounding inner) : m_N(n), m_Ratio(ratio), m_Outer(outer), m_Inner(inner) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override { return std::make_shared<PolyStarShape>(m_N, m_Ratio, m_Outer, m_Inner); }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection, const ui::Density& d) const override {
                const float radius = std::min(size.width, size.height) / 2;
                const Size ref{ radius, radius };
                return shp::Star(m_N, radius, radius * m_Ratio, ToRounding(m_Outer, ref, d), ToRounding(m_Inner, ref, d), nullptr,
                                 size.width / 2, size.height / 2);
            }
        private:
            int m_N;
            float m_Ratio;
            PolygonCornerRounding m_Outer, m_Inner;
        };

        class PolyPillShape final : public PolygonShape {
        public:
            explicit PolyPillShape(float smoothing) : m_Smoothing(smoothing) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override { return std::make_shared<PolyPillShape>(m_Smoothing); }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection, const ui::Density&) const override {
                return shp::Pill(size.width, size.height, m_Smoothing, size.width / 2, size.height / 2);
            }
        private:
            float m_Smoothing;
        };

        class PolyPillStarShape final : public PolygonShape {
        public:
            PolyPillStarShape(int n, float ratio, float spacing, float start, PolygonCornerRounding outer, PolygonCornerRounding inner)
                : m_N(n), m_Ratio(ratio), m_Spacing(spacing), m_Start(start), m_Outer(outer), m_Inner(inner) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override {
                return std::make_shared<PolyPillStarShape>(m_N, m_Ratio, m_Spacing, m_Start, m_Outer, m_Inner);
            }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection, const ui::Density& d) const override {
                const float inset = std::min(size.width, size.height) / 2;
                const float w = size.width - inset, h = size.height - inset;
                const float radius = std::min(w, h);
                const Size ref{ radius, radius };
                return shp::PillStar(w, h, m_N, m_Ratio, ToRounding(m_Outer, ref, d), ToRounding(m_Inner, ref, d), nullptr, m_Spacing, m_Start,
                                     size.width / 2, size.height / 2);
            }
        private:
            int m_N;
            float m_Ratio, m_Spacing, m_Start;
            PolygonCornerRounding m_Outer, m_Inner;
        };

        class PolyCircleShape final : public PolygonShape {
        public:
            explicit PolyCircleShape(int n) : m_N(n) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override { return std::make_shared<PolyCircleShape>(m_N); }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection, const ui::Density&) const override {
                return shp::Circle(m_N, std::min(size.width, size.height) / 2, size.width / 2, size.height / 2);
            }
        private:
            int m_N;
        };

        class PolyRectangleShape final : public PolygonShape {
        public:
            PolyRectangleShape(PolygonCornerRounding ts, PolygonCornerRounding te, PolygonCornerRounding be, PolygonCornerRounding bs, bool absolute)
                : m_Ts(ts), m_Te(te), m_Be(be), m_Bs(bs), m_Absolute(absolute) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override { return std::make_shared<PolyRectangleShape>(m_Ts, m_Te, m_Be, m_Bs, m_Absolute); }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection dir, const ui::Density& d) const override {
                const bool rtl = !m_Absolute && dir == ui::LayoutDirection::Rtl;
                // Vertex order: bottom right, bottom left, top left, top right.
                const std::vector<shp::CornerRounding> corners{
                    ToRounding(rtl ? m_Bs : m_Be, size, d), ToRounding(rtl ? m_Be : m_Bs, size, d),
                    ToRounding(rtl ? m_Te : m_Ts, size, d), ToRounding(rtl ? m_Ts : m_Te, size, d) };
                return shp::Rectangle(size.width, size.height, {}, &corners, size.width / 2, size.height / 2);
            }
        private:
            PolygonCornerRounding m_Ts, m_Te, m_Be, m_Bs;
            bool m_Absolute;
        };

        class CornerShapePolygon final : public PolygonShape {
        public:
            CornerShapePolygon(CornerSize ts, CornerSize te, CornerSize be, CornerSize bs, bool cut, bool absolute)
                : m_Ts(ts), m_Te(te), m_Be(be), m_Bs(bs), m_Cut(cut), m_Absolute(absolute) {}
        protected:
            std::shared_ptr<PolygonShape> CopyBase() const override { return std::make_shared<CornerShapePolygon>(m_Ts, m_Te, m_Be, m_Bs, m_Cut, m_Absolute); }
            shp::RoundedPolygon BuildPolygon(Size size, ui::LayoutDirection dir, const ui::Density& d) const override {
                float ts = m_Ts.ToPx(size, d), te = m_Te.ToPx(size, d), bs = m_Bs.ToPx(size, d), be = m_Be.ToPx(size, d);
                const float minDim = size.MinDimension();
                if (ts + bs > minDim) { const float s = minDim / (ts + bs); ts *= s; bs *= s; }
                if (te + be > minDim) { const float s = minDim / (te + be); te *= s; be *= s; }
                const bool rtl = !m_Absolute && dir == ui::LayoutDirection::Rtl;
                const float tl = rtl ? te : ts, tr = rtl ? ts : te, bl = rtl ? be : bs, br = rtl ? bs : be;
                const float w = size.width, h = size.height;
                if (!m_Cut) {
                    const std::vector<shp::CornerRounding> corners{ { br, 0 }, { bl, 0 }, { tl, 0 }, { tr, 0 } };
                    return shp::RoundedPolygon::FromVertices({ w, h, 0, h, 0, 0, w, 0 }, {}, &corners, w / 2, h / 2);
                }
                // Cut corners: eight vertices, repeats dropped.
                std::vector<float> v;
                auto add = [&v](float x, float y) {
                    if (v.size() >= 2 && v[v.size() - 2] == x && v[v.size() - 1] == y) return;
                    v.push_back(x);
                    v.push_back(y);
                };
                add(tl, 0); add(w - tr, 0); add(w, tr); add(w, h - br);
                add(w - br, h); add(bl, h); add(0, h - bl); add(0, tl);
                if (v.size() >= 4 && v[0] == v[v.size() - 2] && v[1] == v[v.size() - 1]) v.resize(v.size() - 2);
                return shp::RoundedPolygon::FromVertices(v, {}, nullptr, w / 2, h / 2);
            }
        private:
            CornerSize m_Ts, m_Te, m_Be, m_Bs;
            bool m_Cut, m_Absolute;
        };

        class MorphShape final : public gfx::Shape {
        public:
            MorphShape(std::shared_ptr<PolygonShape> s, std::shared_ptr<PolygonShape> e, std::function<float()> p)
                : m_Start(std::move(s)), m_End(std::move(e)), m_Progress(std::move(p)) {}
            gfx::Outline CreateOutline(Size size, ui::LayoutDirection dir, const ui::Density& d) const override {
                auto start = m_Start->ResolvePolygon(size, dir, d);
                auto end = m_End->ResolvePolygon(size, dir, d);
                if (start != m_StartPolygon || end != m_EndPolygon || !m_Morph) {
                    m_Morph = std::make_unique<shp::Morph>(*start, *end);
                    m_StartPolygon = start;
                    m_EndPolygon = end;
                    m_StartOutline.reset();
                    m_EndOutline.reset();
                    m_LastOutline.reset();
                    m_LastProgress = std::numeric_limits<float>::quiet_NaN();
                }
                const float p = m_Progress ? m_Progress() : 0.0f;
                if (p <= 0) { if (!m_StartOutline) m_StartOutline = gfx::Outline{ start->ToPath() }; return *m_StartOutline; }
                if (p >= 1) { if (!m_EndOutline) m_EndOutline = gfx::Outline{ end->ToPath() }; return *m_EndOutline; }
                if (p == m_LastProgress && m_LastOutline) return *m_LastOutline;
                m_LastOutline = gfx::Outline{ m_Morph->ToPath(p) };
                m_LastProgress = p;
                return *m_LastOutline;
            }
        private:
            std::shared_ptr<PolygonShape> m_Start, m_End;
            std::function<float()> m_Progress;
            mutable std::unique_ptr<shp::Morph> m_Morph;
            mutable std::shared_ptr<const shp::RoundedPolygon> m_StartPolygon, m_EndPolygon;
            mutable std::optional<gfx::Outline> m_StartOutline, m_EndOutline, m_LastOutline;
            mutable float m_LastProgress = std::numeric_limits<float>::quiet_NaN();
        };
    }

    std::shared_ptr<PolygonShape> PolygonShape::FromBuilder(Builder b) { return std::make_shared<BuilderPolygonShape>(std::move(b)); }
    std::shared_ptr<PolygonShape> PolygonShape::RegularPolygon(int n, PolygonCornerRounding r) { return std::make_shared<PolyRegularShape>(std::max(3, n), r); }
    std::shared_ptr<PolygonShape> PolygonShape::Star(int n, float ratio, PolygonCornerRounding outer, std::optional<PolygonCornerRounding> inner) {
        return std::make_shared<PolyStarShape>(std::max(2, n), std::clamp(ratio, 1e-4f, 0.9999f), outer, inner.value_or(outer));
    }
    std::shared_ptr<PolygonShape> PolygonShape::Pill(float smoothing) { return std::make_shared<PolyPillShape>(std::clamp(smoothing, 0.0f, 1.0f)); }
    std::shared_ptr<PolygonShape> PolygonShape::PillStar(int n, float ratio, float spacing, float start, PolygonCornerRounding outer,
                                                         std::optional<PolygonCornerRounding> inner) {
        return std::make_shared<PolyPillStarShape>(std::max(2, n), std::clamp(ratio, 1e-4f, 0.9999f), std::clamp(spacing, 0.0f, 1.0f),
                                               std::clamp(start, 0.0f, 1.0f), outer, inner.value_or(outer));
    }
    std::shared_ptr<PolygonShape> PolygonShape::Circle(int n) { return std::make_shared<PolyCircleShape>(std::max(3, n)); }
    std::shared_ptr<PolygonShape> PolygonShape::Rectangle(PolygonCornerRounding r) { return std::make_shared<PolyRectangleShape>(r, r, r, r, false); }
    std::shared_ptr<PolygonShape> PolygonShape::Rectangle(PolygonCornerRounding ts, PolygonCornerRounding te, PolygonCornerRounding be, PolygonCornerRounding bs) {
        return std::make_shared<PolyRectangleShape>(ts, te, be, bs, false);
    }
    std::shared_ptr<PolygonShape> PolygonShape::AbsoluteRectangle(PolygonCornerRounding tl, PolygonCornerRounding tr, PolygonCornerRounding br, PolygonCornerRounding bl) {
        return std::make_shared<PolyRectangleShape>(tl, tr, br, bl, true);
    }

    std::shared_ptr<PolygonShape> ToPolygonShape(const CornerBasedShape& shape) {
        const auto* cut = dynamic_cast<const CutCornerShape*>(&shape);
        const auto* rounded = dynamic_cast<const RoundedCornerShape*>(&shape);
        const bool absolute = cut ? cut->absolute : rounded && rounded->absolute;
        return std::make_shared<CornerShapePolygon>(shape.topStart, shape.topEnd, shape.bottomEnd, shape.bottomStart, cut != nullptr, absolute);
    }

    ShapePtr MorphPolygonShape(std::shared_ptr<PolygonShape> start, std::shared_ptr<PolygonShape> end, std::function<float()> progress) {
        return std::make_shared<MorphShape>(std::move(start), std::move(end), std::move(progress));
    }

}
