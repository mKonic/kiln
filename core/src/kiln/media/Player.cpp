#include "kiln/media/Player.h"

#include "kiln/runtime/Task.h"

#include <algorithm>
#include <map>
#include <stdexcept>

namespace kiln::media {

    namespace {
        kiln::Function<std::unique_ptr<PlayerEngine>()>& Factory() {
            static kiln::Function<std::unique_ptr<PlayerEngine>()> f;
            return f;
        }
        constexpr std::int64_t kDefaultSeekBackIncrementMs = 5'000;      // C.DEFAULT_SEEK_BACK_INCREMENT_MS
        constexpr std::int64_t kDefaultSeekForwardIncrementMs = 15'000;  // C.DEFAULT_SEEK_FORWARD_INCREMENT_MS
        constexpr std::int64_t kDefaultMaxSeekToPreviousMs = 3'000;      // C.DEFAULT_MAX_SEEK_TO_PREVIOUS_POSITION_MS
    }

    void SetPlayerEngineFactory(kiln::Function<std::unique_ptr<PlayerEngine>()> factory) { Factory() = std::move(factory); }

    // The playlist (ExoPlayerImpl's masking of the timeline and position) and the player's own
    // playWhenReady with audio focus applied over the engine's state
    // (ExoPlayerImplInternal.updatePlayWhenReadyWithAudioFocus), on the main thread.
    struct Player::Shared {
        using Command = AudioFocusManager::PlayerCommand;
        rt::MutableStateFlow<PlayerState> state{ PlayerState{} };
        std::unique_ptr<PlayerEngine> engine;

        // The playlist.
        std::vector<MediaItem> items;
        int current = 0;                     // the current item (0 while empty)
        std::int64_t positionMs = 0;         // in the current item (its clipped window)
        ShuffleOrder shuffle{ 0 };
        bool shuffleEnabled = false;
        RepeatMode repeatMode = RepeatMode::Off;
        MediaMetadata playlistMetadata;
        std::int64_t seekBackMs = kDefaultSeekBackIncrementMs, seekForwardMs = kDefaultSeekForwardIncrementMs;
        std::int64_t maxSeekToPreviousMs = kDefaultMaxSeekToPreviousMs;
        PlaybackParameters parameters;

        // Prepare() was called (and not Stop()); ended: played past the last item.
        bool prepared = false;
        bool ended = false;
        std::uint64_t nextLoad = 1, loadId = 0;   // the engine's current load (0: none)
        PlayerState engineState;                  // the current load's
        std::optional<PlaybackException> error;

        bool playWhenReady = false;
        PlayWhenReadyChangeReason reason = PlayWhenReadyChangeReason::UserRequest;
        PlaybackSuppression suppression = PlaybackSuppression::None;
        float volume = 1, unmuteVolume = 1;
        std::map<int, PlayerListener> listeners;
        int nextListener = 1;

        AudioFocusManager focus{ { .setVolumeMultiplier = [this](float) { ApplyVolume(); },
                                   .executePlayerCommand = [this](Command c) { Update(playWhenReady, c, reason); } } };
        AudioBecomingNoisyManager noisy{ [this] { UpdateWithFocus(false, PlayWhenReadyChangeReason::AudioBecomingNoisy); } };

        // --- the timeline (PlaylistTimeline over the shuffle order) ------------------------------
        int Count() const { return int(items.size()); }
        int First() const { return items.empty() ? kIndexUnset : shuffleEnabled ? shuffle.FirstIndex() : 0; }
        int Last() const { return items.empty() ? kIndexUnset : shuffleEnabled ? shuffle.LastIndex() : Count() - 1; }
        // Timeline.getNextWindowIndex / getPreviousWindowIndex.
        int NextIndex(int i, RepeatMode mode) const {
            if (items.empty()) return kIndexUnset;
            if (mode == RepeatMode::One) return i;
            if (i == Last()) return mode == RepeatMode::All ? First() : kIndexUnset;
            return shuffleEnabled ? shuffle.NextIndex(i) : i + 1;
        }
        int PreviousIndex(int i, RepeatMode mode) const {
            if (items.empty()) return kIndexUnset;
            if (mode == RepeatMode::One) return i;
            if (i == First()) return mode == RepeatMode::All ? Last() : kIndexUnset;
            return shuffleEnabled ? shuffle.PreviousIndex(i) : i - 1;
        }
        // BasePlayer.getRepeatModeForNavigation.
        RepeatMode NavigationMode() const { return repeatMode == RepeatMode::One ? RepeatMode::Off : repeatMode; }

