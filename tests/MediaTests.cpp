// Media: the frame release decisions (media3 VideoFrameReleaseControl with MediaCodecVideoRenderer's
// thresholds), the preview size choice, and Player's state through a fake engine.
#include "Check.h"

#include "kiln/media/Camera.h"
#include "kiln/media/MediaSession.h"
#include "kiln/media/Player.h"
#include "kiln/runtime/Task.h"

#include <chrono>
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
        CHECK(engine->load.startPositionMs == kTimeUnset && engine->load.loadId != 0);
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
        // The shown size comes with the state; the first frame is an event, for the current load only.
        int firstFrames = 0;
        p.AddListener({ .onRenderedFirstFrame = [&] { ++firstFrames; } });
        s.videoSize = { .width = 360, .height = 640, .pixelWidthHeightRatio = 1 };
        engine->Report(s);
        engine->onRenderedFirstFrame(engine->load.loadId);
        engine->onRenderedFirstFrame(engine->load.loadId + 7);   // another load's
        rt::Dispatcher::Main().RunPending();
        CHECK(firstFrames == 1 && p.State().videoSize.width == 360 && p.State().videoSize.height == 640);
        // After the player goes, a late report is dropped.
        engine->Report(s);
    }
    rt::Dispatcher::Main().RunPending();
    SetPlayerEngineFactory({});
}

