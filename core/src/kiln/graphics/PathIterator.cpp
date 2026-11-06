#include "kiln/graphics/PathIterator.h"

#include "kiln/graphics/Conic.h"

namespace kiln::gfx {

    PathIterator::PathIterator(const Path& path, ConicEvaluation conics, float tolerance) {
        const auto& pts = path.Points();
        std::size_t pi = 0, wi = 0;
        Offset last{}, start{};
        std::vector<Offset> quads;
        using T = PathSegment::Type;
        for (const Path::Verb v : path.Verbs()) {
            switch (v) {
                case Path::Verb::Move:
                    start = last = pts[pi++];
                    m_Segments.push_back({ T::Move, { last } });
                    break;
                case Path::Verb::Line:
                    m_Segments.push_back({ T::Line, { last, pts[pi] } });
                    last = pts[pi++];
                    break;
                case Path::Verb::Quad:
                    m_Segments.push_back({ T::Quadratic, { last, pts[pi], pts[pi + 1] } });
                    last = pts[pi + 1];
                    pi += 2;
                    break;
                case Path::Verb::Conic: {
                    const float w = path.ConicWeights()[wi++];
                    const Offset cp[3] = { last, pts[pi], pts[pi + 1] };
                    ++m_ConicCount;
                    if (conics == ConicEvaluation::AsConic) {
                        m_Segments.push_back({ T::Conic, { cp[0], cp[1], cp[2] }, w });
                    } else {
                        quads.clear();
                        const int n = ConicToQuadratics(cp, w, tolerance, quads);
                        m_QuadsFromConics += std::size_t(n);
                        for (int k = 0; k < n; ++k)
                            m_Segments.push_back({ T::Quadratic, { quads[std::size_t(2 * k)], quads[std::size_t(2 * k + 1)], quads[std::size_t(2 * k + 2)] } });
                    }
                    last = pts[pi + 1];
                    pi += 2;
                    break;
                }
                case Path::Verb::Cubic:
                    m_Segments.push_back({ T::Cubic, { last, pts[pi], pts[pi + 1], pts[pi + 2] } });
                    last = pts[pi + 2];
                    pi += 3;
                    break;
                case Path::Verb::Close:
                    m_Segments.push_back({ T::Close, {} });
                    last = start;
                    break;
            }
        }
    }

    std::size_t PathIterator::CalculateSize(bool includeConvertedConics) const {
        // Converted conics count once each unless asked to count their quadratics.
        if (includeConvertedConics || m_QuadsFromConics == 0) return m_Segments.size();
        return m_Segments.size() - m_QuadsFromConics + m_ConicCount;
    }

}
