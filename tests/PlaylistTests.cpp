// media3's playlist: DefaultShuffleOrder (ShuffleOrderTest), Timeline navigation with repeat and
// shuffle, BasePlayer's next / previous, ExoPlayerImpl's playlist edits and auto transitions.
#include "Check.h"

#include "kiln/media/Player.h"
#include "kiln/runtime/Task.h"

#include <algorithm>

using namespace kiln;
using namespace kiln::media;

namespace {
    constexpr std::uint64_t kSeed = 1234567;

    // ShuffleOrderTest.assertShuffleOrderCorrectness
    bool Correct(const ShuffleOrder& o, int length) {
        if (o.Length() != length) return false;
        if (length == 0) return o.FirstIndex() == kIndexUnset && o.LastIndex() == kIndexUnset;
        std::vector<int> indices(static_cast<std::size_t>(length));
        indices[0] = o.FirstIndex();
        if (o.PreviousIndex(indices[0]) != kIndexUnset) return false;
        for (int i = 1; i < length; ++i) {
            indices[std::size_t(i)] = o.NextIndex(indices[std::size_t(i - 1)]);
            if (o.PreviousIndex(indices[std::size_t(i)]) != indices[std::size_t(i - 1)]) return false;
            for (int j = 0; j < i; ++j)
                if (indices[std::size_t(i)] == indices[std::size_t(j)]) return false;
        }
        if (o.LastIndex() != indices.back() || o.NextIndex(indices.back()) != kIndexUnset) return false;
        return std::all_of(indices.begin(), indices.end(), [&](int i) { return i >= 0 && i < length; });
    }
    bool InsertKeepsOrder(ShuffleOrder o, int position, int count) {
        ShuffleOrder n = o.CloneAndInsert(position, count);
        if (!Correct(n, o.Length() + count)) return false;
        for (int i = 0; i < o.Length(); ++i) {
            int expected = o.NextIndex(i);
            if (expected != kIndexUnset && expected >= position) expected += count;
            int next = n.NextIndex(i < position ? i : i + count);
            while (next >= position && next < position + count) next = n.NextIndex(next);
            if (next != expected) return false;
        }
        return true;
    }
    bool RemoveKeepsOrder(ShuffleOrder o, int from, int to) {
        const int removed = to - from;
        ShuffleOrder n = o.CloneAndRemove(from, to);
        if (!Correct(n, o.Length() - removed)) return false;
        for (int i = 0; i < o.Length(); ++i) {
            if (i >= from && i < to) continue;
            int expected = o.NextIndex(i);
            while (expected >= from && expected < to) expected = o.NextIndex(expected);
            if (expected != kIndexUnset && expected >= from) expected -= removed;
            if (n.NextIndex(i < from ? i : i - removed) != expected) return false;
        }
        return true;
    }

    struct Engine : PlayerEngine {
        EngineLoad load;
        bool play = false, repeat = false;
        std::int64_t seek = -1;
        PlaybackParameters parameters;
        int loads = 0;
        void Load(EngineLoad l) override {
            load = l;
            seek = -1;
            ++loads;
        }
        void SetPlayWhenReady(bool p) override { play = p; }
        void SeekTo(std::int64_t ms) override { seek = ms; }
        void SetRepeat(bool r) override { repeat = r; }
        void SetVolume(float) override {}
        void SetSurface(ui::PlatformSurface) override {}
        void SetPlaybackParameters(PlaybackParameters p) override { parameters = p; }
        void Report(PlaybackState st, std::int64_t positionMs = 0, std::int64_t durationMs = 10'000) {
            PlayerState s;
            s.state = st;
            s.positionMs = positionMs;
            s.durationMs = durationMs;
            s.isPlaying = st == PlaybackState::Ready && play;
            onState(s, load.loadId);
            rt::Dispatcher::Main().RunPending();
        }
    };
    struct Fixture {
        Engine* engine = nullptr;
        Fixture() {
            SetPlayerEngineFactory([this] {
                auto e = std::make_unique<Engine>();
                engine = e.get();
                return std::unique_ptr<PlayerEngine>(std::move(e));
            });
        }
        ~Fixture() {
            rt::Dispatcher::Main().RunPending();
            SetPlayerEngineFactory({});
        }
    };
    std::vector<MediaItem> Items(int n) {
        std::vector<MediaItem> v;
        for (int i = 0; i < n; ++i) {
            MediaItem m = MediaItem::FromUri("asset:///" + std::to_string(i) + ".mp3");
            m.mediaId = "id" + std::to_string(i);
            v.push_back(std::move(m));
        }
        return v;
    }
}

