#include "kiln/graphics/vector/ImageVector.h"

#include <atomic>
#include <charconv>
#include <cmath>
#include <numbers>

namespace kiln::gfx::vector {

    namespace {
        constexpr double kPi = std::numbers::pi;

        // FastFloatParser.nextFloat: a float at `start` (an optional '-', digits, a fraction, an
        // exponent; no '+' sign), returning where it ended and NaN when there was none. The
        // value is significand / 10^k in float when that is exact (the parser's fast path), else
        // the correctly rounded double narrowed to float.
        std::pair<std::size_t, float> NextFloat(std::string_view s, std::size_t start, std::size_t end) {
            constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
            if (start == end) return { start, kNaN };
            std::size_t index = start;
            auto at = [&](std::size_t i) -> char { return i < s.size() ? s[i] : '\0'; };
            auto digit = [](char c) { return c >= '0' && c <= '9'; };
            char c = s[index];
            const bool negative = c == '-';
            if (negative) {
                ++index;
                if (index == end) return { index, kNaN };
                c = s[index];
                if (!digit(c) && c != '.') return { index, kNaN };
            }
            std::uint64_t significand = 0;
            int digits = 0, exponent = 0;
            bool overflow = false;
            auto accumulate = [&](char ch) {
                if (significand > (std::numeric_limits<std::uint64_t>::max() - 9) / 10) overflow = true;
                else significand = significand * 10 + std::uint64_t(ch - '0');
            };
            const std::size_t numberStart = index;
            while (index != end && digit(c)) { accumulate(c); ++digits; c = at(++index); }
            if (index != end && c == '.') {
                c = at(++index);
                const std::size_t fractionStart = index;
                while (index != end && digit(c)) { accumulate(c); c = at(++index); }
                exponent = -int(index - fractionStart);
                digits += int(index - fractionStart);
            }
            if (digits == 0) return { index, kNaN };
            if ((c | 0x20) == 'e') {
                c = at(++index);
                const bool expNegative = c == '-';
                if (expNegative || c == '+') c = at(++index);
                int e = 0;
                while (index != end && digit(c)) { if (e < 1024) e = e * 10 + (c - '0'); c = at(++index); }
                exponent += expNegative ? -e : e;
            }
            float value;
            if (!overflow && digits <= 19 && exponent >= -10 && exponent <= 10 && significand <= (1ull << 24)) {
                static constexpr float kPowers[] = { 1e0f, 1e1f, 1e2f, 1e3f, 1e4f, 1e5f, 1e6f, 1e7f, 1e8f, 1e9f, 1e10f };
                value = float(significand);
                value = exponent < 0 ? value / kPowers[-exponent] : value * kPowers[exponent];
            } else {
                double d = 0;
                std::from_chars(s.data() + numberStart, s.data() + index, d);
                value = float(d);
            }
            return { index, negative ? -value : value };
        }

        void AddNodes(char command, PathData& nodes, const std::vector<float>& args, int count) {
            using K = PathNode::Kind;
            auto many = [&](K kind, int numArgs) {
                for (int i = 0; i + numArgs <= count; i += numArgs) {
                    PathNode n;
                    n.kind = kind;
                    for (int j = 0; j < numArgs; ++j) n.v[std::size_t(j)] = args[std::size_t(i + j)];
                    if (kind == K::ArcTo || kind == K::RelativeArcTo) {   // flags: nonzero is true
                        n.v[3] = n.v[3] != 0 ? 1.0f : 0.0f;
                        n.v[4] = n.v[4] != 0 ? 1.0f : 0.0f;
                    }
                    nodes.push_back(n);
                }
            };
            // A moveto's extra pairs are linetos.
            auto move = [&](K first, K rest) {
                if (count < 2) return;
                nodes.push_back({ first, { args[0], args[1] } });
                for (int i = 2; i + 2 <= count; i += 2) nodes.push_back({ rest, { args[std::size_t(i)], args[std::size_t(i + 1)] } });
            };
            switch (command) {
                case 'z': case 'Z': nodes.push_back({ K::Close, {} }); break;
                case 'm': move(K::RelativeMoveTo, K::RelativeLineTo); break;
                case 'M': move(K::MoveTo, K::LineTo); break;
                case 'l': many(K::RelativeLineTo, 2); break;
                case 'L': many(K::LineTo, 2); break;
                case 'h': many(K::RelativeHorizontalTo, 1); break;
                case 'H': many(K::HorizontalTo, 1); break;
                case 'v': many(K::RelativeVerticalTo, 1); break;
                case 'V': many(K::VerticalTo, 1); break;
                case 'c': many(K::RelativeCurveTo, 6); break;
                case 'C': many(K::CurveTo, 6); break;
                case 's': many(K::RelativeReflectiveCurveTo, 4); break;
                case 'S': many(K::ReflectiveCurveTo, 4); break;
                case 'q': many(K::RelativeQuadTo, 4); break;
                case 'Q': many(K::QuadTo, 4); break;
                case 't': many(K::RelativeReflectiveQuadTo, 2); break;
                case 'T': many(K::ReflectiveQuadTo, 2); break;
                case 'a': many(K::RelativeArcTo, 7); break;
                case 'A': many(K::ArcTo, 7); break;
                default: break;   // Compose throws for an unknown command
            }
        }

