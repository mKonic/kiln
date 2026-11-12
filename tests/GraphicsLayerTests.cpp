// Retained graphics layers: a recording replays later with the layer's placement and
// transform; a layer drawn into another layer's recording is drawn by reference; released or
// invisible layers draw nothing. Marker commands (DrawScope::Record) observe the replay.
#include "Check.h"

#include "kiln/foundation/GraphicsLayer.h"

using namespace kiln;
using namespace kiln::foundation;
using kiln::foundation::layer::DrawLayer;

namespace {
    struct Marks {
        int hits = 0;
        gfx::Matrix last;
        void Mark(ui::DrawScope& s) { s.Record([this](ui::DrawScope& d) { ++hits; last = d.CurrentTransform(); }); }
    };
    ui::ContentDrawScope Headless() { return ui::ContentDrawScope(ui::Density{ 1, 1 }, ui::LayoutDirection::Ltr, ui::DrawTarget{}); }
}

TEST(GraphicsLayerReplaysItsRecordingWithItsProperties) {
    Marks m;
    auto a = std::make_shared<layer::GraphicsLayer>();
    a->Record(ui::Density{ 1, 1 }, ui::LayoutDirection::Ltr, { 100, 50 }, [&](ui::DrawScope& s) {
        s.DrawRect(ui::Color(0xffff0000u), {}, { 100, 50 });
        s.Save();
        s.Translate({ 5, 0 });
        m.Mark(s);
        s.Restore();
    });
    CHECK_EQ(int(a->DisplayList().size()), 5);   // rect, save, translate, mark, restore
    CHECK_EQ(m.hits, 0);                          // recorded, not run
    a->topLeft = { 10, 20 };
    a->scaleX = 2;
    a->pivotOffset = ui::Offset{ 0, 0 };
    auto top = Headless();
    DrawLayer(top, *a);
    CHECK_EQ(m.hits, 1);
    const ui::Offset o = m.last.Map({ 0, 0 }), x = m.last.Map({ 1, 0 });
    CHECK_EQ(o.x, 20.0f);   // topLeft 10 + the recorded 5 scaled by 2
    CHECK_EQ(o.y, 20.0f);
    CHECK_EQ(x.x - o.x, 2.0f);
    a->alpha = 0;
    DrawLayer(top, *a);
    CHECK_EQ(m.hits, 1);
}

TEST(GraphicsLayerInsideARecordingIsDrawnByReference) {
    Marks m;
    auto a = std::make_shared<layer::GraphicsLayer>(), b = std::make_shared<layer::GraphicsLayer>();
    a->Record(ui::Density{ 1, 1 }, ui::LayoutDirection::Ltr, { 10, 10 }, [&](ui::DrawScope& s) { m.Mark(s); });
    b->Record(ui::Density{ 1, 1 }, ui::LayoutDirection::Ltr, { 10, 10 }, [&](ui::DrawScope& s) { DrawLayer(s, *a); });
    CHECK_EQ(int(b->DisplayList().size()), 1);
    a->Record(ui::Density{ 1, 1 }, ui::LayoutDirection::Ltr, { 10, 10 }, [&](ui::DrawScope& s) { m.Mark(s); m.Mark(s); });
    auto top = Headless();
    DrawLayer(top, *b);
    CHECK_EQ(m.hits, 2);   // what `a` holds now
    a->Release();
    CHECK(a->IsReleased());
    DrawLayer(top, *b);
    CHECK_EQ(m.hits, 2);
}

TEST(RecordingInTheDrawnScopeCapturesItsContent) {
    // drawWithContent { layer.record { drawContent() } }: the content records instead of drawing.
    Marks m;
    auto layer = std::make_shared<layer::GraphicsLayer>();
    auto top = Headless();
    top.SetContent([&] { m.Mark(top); });
    top.Translate({ 7, 0 });
    layer->Record(top, { 20, 20 }, [](ui::ContentDrawScope& s) { s.DrawContent(); });
    CHECK_EQ(m.hits, 0);
    CHECK(!top.IsRecording());
    CHECK_EQ(top.CurrentTransform().Map({ 0, 0 }).x, 7.0f);   // the scope's state is back
    DrawLayer(top, *layer);
    CHECK_EQ(m.hits, 1);
    CHECK_EQ(m.last.Map({ 0, 0 }).x, 7.0f);   // replayed where it is drawn
}