TEST(DefaultShuffleOrderFollowsMedia3) {
    CHECK(Correct(ShuffleOrder(0, kSeed), 0) && Correct(ShuffleOrder(1, kSeed), 1) && Correct(ShuffleOrder(5, kSeed), 5));
    for (int length = 0; length < 4; ++length)
        for (int at = 0; at <= length; at += 2)
            for (int count : { 0, 1, 5 }) CHECK(InsertKeepsOrder(ShuffleOrder(length, kSeed), at, count));
    CHECK(RemoveKeepsOrder(ShuffleOrder(5, kSeed), 0, 1) && RemoveKeepsOrder(ShuffleOrder(5, kSeed), 2, 3) &&
          RemoveKeepsOrder(ShuffleOrder(5, kSeed), 4, 5) && RemoveKeepsOrder(ShuffleOrder(1, kSeed), 0, 1));
    CHECK(RemoveKeepsOrder(ShuffleOrder(1000, kSeed), 0, 1000) && RemoveKeepsOrder(ShuffleOrder(1000, kSeed), 0, 999) &&
          RemoveKeepsOrder(ShuffleOrder(1000, kSeed), 0, 500) && RemoveKeepsOrder(ShuffleOrder(1000, kSeed), 100, 600) &&
          RemoveKeepsOrder(ShuffleOrder(1000, kSeed), 500, 1000));
    // defaultShuffleOrderSideloaded
    const std::vector<int> given{ 2, 1, 0, 4, 3 };
    ShuffleOrder o(given, kSeed);
    CHECK(o.FirstIndex() == 2 && o.LastIndex() == 3);
    bool next = true, previous = true;
    for (std::size_t i = 0; i + 1 < given.size(); ++i) next = next && o.NextIndex(given[i]) == given[i + 1];
    for (std::size_t i = 1; i < given.size(); ++i) previous = previous && o.PreviousIndex(given[i]) == given[i - 1];
    CHECK(next && previous && o.NextIndex(3) == kIndexUnset && o.PreviousIndex(2) == kIndexUnset);
}

