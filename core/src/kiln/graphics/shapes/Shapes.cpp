#include "kiln/graphics/shapes/Shapes.h"

#include <algorithm>

namespace kiln::gfx::shapes {

    RoundedPolygon Circle(int n, float radius, float cx, float cy) {
        const float theta = FloatPi / float(n);
        return RoundedPolygon::Regular(n, radius / std::cos(theta), cx, cy, CornerRounding{ radius, 0 });
    }

    RoundedPolygon Rectangle(float w, float h, CornerRounding rounding, const std::vector<CornerRounding>* perVertex, float cx, float cy) {
        const float l = cx - w / 2, t = cy - h / 2, r = cx + w / 2, b = cy + h / 2;
        return RoundedPolygon::FromVertices({ r, b, l, b, l, t, r, t }, rounding, perVertex, cx, cy);
    }

    namespace {
        std::vector<CornerRounding> Alternating(int n, CornerRounding a, CornerRounding b) {
            std::vector<CornerRounding> out;
            for (int i = 0; i < n; ++i) { out.push_back(a); out.push_back(b); }
            return out;
        }
    }

    RoundedPolygon Star(int n, float radius, float innerRadius, CornerRounding rounding, std::optional<CornerRounding> innerRounding,
                        const std::vector<CornerRounding>* perVertex, float cx, float cy) {
        std::vector<CornerRounding> pv;
        if (!perVertex && innerRounding) { pv = Alternating(n, rounding, *innerRounding); perVertex = &pv; }
        std::vector<float> v;
        for (int i = 0; i < n; ++i) {
            Point p = RadialToCartesian(radius, FloatPi / float(n) * 2 * float(i));
            v.push_back(p.x + cx); v.push_back(p.y + cy);
            p = RadialToCartesian(innerRadius, FloatPi / float(n) * float(2 * i + 1));
            v.push_back(p.x + cx); v.push_back(p.y + cy);
        }
        return RoundedPolygon::FromVertices(v, rounding, perVertex, cx, cy);
    }

    RoundedPolygon Pill(float w, float h, float smoothing, float cx, float cy) {
        const float wh = w / 2, hh = h / 2;
        return RoundedPolygon::FromVertices({ wh + cx, hh + cy, -wh + cx, hh + cy, -wh + cx, -hh + cy, wh + cx, -hh + cy },
                                            CornerRounding{ std::min(wh, hh), smoothing }, nullptr, cx, cy);
    }

    RoundedPolygon PillStar(float w, float h, int n, float innerRadius, CornerRounding rounding, std::optional<CornerRounding> innerRounding,
                            const std::vector<CornerRounding>* perVertex, float vertexSpacing, float startLocation, float cx, float cy) {
        std::vector<CornerRounding> pv;
        if (!perVertex && innerRounding) { pv = Alternating(n, rounding, *innerRounding); perVertex = &pv; }
        const float endcapRadius = std::min(w, h);
        const float vSegLen = std::max(h - w, 0.0f), hSegLen = std::max(w - h, 0.0f);
        const float vSegHalf = vSegLen / 2, hSegHalf = hSegLen / 2;
        const float circlePerimeter = TwoPi * endcapRadius * Interpolate(innerRadius, 1.0f, vertexSpacing);
        const float perimeter = 2 * hSegLen + 2 * vSegLen + circlePerimeter;
        float sections[11];
        sections[0] = 0;
        sections[1] = vSegLen / 2;
        sections[2] = sections[1] + circlePerimeter / 4;
        sections[3] = sections[2] + hSegLen;
        sections[4] = sections[3] + circlePerimeter / 4;
        sections[5] = sections[4] + vSegLen;
        sections[6] = sections[5] + circlePerimeter / 4;
        sections[7] = sections[6] + hSegLen;
        sections[8] = sections[7] + circlePerimeter / 4;
        sections[9] = sections[8] + vSegLen / 2;
        sections[10] = perimeter;
        const float tPerVertex = perimeter / float(2 * n);
        bool inner = false;
        int sec = 0;
        float secStart = 0, secEnd = sections[1];
        float t = startLocation * perimeter;
        const Point rectBR{ hSegHalf, vSegHalf }, rectBL{ -hSegHalf, vSegHalf }, rectTL{ -hSegHalf, -vSegHalf }, rectTR{ hSegHalf, -vSegHalf };
        std::vector<float> v;
        for (int i = 0; i < n * 2; ++i) {
            const float bounded = std::fmod(t, perimeter);
            if (bounded < secStart) sec = 0;
            while (bounded >= sections[(sec + 1) % 11]) {
                sec = (sec + 1) % 11;
                secStart = sections[sec];
                secEnd = sections[(sec + 1) % 11];
            }
            const float prop = (bounded - secStart) / (secEnd - secStart);
            const float r = inner ? endcapRadius * innerRadius : endcapRadius;
            Point p;
            switch (sec) {
                case 0: p = { r, prop * vSegHalf }; break;
                case 1: p = RadialToCartesian(r, prop * FloatPi / 2) + rectBR; break;
                case 2: p = { hSegHalf - prop * hSegLen, r }; break;
                case 3: p = RadialToCartesian(r, FloatPi / 2 + prop * FloatPi / 2) + rectBL; break;
                case 4: p = { -r, vSegHalf - prop * vSegLen }; break;
                case 5: p = RadialToCartesian(r, FloatPi + prop * FloatPi / 2) + rectTL; break;
                case 6: p = { -hSegHalf + prop * hSegLen, -r }; break;
                case 7: p = RadialToCartesian(r, FloatPi * 1.5f + prop * FloatPi / 2) + rectTR; break;
                default: p = { r, -vSegHalf + prop * vSegHalf }; break;
            }
            v.push_back(p.x + cx);
            v.push_back(p.y + cy);
            t += tPerVertex;
            inner = !inner;
        }
        return RoundedPolygon::FromVertices(v, rounding, perVertex, cx, cy);
    }

}
