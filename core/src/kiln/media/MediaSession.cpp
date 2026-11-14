#include "kiln/media/MediaSession.h"

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
        }
        Publish();
    }

}
