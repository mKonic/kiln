#include "kiln/media/AudioFocus.h"

#include "kiln/Log.h"
#include "kiln/media/Player.h"

#include <map>
#include <stdexcept>

namespace kiln::media {

    namespace {
        struct Registry {
            AudioHost host;
            int next = 1;
            std::map<int, AudioFocusManager*> focus;
            std::map<int, kiln::Function<void()>*> noisy;
            std::map<int, StreamVolumeManager*> volume;
        };
        Registry& R() {
            static Registry* r = new Registry;
            return *r;
        }
    }

    void SetAudioHost(AudioHost host) { R().host = std::move(host); }

    void OnAudioFocusChange(int id, int change) {
        auto it = R().focus.find(id);
        if (it != R().focus.end()) it->second->OnFocusChange(change);
    }
    void OnAudioBecomingNoisy(int id) {
        auto it = R().noisy.find(id);
        if (it != R().noisy.end() && *it->second) (*it->second)();
    }

    int NewAudioClientId() { return R().next++; }
    void SetPlaybackLocks(int id, bool wake, bool wifi) {
        if (R().host.setLocks) R().host.setLocks(id, wake, wifi);
    }

    void OnStreamVolumeChanged(int id) {
        auto it = R().volume.find(id);
        if (it != R().volume.end()) it->second->OnSystemChange();
    }

    int VolumeControlStream(const AudioAttributes& a) {
        switch (a.usage) {
            case AudioUsage::Media:
            case AudioUsage::Game:
            case AudioUsage::AssistanceNavigationGuidance: return kStreamMusic;
            case AudioUsage::AssistanceSonification: return kStreamSystem;
            case AudioUsage::VoiceCommunication: return kStreamVoiceCall;
            case AudioUsage::VoiceCommunicationSignalling: return kStreamDtmf;
            case AudioUsage::Alarm: return kStreamAlarm;
            case AudioUsage::NotificationRingtone: return kStreamRing;
            case AudioUsage::Notification:
            case AudioUsage::NotificationCommunicationRequest:
            case AudioUsage::NotificationCommunicationInstant:
            case AudioUsage::NotificationCommunicationDelayed:
            case AudioUsage::NotificationEvent: return kStreamNotification;
            case AudioUsage::AssistanceAccessibility: return kStreamAccessibility;
            default: return kStreamMusic;   // C.STREAM_TYPE_DEFAULT
        }
    }

    // --- StreamVolumeManager ------------------------------------------------------------------

