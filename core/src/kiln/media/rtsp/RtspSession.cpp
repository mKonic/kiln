// media3 exoplayer-rtsp: RtspMediaSource / RtspMediaPeriod, RtpDataLoadable, RtpExtractor, the
// RTP data channels, and SampleQueue as the player reads it.
#include "kiln/media/rtsp/RtspSession.h"

#include <algorithm>
#include <chrono>
#include <future>

namespace kiln::media::rtsp {

    using namespace detail;

    namespace {
        std::int64_t ElapsedRealtimeMs() {
            return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        // Uri.getPath.
        std::string UriPath(const std::string& uri) {
            std::string s = uri.substr(0, uri.find_first_of("?#"));
            if (const auto scheme = s.find("://"); scheme != std::string::npos) {
                const auto slash = s.find('/', scheme + 3);
                return slash == std::string::npos ? "" : s.substr(slash);
            }
            return s;
        }
    }

    // --- SampleQueue ----------------------------------------------------------------------------
    void SampleQueue::OnFormat(const Format& format) {
        std::lock_guard lock(m_Mutex);
        if (m_UpstreamFormat && *m_UpstreamFormat == format) return;   // deduplicated, as upstream's
        m_UpstreamFormat = format;
        m_Items.push_back({ Item::Kind::Format, format, 0, 0, {} });
        m_FormatGiven = true;
    }

    void SampleQueue::SampleData(codec::ParsableByteArray& data, int length) {
        std::lock_guard lock(m_Mutex);
        const std::size_t at = m_Pending.size();
        m_Pending.resize(at + std::size_t(length));
        data.ReadBytes(m_Pending.data() + at, length);
    }

    void SampleQueue::SampleMetadata(std::int64_t timeUs, int flags, int size, int offset) {
        std::lock_guard lock(m_Mutex);
        if (std::size_t(size) + std::size_t(offset) > m_Pending.size()) return;
        const std::size_t end = m_Pending.size() - std::size_t(offset), start = end - std::size_t(size);
        if (!m_FormatGiven && m_UpstreamFormat) {
            m_Items.push_back({ Item::Kind::Format, *m_UpstreamFormat, 0, 0, {} });
            m_FormatGiven = true;
        }
        m_Items.push_back({ Item::Kind::Sample, {}, timeUs, flags, std::vector<std::uint8_t>(m_Pending.begin() + std::ptrdiff_t(start), m_Pending.begin() + std::ptrdiff_t(end)) });
        m_Pending.erase(m_Pending.begin(), m_Pending.begin() + std::ptrdiff_t(end));
        m_Largest = std::max(m_Largest, timeUs);
    }

    std::optional<SampleQueue::Item> SampleQueue::Read() {
        std::lock_guard lock(m_Mutex);
        if (m_Items.empty()) return std::nullopt;
        Item item = std::move(m_Items.front());
        m_Items.pop_front();
        return item;
    }

    bool SampleQueue::HasItems() const {
        std::lock_guard lock(m_Mutex);
        return !m_Items.empty();
    }

    std::optional<Format> SampleQueue::UpstreamFormat() const {
        std::lock_guard lock(m_Mutex);
        return m_UpstreamFormat;
    }

    std::int64_t SampleQueue::LargestQueuedTimestampUs() const {
        std::lock_guard lock(m_Mutex);
        return m_Largest;
    }

    bool SampleQueue::SeekTo(std::int64_t positionUs, bool allowTimeBeyondBuffer) {
        std::lock_guard lock(m_Mutex);
        std::ptrdiff_t first = -1, last = -1, keyAtOrBefore = -1;
        for (std::size_t i = 0; i < m_Items.size(); ++i) {
            if (m_Items[i].kind != Item::Kind::Sample) continue;
            if (first < 0) first = std::ptrdiff_t(i);
            last = std::ptrdiff_t(i);
            if (m_Items[i].timeUs <= positionUs && (m_Items[i].flags & kBufferFlagKeyFrame)) keyAtOrBefore = std::ptrdiff_t(i);
        }
        if (first < 0 || positionUs < m_Items[std::size_t(first)].timeUs) return false;
        if (positionUs > m_Items[std::size_t(last)].timeUs && !allowTimeBeyondBuffer) return false;
        if (keyAtOrBefore < 0) return false;
        // The samples before the key frame go; formats before it stay in effect (the last one).
        std::optional<Item> format;
        for (std::ptrdiff_t i = 0; i < keyAtOrBefore; ++i)
            if (m_Items[std::size_t(i)].kind == Item::Kind::Format) format = m_Items[std::size_t(i)];
        m_Items.erase(m_Items.begin(), m_Items.begin() + keyAtOrBefore);
        if (format) m_Items.push_front(*format);
        return true;
    }

