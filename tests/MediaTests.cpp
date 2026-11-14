// Media: the frame release decisions (media3 VideoFrameReleaseControl with MediaCodecVideoRenderer's
// thresholds), the preview size choice, and Player's state through a fake engine.
#include "Check.h"

#include "kiln/media/Camera.h"
#include "kiln/media/Player.h"
#include "kiln/runtime/Task.h"

using namespace kiln;
using namespace kiln::media;

TEST(MediaFrameReleaseFollowsMedia3) {
    auto decide = [](std::int64_t early, bool started = true, bool first = true, bool last = false, std::int64_t since = 0) {
        return DecideFrameRelease({ early, started, first, last, since });
    };
    // The first frame shows at once, playing or not, however early or late.
    CHECK(decide(500'000, false, false) == FrameRelease::Immediately);
    CHECK(decide(-500'000, true, false) == FrameRelease::Immediately);
    // Paused after it: nothing more shows.
    CHECK(decide(0, false) == FrameRelease::TryAgainLater);
    // Within 50 ms ahead: scheduled; further: later.
    CHECK(decide(kEarlySchedulingThresholdUs) == FrameRelease::Scheduled);
    CHECK(decide(kEarlySchedulingThresholdUs + 1) == FrameRelease::TryAgainLater);
    CHECK(decide(-29'999) == FrameRelease::Scheduled);
    // Over 30 ms late: dropped, unless it is the last frame, or none has shown for 100 ms.
    CHECK(decide(-30'001) == FrameRelease::Drop);
    CHECK(decide(-30'001, true, true, true) == FrameRelease::Scheduled);
    CHECK(decide(-30'001, true, true, false, 100'001) == FrameRelease::Immediately);
    CHECK(decide(-30'001, true, true, false, 100'000) == FrameRelease::Drop);
}

TEST(MediaPreviewSizeIsTheLargestWithin1080p) {
    std::vector<ui::IntSize> sizes = { { 4032, 3024 }, { 1920, 1080 }, { 1440, 1080 }, { 1280, 720 }, { 640, 480 }, { 1080, 1920 } };
    CHECK(ChoosePreviewSize(sizes) == (ui::IntSize{ 1920, 1080 }));
    CHECK(ChoosePreviewSize(sizes, 4.0f / 3.0f) == (ui::IntSize{ 1440, 1080 }));
    CHECK(ChoosePreviewSize(sizes, 9.0f / 16.0f) == (ui::IntSize{ 1080, 1920 }));
    CHECK(ChoosePreviewSize({ { 4032, 3024 } }) == (ui::IntSize{}));
    CHECK(ChoosePreviewSize({}) == (ui::IntSize{}));
}

namespace {
    struct FakeEngine : PlayerEngine {
        std::string uri;
        bool play = false;
        std::int64_t seek = -1;
        float volume = 1;
        void* window = nullptr;
        void SetMediaUri(std::string u) override { uri = std::move(u); }
        void SetPlayWhenReady(bool p) override { play = p; }
        void SeekTo(std::int64_t ms) override { seek = ms; }
        void SetRepeat(bool) override {}
        void SetVolume(float v) override { volume = v; }
        void SetSurface(ui::PlatformSurface s) override { window = s.nativeWindow; }
    };
}

TEST(MediaPlayerReachesItsEngineAndReportsOnTheMainThread) {
    {
        // No engine (the desktop): the state says so.
        SetPlayerEngineFactory({});
        Player p;
        p.SetMediaUri("asset:///a.mp4");
        CHECK(p.State().state == PlaybackState::Idle && !p.State().error.empty());
    }
    FakeEngine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<FakeEngine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    {
        Player p;
        CHECK(engine != nullptr);
        p.SetMediaUri("asset:///a.mp4");
        p.Play();
        p.SeekTo(-5);
        p.SetVolume(3);
        int window = 0;
        p.SetSurface({ &window });
        CHECK(engine->uri == "asset:///a.mp4" && engine->play && engine->seek == 0 && engine->volume == 1 && engine->window == &window);
        p.Pause();
        CHECK(!engine->play);
        // A report from the decoding thread arrives with the dispatcher's next run.
        PlayerState s;
        s.state = PlaybackState::Ready;
        s.durationMs = 1500;
        s.videoWidth = 640;
        engine->onState(s);
        CHECK(p.State().state == PlaybackState::Idle);
        rt::Dispatcher::Main().RunPending();
        CHECK(p.State() == s);
        // After the player goes, a late report is dropped.
        engine->onState(s);
    }
    rt::Dispatcher::Main().RunPending();
    SetPlayerEngineFactory({});
}

TEST(MediaPreviewTransformTurnsTheFrameUpright) {
    // A 1280x720 frame (sensor 90 degrees) drawn stretched over a 720x1280 portrait element.
    auto apply = [](const std::array<float, 9>& m, float x, float y) { return std::pair{ m[0] * x + m[1] * y + m[2], m[3] * x + m[4] * y + m[5] }; };
    const float w = 720, h = 1280;
    auto m = PreviewTransform(90, false, w, h);
    // The frame's top-left (drawn at 0,0) goes to the top-right; its bottom-left to the top-left.
    CHECK(apply(m, 0, 0) == std::pair(w, 0.0f));
    CHECK(apply(m, 0, h) == std::pair(0.0f, 0.0f));
    CHECK(apply(m, w, h) == std::pair(0.0f, h));
    // No turn: identity; half a turn: the corners swap through the centre.
    CHECK(PreviewTransform(0, false, w, h) == (std::array<float, 9>{ 1, 0, 0, 0, 1, 0, 0, 0, 1 }));
    CHECK(apply(PreviewTransform(180, false, w, h), 0, 0) == std::pair(w, h));
    // Mirrored (a front camera): flipped across the vertical centre line after the turn.
    CHECK(apply(PreviewTransform(90, true, w, h), 0, 0) == std::pair(0.0f, 0.0f));
    CHECK(apply(PreviewTransform(270, false, w, h), 0, 0) == std::pair(0.0f, h));
    CameraInfo back{ "0", LensFacing::Back, 90, {} }, front{ "1", LensFacing::Front, 270, {} };
    CHECK(PreviewRotation(back, 0) == 90 && PreviewRotation(back, 90) == 0 && PreviewRotation(back, 270) == 180);
    CHECK(PreviewRotation(front, 0) == 270 && PreviewRotation(front, 90) == 0);
}