        void ArcToBezier(Path& p, double cx, double cy, double a, double b, double e1x, double e1y, double theta, double start, double sweep) {
            const int numSegments = int(std::ceil(std::abs(sweep * 4 / kPi)));
            double eta1 = start;
            const double cosTheta = std::cos(theta), sinTheta = std::sin(theta);
            const double cosEta1 = std::cos(eta1), sinEta1 = std::sin(eta1);
            double ep1x = (-a * cosTheta * sinEta1) - (b * sinTheta * cosEta1);
            double ep1y = (-a * sinTheta * sinEta1) + (b * cosTheta * cosEta1);
            const double anglePerSegment = sweep / numSegments;
            for (int i = 0; i < numSegments; ++i) {
                const double eta2 = eta1 + anglePerSegment;
                const double sinEta2 = std::sin(eta2), cosEta2 = std::cos(eta2);
                const double e2x = cx + (a * cosTheta * cosEta2) - (b * sinTheta * sinEta2);
                const double e2y = cy + (a * sinTheta * cosEta2) + (b * cosTheta * sinEta2);
                const double ep2x = (-a * cosTheta * sinEta2) - (b * sinTheta * cosEta2);
                const double ep2y = (-a * sinTheta * sinEta2) + (b * cosTheta * cosEta2);
                const double tanDiff2 = std::tan((eta2 - eta1) / 2);
                const double alpha = std::sin(eta2 - eta1) * (std::sqrt(4 + 3.0 * tanDiff2 * tanDiff2) - 1) / 3;
                p.CubicTo(float(e1x + alpha * ep1x), float(e1y + alpha * ep1y), float(e2x - alpha * ep2x), float(e2y - alpha * ep2y),
                          float(e2x), float(e2y));
                eta1 = eta2;
                e1x = e2x;
                e1y = e2y;
                ep1x = ep2x;
                ep1y = ep2y;
            }
        }

        void DrawArc(Path& p, double x0, double y0, double x1, double y1, double a, double b, double theta, bool isMoreThanHalf, bool isPositiveArc) {
            const double thetaD = theta / 180 * kPi;
            const double cosTheta = std::cos(thetaD), sinTheta = std::sin(thetaD);
            // Into unit space: inverse rotation, then inverse scale.
            const double x0p = (x0 * cosTheta + y0 * sinTheta) / a;
            const double y0p = (-x0 * sinTheta + y0 * cosTheta) / b;
            const double x1p = (x1 * cosTheta + y1 * sinTheta) / a;
            const double y1p = (-x1 * sinTheta + y1 * cosTheta) / b;
            const double dx = x0p - x1p, dy = y0p - y1p;
            const double xm = (x0p + x1p) / 2, ym = (y0p + y1p) / 2;
            const double dsq = dx * dx + dy * dy;
            if (dsq == 0.0) return;   // coincident points
            const double disc = 1.0 / dsq - 1.0 / 4.0;
            if (disc < 0.0) {   // too far apart: grow the radii
                const float adjust = float(std::sqrt(dsq) / 1.99999);
                DrawArc(p, x0, y0, x1, y1, a * adjust, b * adjust, theta, isMoreThanHalf, isPositiveArc);
                return;
            }
            const double s = std::sqrt(disc), sdx = s * dx, sdy = s * dy;
            double cx, cy;
            if (isMoreThanHalf == isPositiveArc) { cx = xm - sdy; cy = ym + sdx; }
            else { cx = xm + sdy; cy = ym - sdx; }
            const double eta0 = std::atan2(y0p - cy, x0p - cx);
            const double eta1 = std::atan2(y1p - cy, x1p - cx);
            double sweep = eta1 - eta0;
            if (isPositiveArc != (sweep >= 0)) sweep += sweep > 0 ? -2 * kPi : 2 * kPi;
            cx *= a;
            cy *= b;
            const double tcx = cx;
            cx = cx * cosTheta - cy * sinTheta;
            cy = tcx * sinTheta + cy * cosTheta;
            ArcToBezier(p, cx, cy, a, b, x0, y0, thetaD, eta0, sweep);
        }
    }

