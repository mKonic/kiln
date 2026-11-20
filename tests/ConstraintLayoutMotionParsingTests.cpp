// Ported from constraintlayout-core MotionParsingTest.java (androidx). Expected values are
// upstream's.
#include "Check.h"

#include "kiln/constraintlayout/core/motion/key/MotionKeyAttributes.h"
#include "kiln/constraintlayout/core/motion/parse/KeyParser.h"

#include <algorithm>
#include <regex>
#include <set>
#include <string>
#include <vector>

using namespace kiln::constraintlayout::core;
using namespace kiln::constraintlayout::core::motion;

TEST(MotionParsing_parseKeAttributes) {
    const std::string str = "{"
                            "frame:22,\n"
                            "target:'widget1',\n"
                            "easing:'easeIn',\n"
                            "curveFit:'spline',\n"
                            "progress:0.3,\n"
                            "alpha:0.2,\n"
                            "elevation:0.7,\n"
                            "rotationZ:23,\n"
                            "rotationX:25.0,\n"
                            "rotationY:27.0,\n"
                            "pivotX:15,\n"
                            "pivotY:17,\n"
                            "pivotTarget:'32',\n"
                            "pathRotate:23,\n"
                            "scaleX:0.5,\n"
                            "scaleY:0.7,\n"
                            "translationX:5,\n"
                            "translationY:7,\n"
                            "translationZ:11,\n"
                            "}";
    MotionKeyAttributes mka;
    KeyParser::parseAttributes(str).applyDelta(mka);
    CHECK_EQ(22, mka.mFramePosition);
    JavaHashSet attrs;
    mka.getAttributeNames(attrs);
    // str.replace("\n", "").split("[,:\\{\\}]"), trailing empty strings dropped.
    std::string flat = std::regex_replace(str, std::regex("\n"), "");
    std::vector<std::string> split;
    std::string cur;
    for (char c : flat) {
        if (c == ',' || c == ':' || c == '{' || c == '}') {
            split.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    split.push_back(cur);
    while (!split.empty() && split.back().empty()) split.pop_back();
    const std::set<std::string> exclude = { "curveFit", "easing", "frame", "target", "pivotTarget" };
    std::vector<std::string> expect;
    for (std::size_t i = 1; i < split.size(); i += 2)
        if (!exclude.count(split[i])) expect.push_back(split[i]);
    std::vector<std::string> result = attrs.keys();
    std::sort(result.begin(), result.end());
    std::sort(expect.begin(), expect.end());
    CHECK(expect == result);
}