        PositionInfo Position() const {
            return { current, positionMs, current < Count() ? items[std::size_t(current)].mediaId : std::string() };
        }

        // --- the engine -------------------------------------------------------------------------
        // The engine loops by itself when the item repeats into itself (gaplessly).
        bool EngineRepeats() const { return repeatMode == RepeatMode::One || (repeatMode == RepeatMode::All && Count() == 1); }

        void Load(std::int64_t windowPositionMs) {
            ended = false;
            error.reset();
            engineState = {};
            positionMs = windowPositionMs == kTimeUnset ? 0 : std::max<std::int64_t>(windowPositionMs, 0);
            if (!engine) {
                error = PlaybackException{ .errorCode = PlaybackException::Unspecified, .message = "no media engine on this platform" };
                Notify([&](PlayerListener& l) { if (l.onPlayerError) l.onPlayerError(*error); });
                return;
            }
            const MediaItem& m = items[std::size_t(current)];
            loadId = nextLoad++;
            engine->SetRepeat(EngineRepeats());
            engine->Load({ .uri = m.uri, .mimeType = m.mimeType,
                           .startPositionMs = m.clippingConfiguration.startPositionMs + positionMs,
                           .clipStartMs = m.clippingConfiguration.startPositionMs,
                           .endPositionMs = m.clippingConfiguration.endPositionMs, .loadId = loadId });
        }
        void Unload() {
            loadId = 0;
            engineState = {};
            if (engine) engine->Load({});
        }

        // The current item changed (index or contents): load it when prepared.
        void LoadCurrentIfPrepared(std::int64_t windowPositionMs) {
            if (!prepared) {
                positionMs = windowPositionMs == kTimeUnset ? 0 : windowPositionMs;
                return;
            }
            if (items.empty()) {
                Unload();
                ended = true;
                positionMs = 0;
                return;
            }
            Load(windowPositionMs);
        }

        void ApplyVolume() {
            if (engine) engine->SetVolume(volume * focus.VolumeMultiplier());
        }
        PlaybackState CurrentPlaybackState() const {
            if (!prepared || error) return PlaybackState::Idle;
            if (ended || items.empty()) return PlaybackState::Ended;
            if (engineState.state == PlaybackState::Idle) return PlaybackState::Buffering;   // loading
            return engineState.state;
        }
        void UpdateWithFocus(bool play, PlayWhenReadyChangeReason why) { Update(play, focus.UpdateAudioFocus(play, CurrentPlaybackState()), why); }
        void Update(bool play, Command c, PlayWhenReadyChangeReason why) {
            play = play && c != Command::DoNotPlay;
            if (c == Command::DoNotPlay) why = PlayWhenReadyChangeReason::AudioFocusLoss;
            else if (why == PlayWhenReadyChangeReason::AudioFocusLoss) why = PlayWhenReadyChangeReason::UserRequest;
            PlaybackSuppression suppressed = suppression;
            if (c == Command::WaitForCallback) suppressed = PlaybackSuppression::TransientAudioFocusLoss;
            else if (suppressed == PlaybackSuppression::TransientAudioFocusLoss) suppressed = PlaybackSuppression::None;
            if (play == playWhenReady && suppressed == suppression && why == reason) return;
            playWhenReady = play;
            reason = why;
            suppression = suppressed;
            if (engine) engine->SetPlayWhenReady(playWhenReady && suppression == PlaybackSuppression::None);
            Publish();
        }

        template<typename F>
        void Notify(F&& f) {
            // A listener may add or remove listeners: notify a snapshot of the ids.
            std::vector<int> ids;
            for (auto& [id, l] : listeners) ids.push_back(id);
            for (int id : ids)
                if (auto it = listeners.find(id); it != listeners.end()) f(it->second);
        }
        void Transition(MediaItemTransitionReason why) {
            std::optional<MediaItem> item;
            if (current < Count()) item = items[std::size_t(current)];
            Notify([&](PlayerListener& l) { if (l.onMediaItemTransition) l.onMediaItemTransition(item, why); });
        }
        void Discontinuity(const PositionInfo& before, DiscontinuityReason why) {
            const PositionInfo after = Position();
            Notify([&](PlayerListener& l) { if (l.onPositionDiscontinuity) l.onPositionDiscontinuity(before, after, why); });
        }

