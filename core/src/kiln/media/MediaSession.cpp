#include "kiln/media/MediaSession.h"

#include <algorithm>

#include <chrono>
#include <cstdlib>
#include <map>

namespace kiln::media {

    namespace {
        struct Registry {
            MediaSessionHost host;
            int next = 1;
            std::map<int, MediaSession*> sessions;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
    }

    void SetMediaSessionHost(MediaSessionHost host) { R().host = std::move(host); }

    // --- command buttons ------------------------------------------------------------------------

    const char* CommandButtonIconDrawable(int icon) {
        switch (icon) {
            case CommandButton::Play: return "media3_icon_play";
            case CommandButton::Pause: return "media3_icon_pause";
            case CommandButton::Stop: return "media3_icon_stop";
            case CommandButton::Next: return "media3_icon_next";
            case CommandButton::Previous: return "media3_icon_previous";
            case CommandButton::SkipForward: return "media3_icon_skip_forward";
            case CommandButton::SkipForward5: return "media3_icon_skip_forward_5";
            case CommandButton::SkipForward10: return "media3_icon_skip_forward_10";
            case CommandButton::SkipForward15: return "media3_icon_skip_forward_15";
            case CommandButton::SkipForward30: return "media3_icon_skip_forward_30";
            case CommandButton::SkipBack: return "media3_icon_skip_back";
            case CommandButton::SkipBack5: return "media3_icon_skip_back_5";
            case CommandButton::SkipBack10: return "media3_icon_skip_back_10";
            case CommandButton::SkipBack15: return "media3_icon_skip_back_15";
            case CommandButton::SkipBack30: return "media3_icon_skip_back_30";
            case CommandButton::FastForward: return "media3_icon_fast_forward";
            case CommandButton::Rewind: return "media3_icon_rewind";
            case CommandButton::RepeatAll: return "media3_icon_repeat_all";
            case CommandButton::RepeatOne: return "media3_icon_repeat_one";
            case CommandButton::RepeatOff: return "media3_icon_repeat_off";
            case CommandButton::ShuffleOn: return "media3_icon_shuffle_on";
            case CommandButton::ShuffleOff: return "media3_icon_shuffle_off";
            case CommandButton::ShuffleStar: return "media3_icon_shuffle_star";
            case CommandButton::HeartFilled: return "media3_icon_heart_filled";
            case CommandButton::HeartUnfilled: return "media3_icon_heart_unfilled";
            case CommandButton::StarFilled: return "media3_icon_star_filled";
            case CommandButton::StarUnfilled: return "media3_icon_star_unfilled";
            case CommandButton::BookmarkFilled: return "media3_icon_bookmark_filled";
            case CommandButton::BookmarkUnfilled: return "media3_icon_bookmark_unfilled";
            case CommandButton::ThumbUpFilled: return "media3_icon_thumb_up_filled";
            case CommandButton::ThumbUpUnfilled: return "media3_icon_thumb_up_unfilled";
            case CommandButton::ThumbDownFilled: return "media3_icon_thumb_down_filled";
            case CommandButton::ThumbDownUnfilled: return "media3_icon_thumb_down_unfilled";
            case CommandButton::FlagFilled: return "media3_icon_flag_filled";
            case CommandButton::FlagUnfilled: return "media3_icon_flag_unfilled";
            case CommandButton::Plus: return "media3_icon_plus";
            case CommandButton::Minus: return "media3_icon_minus";
            case CommandButton::PlaylistAdd: return "media3_icon_playlist_add";
            case CommandButton::PlaylistRemove: return "media3_icon_playlist_remove";
            case CommandButton::QueueAdd: return "media3_icon_queue_add";
            case CommandButton::QueueNext: return "media3_icon_queue_next";
            case CommandButton::QueueRemove: return "media3_icon_queue_remove";
            case CommandButton::Block: return "media3_icon_block";
            case CommandButton::PlusCircleFilled: return "media3_icon_plus_circle_filled";
            case CommandButton::PlusCircleUnfilled: return "media3_icon_plus_circle_unfilled";
            case CommandButton::MinusCircleFilled: return "media3_icon_minus_circle_filled";
            case CommandButton::MinusCircleUnfilled: return "media3_icon_minus_circle_unfilled";
            case CommandButton::CheckCircleFilled: return "media3_icon_check_circle_filled";
            case CommandButton::CheckCircleUnfilled: return "media3_icon_check_circle_unfilled";
            case CommandButton::PlaybackSpeed: return "media3_icon_playback_speed";
            case CommandButton::PlaybackSpeed05: return "media3_icon_playback_speed_0_5";
            case CommandButton::PlaybackSpeed08: return "media3_icon_playback_speed_0_8";
            case CommandButton::PlaybackSpeed10: return "media3_icon_playback_speed_1_0";
            case CommandButton::PlaybackSpeed12: return "media3_icon_playback_speed_1_2";
            case CommandButton::PlaybackSpeed15: return "media3_icon_playback_speed_1_5";
            case CommandButton::PlaybackSpeed18: return "media3_icon_playback_speed_1_8";
            case CommandButton::PlaybackSpeed20: return "media3_icon_playback_speed_2_0";
            case CommandButton::Settings: return "media3_icon_settings";
            case CommandButton::Quality: return "media3_icon_quality";
            case CommandButton::Subtitles: return "media3_icon_subtitles";
            case CommandButton::SubtitlesOff: return "media3_icon_subtitles_off";
            case CommandButton::ClosedCaptions: return "media3_icon_closed_captions";
            case CommandButton::ClosedCaptionsOff: return "media3_icon_closed_captions_off";
            case CommandButton::Sync: return "media3_icon_sync";
            case CommandButton::Share: return "media3_icon_share";
            case CommandButton::VolumeUp: return "media3_icon_volume_up";
            case CommandButton::VolumeDown: return "media3_icon_volume_down";
            case CommandButton::VolumeOff: return "media3_icon_volume_off";
            case CommandButton::Artist: return "media3_icon_artist";
            case CommandButton::Album: return "media3_icon_album";
            case CommandButton::Radio: return "media3_icon_radio";
            case CommandButton::Signal: return "media3_icon_signal";
            case CommandButton::Feed: return "media3_icon_feed";
            default: return "";
        }
    }

