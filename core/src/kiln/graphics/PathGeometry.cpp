#include "kiln/graphics/PathGeometry.h"

#include "kiln/graphics/PathIterator.h"
#include "kiln/graphics/vector/ImageVector.h"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace kiln::gfx {

    namespace {
        float CubicArea(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
            return ((y3 - y0) * (x1 + x2) - (x3 - x0) * (y1 + y2) + y1 * (x0 - x2) - x1 * (y0 - y2) + y3 * (x2 + x0 / 3.0f) -
                    x3 * (y2 + y0 / 3.0f)) * 3.0f / 20.0f;
        }
        bool CloseTo(float a, float b) { return std::abs(a - b) < 1e-7f; }
    }

    Path::Direction ComputeDirection(const Path& path) {
        PathIterator it(path);
        bool first = true;
        float area = 0, startX = 0, startY = 0, endX = 0, endY = 0;
        for (PathSegment s = it.Next(); s.type != PathSegment::Type::Done; s = it.Next()) {
            const auto& p = s.points;
            switch (s.type) {
                case PathSegment::Type::Move:
                    if (!first) goto done;
                    first = false;
                    startX = p[0].x; startY = p[0].y;
                    break;
                case PathSegment::Type::Line:
                    area += CubicArea(p[0].x, p[0].y, p[0].x, p[0].y, p[1].x, p[1].y, p[1].x, p[1].y);
                    endX = p[1].x; endY = p[1].y;
                    break;
                case PathSegment::Type::Quadratic: {
                    const float c1x = p[0].x + 2.0f / 3.0f * (p[1].x - p[0].x), c1y = p[0].y + 2.0f / 3.0f * (p[1].y - p[0].y);
                    const float c2x = p[2].x + 2.0f / 3.0f * (p[1].x - p[2].x), c2y = p[2].y + 2.0f / 3.0f * (p[1].y - p[2].y);
                    area += CubicArea(p[0].x, p[0].y, c1x, c1y, c2x, c2y, p[2].x, p[2].y);
                    endX = p[2].x; endY = p[2].y;
                    break;
                }
                case PathSegment::Type::Cubic:
                    area += CubicArea(p[0].x, p[0].y, p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);
                    endX = p[3].x; endY = p[3].y;
                    break;
                case PathSegment::Type::Close:
                    if (!CloseTo(endX, startX) || !CloseTo(endY, startY)) {
                        area += CubicArea(endX, endY, endX, endY, startX, startY, startX, startY);
                        endX = startX; endY = startY;
                    }
                    break;
                default: break;
            }
        }
    done:
        return area >= 0 ? Path::Direction::Clockwise : Path::Direction::CounterClockwise;
    }

    std::vector<Path> Divide(const Path& path) {
        std::vector<Path> out;
        Path current;
        bool first = true, empty = true;
        PathIterator it(path);
        for (PathSegment s = it.Next(); s.type != PathSegment::Type::Done; s = it.Next()) {
            const auto& p = s.points;
            switch (s.type) {
                case PathSegment::Type::Move:
                    if (!first && !empty) { current.fillType = path.fillType; out.push_back(std::move(current)); current = Path(); }
                    first = false;
                    empty = true;
                    current.MoveTo(p[0].x, p[0].y);
                    break;
                case PathSegment::Type::Line: current.LineTo(p[1].x, p[1].y); empty = false; break;
                case PathSegment::Type::Quadratic: current.QuadraticTo(p[1].x, p[1].y, p[2].x, p[2].y); empty = false; break;
                case PathSegment::Type::Cubic: current.CubicTo(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y); empty = false; break;
                case PathSegment::Type::Close: current.Close(); break;
                default: break;
            }
        }
        if (!first && !empty) { current.fillType = path.fillType; out.push_back(std::move(current)); }
        return out;
    }

    Path Reverse(const Path& path) {
        std::vector<PathSegment> segs;
        PathIterator it(path);
        for (PathSegment s = it.Next(); s.type != PathSegment::Type::Done; s = it.Next()) segs.push_back(std::move(s));
        Path out;
        out.fillType = path.fillType;
        // Contour by contour, from the last segment back to its move.
        std::size_t end = segs.size();
        while (end > 0) {
            std::size_t start = end;
            while (start > 0 && segs[start - 1].type != PathSegment::Type::Move) --start;
            if (start == 0) break;   // segments before any move: none in practice
            const std::size_t move = start - 1;
            bool closed = false;
            std::size_t last = end;
            if (last > start && segs[last - 1].type == PathSegment::Type::Close) { closed = true; --last; }
            const Offset from = last > start ? segs[last - 1].points.back() : segs[move].points[0];
            out.MoveTo(from.x, from.y);
            for (std::size_t i = last; i > start; --i) {
                const auto& p = segs[i - 1].points;
                switch (segs[i - 1].type) {
                    case PathSegment::Type::Line: out.LineTo(p[0].x, p[0].y); break;
                    case PathSegment::Type::Quadratic: out.QuadraticTo(p[1].x, p[1].y, p[0].x, p[0].y); break;
                    case PathSegment::Type::Cubic: out.CubicTo(p[2].x, p[2].y, p[1].x, p[1].y, p[0].x, p[0].y); break;
                    default: break;
                }
            }
            if (closed) out.Close();
            end = move;
        }
        return out;
    }

    void AddOutline(Path& path, const Outline& outline) {
        if (const auto* r = std::get_if<ui::Rect>(&outline.value)) path.AddRect(*r);
        else if (const auto* rr = std::get_if<RoundRect>(&outline.value)) path.AddRoundRect(*rr);
        else path.AddPath(std::get<Path>(outline.value));
    }

    void AddSvg(Path& path, std::string_view pathData) {
        Path parsed;
        vector::ToPath(vector::ParsePathString(pathData), parsed);
        path.AddPath(parsed);
    }

    namespace {
        // Kotlin's Float.toString for ordinary values: the shortest digits, always with a point.
        std::string Num(float v) {
            char buf[32];
            auto r = std::to_chars(buf, buf + sizeof(buf), v);
            std::string s(buf, r.ptr);
            if (s.find_first_of(".en") == std::string::npos) s += ".0";
            return s;
        }
        const char* Command(PathSegment::Type t) {
            switch (t) {
                case PathSegment::Type::Move: return "M";
                case PathSegment::Type::Line: return "L";
                case PathSegment::Type::Quadratic: return "Q";
                case PathSegment::Type::Cubic: return "C";
                case PathSegment::Type::Close: return "Z";
                default: return "";
            }
        }
    }

    std::string ToSvg(const Path& path, bool asDocument) {
        std::string out;
        const ui::Rect b = path.Bounds();
        if (asDocument)
            out += "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"" + Num(b.left) + " " + Num(b.top) + " " + Num(b.Width()) + " " + Num(b.Height()) + "\">\n";
        PathIterator it(path);
        if (it.HasNext()) {
            if (asDocument) out += path.fillType == FillType::EvenOdd ? "  <path fill-rule=\"evenodd\" d=\"" : "  <path d=\"";
            PathSegment::Type lastType = PathSegment::Type::Done;
            while (it.HasNext()) {
                const PathSegment s = it.Next();
                const auto& p = s.points;
                out += s.type != lastType ? Command(s.type) : " ";
                switch (s.type) {
                    case PathSegment::Type::Move: out += Num(p[0].x) + " " + Num(p[0].y); break;
                    case PathSegment::Type::Line: out += Num(p[1].x) + " " + Num(p[1].y); break;
                    case PathSegment::Type::Quadratic: out += Num(p[1].x) + " " + Num(p[1].y) + " " + Num(p[2].x) + " " + Num(p[2].y); break;
                    case PathSegment::Type::Cubic:
                        out += Num(p[1].x) + " " + Num(p[1].y) + " " + Num(p[2].x) + " " + Num(p[2].y) + " " + Num(p[3].x) + " " + Num(p[3].y);
                        break;
                    default: break;
                }
                lastType = s.type;
            }
            if (asDocument) out += "\"/>\n";
        }
        if (asDocument) out += "</svg>\n";
        return out;
    }

    void PathHitTester::UpdatePath(const Path& path, float tolerance) {
        m_Edges.clear();
        m_Empty = path.IsEmpty();
        m_EvenOdd = path.fillType == FillType::EvenOdd;
        m_Bounds = path.Bounds();
        // A fill closes every contour.
        for (const Polyline& line : path.Flatten({}, std::max(tolerance, 0.01f))) {
            const auto& pts = line.points;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                const Offset a = pts[i], b = pts[(i + 1) % pts.size()];
                if (a.y != b.y) m_Edges.push_back({ a, b });
            }
        }
        std::sort(m_Edges.begin(), m_Edges.end(), [](const Edge& x, const Edge& y) { return std::min(x.a.y, x.b.y) < std::min(y.a.y, y.b.y); });
    }

    bool PathHitTester::Contains(Offset p) const {
        if (m_Empty || p.x < m_Bounds.left || p.x >= m_Bounds.right || p.y < m_Bounds.top || p.y >= m_Bounds.bottom) return false;
        int winding = 0;
        for (const Edge& e : m_Edges) {
            const float top = std::min(e.a.y, e.b.y), bottom = std::max(e.a.y, e.b.y);
            if (top > p.y) break;   // sorted by top: no later edge spans y
            if (p.y >= bottom) continue;
            const float t = (p.y - e.a.y) / (e.b.y - e.a.y);
            const float x = e.a.x + t * (e.b.x - e.a.x);
            if (x > p.x) winding += e.b.y > e.a.y ? 1 : -1;
        }
        return m_EvenOdd ? (winding & 1) != 0 : winding != 0;
    }

}