        void OnEngineState(const PlayerState& s, std::uint64_t id) {
            if (id != loadId || loadId == 0) return;   // an earlier item's
            const PlaybackState before = CurrentPlaybackState();
            engineState = s;
            const MediaItem& m = items[std::size_t(current)];
            const auto& clip = m.clippingConfiguration;
            positionMs = std::max<std::int64_t>(s.positionMs - clip.startPositionMs, 0);
            if (s.playerError || !s.error.empty()) {
                const bool first = !error;
                error = s.playerError ? *s.playerError : PlaybackException{ .message = s.error };
                if (error->message.empty()) error->message = s.error;
                if (first) Notify([&](PlayerListener& l) { if (l.onPlayerError) l.onPlayerError(*error); });
            } else if (s.state == PlaybackState::Ended) {
                // The item played out: the next one, or the playlist ended
                // (ExoPlayerImplInternal's auto transition).
                const int next = NextIndex(current, repeatMode);
                if (next != kIndexUnset && next != current) {
                    const PositionInfo was = Position();
                    current = next;
                    Load(0);
                    Discontinuity(was, DiscontinuityReason::AutoTransition);
                    Transition(MediaItemTransitionReason::Auto);
                } else if (next == current && !EngineRepeats()) {
                    const PositionInfo was = Position();
                    Load(0);
                    Discontinuity(was, DiscontinuityReason::AutoTransition);
                    Transition(MediaItemTransitionReason::Repeat);
                } else {
                    ended = true;
                }
            }
            // Preparing asks for focus; stopping or failing gives it back.
            if (CurrentPlaybackState() != before) UpdateWithFocus(playWhenReady, reason);
            Publish();
        }
        void OnLooped(std::uint64_t id) {
            if (id != loadId || loadId == 0) return;
            const PositionInfo was = Position();
            positionMs = 0;
            Discontinuity(was, DiscontinuityReason::AutoTransition);
            Transition(MediaItemTransitionReason::Repeat);
        }

        void Publish() {
            PlayerState s = engineState;
            s.state = CurrentPlaybackState();
            const bool hasItem = current < Count();
            const MediaItem* m = hasItem ? &items[std::size_t(current)] : nullptr;
            if (m) {
                const auto& clip = m->clippingConfiguration;
                s.positionMs = positionMs;
                std::int64_t duration = engineState.durationMs;
                if (clip.endPositionMs != kTimeEndOfSource) duration = duration < 0 ? clip.endPositionMs : std::min(duration, clip.endPositionMs);
                s.durationMs = duration < 0 ? -1 : std::max<std::int64_t>(duration - clip.startPositionMs, 0);
                s.bufferedPositionMs = std::max<std::int64_t>(engineState.bufferedPositionMs - clip.startPositionMs, s.positionMs);
                if (s.state == PlaybackState::Ended && s.durationMs >= 0) s.positionMs = s.durationMs;
                // getMediaMetadata: the item's, over what the stream itself says.
                s.mediaMetadata = engineState.mediaMetadata;
                s.mediaMetadata.Populate(m->mediaMetadata);
                s.currentMediaId = m->mediaId;
            } else {
                s.positionMs = 0;
                s.durationMs = -1;
                s.bufferedPositionMs = 0;
                s.mediaMetadata = {};
                s.currentMediaId.clear();
            }
            if (s.state == PlaybackState::Idle && !error) {
                s.error.clear();
                s.playerError.reset();
                s.isLoading = false;
            }
            if (error) {
                s.playerError = error;
                s.error = error->message;
            }
            s.currentMediaItemIndex = current;
            s.mediaItemCount = Count();
            s.hasNextMediaItem = NextIndex(current, NavigationMode()) != kIndexUnset;
            s.hasPreviousMediaItem = PreviousIndex(current, NavigationMode()) != kIndexUnset;
            s.repeatMode = repeatMode;
            s.shuffleModeEnabled = shuffleEnabled;
            s.playbackParameters = parameters;
            s.playWhenReady = playWhenReady;
            s.playWhenReadyChangeReason = reason;
            s.suppression = suppression;
            s.isPlaying = s.state == PlaybackState::Ready && playWhenReady && suppression == PlaybackSuppression::None && engineState.isPlaying;
            state.SetValue(s);
        }

        // --- playlist edits (ExoPlayerImpl) -----------------------------------------------------
        void CheckRange(int from, int to, int size) const {
            if (from < 0 || from > to || to > size) throw std::out_of_range("media item range out of bounds");
        }