    // CommandButton.getDefaultSlot.
    int CommandButton::FirstSlot() const {
        if (!slots.empty()) return slots.front();
        using C = PlayerCommand;
        const auto c = playerCommand;
        if (c == C::PlayPause || icon == Play || icon == Pause) return kSlotCentral;
        if (c == C::SeekBack || c == C::SeekToPrevious || c == C::SeekToPreviousMediaItem || icon == Previous || icon == Rewind || icon == SkipBack
            || icon == SkipBack5 || icon == SkipBack10 || icon == SkipBack15 || icon == SkipBack30)
            return kSlotBack;
        if (c == C::SeekForward || c == C::SeekToNext || c == C::SeekToNextMediaItem || icon == Next || icon == FastForward || icon == SkipForward
            || icon == SkipForward5 || icon == SkipForward10 || icon == SkipForward15 || icon == SkipForward30)
            return kSlotForward;
        return kSlotOverflow;
    }

    namespace {
        constexpr std::string_view kPlayerCommandPrefix = "androidx.media3.session.PLAYER_COMMAND_";

        // copyWithUnavailableButtonsDisabled (a player command must be available), then
        // canExecuteAction (the player commands a button runs without a parameter).
        bool CanShow(const CommandButton& b, const PlayerState& p) {
            if (!b.enabled) return false;
            if (!b.customAction.empty()) return true;
            if (!b.playerCommand || !p.availableCommands.Contains(*b.playerCommand)) return false;
            using C = PlayerCommand;
            switch (*b.playerCommand) {
                case C::PlayPause: case C::Prepare: case C::Stop: case C::SeekToPrevious: case C::SeekToNext:
                case C::SeekToPreviousMediaItem: case C::SeekToNextMediaItem: case C::SeekBack: case C::SeekForward:
                case C::SeekToDefaultPosition: case C::SetShuffleMode: case C::SetVolume:
                    return true;
                default: return false;
            }
        }
        std::vector<int> SlotsOf(const CommandButton& b) { return b.slots.empty() ? std::vector<int>{ b.FirstSlot() } : b.slots; }

