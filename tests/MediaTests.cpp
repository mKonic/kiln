// Media: the frame release decisions (media3 VideoFrameReleaseControl with MediaCodecVideoRenderer's
// thresholds), the preview size choice, and Player's state through a fake engine.
#include "Check.h"

#include "kiln/media/Camera.h"
#include "kiln/media/MediaSession.h"
#include "kiln/media/Player.h"
#include "kiln/runtime/Task.h"

#include <map>
#include <stdexcept>

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
        EngineLoad load;
        bool repeat = false;
        PlaybackParameters parameters;
        int loads = 0;
        void Load(EngineLoad l) override {
            load = l;
            uri = l.uri;
            seek = -1;
            ++loads;
        }
        void SetPlaybackParameters(PlaybackParameters p) override { parameters = p; }
        // A report for the current load, as the decoding thread sends it.
        void Report(const PlayerState& s) { if (onState) onState(s, load.loadId); }
        void SetPlayWhenReady(bool p) override { play = p; }
        void SeekTo(std::int64_t ms) override { seek = ms; }
        void SetRepeat(bool r) override { repeat = r; }
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
        CHECK(engine->load.startPositionMs == 0 && engine->load.loadId != 0);
        p.Pause();
        CHECK(!engine->play);
        // A report from the decoding thread arrives with the dispatcher's next run.
        PlayerState s;
        s.state = PlaybackState::Ready;
        s.durationMs = 1500;
        s.videoWidth = 640;
        engine->Report(s);
        CHECK(p.State().state == PlaybackState::Buffering);   // prepared, loading
        rt::Dispatcher::Main().RunPending();
        CHECK(p.State().state == PlaybackState::Ready && p.State().durationMs == 1500 && p.State().videoWidth == 640);
        CHECK(p.State().mediaItemCount == 1 && p.State().currentMediaItemIndex == 0);
        // After the player goes, a late report is dropped.
        engine->Report(s);
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

namespace {
    // The platform's AudioManager, faked: the next request's answer, and what was asked.
    struct FakeAudio {
        int nextResult = kAudioFocusRequestGranted;
        int requests = 0, abandons = 0, lastId = 0, lastGain = 0;
        bool lastPauseWhenDucked = false;
        std::map<int, bool> noisy;
        FakeAudio() {
            SetAudioHost({ .requestFocus = [this](int id, int gain, AudioAttributes, bool pause) {
                              ++requests; lastId = id; lastGain = gain; lastPauseWhenDucked = pause;
                              return nextResult;
                          },
                           .abandonFocus = [this](int) { ++abandons; },
                           .setNoisyReceiver = [this](int id, bool on) { noisy[id] = on; } });
        }
        ~FakeAudio() { SetAudioHost({}); }
    };
    struct Control {
        float volume = -1;
        int command = 99;   // none received
        AudioFocusManager::Control Make() {
            return { .setVolumeMultiplier = [this](float v) { volume = v; },
                     .executePlayerCommand = [this](AudioFocusManager::PlayerCommand c) { command = int(c); } };
        }
    };
    constexpr int kDoNotPlay = -1, kWait = 0, kPlay = 1;
}