    PathData ParsePathString(std::string_view s) {
        PathData nodes;
        std::vector<float> data;
        std::size_t start = 0, end = s.size();
        while (start < end && s[start] <= ' ') ++start;
        while (end > start && s[end - 1] <= ' ') --end;
        std::size_t index = start;
        while (index < end) {
            char command = 0;
            // The next letter that is not e / E (those belong to numbers).
            do {
                const char c = s[index++];
                const int lower = c | 0x20;
                if ((lower - 'a') * (lower - 'z') <= 0 && lower != 'e') { command = c; break; }
            } while (index < end);
            if (!command) continue;
            data.clear();
            if ((command | 0x20) != 'z') {
                while (index < end && s[index] <= ' ') ++index;
                const bool arc = (command | 0x20) == 'a';
                float value;
                do {
                    // An arc's two flags may run into the next number: they are one character.
                    // (Only the first arc of a command: the parser counts from its start.)
                    const std::size_t n = data.size();
                    auto [next, v] = arc && (n == 3 || n == 4) ? NextFloat(s, index, std::min(index + 1, end)) : NextFloat(s, index, end);
                    index = next;
                    value = v;
                    if (!std::isnan(value)) data.push_back(value);
                    while (index < end && (s[index] <= ' ' || s[index] == ',')) ++index;
                } while (index < end && !std::isnan(value));
            }
            AddNodes(command, nodes, data, int(data.size()));
        }
        return nodes;
    }

    void ToPath(const PathData& nodes, Path& target) {
        using K = PathNode::Kind;
        const FillType fillType = target.fillType;
        target.Reset();
        target.fillType = fillType;
        float currentX = 0, currentY = 0, ctrlX = 0, ctrlY = 0, segmentX = 0, segmentY = 0;
        PathNode previous = nodes.empty() ? PathNode{} : nodes[0];
        for (const PathNode& n : nodes) {
            const auto& v = n.v;
            switch (n.kind) {
                case K::Close:
                    currentX = ctrlX = segmentX;
                    currentY = ctrlY = segmentY;
                    target.Close();
                    break;
                case K::RelativeMoveTo:
                    currentX += v[0]; currentY += v[1];
                    target.RelativeMoveTo(v[0], v[1]);
                    segmentX = currentX; segmentY = currentY;
                    break;
                case K::MoveTo:
                    currentX = v[0]; currentY = v[1];
                    target.MoveTo(v[0], v[1]);
                    segmentX = currentX; segmentY = currentY;
                    break;
                case K::RelativeLineTo: target.RelativeLineTo(v[0], v[1]); currentX += v[0]; currentY += v[1]; break;
                case K::LineTo: target.LineTo(v[0], v[1]); currentX = v[0]; currentY = v[1]; break;
                case K::RelativeHorizontalTo: target.RelativeLineTo(v[0], 0); currentX += v[0]; break;
                case K::HorizontalTo: target.LineTo(v[0], currentY); currentX = v[0]; break;
                case K::RelativeVerticalTo: target.RelativeLineTo(0, v[0]); currentY += v[0]; break;
                case K::VerticalTo: target.LineTo(currentX, v[0]); currentY = v[0]; break;
                case K::RelativeCurveTo:
                    target.RelativeCubicTo(v[0], v[1], v[2], v[3], v[4], v[5]);
                    ctrlX = currentX + v[2]; ctrlY = currentY + v[3];
                    currentX += v[4]; currentY += v[5];
                    break;
                case K::CurveTo:
                    target.CubicTo(v[0], v[1], v[2], v[3], v[4], v[5]);
                    ctrlX = v[2]; ctrlY = v[3]; currentX = v[4]; currentY = v[5];
                    break;
                case K::RelativeReflectiveCurveTo: {
                    const float rx = previous.IsCurve() ? currentX - ctrlX : 0, ry = previous.IsCurve() ? currentY - ctrlY : 0;
                    target.RelativeCubicTo(rx, ry, v[0], v[1], v[2], v[3]);
                    ctrlX = currentX + v[0]; ctrlY = currentY + v[1];
                    currentX += v[2]; currentY += v[3];
                    break;
                }
                case K::ReflectiveCurveTo: {
                    const float rx = previous.IsCurve() ? 2 * currentX - ctrlX : currentX, ry = previous.IsCurve() ? 2 * currentY - ctrlY : currentY;
                    target.CubicTo(rx, ry, v[0], v[1], v[2], v[3]);
                    ctrlX = v[0]; ctrlY = v[1]; currentX = v[2]; currentY = v[3];
                    break;
                }
                case K::RelativeQuadTo:
                    target.RelativeQuadraticTo(v[0], v[1], v[2], v[3]);
                    ctrlX = currentX + v[0]; ctrlY = currentY + v[1];
                    currentX += v[2]; currentY += v[3];
                    break;
                case K::QuadTo:
                    target.QuadraticTo(v[0], v[1], v[2], v[3]);
                    ctrlX = v[0]; ctrlY = v[1]; currentX = v[2]; currentY = v[3];
                    break;
                case K::RelativeReflectiveQuadTo: {
                    const float rx = previous.IsQuad() ? currentX - ctrlX : 0, ry = previous.IsQuad() ? currentY - ctrlY : 0;
                    target.RelativeQuadraticTo(rx, ry, v[0], v[1]);
                    ctrlX = currentX + rx; ctrlY = currentY + ry;
                    currentX += v[0]; currentY += v[1];
                    break;
                }
                case K::ReflectiveQuadTo: {
                    const float rx = previous.IsQuad() ? 2 * currentX - ctrlX : currentX, ry = previous.IsQuad() ? 2 * currentY - ctrlY : currentY;
                    target.QuadraticTo(rx, ry, v[0], v[1]);
                    ctrlX = rx; ctrlY = ry; currentX = v[0]; currentY = v[1];
                    break;
                }
                case K::RelativeArcTo: {
                    const float endX = v[5] + currentX, endY = v[6] + currentY;
                    DrawArc(target, currentX, currentY, endX, endY, v[0], v[1], v[2], v[3] != 0, v[4] != 0);
                    currentX = ctrlX = endX;
                    currentY = ctrlY = endY;
                    break;
                }
                case K::ArcTo:
                    DrawArc(target, currentX, currentY, v[5], v[6], v[0], v[1], v[2], v[3] != 0, v[4] != 0);
                    currentX = ctrlX = v[5];
                    currentY = ctrlY = v[6];
                    break;
            }
            previous = n;
        }
    }

