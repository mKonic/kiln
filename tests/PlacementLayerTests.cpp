// Placeable.placeWithLayer: a parent places a child inside a graphics layer; the layer moves
// what the child draws and where it is (bounds, hit testing), and placing it again without a
// layer removes it.
#include "Check.h"


#include "kiln/test/ComposeUiTest.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Semantics.h"

using namespace kiln;
using namespace kiln::foundation;
using namespace kiln::test;
using ui::Modifier;

namespace {
    // Places its one child at (10, 0), in a layer while `layered` holds.
    void LayeredBox(rt::State<bool>& layered, kiln::Function<void()> content) {
        auto policy = std::make_shared<ui::LambdaMeasurePolicy>(
            [&layered](ui::MeasureScope& s, std::span<ui::Measurable* const> children, const ui::Constraints& c) {
                ui::Placeable& p = children[0]->Measure(ui::Constraints::Fixed(40, 20));
                const bool withLayer = layered.Get();
                return s.Layout(c.maxWidth, c.maxHeight, [&p, withLayer](ui::PlacementScope& place) {
                    if (!withLayer) {
                        place.Place(p, 10, 0);
                        return;
                    }
                    GraphicsLayerParams layer;
                    layer.translationX = 30;
                    layer.scaleX = 2;
                    layer.originX = 0;
                    layer.originY = 0;
                    place.PlaceWithLayer(p, 10, 0, PlacementLayer(layer));
                });
            });
        Layout(Modifier{}, policy, std::move(content));
    }
}

TEST(PlacementLayerTransformsTheChildAndGoesWhenPlacedWithoutIt) {
    ComposeUiTest t;
    rt::State<bool> layered{ true };
    t.SetContent([&] { LayeredBox(layered, [] { Box(TestTag(Modifier{}, "child")); }); });
    ui::Rect b = t.OnNodeWithTag("child").GetBoundsInRoot();
    CHECK_EQ(b.left, 40.0f);              // placed at 10, translated by 30
    CHECK_EQ(b.right - b.left, 80.0f);    // 40 wide, scaled by 2 about its left edge
    CHECK_EQ(b.bottom - b.top, 20.0f);
    layered.Set(false);
    t.WaitForIdle();
    b = t.OnNodeWithTag("child").GetBoundsInRoot();
    CHECK_EQ(b.left, 10.0f);
    CHECK_EQ(b.right - b.left, 40.0f);
}