// media3 AudioFocusManagerTest.
TEST(AudioFocusFollowsMedia3) {
    FakeAudio audio;
    using C = AudioFocusManager::PlayerCommand;
    {
        // Without attributes focus is not managed.
        Control control;
        AudioFocusManager m(control.Make());
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Ready) == C::PlayWhenReady && audio.requests == 0);
        // Only media and game can be handled; an alarm throws.
        bool threw = false;
        try { m.SetAudioAttributes(AudioAttributes{ .usage = AudioUsage::Alarm }); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        m.SetAudioAttributes(AudioAttributes{});
        // Idle asks nothing; buffering with playWhenReady asks for AUDIOFOCUS_GAIN.
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Idle) == C::PlayWhenReady && audio.requests == 0);
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Buffering) == C::PlayWhenReady && audio.requests == 1 && audio.lastGain == kAudioFocusGain);
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Ready) == C::PlayWhenReady && audio.requests == 1);   // held: not asked again
        // Ended still holds focus; paused with focus may play.
        CHECK(m.UpdateAudioFocus(false, PlaybackState::Ended) == C::PlayWhenReady);
        // Ducked, then gained back: volume down and up, no command.
        m.OnFocusChange(kAudioFocusLossTransientCanDuck);
        CHECK(control.volume == AudioFocusManager::kVolumeMultiplierDuck && control.command == 99);
        // Ducked, playing again: still allowed.
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Ready) == C::PlayWhenReady);
        m.OnFocusChange(kAudioFocusGain);
        CHECK(control.volume == 1.0f && control.command == kPlay);
        // A transient loss waits for the callback; paused meanwhile, it still waits.
        m.OnFocusChange(kAudioFocusLossTransient);
        CHECK(control.command == kWait && control.volume == 1.0f);
        CHECK(m.UpdateAudioFocus(false, PlaybackState::Ready) == C::WaitForCallback);
        // A permanent loss: do not play, and the focus is given up.
        const int abandons = audio.abandons;
        m.OnFocusChange(kAudioFocusLoss);
        CHECK(control.command == kDoNotPlay && audio.abandons == abandons + 1);
        CHECK(m.UpdateAudioFocus(false, PlaybackState::Ready) == C::DoNotPlay);
        // Going idle without focus abandons nothing more.
        CHECK(m.UpdateAudioFocus(false, PlaybackState::Idle) == C::PlayWhenReady && audio.abandons == abandons + 1);
    }
    {
        // A refused request: do not play.
        Control control;
        AudioFocusManager m(control.Make());
        m.SetAudioAttributes(AudioAttributes{});
        audio.nextResult = kAudioFocusRequestFailed;
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Ready) == C::DoNotPlay);
        audio.nextResult = kAudioFocusRequestGranted;
        // Ready to idle with focus abandons it; ducked and then no longer handling focus restores the volume.
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Ready) == C::PlayWhenReady);
        m.OnFocusChange(kAudioFocusLossTransientCanDuck);
        CHECK(control.volume < 1);
        m.SetAudioAttributes(std::nullopt);
        const int abandons = audio.abandons;
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Ready) == C::PlayWhenReady && control.volume == 1.0f && audio.abandons == abandons + 1);
    }
    {
        // Speech pauses instead of ducking; released, the volume is not restored.
        Control control;
        AudioFocusManager m(control.Make());
        m.SetAudioAttributes(AudioAttributes{ .usage = AudioUsage::Media, .contentType = AudioContentType::Speech });
        CHECK(m.UpdateAudioFocus(true, PlaybackState::Ready) == C::PlayWhenReady && audio.lastPauseWhenDucked);
        m.OnFocusChange(kAudioFocusLossTransientCanDuck);
        CHECK(control.command == kWait && control.volume == -1);
        m.OnFocusChange(kAudioFocusGain);
        CHECK(control.command == kPlay);
        Control other;
        AudioFocusManager d(other.Make());
        d.SetAudioAttributes(AudioAttributes{});
        d.UpdateAudioFocus(true, PlaybackState::Ready);
        d.OnFocusChange(kAudioFocusLossTransientCanDuck);
        d.Release();
        CHECK(other.volume < 1);
    }
    CHECK(FocusGainFor(AudioAttributes{ .usage = AudioUsage::AssistanceNavigationGuidance }) == kAudioFocusGainTransientMayDuck);
    CHECK(FocusGainFor(AudioAttributes{ .usage = AudioUsage::AssistanceAccessibility, .contentType = AudioContentType::Speech }) == kAudioFocusGainTransient);
    CHECK(FocusGainFor(AudioAttributes{ .usage = AudioUsage::Assistant }) == kAudioFocusGainTransientExclusive);
    CHECK(FocusGainFor(std::nullopt) == kAudioFocusNone);
}

TEST(MediaPlayerFollowsAudioFocusAndBecomingNoisy) {
    FakeAudio audio;
    FakeEngine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<FakeEngine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    {
        Player p;
        p.SetAudioAttributes({ .usage = AudioUsage::Media, .contentType = AudioContentType::Music }, true);
        p.SetHandleAudioBecomingNoisy(true);
        p.SetMediaUri("asset:///a.mp3");
        p.Play();
        // Prepared and asked to play: focus is asked for (ExoPlayer.prepare with playWhenReady).
        CHECK(audio.requests == 1 && engine->play);
        PlayerState s;
        s.state = PlaybackState::Ready;
        s.isPlaying = true;
        engine->Report(s);
        rt::Dispatcher::Main().RunPending();
        CHECK(audio.requests == 1 && p.State().isPlaying);
        const int id = audio.lastId;
        // A call: suppressed, still wanting to play.
        OnAudioFocusChange(id, kAudioFocusLossTransient);
        CHECK(!engine->play && p.State().playWhenReady && p.State().suppression == PlaybackSuppression::TransientAudioFocusLoss && !p.State().isPlaying);
        OnAudioFocusChange(id, kAudioFocusGain);
        CHECK(engine->play && p.State().suppression == PlaybackSuppression::None);
        // A notification: ducked to a fifth of the volume.
        p.SetVolume(0.5f);
        OnAudioFocusChange(id, kAudioFocusLossTransientCanDuck);
        CHECK(engine->play && engine->volume == 0.5f * AudioFocusManager::kVolumeMultiplierDuck);
        OnAudioFocusChange(id, kAudioFocusGain);
        CHECK(engine->volume == 0.5f);
        // Another app plays: paused for good.
        OnAudioFocusChange(id, kAudioFocusLoss);
        CHECK(!engine->play && !p.State().playWhenReady && p.State().playWhenReadyChangeReason == PlayWhenReadyChangeReason::AudioFocusLoss);
        p.Play();
        CHECK(engine->play && p.State().playWhenReadyChangeReason == PlayWhenReadyChangeReason::UserRequest);
        // Headphones out: paused.
        int noisyId = 0;
        for (auto [k, on] : audio.noisy) if (on) noisyId = k;
        CHECK(noisyId != 0);
        OnAudioBecomingNoisy(noisyId);
        CHECK(!engine->play && p.State().playWhenReadyChangeReason == PlayWhenReadyChangeReason::AudioBecomingNoisy);
    }
    // Gone: its receiver is unregistered.
    for (auto [k, on] : audio.noisy) CHECK(!on);
    rt::Dispatcher::Main().RunPending();
    SetPlayerEngineFactory({});
}