TEST(PlaylistNavigationFollowsTimelineAndBasePlayer) {
    Fixture f;
    Player p;
    p.SetMediaItems(Items(3));
    CHECK(p.MediaItemCount() == 3 && p.CurrentMediaItemIndex() == 0 && p.State().state == PlaybackState::Idle);
    CHECK(f.engine->loads == 0);   // not prepared: nothing loads
    p.Prepare();
    CHECK(f.engine->loads == 1 && f.engine->load.uri == "asset:///0.mp3");
    // Off: no previous at the first, no next at the last.
    CHECK(!p.HasPreviousMediaItem() && p.NextMediaItemIndex() == 1);
    p.SeekToNext();
    CHECK(p.CurrentMediaItemIndex() == 1 && f.engine->load.uri == "asset:///1.mp3");
    p.SeekTo(2, 0);
    CHECK(!p.HasNextMediaItem());
    p.SeekToNext();   // nothing after the last
    CHECK(p.CurrentMediaItemIndex() == 2);
    // All: wraps around; One navigates as Off.
    p.SetRepeatMode(RepeatMode::All);
    CHECK(p.NextMediaItemIndex() == 0 && p.State().hasNextMediaItem);
    p.SetRepeatMode(RepeatMode::One);
    CHECK(!p.HasNextMediaItem() && p.PreviousMediaItemIndex() == 1);
    p.SetRepeatMode(RepeatMode::Off);
    // seekToPrevious: the previous item within the first 3 s, else the start of this one.
    f.engine->Report(PlaybackState::Ready, 2'000);
    p.SeekToPrevious();
    CHECK(p.CurrentMediaItemIndex() == 1);
    f.engine->Report(PlaybackState::Ready, 5'000);
    p.SeekToPrevious();
    CHECK(p.CurrentMediaItemIndex() == 1 && f.engine->seek == 0);
    p.SetMaxSeekToPreviousPositionMs(10'000);
    f.engine->Report(PlaybackState::Ready, 5'000);
    p.SeekToPrevious();
    CHECK(p.CurrentMediaItemIndex() == 0);
    // seekBack / seekForward by the increments, within the item.
    f.engine->Report(PlaybackState::Ready, 3'000);
    p.SeekBack();
    CHECK(f.engine->seek == 0);
    f.engine->Report(PlaybackState::Ready, 3'000);
    p.SeekForward();
    CHECK(f.engine->seek == 10'000);   // clamped to the 10 s duration
    p.SetSeekForwardIncrementMs(1'000);
    f.engine->Report(PlaybackState::Ready, 3'000);
    p.SeekForward();
    CHECK(f.engine->seek == 4'000);
    // Past the end: ignored.
    p.SeekTo(7, 0);
    CHECK(p.CurrentMediaItemIndex() == 0);
}

TEST(PlaylistShuffleAndAutoTransitions) {
    Fixture f;
    Player p;
    std::vector<std::pair<std::string, MediaItemTransitionReason>> transitions;
    p.AddListener({ .onMediaItemTransition = [&](const std::optional<MediaItem>& m, MediaItemTransitionReason r) {
        transitions.push_back({ m ? m->mediaId : "-", r });
    } });
    p.SetMediaItems(Items(4));
    p.SetShuffleOrder(ShuffleOrder({ 2, 0, 3, 1 }, kSeed));
    p.SetShuffleModeEnabled(true);
    p.Prepare();
    p.Play();
    // Shuffled navigation follows the order: 2, 0, 3, 1.
    p.SeekTo(2, 0);
    CHECK(p.NextMediaItemIndex() == 0 && !p.HasPreviousMediaItem());
    transitions.clear();
    f.engine->Report(PlaybackState::Ready, 9'000);
    f.engine->Report(PlaybackState::Ended, 10'000);   // played out: the next one, automatically
    CHECK(p.CurrentMediaItemIndex() == 0 && f.engine->load.uri == "asset:///0.mp3");
    CHECK(transitions.size() == 1 && transitions[0].first == "id0" && transitions[0].second == MediaItemTransitionReason::Auto);
    p.SeekTo(1, 0);   // the last in shuffled order
    f.engine->Report(PlaybackState::Ended, 10'000);
    CHECK(p.State().state == PlaybackState::Ended && p.CurrentMediaItemIndex() == 1);
    // All: from the last back to the first.
    p.SetRepeatMode(RepeatMode::All);
    p.SeekTo(1, 0);
    f.engine->Report(PlaybackState::Ended, 10'000);
    CHECK(p.CurrentMediaItemIndex() == 2 && p.State().state == PlaybackState::Buffering);
    // One: the engine loops by itself, and each loop is a REPEAT transition.
    p.SetRepeatMode(RepeatMode::One);
    CHECK(f.engine->repeat);
    transitions.clear();
    f.engine->onLooped(f.engine->load.loadId);
    rt::Dispatcher::Main().RunPending();
    CHECK(transitions.size() == 1 && transitions[0].second == MediaItemTransitionReason::Repeat);
    // A late report from an earlier item is dropped.
    const std::uint64_t old = f.engine->load.loadId;
    p.SeekTo(3, 0);
    PlayerState late;
    late.state = PlaybackState::Ended;
    f.engine->onState(late, old);
    rt::Dispatcher::Main().RunPending();
    CHECK(p.CurrentMediaItemIndex() == 3 && p.State().state == PlaybackState::Buffering);
}

TEST(PlaylistEditsFollowExoPlayer) {
    Fixture f;
    Player p;
    std::vector<DiscontinuityReason> jumps;
    p.AddListener({ .onPositionDiscontinuity = [&](const PositionInfo&, const PositionInfo&, DiscontinuityReason r) { jumps.push_back(r); } });
    p.SetMediaItems(Items(5));
    p.Prepare();
    p.SeekTo(2, 1'000);
    // Adding before the current item moves its index, not the playback.
    const int loads = f.engine->loads;
    p.AddMediaItem(0, MediaItem::FromUri("asset:///new.mp3"));
    CHECK(p.CurrentMediaItemIndex() == 3 && p.CurrentMediaItem()->mediaId == "id2" && f.engine->loads == loads);
    // Moving the current item keeps it playing at its new place.
    p.MoveMediaItem(3, 0);
    CHECK(p.CurrentMediaItemIndex() == 0 && p.CurrentMediaItem()->mediaId == "id2" && f.engine->loads == loads);
    // Removing the current item: the next one that remains plays, from its start.
    p.RemoveMediaItem(0);
    CHECK(p.CurrentMediaItem()->uri == "asset:///new.mp3" && f.engine->loads == loads + 1 && f.engine->load.startPositionMs == 0);
    CHECK(!jumps.empty() && jumps.back() == DiscontinuityReason::Remove);
    // Removing it at the end with nothing after: ENDED at the first item.
    p.SeekTo(p.MediaItemCount() - 1, 0);
    p.RemoveMediaItem(p.MediaItemCount() - 1);
    CHECK(p.State().state == PlaybackState::Ended && p.CurrentMediaItemIndex() == 0);
    // Replacing with the same source keeps playing; another source replaces it.
    p.SeekTo(1, 0);
    const int before = f.engine->loads;
    MediaItem same = p.MediaItemAt(1);
    same.mediaMetadata.title = "Renamed";
    p.ReplaceMediaItem(1, same);
    CHECK(f.engine->loads == before && p.State().mediaMetadata.title == "Renamed");
    p.ReplaceMediaItem(1, MediaItem::FromUri("asset:///other.mp3"));
    CHECK(f.engine->loads == before + 1 && f.engine->load.uri == "asset:///other.mp3" && p.CurrentMediaItemIndex() == 1);
    // Clearing ends playback; adding to the empty playlist plays again.
    p.ClearMediaItems();
    CHECK(p.MediaItemCount() == 0 && p.State().state == PlaybackState::Ended);
    p.AddMediaItems(Items(2));
    CHECK(p.State().state == PlaybackState::Buffering && f.engine->load.uri == "asset:///0.mp3");
    // Out of range.
    bool threw = false;
    try {
        p.RemoveMediaItems(1, 0);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(PlayerPrepareStopClippingAndSpeed) {
    Fixture f;
    Player p;
    MediaItem m = MediaItem::FromUri("asset:///long.mp4");
    m.clippingConfiguration.startPositionMs = 5'000;
    m.clippingConfiguration.endPositionMs = 8'000;
    p.SetMediaItem(m, std::int64_t(1'000));
    p.Prepare();
    // The engine plays the media from the clip's start (plus the start position), up to its end.
    CHECK(f.engine->load.startPositionMs == 6'000 && f.engine->load.clipStartMs == 5'000 && f.engine->load.endPositionMs == 8'000);
    f.engine->Report(PlaybackState::Ready, 6'500, 60'000);
    CHECK(p.State().positionMs == 1'500 && p.State().durationMs == 3'000);   // in the clipped window
    p.SeekTo(500);
    CHECK(f.engine->seek == 5'500);
    // Stop: idle, the playlist and position kept; prepare resumes there.
    f.engine->Report(PlaybackState::Ready, 7'000, 60'000);
    p.Stop();
    CHECK(p.State().state == PlaybackState::Idle && p.MediaItemCount() == 1 && f.engine->load.uri.empty());
    p.Prepare();
    CHECK(f.engine->load.startPositionMs == 7'000);
    // Speed and pitch reach the engine; zero is refused.
    p.SetPlaybackSpeed(1.5f);
    CHECK(f.engine->parameters.speed == 1.5f && p.State().playbackParameters.speed == 1.5f);
    bool threw = false;
    try {
        p.SetPlaybackSpeed(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw && p.GetPlaybackParameters().speed == 1.5f);
    // Errors: reported once, idle; prepare again clears them.
    int errors = 0;
    p.AddListener({ .onPlayerError = [&](const PlaybackException& e) { errors += e.errorCode == PlaybackException::IoFileNotFound; } });
    PlayerState failed;
    failed.error = "gone";
    failed.playerError = PlaybackException{ .errorCode = PlaybackException::IoFileNotFound, .message = "gone" };
    f.engine->onState(failed, f.engine->load.loadId);
    f.engine->onState(failed, f.engine->load.loadId);
    rt::Dispatcher::Main().RunPending();
    CHECK(errors == 1 && p.State().state == PlaybackState::Idle && p.State().playerError->errorCode == PlaybackException::IoFileNotFound);
    p.Prepare();
    CHECK(!p.State().playerError && p.State().state == PlaybackState::Buffering);
    CHECK(PlaybackException::ErrorCodeName(2005) == "ERROR_CODE_IO_FILE_NOT_FOUND" && PlaybackException::ErrorCodeName(7) == "invalid error code");
    // Mute remembers the volume.
    p.SetVolume(0.4f);
    p.Mute();
    CHECK(p.Volume() == 0);
    p.Unmute();
    CHECK(p.Volume() == 0.4f);
}

TEST(MediaMetadataPopulatesAsMedia3) {
    MediaMetadata a{ .title = "A", .artist = "Artist", .trackNumber = 3 };
    MediaMetadata b{ .title = "B", .albumTitle = "Album", .artworkUri = "asset:///b.png" };
    a.Populate(b);
    CHECK(a.title == "B" && a.artist == "Artist" && a.albumTitle == "Album" && a.trackNumber == 3 && a.artworkUri == "asset:///b.png");
    // maybeSetArtworkData: a front cover wins; another picture does not replace one.
    MediaMetadata m;
    m.MaybeSetArtworkData({ 1 }, 4);
    m.MaybeSetArtworkData({ 2 }, 3);
    m.MaybeSetArtworkData({ 3 }, 5);
    CHECK(m.artworkData == std::vector<std::uint8_t>{ 2 } && m.artworkDataType == 3);
}
