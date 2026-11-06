#include "kiln/graphics/shapes/FeatureTools.h"

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>

namespace kiln::gfx::shapes {

    // --- FeatureDetector ---------------------------------------------------------------------

    namespace {
        bool StraightIsh(const Cubic& c) {
            return !c.ZeroLength() &&
                   CollinearIsh(c.Anchor0X(), c.Anchor0Y(), c.Anchor1X(), c.Anchor1Y(), c.Control0X(), c.Control0Y(), RelaxedDistanceEpsilon) &&
                   CollinearIsh(c.Anchor0X(), c.Anchor0Y(), c.Anchor1X(), c.Anchor1Y(), c.Control1X(), c.Control1Y(), RelaxedDistanceEpsilon);
        }
        bool SmoothesIntoIsh(const Cubic& c, const Cubic& next) {
            return CollinearIsh(c.Control1X(), c.Control1Y(), next.Control0X(), next.Control0Y(), c.Anchor1X(), c.Anchor1Y(), RelaxedDistanceEpsilon);
        }
        bool AlignsIshWith(const Cubic& c, const Cubic& next) {
            return (StraightIsh(c) && StraightIsh(next) && SmoothesIntoIsh(c, next)) || c.ZeroLength() || next.ZeroLength();
        }
        Cubic Extend(const Cubic& a, const Cubic& b) {
            if (a.ZeroLength()) return { a.Anchor0X(), a.Anchor0Y(), b.Control0X(), b.Control0Y(), b.Control1X(), b.Control1Y(), b.Anchor1X(), b.Anchor1Y() };
            return { a.Anchor0X(), a.Anchor0Y(), a.Control0X(), a.Control0Y(), a.Control1X(), a.Control1Y(), b.Anchor1X(), b.Anchor1Y() };
        }
        Feature AsFeature(const Cubic& c, const Cubic& next) {
            return StraightIsh(c) ? Feature::Edge({ c }) : Feature::Corner({ c }, c.ConvexTo(next));
        }
    }

    std::vector<Feature> DetectFeatures(const std::vector<Cubic>& cubics) {
        std::vector<Feature> out;
        if (cubics.empty()) return out;
        Cubic current = cubics.front();
        for (std::size_t i = 0; i < cubics.size(); ++i) {
            const Cubic& next = cubics[(i + 1) % cubics.size()];
            if (i + 1 < cubics.size() && AlignsIshWith(current, next)) {
                current = Extend(current, next);
                continue;
            }
            out.push_back(AsFeature(current, next));
            // A kink: a zero-length corner where the two meet.
            if (!SmoothesIntoIsh(current, next)) out.push_back(AsFeature(Cubic::Empty(current.Anchor1X(), current.Anchor1Y()), next));
            current = next;
        }
        return out;
    }

    // --- PolygonValidation -------------------------------------------------------------------

    RoundedPolygon FixOrientation(const RoundedPolygon& polygon) {
        float signedArea = 0;
        for (const Cubic& c : polygon.Cubics()) signedArea += (c.Anchor1X() - c.Anchor0X()) * (c.Anchor1Y() + c.Anchor0Y());
        if (signedArea < 0) return polygon;   // clockwise already
        const auto& f = polygon.Features();
        std::vector<Feature> reversed;
        if (!f.empty()) {
            reversed.push_back(f.front().Reversed());
            for (std::size_t i = f.size() - 1; i >= 1; --i) reversed.push_back(f[i].Reversed());
        }
        return RoundedPolygon(std::move(reversed), polygon.Center());
    }

    // --- FeatureSerializer -------------------------------------------------------------------

    namespace FeatureSerializer {
        namespace {
            // Shortest round-trip form without an exponent (a letter would read as a tag).
            std::string Number(float v) {
                char buf[64];
                const auto r = std::to_chars(buf, buf + sizeof(buf), v, std::chars_format::fixed);
                return std::string(buf, r.ptr);
            }
            bool IsTag(std::string_view s, std::size_t i) {
                const char c = s[i];
                if (!std::isalpha(static_cast<unsigned char>(c))) return false;
                // An exponent inside a number is not a tag.
                if ((c == 'e' || c == 'E') && i > 0 && (std::isdigit(static_cast<unsigned char>(s[i - 1])) || s[i - 1] == '.')) return false;
                return true;
            }
            std::optional<std::vector<Cubic>> Cubics(std::string_view s) {
                std::vector<float> n;
                std::size_t start = 0;
                for (std::size_t i = 0; i <= s.size(); ++i) {
                    if (i < s.size() && s[i] != ',') continue;
                    const std::string token(s.substr(start, i - start));
                    char* end = nullptr;
                    const float v = std::strtof(token.c_str(), &end);
                    if (token.empty() || end == token.c_str()) return std::nullopt;
                    n.push_back(v);
                    start = i + 1;
                }
                // k cubics take 6k + 2 numbers (neighbours share their joining anchor).
                if (n.size() < 8 || (n.size() - 2) % 6 != 0) return std::nullopt;
                std::vector<Cubic> out;
                for (std::size_t at = 0; at + 8 <= n.size(); at += 6)
                    out.push_back({ n[at], n[at + 1], n[at + 2], n[at + 3], n[at + 4], n[at + 5], n[at + 6], n[at + 7] });
                return out;
            }
        }