        // CommandButton.getCustomLayoutFromMediaButtonPreferences: a back and a forward button
        // (their first such slot before an overflow one), then the overflow ones; each with the
        // slot it got as its only one.
        std::vector<CommandButton> CustomLayout(const std::vector<CommandButton>& prefs, const PlayerState& p) {
            int back = -1, forward = -1;
            for (std::size_t i = 0; i < prefs.size(); ++i) {
                if (!CanShow(prefs[i], p)) continue;
                for (int slot : SlotsOf(prefs[i])) {
                    if (slot == CommandButton::kSlotOverflow) break;
                    if (back < 0 && slot == CommandButton::kSlotBack) { back = int(i); break; }
                    if (forward < 0 && slot == CommandButton::kSlotForward) { forward = int(i); break; }
                }
            }
            std::vector<CommandButton> out;
            // convertToPredefinedCustomCommandButton: the slot, and a player command as the
            // predefined custom action that runs it.
            auto with = [](CommandButton b, int slot) {
                b.slots = { slot };
                if (b.customAction.empty() && b.playerCommand) {
                    b.customAction = std::string(kPlayerCommandPrefix) + std::to_string(int(*b.playerCommand));
                    b.playerCommand.reset();
                }
                return b;
            };
            if (back >= 0) out.push_back(with(prefs[std::size_t(back)], CommandButton::kSlotBack));
            if (forward >= 0) out.push_back(with(prefs[std::size_t(forward)], CommandButton::kSlotForward));
            for (std::size_t i = 0; i < prefs.size(); ++i) {
                if (!CanShow(prefs[i], p) || int(i) == back || int(i) == forward) continue;
                const auto slots = SlotsOf(prefs[i]);
                if (std::find(slots.begin(), slots.end(), CommandButton::kSlotOverflow) != slots.end())
                    out.push_back(with(prefs[i], CommandButton::kSlotOverflow));
            }
            return out;
        }
        SessionButton ToSession(const CommandButton& b) {
            return { b.iconResource.empty() ? CommandButtonIconDrawable(b.icon) : b.iconResource, b.displayName, b.customAction,
                     b.playerCommand ? int(*b.playerCommand) : 0, b.icon };
        }
        // MediaSessionLegacyStub.convertCommandToPlaybackStateActions, in PlaybackState's values.
        std::uint64_t PlaybackStateActions(PlayerCommand c, bool showPlay) {
            using C = PlayerCommand;
            switch (c) {
                case C::PlayPause: return (showPlay ? 4u : 2u) | 512u;   // PLAY or PAUSE, PLAY_PAUSE
                case C::Prepare: return 16384;
                case C::SeekBack: return 8;
                case C::SeekForward: return 64;
                case C::SeekInCurrentMediaItem: return 256;
                case C::SeekToMediaItem: return 4096;
                case C::SeekToNext:
                case C::SeekToNextMediaItem: return 32;
                case C::SeekToPrevious:
                case C::SeekToPreviousMediaItem: return 16;
                case C::SetMediaItem: return 1024 | 2048 | 8192 | 32768 | 65536 | 131072;   // play / prepare from id, search, uri
                case C::SetRepeatMode: return 262144;
                case C::SetSpeedAndPitch: return 4194304;
                case C::SetShuffleMode: return 2097152 | 524288;
                case C::Stop: return 1;
                default: return 0;
            }
        }
        bool ShouldShowPlayButton(const PlayerState& p) {
            return !p.playWhenReady || p.state == PlaybackState::Idle || p.state == PlaybackState::Ended;
        }
        // RatingCompat style of a rating (getRatingCompatStyle).
        int RatingStyle(const std::optional<Rating>& r) {
            if (!r) return 0;
            switch (r->kind) {
                case Rating::Kind::Heart: return 1;
                case Rating::Kind::Thumb: return 2;
                case Rating::Kind::Star: return r->maxStars >= 3 && r->maxStars <= 5 ? r->maxStars : 0;
                case Rating::Kind::Percentage: return 6;
            }
            return 0;
        }
        // LegacyConversions.convertToMediaDescriptionCompat's texts.
        QueueEntry Describe(const MediaItem& item) {
            const MediaMetadata& m = item.mediaMetadata;
            QueueEntry e;
            e.mediaId = item.mediaId;
            e.iconUri = m.artworkUri;
            if (!m.displayTitle.empty()) {
                e.title = m.displayTitle;
                e.subtitle = m.subtitle;
                e.description = m.description;
                return e;
            }
            // PREFERRED_DESCRIPTION_ORDER: the first three texts set.
            const std::string* order[] = { &m.title, &m.artist, &m.albumTitle, &m.albumArtist, &m.writer, &m.author, &m.composer, &m.subtitle, &m.description };
            std::string* texts[] = { &e.title, &e.subtitle, &e.description };
            std::size_t n = 0;
            for (const std::string* t : order)
                if (n < 3 && !t->empty()) *texts[n++] = *t;
            return e;
        }
    }

