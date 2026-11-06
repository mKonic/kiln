#pragma once

// androidx.graphics.shapes port, part 4: features from arbitrary cubics (FeatureDetector.kt),
// orientation repair (PolygonValidation.kt), a text format for features (FeatureSerializer.kt)
// and SVG path data to features (SvgPathParser.kt), so shapes drawn elsewhere can be morphed.

#include "kiln/graphics/shapes/RoundedPolygon.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kiln::gfx::shapes {

    // detectFeatures: straight runs become edges, the rest corners (convex or concave); where
    // two cubics meet with a kink a zero-length corner marks it.
    std::vector<Feature> DetectFeatures(const std::vector<Cubic>& cubics);

    // PolygonValidator.fix: shapes are expected clockwise (y down); an anticlockwise one is
    // reversed, its corners flipping between convex and concave.
    RoundedPolygon FixOrientation(const RoundedPolygon& polygon);

    // FeatureSerializer, format V1: "V1" then per feature a tag (n edge, x convex corner,
    // o concave corner) and its cubics' points, comma separated, consecutive cubics sharing
    // their joining anchor. E.g. "V1n0,0,1,1,2,2,3,3".
    namespace FeatureSerializer {
        std::string Serialize(const std::vector<Feature>& features);
        // nullopt for malformed input (no tags, or a cubic short of numbers).
        std::optional<std::vector<Feature>> Parse(std::string_view serialized);
    }

    // SvgPathParser: path data (M L H V C S Q T A Z, absolute or relative) to cubics, and the
    // first continuous subpath to features (oriented clockwise).
    namespace SvgPathParser {
        std::vector<Cubic> ParseCubics(std::string_view svgPath);
        std::vector<Feature> ParseFeatures(std::string_view svgPath);
    }

}