        // resetToFirst: the new timeline's first item (with the new shuffle order), at its default
        // position; startIndex kIndexUnset: keep the current index and position.
        void SetItems(std::vector<MediaItem> next, int startIndex, std::int64_t startPositionMs, bool resetToFirst = false) {
            const std::optional<MediaItem> before = current < Count() ? std::optional(items[std::size_t(current)]) : std::nullopt;
            const PositionInfo was = Position();
            items = std::move(next);
            shuffle = shuffle.CloneAndClear().CloneAndInsert(0, Count());
            bool invalidSeek = false;
            if (resetToFirst) {
                startIndex = items.empty() ? 0 : First();
                startPositionMs = kTimeUnset;
            } else if (startIndex == kIndexUnset) {
                startIndex = current;
                startPositionMs = positionMs;
            }
            if (!items.empty() && startIndex >= Count()) {
                invalidSeek = true;   // ExoPlayerImpl: an invalid seek goes to ENDED
                startIndex = First();
                startPositionMs = kTimeUnset;
            }
            current = items.empty() ? 0 : startIndex;
            const std::optional<MediaItem> after = current < Count() ? std::optional(items[std::size_t(current)]) : std::nullopt;
            if (invalidSeek && prepared) {
                Unload();
                ended = true;
                positionMs = 0;
            } else {
                LoadCurrentIfPrepared(startPositionMs);
            }
            if (before != after || was.mediaItemIndex != current) {
                Discontinuity(was, DiscontinuityReason::Remove);
                Transition(MediaItemTransitionReason::PlaylistChanged);
            }
            Publish();
        }

        void Insert(int index, std::vector<MediaItem> added) {
            if (index < 0) throw std::out_of_range("media item index out of bounds");
            index = std::min(index, Count());
            const int count = int(added.size());
            if (count == 0) return;
            const bool wasEmpty = items.empty();
            items.insert(items.begin() + index, std::make_move_iterator(added.begin()), std::make_move_iterator(added.end()));
            shuffle = shuffle.CloneAndInsert(index, count);
            if (wasEmpty) {
                current = shuffleEnabled ? First() : 0;
                LoadCurrentIfPrepared(positionMs);   // from an empty playlist: the first item plays
                Transition(MediaItemTransitionReason::PlaylistChanged);
            } else if (index <= current) {
                current += count;
            }
            Publish();
        }

        // resolveSubsequentPeriod: the item after the old current one (in playback order) that
        // is still in the playlist, as its new index; kIndexUnset when none is.
        int ResolveSubsequent(int oldCurrent, const std::vector<char>& removed, const std::vector<int>& newIndexOf,
                              const Shared& oldTimeline) const {
            int i = oldCurrent;
            for (int n = 0; n < oldTimeline.Count(); ++n) {
                i = oldTimeline.NextIndex(i, oldTimeline.repeatMode);
                if (i == kIndexUnset) break;
                if (!removed[std::size_t(i)]) return newIndexOf[std::size_t(i)];
            }
            return kIndexUnset;
        }

        void Erase(int from, int to) {
            if (from < 0 || to < from) throw std::out_of_range("media item range out of bounds");
            to = std::min(to, Count());
            if (from >= to) return;
            const PositionInfo was = Position();
            const bool removesCurrent = current >= from && current < to;
            std::vector<char> removed(items.size(), 0);
            std::vector<int> newIndexOf(items.size(), kIndexUnset);
            for (int i = 0, n = 0; i < Count(); ++i) {
                removed[std::size_t(i)] = i >= from && i < to;
                if (!removed[std::size_t(i)]) newIndexOf[std::size_t(i)] = n++;
            }
            int resolved = kIndexUnset;
            if (removesCurrent) {
                // The old timeline's navigation, before the edit.
                resolved = ResolveSubsequent(current, removed, newIndexOf, *this);
            }
            items.erase(items.begin() + from, items.begin() + to);
            shuffle = shuffle.CloneAndRemove(from, to);
            if (!removesCurrent) {
                if (current >= to) current -= to - from;
                Publish();
                return;
            }
            if (resolved != kIndexUnset) {
                current = resolved;
                LoadCurrentIfPrepared(kTimeUnset);
            } else {
                // Nothing after it: ENDED, at the first item's default position.
                current = items.empty() ? 0 : First();
                positionMs = 0;
                if (prepared) {
                    Unload();
                    ended = true;
                }
            }
            Discontinuity(was, DiscontinuityReason::Remove);
            Transition(MediaItemTransitionReason::PlaylistChanged);
            Publish();
        }