    void OnMediaSessionCustomAction(int id, const std::string& action) {
        auto it = R().sessions.find(id);
        if (it == R().sessions.end()) return;
        MediaSession& s = *it->second;
        // convertFromPredefinedCustomCommand: a player command from the custom layout.
        if (action.starts_with(kPlayerCommandPrefix)) {
            int c = 0;
            for (char ch : action.substr(kPlayerCommandPrefix.size())) {
                if (ch < '0' || ch > '9') return;
                c = c * 10 + (ch - '0');
            }
            s.OnPlayerCommand(PlayerCommand(c));
            return;
        }
        if (s.m_Options.onCustomCommand) s.m_Options.onCustomCommand(action);
        s.Publish();
    }
    void OnMediaSessionSetRating(int id, const Rating& rating) {
        auto it = R().sessions.find(id);
        if (it != R().sessions.end() && it->second->m_Options.onSetRating) it->second->m_Options.onSetRating(rating);
    }
    void OnMediaSessionMediaRequest(int id, const std::string& mediaId, const std::string& uri, const std::string& query, bool play) {
        auto it = R().sessions.find(id);
        if (it == R().sessions.end()) return;
        MediaItem item;
        item.mediaId = mediaId;
        item.requestMetadata.mediaUri = uri;
        item.requestMetadata.searchQuery = query;
        it->second->OnMediaRequest(std::move(item), play);
    }

    void MediaSession::OnMediaRequest(MediaItem item, bool play) {
        // handleMediaRequest: onAddMediaItems, then set, prepare (or restart when ended), play.
        if (!m_Options.onAddMediaItems) return;   // media3's default refuses
        auto items = m_Options.onAddMediaItems({ std::move(item) });
        if (!items) return;
        m_Player.SetMediaItems(std::move(*items));
        if (m_Player.State().state == PlaybackState::Idle) m_Player.Prepare();
        else if (m_Player.State().state == PlaybackState::Ended) m_Player.SeekToDefaultPosition();
        if (play) m_Player.Play();
        Publish();
    }