    void SampleQueue::Reset() {
        std::lock_guard lock(m_Mutex);
        m_Items.clear();
        m_Pending.clear();
        m_Largest = INT64_MIN;
        m_FormatGiven = false;
    }

    // --- data channels --------------------------------------------------------------------------
    class RtspSession::DataChannel {
    public:
        virtual ~DataChannel() = default;
        virtual std::string Transport() const = 0;
        virtual int LocalPort() const = 0;
        virtual bool NeedsClosingOnLoadCompletion() const = 0;
        virtual bool Interleaved() const { return false; }
        virtual void OnInterleavedBinaryData(std::vector<std::uint8_t>) {}
        // One packet: its size, -1 at the end of input (the timeout passed, or closed).
        virtual int Read(std::uint8_t* buffer, int capacity) = 0;
        virtual void Close() = 0;
    };

    // UdpDataSourceRtpDataChannel: RTP on an even port, its RTCP port beside it held open.
    class RtspSession::UdpDataChannel final : public DataChannel {
    public:
        explicit UdpDataChannel(std::int64_t timeoutMs) : m_TimeoutMs(int(timeoutMs)) {}
        static std::shared_ptr<DataChannel> CreateAndOpen(std::int64_t timeoutMs) {
            auto first = std::make_shared<UdpDataChannel>(timeoutMs);
            auto second = std::make_unique<UdpDataChannel>(timeoutMs);
            first->m_Socket.Bind(0);
            const int firstPort = first->LocalPort();
            const bool even = firstPort % 2 == 0;
            second->m_Socket.Bind(even ? firstPort + 1 : firstPort - 1);
            if (even) {
                first->m_Rtcp = std::move(second);
                return first;
            }
            // The second is the RTP channel; the first becomes its RTCP.
            auto rtp = std::shared_ptr<UdpDataChannel>(second.release());
            auto rtcp = std::make_unique<UdpDataChannel>(timeoutMs);
            rtcp->m_Socket.Close();
            first->m_Socket.Close();
            rtcp->m_Socket.Bind(firstPort);
            rtp->m_Rtcp = std::move(rtcp);
            return rtp;
        }
        std::string Transport() const override {
            const int port = LocalPort();
            return "RTP/AVP;unicast;client_port=" + std::to_string(port) + "-" + std::to_string(port + 1);
        }
        int LocalPort() const override { return m_Socket.LocalPort(); }
        bool NeedsClosingOnLoadCompletion() const override { return true; }
        int Read(std::uint8_t* buffer, int capacity) override {
            const int n = m_Socket.Receive(buffer, capacity, m_TimeoutMs);
            return n <= 0 ? -1 : n;   // a timeout ends the input
        }
        void Close() override {
            m_Socket.Close();
            if (m_Rtcp) m_Rtcp->Close();
        }

    private:
        int m_TimeoutMs;
        UdpSocket m_Socket;
        std::unique_ptr<UdpDataChannel> m_Rtcp;
    };