        void Move(int from, int to, int newIndex) {
            CheckRange(from, to, Count());
            if (newIndex < 0) throw std::out_of_range("media item index out of bounds");
            const int count = to - from;
            newIndex = std::min(newIndex, Count() - count);
            if (count == 0 || from == newIndex) return;
            // Util.moveItems; the current item keeps playing wherever it goes.
            std::vector<int> order(items.size());
            for (int i = 0; i < Count(); ++i) order[std::size_t(i)] = i;
            std::vector<int> moved(order.begin() + from, order.begin() + to);
            order.erase(order.begin() + from, order.begin() + to);
            order.insert(order.begin() + newIndex, moved.begin(), moved.end());
            std::vector<MediaItem> next;
            next.reserve(items.size());
            int newCurrent = current;
            for (int i = 0; i < Count(); ++i) {
                next.push_back(std::move(items[std::size_t(order[std::size_t(i)])]));
                if (order[std::size_t(i)] == current) newCurrent = i;
            }
            items = std::move(next);
            current = newCurrent;
            Publish();
        }
    };

    Player::Player() : m_Shared(std::make_shared<Shared>()) {
        if (Factory()) m_Shared->engine = Factory()();
        if (!m_Shared->engine) return;
        // Reports from the decoding thread reach the state on this (the main) thread.
        rt::Dispatcher* main = &rt::Dispatcher::Main();
        std::weak_ptr<Shared> weak = m_Shared;
        m_Shared->engine->onState = [main, weak](const PlayerState& s, std::uint64_t id) {
            main->PostFromAnyThread([weak, s, id] {
                if (auto shared = weak.lock()) shared->OnEngineState(s, id);
            });
        };
        m_Shared->engine->onLooped = [main, weak](std::uint64_t id) {
            main->PostFromAnyThread([weak, id] {
                if (auto shared = weak.lock()) shared->OnLooped(id);
            });
        };
        m_Shared->engine->onRenderedFirstFrame = [main, weak](std::uint64_t id) {
            main->PostFromAnyThread([weak, id] {
                auto shared = weak.lock();
                if (!shared || id != shared->loadId || shared->loadId == 0) return;
                shared->Notify([](PlayerListener& l) { if (l.onRenderedFirstFrame) l.onRenderedFirstFrame(); });
            });
        };
    }

    Player::~Player() = default;

    const PlayerState& Player::State() const { return m_Shared->state.Value(); }
    rt::MutableStateFlow<PlayerState>& Player::StateFlow() { return m_Shared->state; }

    void Player::SetMediaUri(std::string uri) {
        if (uri.empty()) {
            ClearMediaItems();
            return;
        }
        SetMediaItem(MediaItem::FromUri(std::move(uri)));
        Prepare();
    }

    // --- the playlist --------------------------------------------------------------------------