    // --- ImageVector.Builder ---------------------------------------------------------------------

    ImageVector::Builder::Builder(std::string name, ui::Dp defaultWidth, ui::Dp defaultHeight, float viewportWidth, float viewportHeight,
                                  ui::Color tintColor, BlendMode tintBlendMode, bool autoMirror)
        : m_Result(std::make_shared<ImageVector>()) {
        m_Image = m_Result.get();
        m_Image->name = std::move(name);
        m_Image->defaultWidth = defaultWidth;
        m_Image->defaultHeight = defaultHeight;
        m_Image->viewportWidth = viewportWidth;
        m_Image->viewportHeight = viewportHeight;
        m_Image->tintColor = tintColor;
        m_Image->tintBlendMode = tintBlendMode;
        m_Image->autoMirror = autoMirror;
        m_Stack.push_back(std::make_shared<VectorGroup>());
    }

    ImageVector::Builder& ImageVector::Builder::AddGroup(std::string name, float rotate, float pivotX, float pivotY, float scaleX, float scaleY,
                                                         float translationX, float translationY, PathData clipPathData) {
        auto g = std::make_shared<VectorGroup>();
        g->name = std::move(name);
        g->rotation = rotate;
        g->pivotX = pivotX; g->pivotY = pivotY;
        g->scaleX = scaleX; g->scaleY = scaleY;
        g->translationX = translationX; g->translationY = translationY;
        g->clipPathData = std::move(clipPathData);
        m_Stack.push_back(std::move(g));
        return *this;
    }

    ImageVector::Builder& ImageVector::Builder::ClearGroup() {
        if (m_Stack.size() < 2) return *this;
        auto popped = std::move(m_Stack.back());
        m_Stack.pop_back();
        m_Stack.back()->children.emplace_back(std::shared_ptr<const VectorGroup>(std::move(popped)));
        return *this;
    }

    ImageVector::Builder& ImageVector::Builder::AddPath(VectorPath path) {
        m_Stack.back()->children.emplace_back(std::move(path));
        return *this;
    }