    // TransferRtpDataChannel: the packets the RTSP connection carries for its channel.
    class RtspSession::TransferDataChannel final : public DataChannel {
    public:
        TransferDataChannel(std::int64_t timeoutMs, int channel) : m_TimeoutMs(timeoutMs), m_Channel(channel) {}
        std::string Transport() const override {
            return "RTP/AVP/TCP;unicast;interleaved=" + std::to_string(m_Channel) + "-" + std::to_string(m_Channel + 1);
        }
        int LocalPort() const override { return m_Channel; }
        bool NeedsClosingOnLoadCompletion() const override { return false; }
        bool Interleaved() const override { return true; }
        void OnInterleavedBinaryData(std::vector<std::uint8_t> data) override {
            {
                std::lock_guard lock(m_Mutex);
                m_Packets.push_back(std::move(data));
            }
            m_Cv.notify_all();
        }
        int Read(std::uint8_t* buffer, int capacity) override {
            std::unique_lock lock(m_Mutex);
            if (!m_Cv.wait_for(lock, std::chrono::milliseconds(m_TimeoutMs), [this] { return !m_Packets.empty() || m_Closed; }) || m_Packets.empty()) return -1;
            std::vector<std::uint8_t> data = std::move(m_Packets.front());
            m_Packets.pop_front();
            const int n = std::min(capacity, int(data.size()));
            std::copy(data.begin(), data.begin() + n, buffer);
            return n;
        }
        void Close() override {
            {
                std::lock_guard lock(m_Mutex);
                m_Closed = true;
            }
            m_Cv.notify_all();
        }

    private:
        std::int64_t m_TimeoutMs;
        int m_Channel;
        std::mutex m_Mutex;
        std::condition_variable m_Cv;
        std::deque<std::vector<std::uint8_t>> m_Packets;
        bool m_Closed = false;
    };

    // --- a track's loader (RtspLoaderWrapper, RtpLoadInfo, RtpDataLoadable, RtpExtractor) -------
    struct RtspSession::LoaderWrapper {
        RtspMediaTrack track;
        int trackId;
        bool tcp;
        SampleQueue queue;
        // Handler state.
        std::string transport;
        bool canceled = false, released = false;
        // RtpDataLoadable.
        std::thread thread;
        std::atomic<bool> loadCancelled{ false }, quit{ false };
        std::atomic<std::int64_t> pendingSeekPositionUs{ kTimeUnset }, nextRtpTimestamp{ 0 };
        std::mutex channelMutex;
        std::shared_ptr<DataChannel> channel;
        // RtpExtractor.
        std::unique_ptr<RtpPayloadReader> reader;
        RtpPacketReorderingQueue reordering;
        std::atomic<bool> firstPacketRead{ false };
        std::atomic<std::int64_t> firstTimestamp{ kTimeUnset };
        std::atomic<int> firstSequenceNumber{ -1 };
        std::mutex extractorLock;
        bool seekPending = false;
        std::int64_t extractorNextRtpTimestamp = kTimeUnset, playbackStartTimeUs = kTimeUnset;

        LoaderWrapper(const RtspMediaTrack& t, int id, bool overTcp) : track(t), trackId(id), tcp(overTcp) {}
        ~LoaderWrapper() { Stop(); }
        void Stop() {
            quit = true;
            loadCancelled = true;
            {
                std::lock_guard lock(channelMutex);
                if (channel) channel->Close();
            }
            if (thread.joinable()) thread.join();
        }
        // RtpExtractor.read on one packet.
        void ExtractorRead(const std::uint8_t* data, int size) {
            std::optional<RtpPacket> packet = RtpPacket::Parse(data, size);
            if (!packet) return;
            const std::int64_t arrivalMs = ElapsedRealtimeMs();
            const std::int64_t cutoffMs = arrivalMs - 30;
            reordering.Offer(*packet, arrivalMs);
            std::optional<RtpPacket> next = reordering.Poll(cutoffMs);
            if (!next) return;
            if (!firstPacketRead) {
                if (firstTimestamp == kTimeUnset) firstTimestamp = next->timestamp;
                if (firstSequenceNumber == -1) firstSequenceNumber = next->sequenceNumber;
                reader->OnReceivingFirstPacket(firstTimestamp, firstSequenceNumber);
                firstPacketRead = true;
            }
            std::lock_guard lock(extractorLock);
            if (seekPending) {
                if (extractorNextRtpTimestamp != kTimeUnset && playbackStartTimeUs != kTimeUnset) {
                    reordering.Reset();
                    reader->Seek(extractorNextRtpTimestamp, playbackStartTimeUs);
                    seekPending = false;
                    extractorNextRtpTimestamp = playbackStartTimeUs = kTimeUnset;
                }
            } else {
                do {
                    codec::ParsableByteArray payload(next->payloadData);
                    reader->Consume(payload, next->timestamp, next->sequenceNumber, next->marker);
                    next = reordering.Poll(cutoffMs);
                } while (next);
            }
        }
        void ExtractorSeek(std::int64_t nextTimestamp, std::int64_t startTimeUs) {
            std::lock_guard lock(extractorLock);
            seekPending = true;
            extractorNextRtpTimestamp = nextTimestamp;
            playbackStartTimeUs = startTimeUs;
        }
        void PreSeek() {
            std::lock_guard lock(extractorLock);
            seekPending = true;
        }
    };