    void MediaSession::OnPlayerCommand(PlayerCommand c) {
        using C = PlayerCommand;
        switch (c) {
            case C::PlayPause:
                OnCommand(ShouldShowPlayButton(m_Player.State()) ? MediaSessionCommand::Play : MediaSessionCommand::Pause, 0, 0);
                return;
            case C::SeekToPrevious:
            case C::SeekToPreviousMediaItem: OnCommand(MediaSessionCommand::SkipToPrevious, 0, 0); return;
            case C::SeekToNext:
            case C::SeekToNextMediaItem: OnCommand(MediaSessionCommand::SkipToNext, 0, 0); return;
            case C::SeekBack: m_Player.SeekBack(); break;
            case C::SeekForward: m_Player.SeekForward(); break;
            case C::Stop: m_Player.Stop(); break;
            case C::SeekToDefaultPosition: m_Player.SeekToDefaultPosition(); break;
            case C::SetShuffleMode: m_Player.SetShuffleModeEnabled(!m_Player.ShuffleModeEnabled()); break;
            case C::SetRepeatMode: {
                // RepeatModeUtil.getNextRepeatMode: off, one, all.
                const RepeatMode r = m_Player.GetRepeatMode();
                m_Player.SetRepeatMode(r == RepeatMode::Off ? RepeatMode::One : r == RepeatMode::One ? RepeatMode::All : RepeatMode::Off);
                break;
            }
            default: break;
        }
        Publish();
    }

    void MediaSession::SetMediaButtonPreferences(std::vector<CommandButton> buttons) {
        m_Buttons = std::move(buttons);
        Publish();
    }
    void MediaSession::SetSessionActivity(std::string uri) {
        m_Options.sessionActivityUri = std::move(uri);
        Publish();
    }
    void MediaSession::SetSessionExtras(std::map<std::string, std::string> extras) {
        m_Extras = std::move(extras);
        Publish();
    }

    void OnMediaSessionCommand(int id, MediaSessionCommand command, std::int64_t positionMs, float speed) {
        auto it = R().sessions.find(id);
        if (it != R().sessions.end()) it->second->OnCommand(command, positionMs, speed);
    }

    MediaSession::MediaSession(Player& player, MediaSessionOptions options)
        : m_Player(player), m_Options(std::move(options)), m_Id(R().next++) {
        R().sessions[m_Id] = this;
        m_State = rt::Launch(rt::Dispatcher::Main(), m_Player.StateFlow().AsFlow().Collect([this](const PlayerState&) { Publish(); }));
        Publish();
    }

    MediaSession::~MediaSession() {
        m_State.Cancel();
        m_Timeout.Cancel();
        R().sessions.erase(m_Id);
        if (m_Published && R().host.release) R().host.release(m_Id);
    }

    void MediaSession::SetMetadata(MediaMetadata metadata) {
        if (metadata == m_Metadata) return;
        m_Metadata = std::move(metadata);
        Publish();
    }