    ImageVector::Builder& ImageVector::Builder::Path(PathData nodes, std::optional<Brush> fill, float fillAlpha, std::optional<Brush> stroke,
                                                     float strokeAlpha, float strokeLineWidth, StrokeCap cap, StrokeJoin join, float miter,
                                                     FillType fillType) {
        VectorPath p;
        p.pathData = std::move(nodes);
        p.pathFillType = fillType;
        p.fill = std::move(fill);
        p.fillAlpha = fillAlpha;
        p.stroke = std::move(stroke);
        p.strokeAlpha = strokeAlpha;
        p.strokeLineWidth = strokeLineWidth;
        p.strokeLineCap = cap;
        p.strokeLineJoin = join;
        p.strokeLineMiter = miter;
        return AddPath(std::move(p));
    }

    std::shared_ptr<const ImageVector> ImageVector::Builder::Build() {
        if (m_Consumed) return nullptr;   // single use
        while (m_Stack.size() > 1) ClearGroup();
        static std::atomic<int> count{ 0 };
        m_Image->root = std::move(m_Stack.back());
        m_Image->genId = count++;
        m_Consumed = true;
        return std::move(m_Result);
    }

    bool VectorGroup::operator==(const VectorGroup& o) const {
        if (name != o.name || rotation != o.rotation || pivotX != o.pivotX || pivotY != o.pivotY || scaleX != o.scaleX || scaleY != o.scaleY ||
            translationX != o.translationX || translationY != o.translationY || clipPathData != o.clipPathData || children.size() != o.children.size())
            return false;
        for (std::size_t i = 0; i < children.size(); ++i) {
            const VectorNode& a = children[i];
            const VectorNode& b = o.children[i];
            if (a.index() != b.index()) return false;
            if (const auto* pa = std::get_if<VectorPath>(&a)) {
                if (!(*pa == std::get<VectorPath>(b))) return false;
            } else {
                const auto& ga = std::get<std::shared_ptr<const VectorGroup>>(a);
                const auto& gb = std::get<std::shared_ptr<const VectorGroup>>(b);
                if (ga != gb && (!ga || !gb || !(*ga == *gb))) return false;
            }
        }
        return true;
    }

    void RenderVectorGroup(ImageVector::Builder& builder, const VectorGroup& group, const std::map<std::string, const VectorConfig*>& configs) {
        namespace P = VectorProperties;
        static const VectorConfig none;
        auto configFor = [&](const std::string& name) -> const VectorConfig& {
            const auto it = configs.find(name);
            return it != configs.end() && it->second ? *it->second : none;
        };
        for (const VectorNode& node : group.children) {
            if (const auto* p = std::get_if<VectorPath>(&node)) {
                const VectorConfig& c = configFor(p->name);
                VectorPath out = *p;
                out.pathData = c.GetOrDefault(P::PathData, p->pathData);
                out.fill = c.GetOrDefault(P::Fill, p->fill);
                out.fillAlpha = c.GetOrDefault(P::FillAlpha, p->fillAlpha);
                out.stroke = c.GetOrDefault(P::Stroke, p->stroke);
                out.strokeAlpha = c.GetOrDefault(P::StrokeAlpha, p->strokeAlpha);
                out.strokeLineWidth = c.GetOrDefault(P::StrokeLineWidth, p->strokeLineWidth);
                out.trimPathStart = c.GetOrDefault(P::TrimPathStart, p->trimPathStart);
                out.trimPathEnd = c.GetOrDefault(P::TrimPathEnd, p->trimPathEnd);
                out.trimPathOffset = c.GetOrDefault(P::TrimPathOffset, p->trimPathOffset);
                builder.AddPath(std::move(out));
            } else if (const auto& g = std::get<std::shared_ptr<const VectorGroup>>(node)) {
                const VectorConfig& c = configFor(g->name);
                builder.AddGroup(g->name, c.GetOrDefault(P::Rotation, g->rotation), c.GetOrDefault(P::PivotX, g->pivotX), c.GetOrDefault(P::PivotY, g->pivotY),
                                 c.GetOrDefault(P::ScaleX, g->scaleX), c.GetOrDefault(P::ScaleY, g->scaleY), c.GetOrDefault(P::TranslateX, g->translationX),
                                 c.GetOrDefault(P::TranslateY, g->translationY), c.GetOrDefault(P::PathData, g->clipPathData));
                RenderVectorGroup(builder, *g, configs);
                builder.ClearGroup();
            }
        }
    }

}