    // --- RtspSession ----------------------------------------------------------------------------
    RtspSession::RtspSession(std::string uri, Options options) : m_Options(std::move(options)) {
        // maybeConvertRtsptUriScheme: rtspt means RTP over TCP.
        if (uri.rfind("rtspt://", 0) == 0 || uri.rfind("RTSPT://", 0) == 0) {
            uri = "rtsp://" + uri.substr(8);
            m_Options.forceUseRtpTcp = true;
        }
        m_Uri = std::move(uri);
        RtspClient::SessionInfoListener& sessionInfo = *this;
        RtspClient::PlaybackEventListener& playbackEvents = *this;
        m_Client = std::make_unique<RtspClient>(m_Handler, sessionInfo, playbackEvents, m_Options.userAgent, m_Uri, m_Options.socketFactory, m_Options.debugLoggingEnabled);
    }

    RtspSession::~RtspSession() {
        // release: the loaders stop, the client tears down, then the handler.
        std::vector<std::shared_ptr<LoaderWrapper>> wrappers;
        m_Handler.RunAndWait([&] {
            std::lock_guard lock(m_Mutex);
            m_Released = true;
            wrappers = m_Wrappers;
            wrappers.insert(wrappers.end(), m_Retired.begin(), m_Retired.end());
            try {
                m_Client->Close();
            } catch (...) {
            }
        });
        for (auto& w : wrappers) w->Stop();
        m_Handler.Stop();
        m_Client.reset();
    }

    void RtspSession::Prepare() {
        m_Handler.Post([this] {
            std::lock_guard lock(m_Mutex);
            try {
                m_Client->Start();
            } catch (const std::exception& e) {
                m_PreparationError = e.what();
                try {
                    m_Client->Close();
                } catch (...) {
                }
            }
        });
    }

    bool RtspSession::IsPrepared() const {
        std::lock_guard lock(m_Mutex);
        return m_Prepared;
    }

    std::optional<std::string> RtspSession::Error() const {
        std::lock_guard lock(m_Mutex);
        return m_PlaybackError ? m_PlaybackError : m_PreparationError;
    }

    RtspSession::Timeline RtspSession::GetTimeline() const {
        std::lock_guard lock(m_Mutex);
        return m_Timeline;
    }

    std::vector<Format> RtspSession::TrackFormats() const {
        std::lock_guard lock(m_Mutex);
        return m_TrackFormats;
    }

    std::shared_ptr<RtspSession::LoaderWrapper> RtspSession::NewWrapper(const RtspMediaTrack& track, int trackId, bool tcp) {
        auto w = std::make_shared<LoaderWrapper>(track, trackId, tcp);
        StartLoading(w);
        return w;
    }