    StreamVolumeManager::StreamVolumeManager(int streamType, kiln::Function<void(int, bool)> onVolume, kiln::Function<void(int)> onStreamType)
        : m_OnVolume(std::move(onVolume)), m_OnStreamType(std::move(onStreamType)), m_Id(R().next++) {
        R().volume[m_Id] = this;
        if (R().host.setVolumeChangedReceiver) R().host.setVolumeChangedReceiver(m_Id, true);
        m_State = Generate(streamType);
    }
    StreamVolumeManager::~StreamVolumeManager() {
        R().volume.erase(m_Id);
        if (R().host.setVolumeChangedReceiver) R().host.setVolumeChangedReceiver(m_Id, false);
    }
    StreamVolumeManager::State StreamVolumeManager::Generate(int streamType) const {
        State s;
        s.streamType = streamType;
        if (R().host.streamVolume) s.volume = R().host.streamVolume(streamType);
        return s;
    }
    void StreamVolumeManager::Apply(State next) {
        const State old = m_State;
        m_State = next;
        // onStreamVolumeStateChanged.
        if (!old.volume.muted && next.volume.muted) m_VolumeBeforeMute = old.volume.volume;
        if (old.volume.volume != next.volume.volume || old.volume.muted != next.volume.muted)
            if (m_OnVolume) m_OnVolume(next.volume.volume, next.volume.muted);
        if (old.streamType != next.streamType || old.volume.minVolume != next.volume.minVolume || old.volume.maxVolume != next.volume.maxVolume)
            if (m_OnStreamType) m_OnStreamType(next.streamType);
    }
    void StreamVolumeManager::SetStreamType(int streamType) {
        if (streamType != m_State.streamType) Apply(Generate(streamType));
    }
    void StreamVolumeManager::SetVolume(int volume, int flags) {
        const auto& v = m_State.volume;
        if (volume == v.volume || volume < v.minVolume || volume > v.maxVolume) return;
        if (R().host.setStreamVolume) R().host.setStreamVolume(m_State.streamType, volume, flags);
        Apply(Generate(m_State.streamType));
    }
    void StreamVolumeManager::IncreaseVolume(int flags) {
        if (m_State.volume.volume >= m_State.volume.maxVolume) return;
        if (R().host.adjustStreamVolume) R().host.adjustStreamVolume(m_State.streamType, kAdjustRaise, flags);
        Apply(Generate(m_State.streamType));
    }
    void StreamVolumeManager::DecreaseVolume(int flags) {
        if (m_State.volume.volume <= m_State.volume.minVolume) return;
        if (R().host.adjustStreamVolume) R().host.adjustStreamVolume(m_State.streamType, kAdjustLower, flags);
        Apply(Generate(m_State.streamType));
    }
    void StreamVolumeManager::SetMuted(bool muted, int flags) {
        if (m_State.volume.muted == muted) return;
        if (R().host.adjustStreamVolume) R().host.adjustStreamVolume(m_State.streamType, muted ? kAdjustMute : kAdjustUnmute, flags);
        Apply(Generate(m_State.streamType));
    }
    void StreamVolumeManager::OnSystemChange() { Apply(Generate(m_State.streamType)); }

    int FocusGainFor(const std::optional<AudioAttributes>& a) {
        // Video-only content, or an app that handles focus itself.
        if (!a) return kAudioFocusNone;
        switch (a->usage) {
            case AudioUsage::VoiceCommunicationSignalling: return kAudioFocusNone;   // DTMF during a call
            case AudioUsage::Game:
            case AudioUsage::Media: return kAudioFocusGain;
            case AudioUsage::Unknown:
                log::Warn("kiln", "audio focus: specify a proper usage in the audio attributes; using AUDIOFOCUS_GAIN");
                return kAudioFocusGain;
            case AudioUsage::Alarm:
            case AudioUsage::VoiceCommunication: return kAudioFocusGainTransient;
            case AudioUsage::AssistanceNavigationGuidance:
            case AudioUsage::AssistanceSonification:
            case AudioUsage::Notification:
            case AudioUsage::NotificationCommunicationDelayed:
            case AudioUsage::NotificationCommunicationInstant:
            case AudioUsage::NotificationCommunicationRequest:
            case AudioUsage::NotificationEvent:
            case AudioUsage::NotificationRingtone: return kAudioFocusGainTransientMayDuck;
            case AudioUsage::Assistant: return kAudioFocusGainTransientExclusive;
            case AudioUsage::AssistanceAccessibility:
                // Voice shouldn't be interrupted by other playback.
                return a->contentType == AudioContentType::Speech ? kAudioFocusGainTransient : kAudioFocusGainTransientMayDuck;
        }
        return kAudioFocusNone;
    }

    AudioFocusManager::AudioFocusManager(Control control) : m_Control(std::move(control)), m_Id(R().next++) { R().focus[m_Id] = this; }

    AudioFocusManager::~AudioFocusManager() {
        Release();
        R().focus.erase(m_Id);
    }

    void AudioFocusManager::SetAudioAttributes(std::optional<AudioAttributes> attributes) {
        if (m_Attributes == attributes) return;
        const int gain = FocusGainFor(attributes);
        if (gain != kAudioFocusGain && gain != kAudioFocusNone)
            throw std::invalid_argument("Automatic handling of audio focus is only available for USAGE_MEDIA and USAGE_GAME.");
        m_Attributes = attributes;
        m_FocusGain = gain;
    }