// A live stream: the item's live configuration goes to the engine, the default position is
// asked for as such (kTimeUnset), and the live state and offset come from the engine's window.
TEST(MediaPlayerLiveStateAndDefaultPosition) {
    FakeEngine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<FakeEngine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    {
        Player p;
        MediaItem item = MediaItem::FromUri("https://x/live.m3u8");
        item.liveConfiguration.targetOffsetMs = 4000;
        p.SetMediaItem(item);
        p.Prepare();
        CHECK(engine->load.startPositionMs == kTimeUnset && engine->load.liveConfiguration.targetOffsetMs == 4000);
        CHECK(!p.IsCurrentMediaItemLive() && p.CurrentLiveOffsetMs() == kTimeUnset);
        const std::int64_t nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        PlayerState s;
        s.state = PlaybackState::Ready;
        s.durationMs = 30'000;
        s.positionMs = 20'000;
        s.isCurrentMediaItemLive = s.isCurrentMediaItemDynamic = true;
        s.currentLiveConfiguration = item.liveConfiguration;
        s.windowStartTimeMs = nowMs - 26'000;   // the window started 26 s ago, 20 s in: 6 s behind
        engine->Report(s);
        rt::Dispatcher::Main().RunPending();
        CHECK(p.IsCurrentMediaItemLive() && p.IsCurrentMediaItemDynamic() && p.IsCurrentMediaItemSeekable());
        const std::int64_t offset = p.CurrentLiveOffsetMs();
        CHECK(offset >= 6'000 && offset < 7'000);
        p.SeekTo(1000);
        CHECK(engine->seek == 1000);
        p.SeekToDefaultPosition();
        CHECK(engine->seek == kTimeUnset);
        // Without the window's wall-clock start the offset is not known.
        s.windowStartTimeMs = kTimeUnset;
        engine->Report(s);
        rt::Dispatcher::Main().RunPending();
        CHECK(p.CurrentLiveOffsetMs() == kTimeUnset);
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
        CHECK(engine->loads == loads + 1 && engine->load.startPositionMs == kTimeUnset && engine->play);
        report(PlaybackState::Ready);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::SeekTo, 1234);
        CHECK(engine->seek == 1234);
        // MediaSessionLegacyStub's other transport controls; the state carries the speed.
        report(PlaybackState::Ready, 20'000);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::Rewind);
        CHECK(engine->seek == 15'000);
        report(PlaybackState::Ready, 20'000);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::FastForward);
        CHECK(engine->seek == 35'000);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::SetPlaybackSpeed, 0, 1.5f);
        CHECK(engine->parameters.speed == 1.5f && now().playbackSpeed == 1.5f && now().canSeek && now().activeQueueItemId == 0);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::SetPlaybackSpeed, 0, -1);   // ignored
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::SkipToQueueItem, 5);       // no such item
        CHECK(engine->parameters.speed == 1.5f);
    }
    CHECK(released == 1);
    SetMediaSessionHost({});
    SetPlayerEngineFactory({});
    d.RunPending();
    d.SetVirtualTime(wasVirtual);
}

// media3's media button preferences (CommandButtonTest.getCustomLayoutFromMediaButtonPreferences,
// DefaultMediaNotificationProvider.getMediaButtons / addNotificationActions), the legacy session's
// queue, rating, custom actions and play-from requests (MediaSessionLegacyStub).
TEST(MediaSessionButtonsQueueRatingsAndRequestsAsMedia3) {
    auto& d = rt::Dispatcher::Main();
    FakeEngine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<FakeEngine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    std::map<int, MediaSessionSnapshot> shown;
    SetMediaSessionHost({ .update = [&](int id, const MediaSessionSnapshot& s) { shown[id] = s; }, .release = [](int) {} });
    {
        Player p;
        std::vector<std::string> custom;
        std::vector<Rating> ratings;
        std::vector<MediaItem> requested;
        MediaSession session(p, {
            .onCustomCommand = [&](const std::string& a) { custom.push_back(a); },
            .onSetRating = [&](const Rating& r) { ratings.push_back(r); },
            .onAddMediaItems = [&](std::vector<MediaItem> items) -> std::optional<std::vector<MediaItem>> {
                requested = items;
                if (items[0].requestMetadata.searchQuery == "nothing") return std::nullopt;
                MediaItem found = MediaItem::FromUri("asset:///found.mp3");
                found.mediaId = items[0].mediaId;
                return std::vector<MediaItem>{ found };
            },
        });
        auto now = [&] { d.RunPending(); return shown[session.Id()]; };
        auto actionsOf = [](const std::vector<SessionButton>& v) {
            std::vector<std::string> out;
            for (const auto& b : v) out.push_back(b.customAction.empty() ? "cmd" + std::to_string(b.playerCommand) : b.customAction);
            return out;
        };
        using CB = CommandButton;
        // withBackSlot: the first back button that can go there, then the overflow ones.
        session.SetMediaButtonPreferences({
            { .icon = CB::Album, .customAction = "action1", .slots = { CB::kSlotOverflow, CB::kSlotBack } },
            { .icon = CB::Previous, .customAction = "action2", .slots = { CB::kSlotBack, CB::kSlotOverflow } },
            { .icon = CB::Next, .customAction = "action3", .slots = { CB::kSlotForwardSecondary } },
            { .icon = CB::Rewind, .customAction = "action4", .slots = { CB::kSlotBackSecondary, CB::kSlotOverflow } },
            { .icon = CB::Block, .customAction = "off", .enabled = false },
        });
        MediaItem a = MediaItem::FromUri("asset:///a.mp3");
        a.mediaId = "a";
        a.mediaMetadata.title = "A";
        a.mediaMetadata.artist = "Band";
        a.mediaMetadata.writer = "Writer";
        a.mediaMetadata.composer = "Composer";
        a.mediaMetadata.userRating = Rating{ .kind = Rating::Kind::Heart, .isRated = true, .liked = true };
        MediaItem b = MediaItem::FromUri("asset:///b.mp3");
        b.mediaMetadata.displayTitle = "Shown";
        b.mediaMetadata.title = "B";
        b.mediaMetadata.subtitle = "Sub";
        p.SetMediaItems({ a, b });
        p.SetPlaylistMetadata({ .title = "Mix" });
        MediaSessionSnapshot s = now();
        CHECK((actionsOf(s.customActions) == std::vector<std::string>{ "action2", "action1", "action4" }));
        CHECK(s.customBack && !s.customForward);
        // The notification: the custom back button, play, next, then the overflow; collapsed:
        // back, play, next.
        CHECK((actionsOf(s.notificationButtons) == std::vector<std::string>{ "action2", "cmd1", "cmd8", "action1", "action4" }));
        CHECK((s.compactView == std::vector<int>{ 0, 1, 2 }));
        CHECK(s.notificationButtons[0].drawable == std::string("media3_icon_previous") && s.notificationButtons[1].drawable == std::string("media3_icon_play"));
        CHECK(s.notificationButtons[1].displayName == "Play" && s.notificationButtons[1].icon == CB::Play);
        // Skip to previous is the custom button's: not a platform action.
        CHECK(!(s.actions & 16) && (s.actions & 32) && (s.actions & 128) && (s.actions & 4) && (s.actions & 1024));
        // The queue (displayTitle, else the first three texts set), its title, the rating.
        CHECK(s.queue.size() == 2);
        if (s.queue.size() != 2) return;
        CHECK(s.queue[0].mediaId == "a" && s.queue[0].title == "A" && s.queue[0].subtitle == "Band" && s.queue[0].description == "Writer");
        CHECK(s.queue[1].title == "Shown" && s.queue[1].subtitle == "Sub");
        CHECK(s.queueTitle == "Mix" && s.ratingType == 1 && s.userRating && s.userRating->liked);
        session.SetSessionExtras({ { "k", "v" } });
        CHECK(now().extras.at("k") == "v" && session.SessionExtras().size() == 1);
        session.SetSessionActivity("app://player");
        CHECK(now().sessionActivityUri == "app://player" && session.SessionActivity() == "app://player");

        // Custom actions go to the app; ratings too.
        OnMediaSessionCustomAction(session.Id(), "action1");
        OnMediaSessionSetRating(session.Id(), Rating{ .kind = Rating::Kind::Heart, .isRated = true });
        CHECK((custom == std::vector<std::string>{ "action1" }) && ratings.size() == 1 && !ratings[0].liked);

        // Player-command buttons in the layout become the predefined custom actions that run them.
        session.SetMediaButtonPreferences({
            { .icon = CB::ShuffleOff, .playerCommand = PlayerCommand::SetShuffleMode, .slots = { CB::kSlotOverflow } },
            { .icon = CB::Next, .playerCommand = PlayerCommand::SeekToNext, .compactViewIndex = 0 },
            { .icon = CB::Artist, .playerCommand = PlayerCommand::GetAudioAttributes, .slots = { CB::kSlotOverflow } },   // needs no button
        });
        s = now();
        CHECK((actionsOf(s.customActions) == std::vector<std::string>{ "androidx.media3.session.PLAYER_COMMAND_9", "androidx.media3.session.PLAYER_COMMAND_14" }));
        CHECK(s.customForward && !s.customBack && (s.actions & 16) && !(s.actions & 32));
        CHECK((actionsOf(s.notificationButtons) == std::vector<std::string>{ "cmd6", "cmd1", "androidx.media3.session.PLAYER_COMMAND_9", "androidx.media3.session.PLAYER_COMMAND_14" }));
        CHECK((s.compactView == std::vector<int>{ 2 }));   // its index only (0), then the first not given
        OnMediaSessionCustomAction(session.Id(), "androidx.media3.session.PLAYER_COMMAND_14");
        CHECK(p.ShuffleModeEnabled() && custom.size() == 1);
        // The repeat button cycles off, one, all; play / pause by the button shown.
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::PlayerCommand, int(PlayerCommand::SetRepeatMode));
        CHECK(p.GetRepeatMode() == RepeatMode::One);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::PlayerCommand, int(PlayerCommand::SetRepeatMode));
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::PlayerCommand, int(PlayerCommand::SetRepeatMode));
        CHECK(p.GetRepeatMode() == RepeatMode::Off);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::PlayerCommand, int(PlayerCommand::PlayPause));
        CHECK(p.State().playWhenReady && engine->play);
        OnMediaSessionCommand(session.Id(), MediaSessionCommand::PlayerCommand, int(PlayerCommand::PlayPause));
        CHECK(!p.State().playWhenReady);

        // Play from a media id: onAddMediaItems, set, prepare, play; refused: nothing changes.
        OnMediaSessionMediaRequest(session.Id(), "", "", "nothing", true);
        CHECK(requested.size() == 1 && p.MediaItemCount() == 2);
        OnMediaSessionMediaRequest(session.Id(), "id7", "", "", true);
        CHECK(requested[0].mediaId == "id7" && p.MediaItemCount() == 1 && p.MediaItemAt(0).mediaId == "id7");
        CHECK(p.State().playWhenReady && engine->load.uri == "asset:///found.mp3");
        OnMediaSessionMediaRequest(session.Id(), "", "https://x/y.mp3", "", false);
        CHECK(requested[0].requestMetadata.mediaUri == "https://x/y.mp3" && requested[0].uri.empty());
        CHECK(now().queue.size() == 1 && now().ratingType == 0);
    }
    SetMediaSessionHost({});
    SetPlayerEngineFactory({});
    d.RunPending();
}
