#include "kiln/foundation/MeshGradient.h"

#include "kiln/graphics/Vertices.h"
#include "kiln/ui/ColorSpace.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace kiln::foundation {

    namespace {
        constexpr int kMinSubdivision = 4, kMaxSubdivision = 64;
        constexpr float kTargetPxPerSegment = 8;

        float Dist(Offset a, Offset b) { return std::hypot(b.x - a.x, b.y - a.y); }
        Offset Normalized(Offset v) {
            const float d = std::hypot(v.x, v.y);
            return d == 0 ? Offset{} : Offset{ v.x / d, v.y / d };
        }
    }

    MeshGradientScope::MeshGradientScope(int rows, int columns) : m_Rows(std::max(1, rows)), m_Columns(std::max(1, columns)) { Reset(); }

    void MeshGradientScope::Reset() {
        const std::size_t n = std::size_t((m_Rows + 1) * (m_Columns + 1));
        m_Positions.assign(n, {});
        m_Colors.assign(n, Color(0x00000000));
        m_Left.assign(n, std::nullopt);
        m_Right.assign(n, std::nullopt);
        m_Top.assign(n, std::nullopt);
        m_Bottom.assign(n, std::nullopt);
    }

    void MeshGradientScope::SetVertex(int row, int column, Offset position, Color color, std::optional<Offset> left,
                                      std::optional<Offset> top, std::optional<Offset> right, std::optional<Offset> bottom) {
        if (row < 0 || row > m_Rows || column < 0 || column > m_Columns) return;
        const std::size_t i = std::size_t(row * (m_Columns + 1) + column);
        m_Positions[i] = position;
        m_Colors[i] = color;
        m_Left[i] = left;
        m_Right[i] = right;
        m_Top[i] = top;
        m_Bottom[i] = bottom;
    }

    // inferBezierControlPointsIfRequired: along the neighbours' direction, a third of the
    // distance to each neighbour.
    void MeshGradientScope::InferControlPoints() {
        const int cols = m_Columns + 1;
        for (int row = 0; row <= m_Rows; ++row)
            for (int column = 0; column <= m_Columns; ++column) {
                const std::size_t i = std::size_t(row * cols + column);
                const Offset cur = m_Positions[i];
                const Offset left = m_Positions[column > 0 ? i - 1 : i], right = m_Positions[column < m_Columns ? i + 1 : i];
                const Offset h = Normalized({ right.x - left.x, right.y - left.y });
                const Offset hv{ h.x * 0.33f, h.y * 0.33f };
                if (!m_Left[i]) { const float d = Dist(left, cur); m_Left[i] = Offset{ -hv.x * d, -hv.y * d }; }
                if (!m_Right[i]) { const float d = Dist(right, cur); m_Right[i] = Offset{ hv.x * d, hv.y * d }; }
                const Offset top = m_Positions[row > 0 ? i - std::size_t(cols) : i], bottom = m_Positions[row < m_Rows ? i + std::size_t(cols) : i];
                const Offset v = Normalized({ bottom.x - top.x, bottom.y - top.y });
                const Offset vv{ v.x * 0.33f, v.y * 0.33f };
                if (!m_Top[i]) { const float d = Dist(top, cur); m_Top[i] = Offset{ -vv.x * d, -vv.y * d }; }
                if (!m_Bottom[i]) { const float d = Dist(bottom, cur); m_Bottom[i] = Offset{ vv.x * d, vv.y * d }; }
            }
    }

    MeshGradientPainter::MeshGradientPainter(int rows, int columns, bool bicubicColor, std::function<void(MeshGradientScope&)> block)
        : m_Rows(std::max(1, rows)), m_Columns(std::max(1, columns)), m_Bicubic(bicubicColor), m_Block(std::move(block)), m_Scope(rows, columns) {}

    void MeshGradientPainter::Draw(DrawScope& scope, ui::Size size, float alpha, const gfx::ColorFilter*) {
        MeshGradientScope& s = m_Scope;
        s.Reset();
        if (m_Block) m_Block(s);
        s.InferControlPoints();
        const int rows = m_Rows, columns = m_Columns, cols = columns + 1;
        auto at = [&](int r, int c) { return std::size_t(r * cols + c); };
        auto px = [&](Offset f) { return Offset{ f.x * size.width, f.y * size.height }; };

        // calculateMeshGradientSubdivisions: from the largest patch, about 8 px per segment.
        float maxW = 0, maxH = 0;
        for (int p = 0; p < rows * columns; ++p) {
            const int r = p / columns, c = p % columns;
            const Offset tl = px(s.m_Positions[at(r, c)]), tr = px(s.m_Positions[at(r, c + 1)]);
            const Offset bl = px(s.m_Positions[at(r + 1, c)]), br = px(s.m_Positions[at(r + 1, c + 1)]);
            maxW = std::max(maxW, (Dist(tl, tr) + Dist(bl, br)) * 0.5f);
            maxH = std::max(maxH, (Dist(tl, bl) + Dist(tr, br)) * 0.5f);
        }
        const int subU = std::clamp(int(std::ceil(maxW / kTargetPxPerSegment)), kMinSubdivision, kMaxSubdivision);
        const int subV = std::clamp(int(std::ceil(maxH / kTargetPxPerSegment)), kMinSubdivision, kMaxSubdivision);

        // The index buffer and the v bases are shared by every patch.
        std::vector<std::uint16_t> indices;
        for (int u = 0; u < subU - 1; ++u)
            for (int v = 0; v < subV - 1; ++v) {
                const auto tl = std::uint16_t(u * subV + v), bl = std::uint16_t(u * subV + v + 1);
                const auto tr = std::uint16_t((u + 1) * subV + v), br = std::uint16_t((u + 1) * subV + v + 1);
                indices.insert(indices.end(), { tl, tr, br, tl, br, bl });
            }
        std::vector<float> bern(std::size_t(subV) * 4), cr(std::size_t(subV) * 4);
        for (int vi = 0; vi < subV; ++vi) {
            const float v = float(vi) / float(subV - 1), v2 = v * v, v3 = v2 * v;
            float* b = &bern[std::size_t(vi) * 4];
            b[0] = -v3 + 3 * v2 - 3 * v + 1; b[1] = 3 * v3 - 6 * v2 + 3 * v; b[2] = -3 * v3 + 3 * v2; b[3] = v3;
            float* k = &cr[std::size_t(vi) * 4];
            k[0] = 0.5f * (-v3 + 2 * v2 - v); k[1] = 0.5f * (3 * v3 - 5 * v2 + 2); k[2] = 0.5f * (-3 * v3 + 4 * v2 + v); k[3] = 0.5f * (v3 - v2);
        }

        const float step = 1.0f / float(subU - 1), step2 = step * step, step3 = step2 * step;
        gfx::Vertices verts;
        verts.indices = indices;
        verts.positions.resize(std::size_t(subU * subV));
        verts.colors.resize(std::size_t(subU * subV));
        for (int p = 0; p < rows * columns; ++p) {
            const int pr = p / columns, pc = p % columns;
            const std::size_t corner[4] = { at(pr, pc), at(pr, pc + 1), at(pr + 1, pc), at(pr + 1, pc + 1) };
            Offset pos[4], left[4], right[4], top[4], bottom[4];
            for (int k = 0; k < 4; ++k) {
                pos[k] = px(s.m_Positions[corner[k]]);
                left[k] = px(*s.m_Left[corner[k]]);
                right[k] = px(*s.m_Right[corner[k]]);
                top[k] = px(*s.m_Top[corner[k]]);
                bottom[k] = px(*s.m_Bottom[corner[k]]);
            }
            // buildControlPointMatrix: the 4x4 Bezier control net.
            Offset cp[4][4];
            cp[0][0] = pos[0]; cp[0][3] = pos[1]; cp[3][0] = pos[2]; cp[3][3] = pos[3];
            auto add = [](Offset a, Offset b) { return Offset{ a.x + b.x, a.y + b.y }; };
            cp[0][1] = add(cp[0][0], right[0]); cp[0][2] = add(cp[0][3], left[1]);
            cp[3][1] = add(cp[3][0], right[2]); cp[3][2] = add(cp[3][3], left[3]);
            cp[1][0] = add(cp[0][0], bottom[0]); cp[2][0] = add(cp[3][0], top[2]);
            cp[1][3] = add(cp[0][3], bottom[1]); cp[2][3] = add(cp[3][3], top[3]);
            auto twist = [](Offset a, Offset b, Offset c) { return Offset{ a.x + b.x - c.x, a.y + b.y - c.y }; };
            cp[1][1] = twist(cp[0][1], cp[1][0], cp[0][0]); cp[1][2] = twist(cp[0][2], cp[1][3], cp[0][3]);
            cp[2][1] = twist(cp[2][0], cp[3][1], cp[3][0]); cp[2][2] = twist(cp[2][3], cp[3][2], cp[3][3]);

            // computeBezierSurfacePoints: forward differences along u per control row, then the
            // Bernstein basis along v.
            std::vector<Offset> rowPts(std::size_t(4 * subU));
            for (int r = 0; r < 4; ++r) {
                auto fd = [&](float c0, float c1, float c2, float c3, int axis) {
                    const float cubic = (-c0 + 3 * c1 - 3 * c2 + c3) * step3;
                    const float quad = (3 * c0 - 6 * c1 + 3 * c2) * step2;
                    float d1 = cubic + quad + (-3 * c0 + 3 * c1) * step, d2 = 6 * cubic + 2 * quad;
                    const float d3 = 6 * cubic;
                    float cur = c0;
                    for (int u = 0; u < subU; ++u) {
                        Offset& o = rowPts[std::size_t(r * subU + u)];
                        (axis == 0 ? o.x : o.y) = cur;
                        cur += d1; d1 += d2; d2 += d3;
                    }
                };
                fd(cp[r][0].x, cp[r][1].x, cp[r][2].x, cp[r][3].x, 0);
                fd(cp[r][0].y, cp[r][1].y, cp[r][2].y, cp[r][3].y, 1);
            }
            for (int vi = 0; vi < subV; ++vi) {
                const float* b = &bern[std::size_t(vi) * 4];
                for (int u = 0; u < subU; ++u) {
                    Offset o{};
                    for (int r = 0; r < 4; ++r) { const Offset q = rowPts[std::size_t(r * subU + u)]; o.x += b[r] * q.x; o.y += b[r] * q.y; }
                    verts.positions[std::size_t(u * subV + vi)] = o;
                }
            }

            // readPatchColors: the 4x4 neighbourhood, clamped at the mesh's edges.
            Color pcol[4][4];
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    pcol[r][c] = s.m_Colors[at(std::clamp(pr - 1 + r, 0, rows), std::clamp(pc - 1 + c, 0, columns))];
            if (m_Bicubic) {
                // computeCatmullRomSurfaceColors in Oklab: forward differences along u between
                // columns 1 and 2 of each row, the Catmull-Rom basis along v.
                std::vector<std::array<float, 4>> rowCol(std::size_t(4 * subU));
                for (int r = 0; r < 4; ++r) {
                    std::array<std::array<float, 4>, 4> lab;   // per column: L, a, b, alpha
                    for (int c = 0; c < 4; ++c) { const auto o = ui::ToOklab(pcol[r][c]); lab[std::size_t(c)] = { o[1], o[2], o[3], o[0] }; }
                    for (int ch = 0; ch < 4; ++ch) {
                        const float c0 = lab[0][std::size_t(ch)], c1 = lab[1][std::size_t(ch)], c2 = lab[2][std::size_t(ch)], c3 = lab[3][std::size_t(ch)];
                        const float cubic = 0.5f * (-c0 + 3 * c1 - 3 * c2 + c3) * step3;
                        const float quad = 0.5f * (2 * c0 - 5 * c1 + 4 * c2 - c3) * step2;
                        float d1 = cubic + quad + 0.5f * (-c0 + c2) * step, d2 = 6 * cubic + 2 * quad;
                        const float d3 = 6 * cubic;
                        const float lo = ch == 0 ? 0.0f : ch < 3 ? -0.5f : 0.0f, hi = ch == 0 ? 1.0f : ch < 3 ? 0.5f : 1.0f;
                        float cur = c1;
                        for (int u = 0; u < subU; ++u) {
                            rowCol[std::size_t(r * subU + u)][std::size_t(ch)] = std::clamp(cur, lo, hi);
                            cur += d1; d1 += d2; d2 += d3;
                        }
                    }
                }
                for (int u = 0; u < subU; ++u)
                    for (int vi = 0; vi < subV; ++vi) {
                        const float* k = &cr[std::size_t(vi) * 4];
                        std::array<float, 4> o{};
                        for (int r = 0; r < 4; ++r)
                            for (int ch = 0; ch < 4; ++ch) o[std::size_t(ch)] += k[r] * rowCol[std::size_t(r * subU + u)][std::size_t(ch)];
                        verts.colors[std::size_t(u * subV + vi)] = ui::FromOklab({ o[3], o[0], o[1], o[2] });
                    }
            } else {
                // computeBilinearSurfaceColors (Color lerp, in Oklab).
                for (int u = 0; u < subU; ++u) {
                    const float fu = float(u) / float(subU - 1);
                    const Color topLR = ui::Lerp(pcol[1][1], pcol[1][2], fu), bottomLR = ui::Lerp(pcol[2][1], pcol[2][2], fu);
                    for (int vi = 0; vi < subV; ++vi)
                        verts.colors[std::size_t(u * subV + vi)] = ui::Lerp(topLR, bottomLR, float(vi) / float(subV - 1));
                }
            }
            scope.DrawVertices(verts, Color(0xff000000), alpha);
        }
    }

}