    // RtspLoaderWrapper.startLoading: the loadable runs on its own thread (again after it ended:
    // resumeLoad); its end is told to the handler.
    void RtspSession::StartLoading(const std::shared_ptr<LoaderWrapper>& w) {
        if (w->thread.joinable()) w->thread.join();   // a finished load
        std::weak_ptr<LoaderWrapper> weak = w;
        LoaderWrapper* wrapper = w.get();
        w->thread = std::thread([this, weak, wrapper] {
            for (;;) {
                std::string error;
                bool bindFailure = false;
                try {
                    // RtpDataLoadable.load.
                    wrapper->loadCancelled = false;
                    if (!wrapper->channel) {
                        auto channel = CreateDataChannel(wrapper->trackId, wrapper->tcp);
                        {
                            std::lock_guard lock(wrapper->channelMutex);
                            wrapper->channel = channel;
                        }
                        if (wrapper->quit) return;
                        wrapper->reader = RtpPayloadReader::Create(wrapper->track.payloadFormat);
                        if (!wrapper->reader) throw std::logic_error("no reader for " + wrapper->track.payloadFormat.mediaEncoding);
                        wrapper->reader->CreateTracks(wrapper->queue);
                        const std::string transport = channel->Transport();
                        m_Handler.Post([this, weak, transport] {
                            std::lock_guard lock(m_Mutex);
                            if (auto w = weak.lock(); w && !w->quit) {
                                w->transport = transport;
                                OnTransportReady(w.get());
                            }
                        });
                    }
                    std::vector<std::uint8_t> buffer(RtpPacket::MAX_SIZE);
                    while (!wrapper->loadCancelled) {
                        if (wrapper->pendingSeekPositionUs != kTimeUnset) {
                            wrapper->ExtractorSeek(wrapper->nextRtpTimestamp, wrapper->pendingSeekPositionUs);
                            wrapper->pendingSeekPositionUs = kTimeUnset;
                        }
                        std::shared_ptr<DataChannel> channel;
                        {
                            std::lock_guard lock(wrapper->channelMutex);
                            channel = wrapper->channel;
                        }
                        const int n = channel ? channel->Read(buffer.data(), int(buffer.size())) : -1;
                        if (n < 0) break;
                        wrapper->ExtractorRead(buffer.data(), n);
                    }
                    wrapper->loadCancelled = false;
                } catch (const RtspIoException& e) {
                    error = e.what();
                    bindFailure = e.bind;
                } catch (const std::exception& e) {
                    error = e.what();
                }
                {
                    std::lock_guard lock(wrapper->channelMutex);
                    if (wrapper->channel && wrapper->channel->NeedsClosingOnLoadCompletion()) {
                        wrapper->channel->Close();
                        wrapper->channel.reset();
                    }
                }
                if (wrapper->quit) return;
                // Loader's callbacks on the handler; a bind failure may retry (Loader.RETRY).
                auto retry = std::make_shared<std::promise<bool>>();
                auto retried = retry->get_future();
                m_Handler.Post([this, weak, error, bindFailure, retry] {
                    bool again = false;
                    {
                        std::lock_guard lock(m_Mutex);
                        if (auto w = weak.lock(); w && !w->quit) {
                            if (error.empty()) OnLoadCompleted(w.get());
                            else if (bindFailure && m_PortBindingRetryCount++ < 3) again = true;
                            else OnLoadError(w.get(), error, bindFailure);
                        }
                    }
                    retry->set_value(again);
                });
                while (retried.wait_for(std::chrono::milliseconds(100)) != std::future_status::ready)
                    if (wrapper->quit) return;
                if (!retried.get() || wrapper->quit) return;
            }
        });
    }

    // UdpDataSourceRtpDataChannelFactory / TransferRtpDataChannelFactory (two interleaved channels a
    // track).
    std::shared_ptr<RtspSession::DataChannel> RtspSession::CreateDataChannel(int trackId, bool tcp) {
        if (tcp) return std::make_shared<TransferDataChannel>(m_Options.timeoutMs, trackId * 2);
        return UdpDataChannel::CreateAndOpen(m_Options.timeoutMs);
    }

    void RtspSession::OnSessionTimelineUpdated(const RtspSessionTiming& timing, const std::vector<RtspMediaTrack>& tracks) {
        std::lock_guard lock(m_Mutex);
        for (std::size_t i = 0; i < tracks.size(); ++i) m_Wrappers.push_back(NewWrapper(tracks[i], int(i), m_Options.forceUseRtpTcp));
        if (m_Options.forceUseRtpTcp) m_UsingRtpTcp = true;
        // RtspMediaSource's listener: the timeline.
        m_Timeline.known = true;
        m_Timeline.live = timing.IsLive();
        m_Timeline.seekable = !timing.IsLive();
        m_Timeline.durationUs = timing.IsLive() ? kTimeUnset : stream::MsToUs(timing.GetDurationMs());
    }