    void MediaSession::Publish() {
        const PlayerState& p = m_Player.State();
        MediaSessionSnapshot s;
        // The current item's metadata (Player.getMediaMetadata), with SetMetadata's over it.
        s.metadata = p.mediaMetadata;
        s.metadata.Populate(m_Metadata);
        s.state = p.state;
        s.playWhenReady = p.playWhenReady;
        s.isPlaying = p.isPlaying;
        s.positionMs = p.positionMs;
        s.durationMs = p.durationMs;
        s.bufferedPositionMs = p.bufferedPositionMs;
        s.playbackSpeed = p.playbackParameters.speed;
        s.activeQueueItemId = p.mediaItemCount > 0 ? p.currentMediaItemIndex : -1;
        s.mediaId = p.currentMediaId;
        s.error = p.error;
        // Util.getAvailableCommands for on-demand media: previous whenever there is an item (it
        // restarts the item), next when there is a next item; the app's handlers add the buttons.
        s.canSkipToPrevious = bool(m_Options.onSkipToPrevious) || p.mediaItemCount > 0;
        s.canSkipToNext = bool(m_Options.onSkipToNext) || p.hasNextMediaItem;
        s.canSeek = p.mediaItemCount > 0;
        s.sessionActivityUri = m_Options.sessionActivityUri;

        // The custom layout (PlaybackState custom actions), and the notification's buttons
        // (DefaultMediaNotificationProvider.getMediaButtons, addNotificationActions).
        const std::vector<CommandButton> layout = CustomLayout(m_Buttons, p);
        for (const auto& b : layout) {
            if (b.slots.front() == CommandButton::kSlotBack) s.customBack = true;
            if (b.slots.front() == CommandButton::kSlotForward) s.customForward = true;
            if (!b.customAction.empty()) s.customActions.push_back(ToSession(b));
        }
        std::vector<CommandButton> buttons;
        std::size_t next = 0;
        if (s.customBack) buttons.push_back(layout[next++]);
        else if (s.canSkipToPrevious)
            buttons.push_back({ .icon = CommandButton::Previous, .displayName = "Previous", .playerCommand = PlayerCommand::SeekToPreviousMediaItem });
        const bool showPause = !ShouldShowPlayButton(p);
        buttons.push_back({ .icon = showPause ? CommandButton::Pause : CommandButton::Play, .displayName = showPause ? "Pause" : "Play",
                            .playerCommand = PlayerCommand::PlayPause });
        if (s.customForward) buttons.push_back(layout[next++]);
        else if (s.canSkipToNext)
            buttons.push_back({ .icon = CommandButton::Next, .displayName = "Next", .playerCommand = PlayerCommand::SeekToNextMediaItem });
        for (std::size_t i = next; i < layout.size(); ++i) buttons.push_back(layout[i]);
        int compact[3] = { -1, -1, -1 }, byIndex[3] = { -1, -1, -1 };
        bool customCompact = false;
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            s.notificationButtons.push_back(ToSession(buttons[i]));
            const int slot = buttons[i].FirstSlot();
            const int index = buttons[i].compactViewIndex;
            if (index >= 0 && index < 3) {
                customCompact = true;
                byIndex[index] = int(i);
            } else if (slot == CommandButton::kSlotBack) compact[0] = int(i);
            else if (slot == CommandButton::kSlotCentral) compact[1] = int(i);
            else if (slot == CommandButton::kSlotForward) compact[2] = int(i);
        }
        if (customCompact) {
            for (int c : byIndex) {   // up to the first index not given
                if (c < 0) break;
                s.compactView.push_back(c);
            }
        } else {
            for (int c : compact) if (c >= 0) s.compactView.push_back(c);
        }

        s.actions = 128;   // SET_RATING, always
        const bool showPlay = !p.error.empty() || ShouldShowPlayButton(p);
        for (int c = 1; c < 64; ++c)
            if (p.availableCommands.Contains(PlayerCommand(c))) s.actions |= PlaybackStateActions(PlayerCommand(c), showPlay);
        if (m_Options.onSkipToPrevious) s.actions |= 16;
        if (m_Options.onSkipToNext) s.actions |= 32;
        if (!m_Buttons.empty() && s.customBack) s.actions &= ~std::uint64_t(16);
        if (!m_Buttons.empty() && s.customForward) s.actions &= ~std::uint64_t(32);

        // The queue (with COMMAND_GET_TIMELINE) and its title; the rating kind; extras.
        for (int i = 0; i < m_Player.MediaItemCount(); ++i) s.queue.push_back(Describe(m_Player.MediaItemAt(i)));
        s.queueTitle = m_Player.PlaylistMetadata().title;
        s.ratingType = p.mediaItemCount > 0 ? RatingStyle(s.metadata.userRating) : 0;
        s.userRating = s.metadata.userRating;
        s.extras = m_Extras;

        // shouldShowNotification, SHOW_NOTIFICATION_FOR_IDLE_PLAYER_AFTER_STOP_OR_ERROR (the
        // default): while there is media, and when idle only after it was prepared and until the
        // user dismisses it.
        if (p.state != PlaybackState::Idle) {
            m_WasDismissed = false;
            m_HasBeenPrepared = true;
            s.showNotification = true;
        } else {
            s.showNotification = !m_WasDismissed && m_HasBeenPrepared;
        }
        s.showNotification = s.showNotification && m_Options.notification;