    AudioFocusManager::PlayerCommand AudioFocusManager::UpdateAudioFocus(bool playWhenReady, PlaybackState state) {
        // shouldHandleAudioFocus
        if (state == PlaybackState::Idle || m_FocusGain != kAudioFocusGain) {
            AbandonAudioFocusIfHeld();
            SetState(State::NotRequested);
            return PlayerCommand::PlayWhenReady;
        }
        if (playWhenReady) return RequestAudioFocus();
        switch (m_State) {
            case State::NoFocus: return PlayerCommand::DoNotPlay;
            case State::LossTransient: return PlayerCommand::WaitForCallback;
            default: return PlayerCommand::PlayWhenReady;
        }
    }

    void AudioFocusManager::Release() {
        m_Control = {};   // the player is going: its volume is not restored
        AbandonAudioFocusIfHeld();
        SetState(State::NotRequested);
    }

    AudioFocusManager::PlayerCommand AudioFocusManager::RequestAudioFocus() {
        if (m_State == State::HaveFocus) return PlayerCommand::PlayWhenReady;
        auto& host = R().host;
        const int result = host.requestFocus ? host.requestFocus(m_Id, m_FocusGain, *m_Attributes, WillPauseWhenDucked()) : kAudioFocusRequestGranted;
        if (result == kAudioFocusRequestGranted || result == kAudioFocusRequestDelayed) {
            SetState(State::HaveFocus);
            return PlayerCommand::PlayWhenReady;
        }
        SetState(State::NoFocus);
        return PlayerCommand::DoNotPlay;
    }

    void AudioFocusManager::AbandonAudioFocusIfHeld() {
        if (m_State == State::NoFocus || m_State == State::NotRequested) return;
        if (R().host.abandonFocus) R().host.abandonFocus(m_Id);
    }

    bool AudioFocusManager::WillPauseWhenDucked() const { return m_Attributes && m_Attributes->contentType == AudioContentType::Speech; }

    void AudioFocusManager::SetState(State s) {
        if (m_State == s) return;
        m_State = s;
        const float multiplier = s == State::LossTransientDuck ? kVolumeMultiplierDuck : 1.0f;
        if (m_VolumeMultiplier == multiplier) return;
        m_VolumeMultiplier = multiplier;
        if (m_Control.setVolumeMultiplier) m_Control.setVolumeMultiplier(multiplier);
    }

    void AudioFocusManager::Execute(PlayerCommand c) {
        if (m_Control.executePlayerCommand) m_Control.executePlayerCommand(c);
    }

    void AudioFocusManager::OnFocusChange(int change) {
        switch (change) {
            case kAudioFocusGain:
                SetState(State::HaveFocus);
                Execute(PlayerCommand::PlayWhenReady);
                return;
            case kAudioFocusLoss:
                Execute(PlayerCommand::DoNotPlay);
                AbandonAudioFocusIfHeld();
                SetState(State::NoFocus);
                return;
            case kAudioFocusLossTransient:
            case kAudioFocusLossTransientCanDuck:
                if (change == kAudioFocusLossTransient || WillPauseWhenDucked()) {
                    Execute(PlayerCommand::WaitForCallback);
                    SetState(State::LossTransient);
                } else {
                    SetState(State::LossTransientDuck);
                }
                return;
            default:
                log::Warn("kiln", "audio focus: unknown focus change {}", change);
        }
    }

    AudioBecomingNoisyManager::AudioBecomingNoisyManager(kiln::Function<void()> listener) : m_Listener(std::move(listener)), m_Id(R().next++) {
        R().noisy[m_Id] = &m_Listener;
    }
    AudioBecomingNoisyManager::~AudioBecomingNoisyManager() {
        SetEnabled(false);
        R().noisy.erase(m_Id);
    }
    void AudioBecomingNoisyManager::SetEnabled(bool enabled) {
        if (enabled == m_Enabled) return;
        m_Enabled = enabled;
        if (R().host.setNoisyReceiver) R().host.setNoisyReceiver(m_Id, enabled);
    }

}