// media3 MediaNotificationManager's rules: shown once prepared, foreground while the user listens
// and for ten minutes after, gone when dismissed while idle; the controls' commands.
TEST(MediaSessionShowsTheNotificationAndKeepsTheServiceAsMedia3Does) {
    auto& d = rt::Dispatcher::Main();
    const bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    FakeEngine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<FakeEngine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    std::map<int, MediaSessionSnapshot> shown;
    int updates = 0, released = 0;
    SetMediaSessionHost({ .update = [&](int id, const MediaSessionSnapshot& s) { shown[id] = s; ++updates; },
                          .release = [&](int) { ++released; } });
    {
        Player p;
        int next = 0;
        MediaSession session(p, { .onSkipToNext = [&] { ++next; } });
        auto now = [&] { d.RunPending(); return shown[session.Id()]; };
        CHECK(!now().showNotification && !now().foreground && now().canSkipToNext && !now().canSkipToPrevious);
        session.SetMetadata({ .title = "Song", .artist = "Band" });
        CHECK(now().metadata.title == "Song");
        // An item, not prepared: the play button prepares it (handlePlayButtonAction).
        MediaItem song = MediaItem::FromUri("asset:///song.mp3");
        song.mediaMetadata.albumTitle = "Album";
        song.mediaMetadata.title = "From the item";
        p.SetMediaItem(song);
        CHECK(now().metadata.title == "Song" && now().metadata.albumTitle == "Album" && now().canSkipToPrevious);
        auto report = [&](PlaybackState st, std::int64_t pos = 0, std::string error = {}) {
            PlayerState s;
            s.error = std::move(error);
            s.state = st;
            s.positionMs = pos;
            s.durationMs = 60'000;
            s.isPlaying = st == PlaybackState::Ready && engine->play;
            engine->Report(s);
            d.RunPending();
        };
        // Played from the controls: prepared and listened to, in the foreground.
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::Play);
        report(PlaybackState::Ready);
        CHECK(now().showNotification && now().foreground && now().isPlaying);
        // Positions moving with time are not sent again; a seek is.
        const int before = updates;
        d.AdvanceTimeBy(std::chrono::milliseconds(2000));
        report(PlaybackState::Ready, 2000);
        CHECK(updates == before);
        report(PlaybackState::Ready, 30'000);
        CHECK(updates == before + 1 && now().positionMs == 30'000);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::SkipToNext);
        CHECK(next == 1);
        // Paused: still in the foreground for the timeout, then not; the notification stays.
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::Pause);
        report(PlaybackState::Ready, 30'000);
        CHECK(!engine->play && now().foreground && now().showNotification);
        d.AdvanceTimeBy(std::chrono::milliseconds(kForegroundServiceTimeoutMs - 1000));
        CHECK(now().foreground);
        d.AdvanceTimeBy(std::chrono::milliseconds(2000));
        CHECK(!now().foreground && now().showNotification);
        // Playing again cancels it; idle after an error keeps the notification until dismissed.
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::Play);
        CHECK(now().foreground);
        report(PlaybackState::Idle, 0, "boom");
        CHECK(now().showNotification && now().foreground && now().error == "boom");   // the timeout, as after a pause
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::NotificationDismissed);
        CHECK(!now().showNotification);
        // After the error, play prepares again; ended, then play: from the start.
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::Play);
        report(PlaybackState::Ended, 60'000);
        CHECK(p.State().state == PlaybackState::Ended);
        const int loads = engine->loads;
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::Play);
        CHECK(engine->loads == loads + 1 && engine->load.startPositionMs == 0 && engine->play);
        report(PlaybackState::Ready);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::SeekTo, 1234);
        CHECK(engine->seek == 1234);
    }
    CHECK(released == 1);
    SetMediaSessionHost({});
    SetPlayerEngineFactory({});
    d.RunPending();
    d.SetVirtualTime(wasVirtual);
}