    void RtspSession::OnSessionTimelineRequestFailed(const std::string& message) {
        std::lock_guard lock(m_Mutex);
        m_PreparationError = message;
    }

    void RtspSession::OnTransportReady(LoaderWrapper* w) {
        if (w->channel && w->channel->Interleaved()) {
            std::weak_ptr<DataChannel> channel = w->channel;
            m_Client->RegisterInterleavedDataChannel(w->channel->LocalPort(), [channel](std::vector<std::uint8_t> data) {
                if (auto c = channel.lock()) c->OnInterleavedBinaryData(std::move(data));
            });
            m_UsingRtpTcp = true;
        }
        MaybeFinishPrepare();   // endTracks: the reader gave its format
        MaybeSetupTracks();
    }

    void RtspSession::MaybeFinishPrepare() {
        if (m_Released || m_Prepared) return;
        for (const auto& w : m_Wrappers)
            if (!w->queue.UpstreamFormat()) return;
        m_Prepared = true;
        m_TrackFormats.clear();
        for (const auto& w : m_Wrappers) m_TrackFormats.push_back(*w->queue.UpstreamFormat());
    }

    void RtspSession::MaybeSetupTracks() {
        bool transportReady = true;
        std::vector<RtspClient::LoadInfo> infos;
        for (int t : m_SelectedTracks) {
            const auto& w = m_Wrappers.at(std::size_t(t));
            transportReady = transportReady && !w->transport.empty();
            infos.push_back({ w->track.uri, w->transport });
        }
        if (transportReady && m_TrackSelected) m_Client->SetupSelectedTracks(infos);
    }

    void RtspSession::SelectTracks(const std::vector<int>& tracks, std::int64_t positionUs) {
        m_Handler.RunAndWait([&] {
            std::lock_guard lock(m_Mutex);
            m_SelectedTracks = tracks;
            for (std::size_t i = 0; i < m_Wrappers.size(); ++i)
                if (std::find(tracks.begin(), tracks.end(), int(i)) == tracks.end() && !m_Wrappers[i]->canceled) {
                    m_Wrappers[i]->loadCancelled = true;
                    m_Wrappers[i]->canceled = true;
                    UpdateLoadingFinished();
                }
            m_TrackSelected = true;
            if (positionUs != 0) m_RequestedSeekPositionUs = m_PendingSeekPositionUs = m_PendingSeekPositionUsForTcpRetry = positionUs;
            MaybeSetupTracks();
        });
    }

    void RtspSession::UpdateLoadingFinished() {
        m_LoadingFinished = true;
        for (const auto& w : m_Wrappers) m_LoadingFinished = m_LoadingFinished && w->canceled;
    }

    std::optional<SampleQueue::Item> RtspSession::Read(int track) {
        std::shared_ptr<LoaderWrapper> w;
        {
            std::lock_guard lock(m_Mutex);
            if (m_NotifyDiscontinuity || track < 0 || std::size_t(track) >= m_Wrappers.size()) return std::nullopt;   // suppressRead
            w = m_Wrappers[std::size_t(track)];
        }
        return w->queue.Read();
    }

    bool RtspSession::IsEnded(int track) const {
        std::lock_guard lock(m_Mutex);
        if (track < 0 || std::size_t(track) >= m_Wrappers.size()) return true;
        const auto& w = m_Wrappers[std::size_t(track)];
        return w->canceled && !w->queue.HasItems();
    }

    bool RtspSession::HasSamples(int track) const {
        std::shared_ptr<LoaderWrapper> w;
        {
            std::lock_guard lock(m_Mutex);
            if (track < 0 || std::size_t(track) >= m_Wrappers.size()) return false;
            w = m_Wrappers[std::size_t(track)];
        }
        return w->queue.HasItems();
    }