    void Player::SetMediaItems(std::vector<MediaItem> items, bool resetPosition) {
        Shared& s = *m_Shared;
        if (resetPosition) {
            s.SetItems(std::move(items), 0, kTimeUnset, /* resetToFirst= */ true);
        } else {
            s.SetItems(std::move(items), kIndexUnset, kTimeUnset);
        }
    }
    void Player::SetMediaItems(std::vector<MediaItem> items, int startIndex, std::int64_t startPositionMs) {
        if (startIndex < kIndexUnset) throw std::out_of_range("media item index out of bounds");
        m_Shared->SetItems(std::move(items), startIndex, startIndex == kIndexUnset ? kTimeUnset : startPositionMs);
    }
    void Player::SetMediaItem(MediaItem item, bool resetPosition) {
        std::vector<MediaItem> v;
        v.push_back(std::move(item));
        SetMediaItems(std::move(v), resetPosition);
    }
    void Player::SetMediaItem(MediaItem item, std::int64_t startPositionMs) {
        std::vector<MediaItem> v;
        v.push_back(std::move(item));
        SetMediaItems(std::move(v), 0, startPositionMs);
    }
    void Player::AddMediaItem(MediaItem item) { AddMediaItem(MediaItemCount(), std::move(item)); }
    void Player::AddMediaItem(int index, MediaItem item) {
        std::vector<MediaItem> v;
        v.push_back(std::move(item));
        AddMediaItems(index, std::move(v));
    }
    void Player::AddMediaItems(std::vector<MediaItem> items) { AddMediaItems(MediaItemCount(), std::move(items)); }
    void Player::AddMediaItems(int index, std::vector<MediaItem> items) { m_Shared->Insert(index, std::move(items)); }
    void Player::MoveMediaItem(int currentIndex, int newIndex) {
        if (currentIndex >= 0 && currentIndex < MediaItemCount() && newIndex >= 0) MoveMediaItems(currentIndex, currentIndex + 1, newIndex);
    }
    void Player::MoveMediaItems(int fromIndex, int toIndex, int newIndex) {
        m_Shared->Move(fromIndex, std::min(toIndex, MediaItemCount()), newIndex);
    }
    void Player::ReplaceMediaItem(int index, MediaItem item) {
        std::vector<MediaItem> v;
        v.push_back(std::move(item));
        ReplaceMediaItems(index, index + 1, std::move(v));
    }
    void Player::ReplaceMediaItems(int fromIndex, int toIndex, std::vector<MediaItem> items) {
        Shared& s = *m_Shared;
        toIndex = std::min(toIndex, s.Count());
        s.CheckRange(fromIndex, toIndex, s.Count());
        // ExoPlayerImpl.replaceMediaItems: an item replaced by one with the same URI keeps playing
        // (its media source updated); otherwise the new ones go in after the range, which is then
        // removed (the current item resolving to the first of them).
        const bool sameSources = int(items.size()) == toIndex - fromIndex &&
            std::equal(items.begin(), items.end(), s.items.begin() + fromIndex, [](const MediaItem& a, const MediaItem& b) {
                return a.uri == b.uri && a.mimeType == b.mimeType && a.clippingConfiguration == b.clippingConfiguration;
            });
        if (sameSources) {
            const bool touchesCurrent = s.current >= fromIndex && s.current < toIndex;
            std::move(items.begin(), items.end(), s.items.begin() + fromIndex);
            if (touchesCurrent) s.Transition(MediaItemTransitionReason::PlaylistChanged);
            s.Publish();
            return;
        }
        s.Insert(toIndex, std::move(items));
        s.Erase(fromIndex, toIndex);
    }
    void Player::RemoveMediaItem(int index) {
        if (index >= 0 && index < MediaItemCount()) RemoveMediaItems(index, index + 1);
    }
    void Player::RemoveMediaItems(int fromIndex, int toIndex) { m_Shared->Erase(fromIndex, toIndex); }
    void Player::ClearMediaItems() { m_Shared->Erase(0, MediaItemCount()); }
    int Player::MediaItemCount() const { return m_Shared->Count(); }
    const MediaItem& Player::MediaItemAt(int index) const { return m_Shared->items.at(std::size_t(index)); }
    int Player::CurrentMediaItemIndex() const { return m_Shared->current; }
    std::optional<MediaItem> Player::CurrentMediaItem() const {
        const Shared& s = *m_Shared;
        return s.current < s.Count() ? std::optional(s.items[std::size_t(s.current)]) : std::nullopt;
    }
    int Player::NextMediaItemIndex() const { return m_Shared->NextIndex(m_Shared->current, m_Shared->NavigationMode()); }
    int Player::PreviousMediaItemIndex() const { return m_Shared->PreviousIndex(m_Shared->current, m_Shared->NavigationMode()); }
    bool Player::HasNextMediaItem() const { return NextMediaItemIndex() != kIndexUnset; }
    bool Player::HasPreviousMediaItem() const { return PreviousMediaItemIndex() != kIndexUnset; }
    void Player::SetPlaylistMetadata(MediaMetadata metadata) {
        Shared& s = *m_Shared;
        if (s.playlistMetadata == metadata) return;
        s.playlistMetadata = std::move(metadata);
        s.Notify([&](PlayerListener& l) { if (l.onPlaylistMetadataChanged) l.onPlaylistMetadataChanged(s.playlistMetadata); });
    }
    const MediaMetadata& Player::PlaylistMetadata() const { return m_Shared->playlistMetadata; }

    void Player::Prepare() {
        Shared& s = *m_Shared;
        if (s.prepared && !s.error) return;
        s.prepared = true;
        if (s.items.empty()) {
            s.ended = true;
            s.error.reset();
        } else {
            s.Load(s.positionMs);
        }
        s.UpdateWithFocus(s.playWhenReady, s.reason);
        s.Publish();
    }
    void Player::Stop() {
        Shared& s = *m_Shared;
        if (!s.prepared) return;
        s.prepared = false;
        s.ended = false;
        s.error.reset();
        s.Unload();
        s.UpdateWithFocus(s.playWhenReady, s.reason);
        s.Publish();
    }
    void Player::Play() { SetPlayWhenReady(true, PlayWhenReadyChangeReason::UserRequest); }
    void Player::Pause() { SetPlayWhenReady(false, PlayWhenReadyChangeReason::UserRequest); }
    void Player::SetPlayWhenReady(bool play, PlayWhenReadyChangeReason reason) { m_Shared->UpdateWithFocus(play, reason); }