        // shouldRunInForeground: while the user is engaged (playing, or about to), and for the
        // timeout after they stop.
        const bool engaged = p.playWhenReady && (p.state == PlaybackState::Ready || p.state == PlaybackState::Buffering);
        if (m_UserEngaged && !engaged) {
            m_TimeoutPending = true;
            m_Timeout = rt::Launch(rt::Dispatcher::Main(), [](MediaSession* self) -> rt::Task<> {
                co_await rt::Delay(kForegroundServiceTimeoutMs);
                self->m_TimeoutPending = false;
                self->Publish();
            }(this));
        } else if (engaged) {
            m_Timeout.Cancel();
            m_TimeoutPending = false;
        }
        m_UserEngaged = engaged;
        s.foreground = s.showNotification && (engaged || m_TimeoutPending);

        // The position moves on by itself while playing (the platform extrapolates it from the
        // last update); only a jump from there (a seek) is sent.
        const auto now = rt::Dispatcher::Main().Now();
        if (m_Published) {
            MediaSessionSnapshot a = s, b = m_Last;
            a.positionMs = b.positionMs = 0;
            a.bufferedPositionMs = b.bufferedPositionMs = 0;
            if (a == b) {
                const std::int64_t elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - m_LastAt).count();
                const std::int64_t expected = m_Last.positionMs + (m_Last.isPlaying ? std::int64_t(double(elapsed) * m_Last.playbackSpeed) : 0);
                if (std::llabs(s.positionMs - expected) <= (s.isPlaying ? 1000 : 0)) return;
            }
        }
        m_Last = s;
        m_LastAt = now;
        m_Published = true;
        if (R().host.update) R().host.update(m_Id, s);
    }

    void MediaSession::OnCommand(MediaSessionCommand c, std::int64_t positionMs, float speed) {
        switch (c) {
            case MediaSessionCommand::Play:
                // Util.handlePlayButtonAction: prepared when idle, from the start when ended.
                if (m_Player.State().state == PlaybackState::Idle) m_Player.Prepare();
                else if (m_Player.State().state == PlaybackState::Ended) m_Player.SeekToDefaultPosition();
                m_Player.Play();
                break;
            case MediaSessionCommand::Pause: m_Player.Pause(); break;
            case MediaSessionCommand::Stop: m_Player.Stop(); break;
            case MediaSessionCommand::SeekTo: m_Player.SeekTo(positionMs); break;
            case MediaSessionCommand::SkipToPrevious:
                if (m_Options.onSkipToPrevious) m_Options.onSkipToPrevious();
                else m_Player.SeekToPrevious();
                break;
            case MediaSessionCommand::SkipToNext:
                if (m_Options.onSkipToNext) m_Options.onSkipToNext();
                else m_Player.SeekToNext();
                break;
            case MediaSessionCommand::NotificationDismissed: m_WasDismissed = true; break;
            // MediaSessionLegacyStub's onPrepare, onRewind, onFastForward, onSkipToQueueItem and
            // onSetPlaybackSpeed.
            case MediaSessionCommand::Prepare: m_Player.Prepare(); break;
            case MediaSessionCommand::Rewind: m_Player.SeekBack(); break;
            case MediaSessionCommand::FastForward: m_Player.SeekForward(); break;
            case MediaSessionCommand::SkipToQueueItem:
                if (positionMs >= 0 && positionMs < m_Player.MediaItemCount()) m_Player.SeekToDefaultPosition(int(positionMs));
                break;
            case MediaSessionCommand::SetPlaybackSpeed:
                if (speed > 0) m_Player.SetPlaybackSpeed(speed);
                break;
            case MediaSessionCommand::PlayerCommand:
                OnPlayerCommand(PlayerCommand(int(positionMs)));
                return;
        }
        Publish();
    }

}
