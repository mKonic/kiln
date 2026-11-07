#include "Check.h"

#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/ui/LayoutNode.h"

using namespace kiln;
using namespace kiln::foundation;

TEST(ScrollCaptureScrollsEachStripToTheMiddleAndBack) {
    ui::Owner owner;
    owner.SetDensity({ 1, 1 });
    rt::Composition comp{ owner.Root(), rt::Dispatcher::Main() };
    ScrollState* scroll = nullptr;
    comp.SetContent([&] {
        auto& s = RememberScrollState(0);
        scroll = &s;
        // A 200 px viewport over 1000 px of content, below a 50 px header.
        Column(Modifier{}, [&] {
            Box(SizeMod(Modifier{}, Dp(100), Dp(50)));
            Box(VerticalScroll(SizeMod(Modifier{}, Dp(100), Dp(200)), s), Alignment::TopStart(), false,
                [] { Box(SizeMod(Modifier{}, Dp(100), Dp(1000))); });
        });
    });
    auto pump = [&] { rt::Dispatcher::Main().RunPending(); comp.Recompose(); owner.MeasureAndLayout({ 300, 400 }); };
    pump();
    pump();
    auto& capture = owner.ScrollCaptureManager();
    const auto candidate = capture.Search();
    CHECK(candidate.has_value());
    if (!candidate) return;
    CHECK(candidate->viewportBounds.top == 50 && candidate->viewportBounds.bottom == 250);
    capture.Start(*candidate);
    CHECK(owner.ScrollCaptureInProgress());
    // The strip 300..400 below the start: centred, the content scrolls by 250.
    std::optional<ui::ScrollCapture::Image> image;
    auto job = rt::Launch(rt::Dispatcher::Main(), [](ui::ScrollCapture* c, std::optional<ui::ScrollCapture::Image>* out) -> rt::Task<> {
        *out = co_await c->ImageRequest({ 0, 300, 100, 400 });
    }(&capture, &image));
    rt::Dispatcher::Main().RunFrame(16'000'000);
    pump();
    CHECK(image.has_value());
    CHECK_EQ(scroll->Value(), 250);
    if (image) CHECK(image->rect.top == 300 && image->rect.bottom == 400 && image->height == 100);
    auto end = rt::Launch(rt::Dispatcher::Main(), [](ui::ScrollCapture* c) -> rt::Task<> { co_await c->End(); }(&capture));
    pump();
    CHECK_EQ(scroll->Value(), 0);
    CHECK(!owner.ScrollCaptureInProgress());
}