        std::string Serialize(const std::vector<Feature>& features) {
            std::string out = "V1";
            for (const Feature& f : features) {
                if (f.cubics.empty()) continue;
                out += f.IsConvexCorner() ? 'x' : f.IsConcaveCorner() ? 'o' : 'n';
                for (const Cubic& c : f.cubics)
                    for (int i = 0; i < 6; ++i) { out += Number(c.points[std::size_t(i)]); out += ','; }
                out += Number(f.cubics.back().Anchor1X());
                out += ',';
                out += Number(f.cubics.back().Anchor1Y());
            }
            return out;
        }

        std::optional<std::vector<Feature>> Parse(std::string_view s) {
            std::size_t at = 0;
            // An optional version ("V1"); other versions parse as V1.
            while (at < s.size() && std::isspace(static_cast<unsigned char>(s[at]))) ++at;
            if (at < s.size() && s[at] == 'V') {
                ++at;
                while (at < s.size() && std::isdigit(static_cast<unsigned char>(s[at]))) ++at;
            }
            std::vector<std::size_t> tags;
            for (std::size_t i = at; i < s.size(); ++i) if (IsTag(s, i)) tags.push_back(i);
            if (tags.empty()) return std::nullopt;
            std::vector<Feature> out;
            for (std::size_t k = 0; k < tags.size(); ++k) {
                const std::size_t begin = tags[k] + 1, end = k + 1 < tags.size() ? tags[k + 1] : s.size();
                auto cubics = Cubics(s.substr(begin, end - begin));
                if (!cubics) return std::nullopt;
                switch (s[tags[k]]) {
                    case 'x': out.push_back(Feature::Corner(std::move(*cubics), true)); break;
                    case 'o': out.push_back(Feature::Corner(std::move(*cubics), false)); break;
                    default: out.push_back(Feature::Edge(std::move(*cubics))); break;   // n, or unknown
                }
            }
            return out;
        }
    }

    // --- SvgPathParser -----------------------------------------------------------------------

    namespace SvgPathParser {
        namespace {
            struct Command { char letter; bool relative; std::vector<float> params; };

            int ParamCount(char letter) {
                switch (letter) {
                    case 'm': case 'l': case 't': return 2;
                    case 'h': case 'v': return 1;
                    case 'c': return 6;
                    case 's': case 'q': return 4;
                    case 'a': return 7;
                    default: return 0;
                }
            }

            // The SVG path grammar's numbers: sign, digits, fraction, exponent; arc flags are a
            // single 0 or 1 and need no separator.
            std::vector<Command> Tokenize(std::string_view s) {
                std::vector<Command> out;
                std::size_t i = 0;
                auto skip = [&] { while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i; };
                while (true) {
                    skip();
                    if (i >= s.size()) break;
                    const char c = s[i];
                    if (std::isalpha(static_cast<unsigned char>(c))) {
                        out.push_back({ char(std::tolower(static_cast<unsigned char>(c))), bool(std::islower(static_cast<unsigned char>(c))), {} });
                        ++i;
                        continue;
                    }
                    if (out.empty()) { ++i; continue; }   // numbers before any command
                    Command& cmd = out.back();
                    const std::size_t index = cmd.params.size();
                    if (cmd.letter == 'a' && (index % 7 == 3 || index % 7 == 4) && (c == '0' || c == '1')) {
                        cmd.params.push_back(c == '1' ? 1.0f : 0.0f);
                        ++i;
                        continue;
                    }
                    std::size_t j = i;
                    if (j < s.size() && (s[j] == '+' || s[j] == '-')) ++j;
                    while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
                    if (j < s.size() && s[j] == '.') { ++j; while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j; }
                    if (j < s.size() && (s[j] == 'e' || s[j] == 'E')) {
                        std::size_t k = j + 1;
                        if (k < s.size() && (s[k] == '+' || s[k] == '-')) ++k;
                        if (k < s.size() && std::isdigit(static_cast<unsigned char>(s[k]))) {
                            j = k;
                            while (j < s.size() && std::isdigit(static_cast<unsigned char>(s[j]))) ++j;
                        }
                    }
                    if (j == i) { ++i; continue; }   // not a number: skip it
                    const std::string token(s.substr(i, j - i));
                    cmd.params.push_back(std::strtof(token.c_str(), nullptr));
                    i = j;
                }
                return out;
            }

