#pragma once

// material3 MaterialShapes.kt: the Material Expressive shape set as normalized RoundedPolygons
// (use foundation::ToShape for a Shape, PolygonToPath / MorphToPath for paths).

#include "kiln/graphics/shapes/RoundedPolygon.h"

namespace kiln::material3::MaterialShapes {

    using gfx::shapes::RoundedPolygon;

    const RoundedPolygon& Circle();
    const RoundedPolygon& Square();
    const RoundedPolygon& Slanted();
    const RoundedPolygon& Arch();
    const RoundedPolygon& Fan();
    const RoundedPolygon& Arrow();
    const RoundedPolygon& SemiCircle();
    const RoundedPolygon& Oval();
    const RoundedPolygon& Pill();
    const RoundedPolygon& Triangle();
    const RoundedPolygon& Diamond();
    const RoundedPolygon& ClamShell();
    const RoundedPolygon& Pentagon();
    const RoundedPolygon& Gem();
    const RoundedPolygon& Sunny();
    const RoundedPolygon& VerySunny();
    const RoundedPolygon& Cookie4Sided();
    const RoundedPolygon& Cookie6Sided();
    const RoundedPolygon& Cookie7Sided();
    const RoundedPolygon& Cookie9Sided();
    const RoundedPolygon& Cookie12Sided();
    const RoundedPolygon& Ghostish();
    const RoundedPolygon& Clover4Leaf();
    const RoundedPolygon& Clover8Leaf();
    const RoundedPolygon& Burst();
    const RoundedPolygon& SoftBurst();
    const RoundedPolygon& Boom();
    const RoundedPolygon& SoftBoom();
    const RoundedPolygon& Flower();
    const RoundedPolygon& Puffy();
    const RoundedPolygon& PuffyDiamond();
    const RoundedPolygon& PixelCircle();
    const RoundedPolygon& PixelTriangle();
    const RoundedPolygon& Bun();
    const RoundedPolygon& Heart();

}
