#pragma once

// An RTSP presentation for a player: media3's RtspMediaSource and RtspMediaPeriod, with their
// RtpDataLoadable / RtpExtractor per track, RTP over UDP (UdpDataSourceRtpDataChannel) or
// interleaved in the RTSP connection (TransferRtpDataChannel, the fallback when UDP brings
// nothing or the server refuses it), and a SampleQueue per track the player reads.

#include "kiln/media/rtsp/RtpReaders.h"
#include "kiln/media/rtsp/RtspClient.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace kiln::media::rtsp {

    // SampleQueue, as the session uses it: formats and samples in order, read once.
    class SampleQueue : public TrackOutput {
    public:
        struct Item {
            enum class Kind { Format, Sample } kind = Kind::Sample;
            Format format;
            std::int64_t timeUs = 0;
            int flags = 0;
            std::vector<std::uint8_t> data;
        };
        void OnFormat(const Format& format) override;
        void SampleData(codec::ParsableByteArray& data, int length) override;
        void SampleMetadata(std::int64_t timeUs, int flags, int size, int offset) override;

        std::optional<Item> Read();
        bool HasItems() const;
        std::optional<Format> UpstreamFormat() const;
        // getLargestQueuedTimestampUs: INT64_MIN when nothing was queued since the last reset.
        std::int64_t LargestQueuedTimestampUs() const;
        // Moves to the key frame at or before the position among the samples not read yet.
        bool SeekTo(std::int64_t positionUs, bool allowTimeBeyondBuffer);
        // Drops what is queued (the upstream format is given again first).
        void Reset();

    private:
        mutable std::mutex m_Mutex;
        std::deque<Item> m_Items;
        std::vector<std::uint8_t> m_Pending;
        std::optional<Format> m_UpstreamFormat;
        bool m_FormatGiven = false;
        std::int64_t m_Largest = INT64_MIN;
    };

    class RtspSession : RtspClient::SessionInfoListener, RtspClient::PlaybackEventListener {
    public:
        struct Options {
            std::int64_t timeoutMs = 8000;   // RtspMediaSource.DEFAULT_TIMEOUT_MS: no RTP for this long ends a track
            bool forceUseRtpTcp = false;     // also when the URI's scheme is rtspt
            std::string userAgent = "kiln";
            RtspClient::SocketFactory socketFactory;
            bool debugLoggingEnabled = false;   // the RTSP messages in the log
        };
        struct Timeline {
            bool known = false;              // after DESCRIBE (a placeholder before)
            std::int64_t durationUs = kTimeUnset;
            bool seekable = false, live = false;
        };

        explicit RtspSession(std::string uri, Options options);
        explicit RtspSession(std::string uri) : RtspSession(std::move(uri), Options()) {}
        ~RtspSession() override;

        // RtspMediaPeriod.prepare: connects and describes; IsPrepared or Error tell how it went.
        void Prepare();
        bool IsPrepared() const;
        // The preparation's or playback's failure, if any.
        std::optional<std::string> Error() const;
        Timeline GetTimeline() const;
        // The tracks (a group each) once prepared.
        std::vector<Format> TrackFormats() const;
        // selectTracks: SETUP for these tracks, then PLAY from the position.
        void SelectTracks(const std::vector<int>& tracks, std::int64_t positionUs);
        // The track's next format or sample; nullopt while there is none.
        std::optional<SampleQueue::Item> Read(int track);
        // Its loading ended and everything was read.
        bool IsEnded(int track) const;
        // Something is queued for it.
        bool HasSamples(int track) const;
        bool IsLoading() const;
        std::int64_t BufferedPositionUs() const;
        void SeekToUs(std::int64_t positionUs);
        // readDiscontinuity: a seek the server could not honour (it said no RTP-Info).
        bool ReadDiscontinuity();

    private:
        class DataChannel;
        class UdpDataChannel;
        class TransferDataChannel;
        struct Loader;
        struct LoaderWrapper;

        // SessionInfoListener / PlaybackEventListener (on the handler).
        void OnSessionTimelineUpdated(const RtspSessionTiming& timing, const std::vector<RtspMediaTrack>& tracks) override;
        void OnSessionTimelineRequestFailed(const std::string& message) override;
        void OnRtspSetupCompleted() override;
        void OnPlaybackStarted(std::int64_t startPositionUs, const std::vector<RtspTrackTiming>& trackTimings) override;
        void OnPlaybackError(const std::string& message, bool udpUnsupported) override;
        // Loader callbacks (on the handler).
        void OnLoadCompleted(LoaderWrapper* wrapper);
        void OnLoadError(LoaderWrapper* wrapper, const std::string& message, bool bindFailure);
        void OnTransportReady(LoaderWrapper* wrapper);

        std::shared_ptr<DataChannel> CreateDataChannel(int trackId, bool tcp);
        std::shared_ptr<LoaderWrapper> NewWrapper(const RtspMediaTrack& track, int trackId, bool tcp);
        void StartLoading(const std::shared_ptr<LoaderWrapper>& wrapper);
        void MaybeFinishPrepare();
        void MaybeSetupTracks();
        void UpdateLoadingFinished();
        void RetryWithRtpTcp();
        std::int64_t BufferedPositionUsLocked() const;
        bool SeekInsideBufferUs(std::int64_t positionUs);
        bool IsSeekPending() const { return m_PendingSeekPositionUs != kTimeUnset; }
        void SeekToUsOnHandler(std::int64_t positionUs);
        void OnSeekingUnsupported();

        std::string m_Uri;
        Options m_Options;
        TaskRunner m_Handler;
        std::unique_ptr<RtspClient> m_Client;
        // Held by every handler task and by the player's calls (the client calls back on the same thread).
        mutable std::recursive_mutex m_Mutex;
        std::vector<std::shared_ptr<LoaderWrapper>> m_Wrappers;
        std::vector<std::shared_ptr<LoaderWrapper>> m_Retired;   // replaced by a TCP retry, stopped at the end
        std::vector<int> m_SelectedTracks;
        std::optional<std::string> m_PreparationError, m_PlaybackError;
        Timeline m_Timeline;
        std::vector<Format> m_TrackFormats;
        bool m_Prepared = false, m_TrackSelected = false, m_LoadingFinished = false, m_NotifyDiscontinuity = false, m_Released = false;
        bool m_UsingRtpTcp = false;
        int m_PortBindingRetryCount = 0;
        std::int64_t m_RequestedSeekPositionUs = kTimeUnset, m_PendingSeekPositionUs = kTimeUnset, m_PendingSeekPositionUsForTcpRetry = kTimeUnset;
    };

}