    // --- seeking (BasePlayer) ------------------------------------------------------------------

    void Player::SeekTo(std::int64_t ms) { SeekTo(m_Shared->current, ms); }
    void Player::SeekTo(int index, std::int64_t ms) {
        Shared& s = *m_Shared;
        if (index < 0) throw std::out_of_range("media item index out of bounds");
        // ExoPlayerImpl.seekTo: past the end of a non-empty playlist is ignored.
        if (!s.items.empty() && index >= s.Count()) return;
        const PositionInfo was = s.Position();
        const bool itemChanges = index != s.current;
        const std::int64_t pos = ms == kTimeUnset ? 0 : std::max<std::int64_t>(ms, 0);
        if (s.items.empty()) {
            s.positionMs = pos;
            s.Publish();
            return;
        }
        s.current = index;
        if (!s.prepared) {
            s.positionMs = pos;
        } else if (itemChanges || s.ended || s.error || !s.engine || s.loadId == 0) {
            s.Load(pos);
        } else {
            const MediaItem& m = s.items[std::size_t(s.current)];
            s.positionMs = pos;
            s.engine->SeekTo(m.clippingConfiguration.startPositionMs + pos);
        }
        s.Discontinuity(was, DiscontinuityReason::Seek);
        if (itemChanges) s.Transition(MediaItemTransitionReason::Seek);
        s.Publish();
    }
    void Player::SeekToDefaultPosition() { SeekTo(m_Shared->current, kTimeUnset); }
    void Player::SeekToDefaultPosition(int index) { SeekTo(index, kTimeUnset); }
    // seekToOffset: clamped to the item.
    void Player::SeekBack() {
        const PlayerState& st = State();
        std::int64_t p = std::max<std::int64_t>(st.positionMs - m_Shared->seekBackMs, 0);
        SeekTo(p);
    }
    void Player::SeekForward() {
        const PlayerState& st = State();
        std::int64_t p = st.positionMs + m_Shared->seekForwardMs;
        if (st.durationMs >= 0) p = std::min(p, st.durationMs);
        SeekTo(std::max<std::int64_t>(p, 0));
    }
    void Player::SeekToNextMediaItem() {
        Shared& s = *m_Shared;
        const int next = NextMediaItemIndex();
        if (next == kIndexUnset) return;
        if (next == s.current) {
            // repeatCurrentMediaItem
            const PositionInfo was = s.Position();
            SeekTo(s.current, kTimeUnset);
            s.Transition(MediaItemTransitionReason::Repeat);
            (void)was;
            return;
        }
        SeekTo(next, kTimeUnset);
    }
    void Player::SeekToPreviousMediaItem() {
        Shared& s = *m_Shared;
        const int previous = PreviousMediaItemIndex();
        if (previous == kIndexUnset) return;
        if (previous == s.current) {
            SeekTo(s.current, kTimeUnset);
            s.Transition(MediaItemTransitionReason::Repeat);
            return;
        }
        SeekTo(previous, kTimeUnset);
    }
    void Player::SeekToNext() {
        if (m_Shared->items.empty()) return;
        if (HasNextMediaItem()) SeekToNextMediaItem();
    }
    void Player::SeekToPrevious() {
        Shared& s = *m_Shared;
        if (s.items.empty()) return;
        if (HasPreviousMediaItem() && State().positionMs <= s.maxSeekToPreviousMs) SeekToPreviousMediaItem();
        else SeekTo(s.current, 0);
    }
    void Player::SetSeekBackIncrementMs(std::int64_t ms) {
        if (ms <= 0) throw std::invalid_argument("seek back increment must be positive");
        m_Shared->seekBackMs = ms;
    }
    void Player::SetSeekForwardIncrementMs(std::int64_t ms) {
        if (ms <= 0) throw std::invalid_argument("seek forward increment must be positive");
        m_Shared->seekForwardMs = ms;
    }
    void Player::SetMaxSeekToPreviousPositionMs(std::int64_t ms) {
        if (ms < 0) throw std::invalid_argument("max seek to previous position must not be negative");
        m_Shared->maxSeekToPreviousMs = ms;
    }
    std::int64_t Player::SeekBackIncrementMs() const { return m_Shared->seekBackMs; }
    std::int64_t Player::SeekForwardIncrementMs() const { return m_Shared->seekForwardMs; }
    std::int64_t Player::MaxSeekToPreviousPositionMs() const { return m_Shared->maxSeekToPreviousMs; }