            // ArcConverter (PathParser's arc to Béziers).
            void ArcToBezier(float cx, float cy, float rx, float ry, float e1x, float e1y, float theta, float start, float sweep,
                             std::vector<Cubic>& out) {
                const int segments = int(std::ceil(std::abs(sweep * 4 / FloatPi)));
                float eta1 = start;
                const float cosTheta = std::cos(theta), sinTheta = std::sin(theta);
                float ep1x = -rx * cosTheta * std::sin(eta1) - ry * sinTheta * std::cos(eta1);
                float ep1y = -rx * sinTheta * std::sin(eta1) + ry * cosTheta * std::cos(eta1);
                const float perSegment = sweep / float(segments);
                for (int i = 0; i < segments; ++i) {
                    const float eta2 = eta1 + perSegment;
                    const float s2 = std::sin(eta2), c2 = std::cos(eta2);
                    const float e2x = cx + rx * cosTheta * c2 - ry * sinTheta * s2;
                    const float e2y = cy + rx * sinTheta * c2 + ry * cosTheta * s2;
                    const float ep2x = -rx * cosTheta * s2 - ry * sinTheta * c2;
                    const float ep2y = -rx * sinTheta * s2 + ry * cosTheta * c2;
                    const float tanDiff2 = std::tan((eta2 - eta1) / 2);
                    const float alpha = std::sin(eta2 - eta1) * (std::sqrt(4 + 3 * tanDiff2 * tanDiff2) - 1) / 3;
                    out.push_back({ e1x, e1y, e1x + alpha * ep1x, e1y + alpha * ep1y, e2x - alpha * ep2x, e2y - alpha * ep2y, e2x, e2y });
                    eta1 = eta2;
                    e1x = e2x;
                    e1y = e2y;
                    ep1x = ep2x;
                    ep1y = ep2y;
                }
            }

            void ArcToCubics(float x0, float y0, float x1, float y1, float a, float b, float theta, bool moreThanHalf, bool positive,
                             std::vector<Cubic>& out) {
                const double thetaD = double(theta) / 180 * 3.14159265358979323846;
                const double cosT = std::cos(thetaD), sinT = std::sin(thetaD);
                // Into unit-circle space.
                const double x0p = (x0 * cosT + y0 * sinT) / a, y0p = (-x0 * sinT + y0 * cosT) / b;
                const double x1p = (x1 * cosT + y1 * sinT) / a, y1p = (-x1 * sinT + y1 * cosT) / b;
                const double dx = x0p - x1p, dy = y0p - y1p;
                const double xm = (x0p + x1p) / 2, ym = (y0p + y1p) / 2;
                const double dsq = dx * dx + dy * dy;
                if (dsq == 0.0) return;   // coincident
                const double disc = 1.0 / dsq - 1.0 / 4.0;
                if (disc < 0.0) {
                    // Radii too small: scale them up until the ends fit.
                    const float adjust = float(std::sqrt(dsq) / 1.99999);
                    ArcToCubics(x0, y0, x1, y1, a * adjust, b * adjust, theta, moreThanHalf, positive, out);
                    return;
                }
                const double s = std::sqrt(disc), sdx = s * dx, sdy = s * dy;
                double cx, cy;
                if (moreThanHalf == positive) { cx = xm - sdy; cy = ym + sdx; }
                else { cx = xm + sdy; cy = ym - sdx; }
                const double eta0 = std::atan2(y0p - cy, x0p - cx), eta1 = std::atan2(y1p - cy, x1p - cx);
                double sweep = eta1 - eta0;
                if (positive != (sweep >= 0)) sweep += sweep > 0 ? -2 * 3.14159265358979323846 : 2 * 3.14159265358979323846;
                cx *= a;
                cy *= b;
                const double tcx = cx;
                cx = cx * cosT - cy * sinT;
                cy = tcx * sinT + cy * cosT;
                ArcToBezier(float(cx), float(cy), a, b, x0, y0, float(thetaD), float(eta0), float(sweep), out);
            }