    bool RtspSession::IsLoading() const {
        std::lock_guard lock(m_Mutex);
        const auto state = m_Client->GetState();
        return !m_LoadingFinished && (state == RtspClient::RTSP_STATE_PLAYING || state == RtspClient::RTSP_STATE_READY);
    }

    std::int64_t RtspSession::BufferedPositionUs() const {
        std::lock_guard lock(m_Mutex);
        return BufferedPositionUsLocked();
    }

    std::int64_t RtspSession::BufferedPositionUsLocked() const {
        if (m_LoadingFinished || m_Wrappers.empty()) return INT64_MIN;   // C.TIME_END_OF_SOURCE
        if (m_RequestedSeekPositionUs != kTimeUnset) return m_RequestedSeekPositionUs;
        bool allCanceled = true;
        std::int64_t buffered = INT64_MAX;
        for (const auto& w : m_Wrappers)
            if (!w->canceled) {
                buffered = std::min(buffered, w->queue.LargestQueuedTimestampUs());
                allCanceled = false;
            }
        return allCanceled || buffered == INT64_MIN ? 0 : buffered;
    }

    bool RtspSession::ReadDiscontinuity() {
        std::lock_guard lock(m_Mutex);
        return std::exchange(m_NotifyDiscontinuity, false);
    }

    bool RtspSession::SeekInsideBufferUs(std::int64_t positionUs) {
        for (const auto& w : m_Wrappers)
            if (!w->queue.SeekTo(positionUs, m_LoadingFinished)) return false;
        return true;
    }

    void RtspSession::SeekToUs(std::int64_t positionUs) {
        m_Handler.RunAndWait([&] {
            std::lock_guard lock(m_Mutex);
            SeekToUsOnHandler(positionUs);
        });
    }

    void RtspSession::SeekToUsOnHandler(std::int64_t positionUs) {
        if (BufferedPositionUsLocked() == 0 && !m_UsingRtpTcp) {
            m_PendingSeekPositionUsForTcpRetry = positionUs;
            return;
        }
        m_RequestedSeekPositionUs = positionUs;
        if (IsSeekPending()) {
            switch (m_Client->GetState()) {
                case RtspClient::RTSP_STATE_READY:
                    return;
                case RtspClient::RTSP_STATE_PLAYING:
                    m_PendingSeekPositionUs = positionUs;
                    m_Client->SeekToUs(m_PendingSeekPositionUs);
                    return;
                default:
                    throw std::logic_error("seek before the session is set up");
            }
        }
        if (SeekInsideBufferUs(positionUs)) return;
        m_PendingSeekPositionUs = positionUs;
        if (m_LoadingFinished) {
            // resumeLoad: the tracks load again.
            for (auto& w : m_Wrappers) {
                w->canceled = false;
                StartLoading(w);
            }
            UpdateLoadingFinished();
            if (m_UsingRtpTcp) m_Client->StartPlayback(positionUs / 1000);
            else m_Client->SeekToUs(positionUs);
        } else {
            m_Client->SeekToUs(positionUs);
        }
        for (auto& w : m_Wrappers)
            if (!w->canceled) {
                w->PreSeek();   // resetForSeek
                w->queue.Reset();
            }
    }

    void RtspSession::OnRtspSetupCompleted() {
        std::lock_guard lock(m_Mutex);
        std::int64_t offsetMs = 0;
        if (m_PendingSeekPositionUs != kTimeUnset) offsetMs = m_PendingSeekPositionUs / 1000;
        else if (m_PendingSeekPositionUsForTcpRetry != kTimeUnset) offsetMs = m_PendingSeekPositionUsForTcpRetry / 1000;
        m_Client->StartPlayback(offsetMs);
    }

    void RtspSession::OnSeekingUnsupported() { m_Timeline.seekable = false; }