    void Player::SetRepeatMode(RepeatMode mode) {
        Shared& s = *m_Shared;
        if (s.repeatMode == mode) return;
        s.repeatMode = mode;
        if (s.engine && s.loadId) s.engine->SetRepeat(s.EngineRepeats());
        s.Publish();
    }
    RepeatMode Player::GetRepeatMode() const { return m_Shared->repeatMode; }
    void Player::SetRepeat(bool repeat) { SetRepeatMode(repeat ? RepeatMode::One : RepeatMode::Off); }
    void Player::SetShuffleModeEnabled(bool enabled) {
        Shared& s = *m_Shared;
        if (s.shuffleEnabled == enabled) return;
        s.shuffleEnabled = enabled;
        s.Publish();
    }
    bool Player::ShuffleModeEnabled() const { return m_Shared->shuffleEnabled; }
    void Player::SetShuffleOrder(ShuffleOrder order) {
        if (order.Length() != MediaItemCount()) throw std::invalid_argument("the shuffle order's length must be the playlist's");
        m_Shared->shuffle = std::move(order);
        m_Shared->Publish();
    }

    void Player::SetPlaybackParameters(PlaybackParameters p) {
        if (!(p.speed > 0) || !(p.pitch > 0)) throw std::invalid_argument("playback speed and pitch must be more than 0");
        Shared& s = *m_Shared;
        if (s.parameters == p) return;
        s.parameters = p;
        if (s.engine) s.engine->SetPlaybackParameters(p);
        s.Publish();
    }
    void Player::SetPlaybackSpeed(float speed) { SetPlaybackParameters(m_Shared->parameters.WithSpeed(speed)); }
    const PlaybackParameters& Player::GetPlaybackParameters() const { return m_Shared->parameters; }

    void Player::SetVolume(float volume) {
        m_Shared->volume = volume < 0 ? 0 : volume > 1 ? 1 : volume;
        m_Shared->ApplyVolume();
    }
    float Player::Volume() const { return m_Shared->volume; }
    void Player::Mute() {
        if (m_Shared->volume == 0) return;
        m_Shared->unmuteVolume = m_Shared->volume;
        SetVolume(0);
    }
    void Player::Unmute() {
        if (m_Shared->volume != 0) return;
        SetVolume(m_Shared->unmuteVolume);
    }

    int Player::AddListener(PlayerListener listener) {
        const int id = m_Shared->nextListener++;
        m_Shared->listeners.emplace(id, std::move(listener));
        return id;
    }
    void Player::RemoveListener(int id) { m_Shared->listeners.erase(id); }

    void Player::SetSurface(ui::PlatformSurface surface) {
        if (m_Shared->engine) m_Shared->engine->SetSurface(surface);
    }
    void Player::SetAudioAttributes(AudioAttributes attributes, bool handleAudioFocus) {
        m_Shared->focus.SetAudioAttributes(handleAudioFocus ? std::optional(attributes) : std::nullopt);
        if (m_Shared->engine) m_Shared->engine->SetAudioAttributes(attributes);
        m_Shared->UpdateWithFocus(m_Shared->playWhenReady, m_Shared->reason);
    }
    void Player::SetHandleAudioBecomingNoisy(bool handle) { m_Shared->noisy.SetEnabled(handle); }

    FrameRelease DecideFrameRelease(const FrameTiming& t) {
        // shouldForceRelease: the first frame always shows (even paused); later, a late frame
        // shows when none has for 100 ms (MediaCodecVideoRenderer.shouldForceRenderOutputBuffer).
        if (!t.firstFrameRendered) return FrameRelease::Immediately;
        if (!t.started) return FrameRelease::TryAgainLater;
        if (t.earlyUs < kLateThresholdUs && t.sinceLastReleaseUs > 100'000) return FrameRelease::Immediately;
        if (t.earlyUs < kLateThresholdUs && !t.lastFrame) return FrameRelease::Drop;
        if (t.earlyUs > kEarlySchedulingThresholdUs) return FrameRelease::TryAgainLater;
        return FrameRelease::Scheduled;
    }

}