            struct Subpath {
                Point start, position;
                std::vector<Cubic> cubics;
                char previous = 0;
                Point lastQuadControl;
                void Line(Point to) { cubics.push_back(Cubic::StraightLine(position.x, position.y, to.x, to.y)); position = to; }
                void Curve(Point c0, Point c1, Point a1) { cubics.push_back({ position, c0, c1, a1 }); position = a1; }
                // A quadratic becomes a cubic with both controls at its control point, as the
                // shapes library does (Material symbol morphs rely on that exact curve).
                void Quad(Point q, Point a1) {
                    lastQuadControl = q;
                    Curve(q, q, a1);
                }
                void Run(char letter, bool relative, const float* p) {
                    const Point base = relative ? position : Point{};
                    auto xy = [&](int i) { return base + Point{ p[i], p[i + 1] }; };
                    switch (letter) {
                        case 'l': Line(xy(0)); break;
                        case 'h': Line({ base.x + p[0], position.y }); break;
                        case 'v': Line({ position.x, base.y + p[0] }); break;
                        case 'c': Curve(xy(0), xy(2), xy(4)); break;
                        case 's': {
                            const bool after = previous == 'c' || previous == 's';
                            const Point c0 = after && !cubics.empty()
                                ? position + (position - Point{ cubics.back().Control1X(), cubics.back().Control1Y() }) : position;
                            Curve(c0, xy(0), xy(2));
                            break;
                        }
                        case 'q': Quad(xy(0), xy(2)); break;
                        case 't': {
                            const bool after = previous == 'q' || previous == 't';
                            Quad(after ? position + (position - lastQuadControl) : position, xy(0));
                            break;
                        }
                        case 'a': {
                            const Point to = xy(5);
                            ArcToCubics(position.x, position.y, to.x, to.y, p[0], p[1], p[2], p[3] != 0, p[4] != 0, cubics);
                            position = to;
                            break;
                        }
                        default: return;
                    }
                    previous = letter;
                }
            };
        }

        std::vector<Cubic> ParseCubics(std::string_view svgPath) {
            std::vector<Cubic> out;
            Point current{};
            std::optional<Subpath> sub;
            auto flush = [&] { if (sub) { out.insert(out.end(), sub->cubics.begin(), sub->cubics.end()); sub.reset(); } };
            for (const Command& cmd : Tokenize(svgPath)) {
                if (cmd.letter == 'm') {
                    flush();
                    if (cmd.params.size() < 2) continue;
                    current = (cmd.relative ? current : Point{}) + Point{ cmd.params[0], cmd.params[1] };
                    sub.emplace();
                    sub->start = sub->position = current;
                    // Further pairs are implicit line-tos.
                    for (std::size_t i = 2; i + 1 < cmd.params.size(); i += 2) sub->Run('l', cmd.relative, &cmd.params[i]);
                    continue;
                }
                if (!sub) { sub.emplace(); sub->start = sub->position = current; }
                if (cmd.letter == 'z') {
                    sub->Line(sub->start);
                    sub->previous = 'z';
                    continue;
                }
                const int n = ParamCount(cmd.letter);
                if (n == 0) continue;   // unknown command
                for (std::size_t i = 0; i + std::size_t(n) <= cmd.params.size(); i += std::size_t(n)) sub->Run(cmd.letter, cmd.relative, &cmd.params[i]);
                current = sub->position;
            }
            flush();
            return out;
        }

        std::vector<Feature> ParseFeatures(std::string_view svgPath) {
            const std::vector<Cubic> cubics = ParseCubics(svgPath);
            // The first continuous run of cubics (the first subpath).
            std::size_t count = cubics.size();
            for (std::size_t i = 0; i + 1 < cubics.size(); ++i) {
                const Cubic& a = cubics[i];
                const Cubic& b = cubics[i + 1];
                if (!(std::abs(b.Anchor0X() - a.Anchor1X()) < DistanceEpsilon && std::abs(b.Anchor0Y() - a.Anchor1Y()) < DistanceEpsilon)) {
                    count = i + 1;
                    break;
                }
            }
            if (count == 0) return {};
            const std::vector<Cubic> first(cubics.begin(), cubics.begin() + std::ptrdiff_t(count));
            return FixOrientation(RoundedPolygon::FromFeatures(DetectFeatures(first))).Features();
        }
    }

}