    void RtspSession::OnPlaybackStarted(std::int64_t startPositionUs, const std::vector<RtspTrackTiming>& timings) {
        std::lock_guard lock(m_Mutex);
        std::vector<std::string> pathsWithTiming;
        for (const auto& t : timings) pathsWithTiming.push_back(UriPath(t.uri));
        for (int s : m_SelectedTracks) {
            const std::string path = UriPath(m_Wrappers.at(std::size_t(s))->track.uri);
            if (std::find(pathsWithTiming.begin(), pathsWithTiming.end(), path) == pathsWithTiming.end()) {
                OnSeekingUnsupported();
                if (IsSeekPending()) {
                    m_NotifyDiscontinuity = true;
                    m_PendingSeekPositionUs = m_RequestedSeekPositionUs = m_PendingSeekPositionUsForTcpRetry = kTimeUnset;
                }
            }
        }
        for (const auto& t : timings) {
            LoaderWrapper* loadable = nullptr;
            for (const auto& w : m_Wrappers)
                if (!w->canceled && w->track.uri == t.uri) loadable = w.get();
            if (!loadable) continue;
            // setTimestamp / setSequenceNumber before the first packet.
            if (t.rtpTimestamp != kTimeUnset && !loadable->firstPacketRead) loadable->firstTimestamp = t.rtpTimestamp;
            if (!loadable->firstPacketRead) loadable->firstSequenceNumber = t.sequenceNumber;
            if (IsSeekPending() && m_PendingSeekPositionUs == m_RequestedSeekPositionUs) {
                loadable->nextRtpTimestamp = t.rtpTimestamp;
                loadable->pendingSeekPositionUs = startPositionUs;
            }
        }
        if (IsSeekPending()) {
            if (m_PendingSeekPositionUs == m_RequestedSeekPositionUs) {
                m_PendingSeekPositionUs = m_RequestedSeekPositionUs = kTimeUnset;
            } else {
                m_PendingSeekPositionUs = kTimeUnset;
                SeekToUsOnHandler(m_RequestedSeekPositionUs);
            }
        } else if (m_PendingSeekPositionUsForTcpRetry != kTimeUnset && m_UsingRtpTcp) {
            SeekToUsOnHandler(m_PendingSeekPositionUsForTcpRetry);
            m_PendingSeekPositionUsForTcpRetry = kTimeUnset;
        }
    }

    void RtspSession::OnPlaybackError(const std::string& message, bool udpUnsupported) {
        std::lock_guard lock(m_Mutex);
        if (udpUnsupported && !m_UsingRtpTcp) RetryWithRtpTcp();
        else m_PlaybackError = message;
    }

    void RtspSession::OnLoadCompleted(LoaderWrapper* loadable) {
        if (BufferedPositionUsLocked() == 0) {
            if (!m_UsingRtpTcp) RetryWithRtpTcp();   // UDP brought nothing
            return;
        }
        for (auto& w : m_Wrappers)
            if (w.get() == loadable && !w->canceled) {
                w->canceled = true;
                UpdateLoadingFinished();
                break;
            }
        m_Client->SignalPlaybackEnded();
    }

    void RtspSession::OnLoadError(LoaderWrapper* loadable, const std::string& message, bool bindFailure) {
        if (!m_Prepared) m_PreparationError = message;
        else if (!bindFailure) m_PlaybackError = loadable->track.uri + ": " + message;
    }

    void RtspSession::RetryWithRtpTcp() {
        m_UsingRtpTcp = true;
        m_Client->RetryWithRtpTcp();
        std::vector<std::shared_ptr<LoaderWrapper>> fresh, old = m_Wrappers;
        for (std::size_t i = 0; i < m_Wrappers.size(); ++i) {
            if (!m_Wrappers[i]->canceled) fresh.push_back(NewWrapper(m_Wrappers[i]->track, int(i), true));
            else fresh.push_back(m_Wrappers[i]);
        }
        m_Wrappers = fresh;
        for (auto& w : old)
            if (std::find(fresh.begin(), fresh.end(), w) == fresh.end()) {
                w->loadCancelled = true;
                w->canceled = true;
                w->quit = true;
                {
                    std::lock_guard channelLock(w->channelMutex);
                    if (w->channel) w->channel->Close();
                }
                m_Retired.push_back(w);   // joined when the session goes
            }
    }

}
