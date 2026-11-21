#include "kiln/media/stream/AdaptiveSource.h"

#include "kiln/media/dash/DashManifest.h"
#include "kiln/media/hls/HlsPlaylist.h"
#include "kiln/media/stream/SegmentProbe.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <string_view>

namespace kiln::media::stream {

    namespace {
        std::string Lower(std::string_view s) {
            std::string out(s);
            for (char& c : out) c = char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
            return out;
        }
        // The URI's path without query or fragment, lower case.
        std::string PathOf(const std::string& uri) {
            std::string_view s = uri;
            if (auto q = s.find_first_of("?#"); q != std::string_view::npos) s = s.substr(0, q);
            return Lower(s);
        }
        bool EndsWith(std::string_view s, std::string_view suffix) { return s.size() >= suffix.size() && s.substr(s.size() - suffix.size()) == suffix; }

        std::string BytesToString(const std::vector<std::uint8_t>& bytes) { return std::string(bytes.begin(), bytes.end()); }
    }

    // --- fetching ------------------------------------------------------------------------------
    std::vector<std::uint8_t> SegmentFetcher::Get(const std::string& url, std::int64_t offset, std::int64_t length, net::HttpCall* call, std::string* finalUrl) {
        if (!m_Engine) throw net::HttpError(net::HttpError::Kind::NoEngine, "kiln: no HTTP engine");
        net::HttpCall ownCall;
        net::HttpCall& c = call ? *call : ownCall;
        TransferListener* listener = m_Meter ? m_Meter->GetTransferListener() : nullptr;
        net::HttpRequest request;
        request.url = url;
        if (offset != 0 || length != kLengthUnset)
            request.headers.Set("Range", "bytes=" + std::to_string(offset) + "-" + (length != kLengthUnset ? std::to_string(offset + length - 1) : std::string()));
        std::int64_t reported = 0;
        c.progressSink = [&](std::int64_t received, std::int64_t) {
            if (listener && received > reported) listener->OnBytesTransferred(true, int(received - reported));
            reported = std::max(reported, received);
        };
        struct EndTransfer {
            TransferListener* listener;
            net::HttpCall& call;
            bool started = false;
            ~EndTransfer() {
                call.progressSink = nullptr;
                if (listener && started) listener->OnTransferEnd(true);
            }
        } end{ listener, c };
        net::HttpRequest current = request;
        int prior = 0;
        for (int followUps = 0;;) {
            if (c.Cancelled()) throw net::HttpError(net::HttpError::Kind::Cancelled, "cancelled");
            if (listener && !end.started) {
                listener->OnTransferStart(true);
                end.started = true;
            }
            net::HttpResponse response = m_Engine->Execute(current, c);
            std::optional<net::HttpRequest> next = net::detail::FollowUp(current, response, prior);
            if (!next) {
                // Bytes the engine did not report as progress still count.
                if (listener && std::int64_t(response.body.size()) > reported) listener->OnBytesTransferred(true, int(response.body.size() - std::size_t(reported)));
                reported = std::max<std::int64_t>(reported, std::int64_t(response.body.size()));
                if (!response.Ok()) throw HttpStatusError(response.status, url);
                if (finalUrl) *finalUrl = response.url.empty() ? current.url : response.url;
                std::vector<std::uint8_t> bytes(response.body.begin(), response.body.end());
                // A server that ignores Range sends the whole resource (200 instead of 206).
                if (response.status == 200 && (offset != 0 || length != kLengthUnset) && !response.headers.Contains("Content-Range")) {
                    if (std::int64_t(bytes.size()) < offset) throw HttpStatusError(416, url);
                    const std::int64_t n = length == kLengthUnset ? std::int64_t(bytes.size()) - offset : std::min<std::int64_t>(length, std::int64_t(bytes.size()) - offset);
                    bytes = std::vector<std::uint8_t>(bytes.begin() + offset, bytes.begin() + offset + n);
                }
                return bytes;
            }
            if (++followUps > 20) throw net::HttpError(net::HttpError::Kind::TooManyRedirects, "Too many follow-up requests: " + std::to_string(followUps));
            prior = response.status;
            current = std::move(*next);
        }
    }

    // --- AdaptiveSource ------------------------------------------------------------------------
    AdaptiveSource::AdaptiveSource(Protocol protocol, std::shared_ptr<SegmentFetcher> fetcher, std::function<std::int64_t()> nowUnixTimeMs)
        : m_Protocol(protocol), m_Fetcher(std::move(fetcher)), m_NowUnixTimeMs(std::move(nowUnixTimeMs)) {}

    std::int64_t AdaptiveSource::NowUnixTimeMs() const {
        if (m_NowUnixTimeMs) return m_NowUnixTimeMs();
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    }
    std::int64_t AdaptiveSource::NowElapsedMs() const {
        if (m_NowUnixTimeMs) return m_NowUnixTimeMs();
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    std::vector<std::uint8_t> AdaptiveSource::Key(const std::string& url, net::HttpCall* call) {
        {
            std::lock_guard lock(m_KeyMutex);
            if (auto it = m_Keys.find(url); it != m_Keys.end()) return it->second;
        }
        std::vector<std::uint8_t> key = m_Fetcher->Get(url, 0, kLengthUnset, call);
        if (key.size() != 16) throw ParserException("an AES-128 key is not 16 bytes: " + url);
        std::lock_guard lock(m_KeyMutex);
        // Upstream's cache holds the last keys (FullSegmentEncryptionKeyCache, 4 entries).
        if (m_Keys.size() >= 4) m_Keys.erase(m_Keys.begin());
        m_Keys[url] = key;
        return key;
    }

    SegmentContainer InferHlsContainer(const std::string& url) {
        const std::string path = PathOf(url);
        for (const char* e : { ".aac", ".ac3", ".ec3", ".eac3", ".ac4", ".mp3", ".adts" })
            if (EndsWith(path, e)) return SegmentContainer::PackedAudio;
        for (const char* e : { ".vtt", ".webvtt" })
            if (EndsWith(path, e)) return SegmentContainer::WebVtt;
        for (const char* e : { ".mp4", ".m4s", ".m4a", ".m4v", ".mp4a", ".mp4v", ".cmfv", ".cmfa", ".cmft", ".fmp4", ".cmf" })
            if (EndsWith(path, e) || path.find(std::string(e) + "_") != std::string::npos) return SegmentContainer::Mp4;
        if (EndsWith(path, ".webm")) return SegmentContainer::WebM;
        return SegmentContainer::Ts;   // .ts and anything else, as DefaultHlsExtractorFactory's fallback
    }

    std::optional<AdaptiveSource::Protocol> AdaptiveSource::InferProtocol(const std::string& uri, const std::string& mimeType) {
        const std::string mime = Lower(mimeType);
        if (mime == "application/x-mpegurl" || mime == "application/vnd.apple.mpegurl") return Protocol::Hls;
        if (mime == "application/dash+xml") return Protocol::Dash;
        const std::string path = PathOf(uri);
        if (EndsWith(path, ".m3u8") || EndsWith(path, ".m3u")) return Protocol::Hls;
        if (EndsWith(path, ".mpd")) return Protocol::Dash;
        return std::nullopt;
    }

    // --- HLS ------------------------------------------------------------------------------------
    namespace {
        using hls::HlsMediaPlaylist;
        using hls::HlsMultivariantPlaylist;

        class HlsSource final : public AdaptiveSource {
        public:
            HlsSource(std::shared_ptr<SegmentFetcher> fetcher, std::function<std::int64_t()> now) : AdaptiveSource(Protocol::Hls, std::move(fetcher), std::move(now)) {}

            void Load(const std::string& uri, const std::string& content) {
                std::shared_ptr<hls::HlsPlaylist> playlist = hls::HlsPlaylistParser().Parse(uri, content);
                if (playlist->IsMultivariant()) {
                    m_Multivariant = std::static_pointer_cast<HlsMultivariantPlaylist>(playlist);
                } else {
                    // A media playlist alone: one variant of unknown format (createSingleVariantMultivariantPlaylist).
                    auto single = std::make_shared<HlsMultivariantPlaylist>(HlsMultivariantPlaylist::CreateSingleVariantMultivariantPlaylist(uri));
                    m_Multivariant = single;
                    Bundle& b = m_Bundles[uri];
                    b.snapshot = std::static_pointer_cast<HlsMediaPlaylist>(playlist);
                    b.loadedAtMs = NowElapsedMs();
                    b.changedAtMs = b.loadedAtMs;
                    m_PrimaryUrl = uri;
                    m_Primary = b.snapshot;
                    m_InitialStartTimeUs = b.snapshot->startTimeUs;
                }
                BuildGroups();
                if (m_Groups.empty()) throw ParserException("no variants in " + uri);
                // The primary playlist: the first variant's (DefaultHlsPlaylistTracker's choice
                // before any track selection).
                if (!m_Primary) {
                    m_PrimaryUrl = m_GroupUrls[0][0];
                    Refresh(m_PrimaryUrl, true);
                }
            }

            StreamWindow Window() override {
                std::lock_guard lock(m_Mutex);
                return WindowOf(*m_Primary);
            }

            SegmentTimeline Segments(int group, int track) override {
                const std::string url = m_GroupUrls.at(std::size_t(group)).at(std::size_t(track));
                std::shared_ptr<const HlsMediaPlaylist> playlist = Refresh(url, false);
                std::lock_guard lock(m_Mutex);
                SegmentTimeline timeline;
                timeline.ended = playlist->hasEndTag;
                const std::int64_t playlistStartUs = playlist->startTimeUs - m_InitialStartTimeUs;
                auto make = [&](const HlsMediaPlaylist::SegmentBase& s, std::int64_t sequence) {
                    MediaSegment out;
                    out.url = ResolveUri(playlist->baseUri, s.url);
                    out.offset = s.byteRangeOffset;
                    out.length = s.byteRangeLength;
                    out.startUs = playlistStartUs + s.relativeStartTimeUs;
                    out.durationUs = s.durationUs;
                    out.sequence = sequence;
                    out.discontinuity = playlist->discontinuitySequence + s.relativeDiscontinuitySequence;
                    if (!s.fullSegmentEncryptionKeyUri.empty()) {
                        out.keyUrl = ResolveUri(playlist->baseUri, s.fullSegmentEncryptionKeyUri);
                        out.iv = s.encryptionIV;
                    }
                    out.gap = s.hasGapTag;
                    return out;
                };
                std::map<const HlsMediaPlaylist::Segment*, std::shared_ptr<const MediaSegment>> inits;
                for (std::size_t i = 0; i < playlist->segments.size(); ++i) {
                    const HlsMediaPlaylist::Segment& s = playlist->segments[i];
                    MediaSegment out = make(s, playlist->mediaSequence + std::int64_t(i));
                    if (s.initializationSegment) {
                        auto& init = inits[s.initializationSegment.get()];
                        if (!init) {
                            MediaSegment initSegment = make(*s.initializationSegment, out.sequence);
                            initSegment.startUs = out.startUs;
                            initSegment.durationUs = 0;
                            init = std::make_shared<const MediaSegment>(std::move(initSegment));
                        }
                        out.init = init;
                    }
                    timeline.segments.push_back(std::move(out));
                }
                if (!timeline.segments.empty()) timeline.container = InferHlsContainer(timeline.segments.front().url);
                return timeline;
            }

            std::int64_t RetryIntervalUs(int group, int track) override {
                std::lock_guard lock(m_Mutex);
                const std::string& url = m_GroupUrls.at(std::size_t(group)).at(std::size_t(track));
                auto it = m_Bundles.find(url);
                return it != m_Bundles.end() && it->second.snapshot ? std::max<std::int64_t>(it->second.snapshot->targetDurationUs / 2, 500'000) : 1'000'000;
            }

        private:
            struct Bundle {
                std::shared_ptr<const HlsMediaPlaylist> snapshot;
                std::int64_t loadedAtMs = 0, changedAtMs = 0, earliestNextLoadMs = 0;
            };

            void BuildGroups() {
                const HlsMultivariantPlaylist& mv = *m_Multivariant;
                // HlsMediaPeriod.buildAndPrepareMainSampleStreamWrapper: the video variants, else
                // those not audio-only, else all.
                std::vector<int> types;
                int videoCount = 0, audioCount = 0;
                for (const auto& v : mv.variants) {
                    if (v.format.height > 0 || !GetCodecsOfType(v.format.codecs, TrackType::Video).empty()) {
                        types.push_back(int(TrackType::Video));
                        ++videoCount;
                    } else if (!GetCodecsOfType(v.format.codecs, TrackType::Audio).empty()) {
                        types.push_back(int(TrackType::Audio));
                        ++audioCount;
                    } else {
                        types.push_back(int(TrackType::Unknown));
                    }
                }
                const bool videoOnly = videoCount > 0;
                const bool nonAudioOnly = !videoOnly && audioCount < int(types.size());
                std::vector<const HlsMultivariantPlaylist::Variant*> selected;
                for (std::size_t i = 0; i < mv.variants.size(); ++i)
                    if ((!videoOnly || types[i] == int(TrackType::Video)) && (!nonAudioOnly || types[i] != int(TrackType::Audio))) selected.push_back(&mv.variants[i]);
                // Redundant variants (the same stream on other pathways or URLs) count once.
                std::vector<const HlsMultivariantPlaylist::Variant*> unique;
                for (const auto* v : selected) {
                    const bool duplicate = std::any_of(unique.begin(), unique.end(), [&](const auto* u) {
                        return u->format.bitrate == v->format.bitrate && u->format.width == v->format.width && u->format.height == v->format.height
                            && u->format.codecs == v->format.codecs && u->url != v->url && !v->pathwayId.empty() && u->pathwayId != v->pathwayId;
                    });
                    if (!duplicate) unique.push_back(v);
                }
                std::vector<const HlsMultivariantPlaylist::Rendition*> audios;
                for (const auto& r : mv.audios)
                    if (!r.url.empty()) audios.push_back(&r);
                if (!unique.empty()) {
                    StreamGroup main;
                    main.id = "main";
                    main.primary = true;
                    const std::string codecs = unique[0]->format.codecs;
                    const int videoCodecs = GetCodecCountOfType(codecs, TrackType::Video);
                    const int audioCodecs = GetCodecCountOfType(codecs, TrackType::Audio);
                    main.type = videoOnly || videoCodecs > 0 || audioCodecs == 0 ? TrackType::Video : TrackType::Audio;
                    std::vector<std::string> urls;
                    for (const auto* v : unique) {
                        Format f = main.type == TrackType::Video ? DeriveVideoFormat(v->format) : DeriveAudioFormat(v->format, std::nullopt, true);
                        main.formats.push_back(std::move(f));
                        urls.push_back(v->url);
                    }
                    if (main.type == TrackType::Video && audioCodecs > 0 && (mv.muxedAudioFormat || audios.empty()))
                        main.embeddedAudio = DeriveAudioFormat(unique[0]->format, mv.muxedAudioFormat, false);
                    if (main.type == TrackType::Video && audioCodecs == 0 && videoCodecs == 0 && audios.empty())
                        main.embeddedAudio = Format{};   // unknown codecs: the segments may hold audio
                    m_Groups.push_back(std::move(main));
                    m_GroupUrls.push_back(std::move(urls));
                }
                // buildAndPrepareAudioSampleStreamWrappers: renditions grouped by name.
                std::set<std::string> grouped;
                for (const auto* r : audios) {
                    if (!grouped.insert(r->name).second) continue;
                    StreamGroup g;
                    g.id = "audio:" + r->name;
                    g.type = TrackType::Audio;
                    std::vector<std::string> urls;
                    for (const auto* other : audios)
                        if (other->name == r->name) {
                            g.formats.push_back(other->format);
                            urls.push_back(other->url);
                        }
                    if (m_Groups.empty()) g.primary = true;
                    m_Groups.push_back(std::move(g));
                    m_GroupUrls.push_back(std::move(urls));
                }
                // buildAndPrepareSubtitleSampleStreamWrappers: subtitle renditions grouped by name.
                grouped.clear();
                for (const auto& r : mv.subtitles) {
                    if (r.url.empty() || !grouped.insert(r.name).second) continue;
                    StreamGroup g;
                    g.id = "subtitle:" + r.name;
                    g.type = TrackType::Text;
                    std::vector<std::string> urls;
                    for (const auto& other : mv.subtitles)
                        if (!other.url.empty() && other.name == r.name) {
                            g.formats.push_back(other.format);
                            urls.push_back(other.url);
                        }
                    m_Groups.push_back(std::move(g));
                    m_GroupUrls.push_back(std::move(urls));
                }
            }

            static Format DeriveVideoFormat(const Format& variant) {
                Format f;
                f.id = variant.id;
                f.label = variant.label;
                f.labels = variant.labels;
                f.containerMimeType = variant.containerMimeType;
                f.codecs = GetCodecsOfType(variant.codecs, TrackType::Video);
                f.sampleMimeType = MimeTypes::GetMediaMimeType(f.codecs);
                f.SetBitrates(variant.averageBitrate, variant.peakBitrate);
                f.width = variant.width;
                f.height = variant.height;
                f.frameRate = variant.frameRate;
                f.selectionFlags = variant.selectionFlags;
                f.roleFlags = variant.roleFlags;
                f.colorSpace = variant.colorSpace;
                f.colorRange = variant.colorRange;
                f.colorTransfer = variant.colorTransfer;
                return f;
            }
            static Format DeriveAudioFormat(const Format& variant, const std::optional<Format>& mediaTag, bool primaryTrackInVariant) {
                Format f;
                f.id = variant.id;
                f.containerMimeType = variant.containerMimeType;
                if (mediaTag) {
                    f.codecs = mediaTag->codecs;
                    f.channelCount = mediaTag->channelCount;
                    f.selectionFlags = mediaTag->selectionFlags;
                    f.roleFlags = mediaTag->roleFlags;
                    f.language = mediaTag->language;
                    f.label = mediaTag->label;
                    f.labels = mediaTag->labels;
                } else {
                    f.codecs = GetCodecsOfType(variant.codecs, TrackType::Audio);
                    if (primaryTrackInVariant) {
                        f.channelCount = variant.channelCount;
                        f.selectionFlags = variant.selectionFlags;
                        f.roleFlags = variant.roleFlags;
                        f.language = variant.language;
                        f.label = variant.label;
                        f.labels = variant.labels;
                    }
                }
                f.sampleMimeType = MimeTypes::GetMediaMimeType(f.codecs);
                if (primaryTrackInVariant) f.SetBitrates(variant.averageBitrate, variant.peakBitrate);
                return f;
            }

            // MediaPlaylistBundle.loadPlaylist / processLoadedPlaylist: loads the playlist when it
            // has none, or when it is live and due (or `force`), and keeps the latest snapshot.
            std::shared_ptr<const HlsMediaPlaylist> Refresh(const std::string& url, bool force) {
                std::shared_ptr<const HlsMediaPlaylist> old;
                {
                    std::lock_guard lock(m_Mutex);
                    Bundle& b = m_Bundles[url];
                    old = b.snapshot;
                    if (old && !force && (old->hasEndTag || NowElapsedMs() < b.earliestNextLoadMs)) return old;
                }
                const std::int64_t startMs = NowElapsedMs();
                std::string answered;
                const std::string content = BytesToString(m_Fetcher->Get(url, 0, kLengthUnset, nullptr, &answered));
                std::shared_ptr<hls::HlsPlaylist> parsed;
                try {
                    parsed = hls::HlsPlaylistParser(m_Multivariant, old).Parse(answered, content);
                } catch (const hls::HlsPlaylistParser::DeltaUpdateException&) {
                    // A delta update the old snapshot cannot apply to: the whole playlist again.
                    parsed = hls::HlsPlaylistParser(m_Multivariant, nullptr).Parse(answered, content);
                }
                if (parsed->IsMultivariant()) throw ParserException("a multivariant playlist where a media playlist was expected: " + url);
                auto loaded = std::static_pointer_cast<HlsMediaPlaylist>(parsed);
                std::lock_guard lock(m_Mutex);
                Bundle& b = m_Bundles[url];
                const std::int64_t nowMs = NowElapsedMs();
                std::shared_ptr<const HlsMediaPlaylist> snapshot = LatestSnapshot(b.snapshot, loaded);
                if (snapshot != b.snapshot) b.changedAtMs = nowMs;
                const bool changed = snapshot != b.snapshot;
                b.snapshot = snapshot;
                b.loadedAtMs = nowMs;
                std::int64_t untilNextUs = 0;
                if (!snapshot->serverControl.canBlockReload) untilNextUs = changed ? snapshot->targetDurationUs : snapshot->targetDurationUs / 2;
                else if (!changed) untilNextUs = snapshot->partTargetDurationUs != kTimeUnset ? snapshot->partTargetDurationUs / 2 : snapshot->targetDurationUs / 2;
                b.earliestNextLoadMs = nowMs + untilNextUs / 1000 - (nowMs - startMs);
                if (url == m_PrimaryUrl) {
                    if (!m_Primary) m_InitialStartTimeUs = snapshot->startTimeUs;
                    m_Primary = snapshot;
                }
                return snapshot;
            }

            // getLatestPlaylistSnapshot.
            std::shared_ptr<const HlsMediaPlaylist> LatestSnapshot(const std::shared_ptr<const HlsMediaPlaylist>& old, const std::shared_ptr<HlsMediaPlaylist>& loaded) {
                if (!loaded->IsNewerThan(old.get())) {
                    if (loaded->hasEndTag) return std::make_shared<HlsMediaPlaylist>(old->CopyWithEndTag());
                    return old;
                }
                const std::int64_t startTimeUs = LoadedStartTimeUs(old.get(), *loaded);
                const int discontinuitySequence = LoadedDiscontinuitySequence(old.get(), *loaded);
                return std::make_shared<HlsMediaPlaylist>(loaded->CopyWith(startTimeUs, discontinuitySequence));
            }
            static const HlsMediaPlaylist::Segment* FirstOldOverlappingSegment(const HlsMediaPlaylist& old, const HlsMediaPlaylist& loaded) {
                const std::int64_t offset = loaded.mediaSequence - old.mediaSequence;
                return offset >= 0 && offset < std::int64_t(old.segments.size()) ? &old.segments[std::size_t(offset)] : nullptr;
            }
            std::int64_t LoadedStartTimeUs(const HlsMediaPlaylist* old, const HlsMediaPlaylist& loaded) const {
                if (loaded.hasProgramDateTime) return loaded.startTimeUs;
                const std::int64_t primaryStartUs = m_Primary ? m_Primary->startTimeUs : 0;
                if (!old) {
                    // A first load: upstream takes the primary snapshot's start. Variants share
                    // media sequence numbers, so where the windows overlap the matching segment's
                    // start is exact (a live window may have moved between the two loads, and the
                    // first piece's start sets the timeline the timestamps map onto).
                    if (m_Primary && &loaded != m_Primary.get())
                        if (const auto* s = FirstOldOverlappingSegment(*m_Primary, loaded)) return m_Primary->startTimeUs + s->relativeStartTimeUs;
                    return primaryStartUs;
                }
                if (const auto* s = FirstOldOverlappingSegment(*old, loaded)) return old->startTimeUs + s->relativeStartTimeUs;
                if (std::int64_t(old->segments.size()) == loaded.mediaSequence - old->mediaSequence) return old->GetEndTimeUs();
                return primaryStartUs;
            }
            int LoadedDiscontinuitySequence(const HlsMediaPlaylist* old, const HlsMediaPlaylist& loaded) const {
                if (loaded.hasDiscontinuitySequence) return loaded.discontinuitySequence;
                const int primary = m_Primary ? m_Primary->discontinuitySequence : 0;
                if (!old) return primary;
                if (const auto* s = FirstOldOverlappingSegment(*old, loaded))
                    return old->discontinuitySequence + s->relativeDiscontinuitySequence - loaded.segments.at(0).relativeDiscontinuitySequence;
                return primary;
            }

            // HlsMediaSource.createTimelineForLive / createTimelineForOnDemand.
            StreamWindow WindowOf(const HlsMediaPlaylist& p) {
                StreamWindow w;
                const bool live = !p.hasEndTag;
                w.startUs = p.startTimeUs - m_InitialStartTimeUs;
                w.durationUs = p.durationUs;
                w.dynamic = live;
                if (p.hasProgramDateTime) w.windowStartTimeMs = p.startTimeUs / 1000;
                auto closestPreceding = [&](std::int64_t positionUs) -> const HlsMediaPlaylist::Segment& {
                    auto it = std::upper_bound(p.segments.begin(), p.segments.end(), positionUs,
                                               [](std::int64_t t, const HlsMediaPlaylist::Segment& s) { return t < s.relativeStartTimeUs; });
                    return it == p.segments.begin() ? p.segments.front() : *(it - 1);
                };
                if (!live) {
                    if (p.startOffsetUs == kTimeUnset || p.segments.empty()) w.defaultPositionUs = 0;
                    else if (p.preciseStart || p.startOffsetUs == p.durationUs) w.defaultPositionUs = p.startOffsetUs;
                    else w.defaultPositionUs = closestPreceding(p.startOffsetUs).relativeStartTimeUs;
                    return w;
                }
                const std::int64_t liveEdgeOffsetUs = p.hasProgramDateTime ? NowUnixTimeMs() * 1000 - p.GetEndTimeUs() : 0;
                MediaItem::LiveConfiguration& config = m_LiveConfiguration;
                std::int64_t target;
                if (config.targetOffsetMs != kTimeUnset) target = config.targetOffsetMs * 1000;
                else if (p.startOffsetUs != kTimeUnset) target = p.durationUs - p.startOffsetUs + liveEdgeOffsetUs;
                else if (p.serverControl.partHoldBackUs != kTimeUnset && p.partTargetDurationUs != kTimeUnset) target = p.serverControl.partHoldBackUs + liveEdgeOffsetUs;
                else if (p.serverControl.holdBackUs != kTimeUnset) target = p.serverControl.holdBackUs + liveEdgeOffsetUs;
                else target = 3 * p.targetDurationUs + liveEdgeOffsetUs;
                target = std::clamp(target, liveEdgeOffsetUs, p.durationUs + liveEdgeOffsetUs);
                // updateLiveConfiguration: the target sticks; no speed adjustment unless the item
                // or the playlist's server control asks for a latency.
                const bool disableSpeedAdjustment = config.minPlaybackSpeed == kRateUnset && config.maxPlaybackSpeed == kRateUnset &&
                                                    p.serverControl.holdBackUs == kTimeUnset && p.serverControl.partHoldBackUs == kTimeUnset;
                config.targetOffsetMs = target / 1000;
                if (disableSpeedAdjustment) config.minPlaybackSpeed = config.maxPlaybackSpeed = 1.f;
                w.liveConfiguration = config;
                const std::int64_t start = p.startOffsetUs != kTimeUnset ? p.startOffsetUs : p.durationUs + liveEdgeOffsetUs - config.targetOffsetMs * 1000;
                if (p.preciseStart) w.defaultPositionUs = start;
                else if (p.segments.empty()) w.defaultPositionUs = 0;
                else w.defaultPositionUs = closestPreceding(start).relativeStartTimeUs;
                return w;
            }

            std::shared_ptr<const HlsMultivariantPlaylist> m_Multivariant;
            std::vector<std::vector<std::string>> m_GroupUrls;
            std::mutex m_Mutex;
            std::map<std::string, Bundle> m_Bundles;
            std::string m_PrimaryUrl;
            std::shared_ptr<const HlsMediaPlaylist> m_Primary;
            std::int64_t m_InitialStartTimeUs = 0;
        };
    }

    // --- DASH -----------------------------------------------------------------------------------
    namespace {
        using dash::AdaptationSet;
        using dash::DashManifest;
        using dash::DashSegmentIndex;
        using dash::Representation;

        constexpr const char* kAdaptationSetSwitching = "urn:mpeg:dash:adaptation-set-switching:2016";
        constexpr const char* kTrickMode = "http://dashif.org/guidelines/trickmode";
        // DashMediaSource.MIN_LIVE_DEFAULT_START_POSITION_US.
        constexpr std::int64_t kMinLiveStartPositionUs = 5'000'000;

        class DashSource final : public AdaptiveSource {
        public:
            DashSource(std::shared_ptr<SegmentFetcher> fetcher, std::function<std::int64_t()> now) : AdaptiveSource(Protocol::Dash, std::move(fetcher), std::move(now)) {}

            void Load(const std::string& uri, const std::string& content) {
                m_Uri = uri;
                m_Manifest = dash::DashManifestParser().Parse(uri, content);
                m_LoadedAtMs = NowElapsedMs();
                ResolveUtcTiming();
                BuildGroups();
                if (m_Groups.empty()) throw ParserException("no audio or video in " + uri);
            }

            StreamWindow Window() override {
                // No refresh here (the playback thread asks): the loader's Segments() refreshes.
                std::lock_guard lock(m_Mutex);
                const DashManifest& m = *m_Manifest;
                StreamWindow w;
                const std::int64_t nowUs = NowServerUnixTimeMs() * 1000;
                const int last = m.GetPeriodCount() - 1;
                std::int64_t startUs = AvailableStartTimeInManifestUs(0, nowUs);
                const std::int64_t endUs = AvailableEndTimeInManifestUs(last, nowUs);
                const bool changingImplicitly = m.dynamic && !IsIndexExplicit(last);
                if (changingImplicitly && m.timeShiftBufferDepthMs != kTimeUnset) startUs = std::max(startUs, endUs - MsToUs(m.timeShiftBufferDepthMs));
                w.startUs = startUs;
                w.durationUs = endUs - startUs;
                // isMovingLiveWindow: a dynamic manifest that is updated and has no duration yet.
                w.dynamic = m.dynamic && m.minUpdatePeriodMs != kTimeUnset && m.durationMs == kTimeUnset;
                w.serverClockOffsetMs = m_ServerOffsetMs;
                if (m.dynamic && m.availabilityStartTimeMs != kTimeUnset) {
                    const std::int64_t nowInWindowUs = nowUs - MsToUs(m.availabilityStartTimeMs) - startUs;
                    UpdateLiveConfiguration(nowInWindowUs, w.durationUs);
                    w.liveConfiguration = m_LiveConfiguration;
                    w.windowStartTimeMs = m.availabilityStartTimeMs + startUs / 1000;
                    w.defaultPositionUs = std::max(nowInWindowUs - m_LiveConfiguration.targetOffsetMs * 1000, std::min(kMinLiveStartPositionUs, w.durationUs / 2));
                }
                return w;
            }

            SegmentTimeline Segments(int group, int track) override {
                MaybeRefreshManifest();
                std::unique_lock lock(m_Mutex);
                std::shared_ptr<const DashManifest> manifest = m_Manifest;
                const DashManifest& m = *manifest;
                SegmentTimeline timeline;
                timeline.ended = !m.dynamic;
                const std::int64_t nowUs = NowServerUnixTimeMs() * 1000;
                const Format& wanted = m_Groups.at(std::size_t(group)).formats.at(std::size_t(track));
                for (int p = 0; p < m.GetPeriodCount(); ++p) {
                    const dash::Period& period = m.GetPeriod(p);
                    const std::shared_ptr<const Representation> rep = RepresentationFor(period, group, wanted);
                    if (!rep) continue;
                    const std::int64_t periodStartUs = MsToUs(period.startMs);
                    const std::int64_t periodDurationUs = m.GetPeriodDurationUs(p);
                    const std::string baseUrl = rep->baseUrls.empty() ? m_Uri : rep->baseUrls.front().url;
                    std::shared_ptr<const MediaSegment> init;
                    if (const auto& initUri = rep->GetInitializationUri()) {
                        MediaSegment s;
                        s.url = initUri->ResolveUriString(baseUrl);
                        s.offset = initUri->start;
                        s.length = initUri->length;
                        s.startUs = periodStartUs;
                        s.discontinuity = p;
                        init = std::make_shared<const MediaSegment>(std::move(s));
                    }
                    const DashSegmentIndex* index = rep->GetIndex();
                    std::shared_ptr<const ChunkIndex> chunks;
                    if (!index && rep->GetIndexUri()) {
                        lock.unlock();
                        chunks = LoadChunkIndex(*rep, baseUrl);
                        lock.lock();
                    }
                    auto add = [&](MediaSegment s) {
                        s.discontinuity = p;
                        s.timeOffsetUs = periodStartUs - rep->presentationTimeOffsetUs;
                        s.init = init;
                        timeline.segments.push_back(std::move(s));
                    };
                    if (chunks) {
                        const std::string url = rep->kind == Representation::Kind::SingleSegment ? static_cast<const dash::SingleSegmentRepresentation&>(*rep).uri : baseUrl;
                        for (int i = 0; i < chunks->Length(); ++i) {
                            MediaSegment s;
                            s.url = url;
                            s.offset = chunks->offsets[std::size_t(i)];
                            s.length = chunks->sizes[std::size_t(i)];
                            s.startUs = periodStartUs + chunks->timesUs[std::size_t(i)] - rep->presentationTimeOffsetUs;
                            s.durationUs = chunks->durationsUs[std::size_t(i)];
                            s.sequence = i;
                            add(std::move(s));
                        }
                        continue;
                    }
                    if (!index) continue;
                    const bool lastDynamic = m.dynamic && p == m.GetPeriodCount() - 1;
                    const std::int64_t first = index->GetFirstAvailableSegmentNum(periodDurationUs, nowUs);
                    const std::int64_t count = index->GetAvailableSegmentCount(periodDurationUs, nowUs);
                    if (count == kIndexUnset || count < 0) continue;
                    for (std::int64_t n = first; n < first + count; ++n) {
                        const std::int64_t timeUs = index->GetTimeUs(n);
                        if (periodDurationUs != kTimeUnset && timeUs >= periodDurationUs) break;
                        // A segment that has not ended yet is not available at full speed.
                        const std::int64_t durationUs = index->GetDurationUs(n, periodDurationUs);
                        if (lastDynamic && !index->IsExplicit() && m.availabilityStartTimeMs != kTimeUnset
                            && periodStartUs + timeUs + durationUs > nowUs - MsToUs(m.availabilityStartTimeMs))
                            break;
                        const dash::RangedUri r = index->GetSegmentUrl(n);
                        MediaSegment s;
                        s.url = r.ResolveUriString(baseUrl);
                        s.offset = r.start;
                        s.length = r.length;
                        s.startUs = periodStartUs + timeUs;
                        s.durationUs = durationUs;
                        s.sequence = n;
                        add(std::move(s));
                    }
                    if (lastDynamic && periodDurationUs == kTimeUnset) timeline.ended = false;
                }
                const std::string& mime = wanted.containerMimeType;
                timeline.container = mime.find("webm") != std::string::npos || mime.find("matroska") != std::string::npos ? SegmentContainer::WebM
                                     : mime == MimeTypes::VIDEO_MP2T                                                         ? SegmentContainer::Ts
                                     : mime == "text/vtt"                                                                    ? SegmentContainer::WebVtt
                                                                                                                             : SegmentContainer::Mp4;
                return timeline;
            }

            std::int64_t RetryIntervalUs(int, int) override {
                std::lock_guard lock(m_Mutex);
                // The next segment of a live template appears about one segment duration later.
                return m_Manifest->dynamic ? 1'000'000 : 5'000'000;
            }

        private:
            // DashMediaPeriod.getGroupedAdaptationSetIndices over the first period: sets joined by
            // adaptation-set-switching, trick-play sets left out, text and other types kept apart.
            void BuildGroups() {
                const dash::Period& period = m_Manifest->GetPeriod(0);
                const auto& sets = period.adaptationSets;
                std::vector<int> parent(sets.size());
                for (std::size_t i = 0; i < sets.size(); ++i) parent[i] = int(i);
                auto find = [&](int i) {
                    while (parent[std::size_t(i)] != i) i = parent[std::size_t(i)];
                    return i;
                };
                std::map<std::int64_t, int> byId;
                for (std::size_t i = 0; i < sets.size(); ++i) byId[sets[i].id] = int(i);
                auto property = [](const AdaptationSet& set, const char* scheme) -> const dash::Descriptor* {
                    for (const auto* list : { &set.supplementalProperties, &set.essentialProperties })
                        for (const auto& d : *list)
                            if (d.schemeIdUri == scheme) return &d;
                    return nullptr;
                };
                for (std::size_t i = 0; i < sets.size(); ++i) {
                    const dash::Descriptor* switching = property(sets[i], kAdaptationSetSwitching);
                    if (!switching) continue;
                    std::string_view ids = switching->value;
                    while (!ids.empty()) {
                        const auto comma = ids.find(',');
                        const std::string id(ids.substr(0, comma));
                        ids = comma == std::string_view::npos ? std::string_view() : ids.substr(comma + 1);
                        try {
                            auto it = byId.find(std::stoll(id));
                            if (it != byId.end() && sets[std::size_t(it->second)].type == sets[i].type) parent[std::size_t(find(it->second))] = find(int(i));
                        } catch (...) {
                        }
                    }
                }
                std::map<int, std::vector<int>> members;
                for (std::size_t i = 0; i < sets.size(); ++i) {
                    if (property(sets[i], kTrickMode)) continue;
                    if (sets[i].type != TrackType::Video && sets[i].type != TrackType::Audio && sets[i].type != TrackType::Text) continue;
                    members[find(int(i))].push_back(int(i));
                }
                // In manifest order of each group's first set.
                std::vector<std::vector<int>> ordered;
                for (auto& [root, list] : members) ordered.push_back(list);
                std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) { return a.front() < b.front(); });
                bool havePrimary = false;
                for (const auto& list : ordered) {
                    StreamGroup g;
                    g.type = sets[std::size_t(list.front())].type;
                    g.id = "0." + std::to_string(list.front());
                    for (int i : list)
                        for (const auto& rep : sets[std::size_t(i)].representations) g.formats.push_back(rep->format);
                    if (g.formats.empty()) continue;
                    if (g.type == TrackType::Video && !havePrimary) {
                        g.primary = true;
                        havePrimary = true;
                    }
                    m_GroupSets.push_back(list);
                    m_Groups.push_back(std::move(g));
                }
                if (!havePrimary && !m_Groups.empty()) m_Groups.front().primary = true;
            }

            // The representation playing a group's format in a period: the same id in the set at
            // the same place among those of its type, else the closest bitrate.
            std::shared_ptr<const Representation> RepresentationFor(const dash::Period& period, int group, const Format& wanted) const {
                const StreamGroup& g = m_Groups.at(std::size_t(group));
                std::vector<const AdaptationSet*> candidates;
                if (&period == &m_Manifest->GetPeriod(0)) {
                    for (int i : m_GroupSets.at(std::size_t(group))) candidates.push_back(&period.adaptationSets.at(std::size_t(i)));
                } else {
                    // Which of the type's groups this is, and that set of the type in this period.
                    int ordinal = 0;
                    for (int i = 0; i < group; ++i)
                        if (m_Groups[std::size_t(i)].type == g.type) ++ordinal;
                    int seen = 0;
                    for (const auto& set : period.adaptationSets) {
                        if (set.type != g.type) continue;
                        bool trick = false;
                        for (const auto& d : set.essentialProperties) trick = trick || d.schemeIdUri == kTrickMode;
                        for (const auto& d : set.supplementalProperties) trick = trick || d.schemeIdUri == kTrickMode;
                        if (trick) continue;
                        if (seen++ == ordinal) candidates.push_back(&set);
                    }
                }
                std::shared_ptr<const Representation> best;
                for (const auto* set : candidates)
                    for (const auto& rep : set->representations) {
                        if (!wanted.id.empty() && rep->format.id == wanted.id) return rep;
                        if (!best || std::llabs(std::int64_t(rep->format.bitrate) - wanted.bitrate) < std::llabs(std::int64_t(best->format.bitrate) - wanted.bitrate)) best = rep;
                    }
                return best;
            }

            std::shared_ptr<const ChunkIndex> LoadChunkIndex(const Representation& rep, const std::string& baseUrl) {
                const std::string key = rep.format.id + "|" + baseUrl;
                {
                    std::lock_guard lock(m_Mutex);
                    if (auto it = m_ChunkIndices.find(key); it != m_ChunkIndices.end()) return it->second;
                }
                const dash::RangedUri indexUri = *rep.GetIndexUri();
                // newInitializationChunk loads the initialization and index ranges in one request
                // when they adjoin.
                std::optional<dash::RangedUri> request = indexUri;
                if (const auto& init = rep.GetInitializationUri())
                    if (auto merged = init->AttemptMerge(indexUri, baseUrl)) request = merged;
                const std::vector<std::uint8_t> bytes = m_Fetcher->Get(request->ResolveUriString(baseUrl), request->start, request->length);
                std::optional<ChunkIndex> parsed = ParseSidx(bytes, request->start);
                if (!parsed) throw ParserException("no segment index (sidx) in " + request->ResolveUriString(baseUrl));
                auto index = std::make_shared<const ChunkIndex>(std::move(*parsed));
                std::lock_guard lock(m_Mutex);
                m_ChunkIndices[key] = index;
                return index;
            }

            bool IsIndexExplicit(int periodIndex) const {
                for (const auto& set : m_Manifest->GetPeriod(periodIndex).adaptationSets) {
                    if (set.representations.empty()) continue;
                    const DashSegmentIndex* index = set.representations.front()->GetIndex();
                    if (!index || index->IsExplicit()) return true;
                }
                return false;
            }
            static bool HasAudioOrVideo(const dash::Period& period) {
                return std::any_of(period.adaptationSets.begin(), period.adaptationSets.end(),
                                   [](const AdaptationSet& s) { return s.type == TrackType::Audio || s.type == TrackType::Video; });
            }
            std::int64_t AvailableStartTimeInManifestUs(int periodIndex, std::int64_t nowUs) const {
                const dash::Period& period = m_Manifest->GetPeriod(periodIndex);
                const std::int64_t periodDurationUs = m_Manifest->GetPeriodDurationUs(periodIndex);
                const std::int64_t periodStartUs = MsToUs(period.startMs);
                std::int64_t available = periodStartUs;
                const bool av = HasAudioOrVideo(period);
                for (const auto& set : period.adaptationSets) {
                    if ((av && set.type != TrackType::Audio && set.type != TrackType::Video) || set.representations.empty()) continue;
                    const DashSegmentIndex* index = set.representations.front()->GetIndex();
                    if (!index) return periodStartUs;
                    const std::int64_t count = index->GetAvailableSegmentCount(periodDurationUs, nowUs);
                    if (count == 0) return periodStartUs;
                    available = std::max(available, periodStartUs + index->GetTimeUs(index->GetFirstAvailableSegmentNum(periodDurationUs, nowUs)));
                }
                return available;
            }
            std::int64_t AvailableEndTimeInManifestUs(int periodIndex, std::int64_t nowUs) const {
                const dash::Period& period = m_Manifest->GetPeriod(periodIndex);
                const std::int64_t periodDurationUs = m_Manifest->GetPeriodDurationUs(periodIndex);
                const std::int64_t periodStartUs = MsToUs(period.startMs);
                std::int64_t available = INT64_MAX;
                const bool av = HasAudioOrVideo(period);
                for (const auto& set : period.adaptationSets) {
                    if ((av && set.type != TrackType::Audio && set.type != TrackType::Video) || set.representations.empty()) continue;
                    const DashSegmentIndex* index = set.representations.front()->GetIndex();
                    if (!index) {
                        // A single-segment representation whose sidx is loaded later: its sidx
                        // decides, else the period's end.
                        if (periodDurationUs == kTimeUnset) return periodStartUs;
                        return periodStartUs + periodDurationUs;
                    }
                    const std::int64_t count = index->GetAvailableSegmentCount(periodDurationUs, nowUs);
                    if (count == 0) return periodStartUs;
                    const std::int64_t last = index->GetFirstAvailableSegmentNum(periodDurationUs, nowUs) + count - 1;
                    available = std::min(available, periodStartUs + index->GetTimeUs(last) + index->GetDurationUs(last, periodDurationUs));
                }
                return available == INT64_MAX ? periodStartUs : available;
            }

            // DashMediaSource.updateLiveConfiguration: the item's values, then the manifest's
            // ServiceDescription, suggestedPresentationDelay and the window's bounds.
            void UpdateLiveConfiguration(std::int64_t nowInWindowUs, std::int64_t windowDurationUs) {
                const DashManifest& m = *m_Manifest;
                const MediaItem::LiveConfiguration& item = m_ItemLiveConfiguration;
                const auto* sd = m.serviceDescription ? &*m.serviceDescription : nullptr;
                auto constrain = [](std::int64_t v, std::int64_t lo, std::int64_t hi) { return std::max(lo, std::min(v, hi)); };
                const std::int64_t maxPossibleMs = nowInWindowUs / 1000;
                std::int64_t maxMs = maxPossibleMs;
                if (item.maxOffsetMs != kTimeUnset) maxMs = std::min(maxMs, item.maxOffsetMs);
                else if (sd && sd->maxOffsetMs != kTimeUnset) maxMs = std::min(maxMs, sd->maxOffsetMs);
                std::int64_t minMs = (nowInWindowUs - windowDurationUs) / 1000;
                if (minMs < 0 && maxMs > 0) minMs = 0;
                if (m.minBufferTimeMs != kTimeUnset) minMs = std::min(minMs + m.minBufferTimeMs, maxPossibleMs);
                if (item.minOffsetMs != kTimeUnset) minMs = constrain(item.minOffsetMs, minMs, maxPossibleMs);
                else if (sd && sd->minOffsetMs != kTimeUnset) minMs = constrain(sd->minOffsetMs, minMs, maxPossibleMs);
                if (minMs > maxMs) maxMs = minMs;
                std::int64_t target;
                if (m_LiveConfiguration.targetOffsetMs != kTimeUnset) target = m_LiveConfiguration.targetOffsetMs;   // kept once set
                else if (sd && sd->targetOffsetMs != kTimeUnset) target = sd->targetOffsetMs;
                else if (m.suggestedPresentationDelayMs != kTimeUnset) target = m.suggestedPresentationDelayMs;
                else target = 30'000;   // DEFAULT_FALLBACK_TARGET_LIVE_OFFSET_MS
                if (target < minMs) target = minMs;
                if (target > maxMs) {
                    const std::int64_t safeUs = std::min<std::int64_t>(kMinLiveStartPositionUs, windowDurationUs / 2);
                    target = constrain((nowInWindowUs - safeUs) / 1000, minMs, maxMs);
                }
                float minSpeed = kRateUnset, maxSpeed = kRateUnset;
                if (item.minPlaybackSpeed != kRateUnset) minSpeed = item.minPlaybackSpeed;
                else if (sd) minSpeed = sd->minPlaybackSpeed;
                if (item.maxPlaybackSpeed != kRateUnset) maxSpeed = item.maxPlaybackSpeed;
                else if (sd) maxSpeed = sd->maxPlaybackSpeed;
                if (minSpeed == kRateUnset && maxSpeed == kRateUnset && (!sd || sd->targetOffsetMs == kTimeUnset)) minSpeed = maxSpeed = 1.f;
                m_LiveConfiguration = {target, minMs, maxMs, minSpeed, maxSpeed};
            }

            // The server's clock (DashMediaSource.resolveUtcTimingElement): direct values and
            // HTTP ISO / xs:dateTime / HEAD Date responses; others keep the device's clock.
            void ResolveUtcTiming() {
                if (!m_Manifest->utcTiming) return;
                const auto& t = *m_Manifest->utcTiming;
                const std::string& scheme = t.schemeIdUri;
                try {
                    std::int64_t serverMs = kTimeUnset;
                    if (scheme == "urn:mpeg:dash:utc:direct:2014" || scheme == "urn:mpeg:dash:utc:direct:2012") {
                        serverMs = ParseXsDateTime(t.value);
                    } else if (scheme == "urn:mpeg:dash:utc:http-iso:2014" || scheme == "urn:mpeg:dash:utc:http-iso:2012"
                               || scheme == "urn:mpeg:dash:utc:http-xsdate:2014" || scheme == "urn:mpeg:dash:utc:http-xsdate:2012") {
                        const std::string body = BytesToString(m_Fetcher->Get(ResolveUri(m_Uri, t.value), 0, kLengthUnset));
                        serverMs = ParseXsDateTime(body);
                    }
                    if (serverMs != kTimeUnset) m_ServerOffsetMs = serverMs - NowUnixTimeMs();
                } catch (...) {
                    // DashMediaSource falls back to the device's clock on failure.
                }
            }
            std::int64_t NowServerUnixTimeMs() const { return NowUnixTimeMs() + m_ServerOffsetMs; }

            // A dynamic manifest loads again after its minimum update period (0: 5 s).
            void MaybeRefreshManifest() {
                std::string location;
                {
                    std::lock_guard lock(m_Mutex);
                    const DashManifest& m = *m_Manifest;
                    if (!m.dynamic || m.minUpdatePeriodMs == kTimeUnset) return;
                    const std::int64_t period = m.minUpdatePeriodMs == 0 ? 5000 : m.minUpdatePeriodMs;
                    if (NowElapsedMs() < m_LoadedAtMs + period) return;
                    location = m.location.empty() ? m_Uri : m.location;
                    m_LoadedAtMs = NowElapsedMs();
                }
                try {
                    std::string answered;
                    const std::string content = BytesToString(m_Fetcher->Get(location, 0, kLengthUnset, nullptr, &answered));
                    auto manifest = dash::DashManifestParser().Parse(answered, content);
                    std::lock_guard lock(m_Mutex);
                    // A manifest older than the one held is ignored (DashMediaSource.onManifestLoadCompleted).
                    if (manifest->publishTimeMs != kTimeUnset && m_Manifest->publishTimeMs != kTimeUnset && manifest->publishTimeMs < m_Manifest->publishTimeMs) return;
                    m_Manifest = std::move(manifest);
                } catch (...) {
                    // Kept: the old manifest goes on; the next refresh tries again.
                }
            }

            std::string m_Uri;
            mutable std::mutex m_Mutex;
            std::shared_ptr<const DashManifest> m_Manifest;
            std::vector<std::vector<int>> m_GroupSets;
            std::map<std::string, std::shared_ptr<const ChunkIndex>> m_ChunkIndices;
            std::int64_t m_LoadedAtMs = 0;
            std::int64_t m_ServerOffsetMs = 0;
        };
    }

    std::shared_ptr<AdaptiveSource> AdaptiveSource::Open(const std::string& uri, const std::string& mimeType, std::shared_ptr<SegmentFetcher> fetcher,
                                                         std::function<std::int64_t()> nowUnixTimeMs) {
        std::string answered;
        const std::string content = BytesToString(fetcher->Get(uri, 0, kLengthUnset, nullptr, &answered));
        std::optional<Protocol> protocol = InferProtocol(uri, mimeType);
        if (!protocol) {
            const std::string head = content.substr(0, 512);
            if (head.find("#EXTM3U") != std::string::npos) protocol = Protocol::Hls;
            else if (head.find("<MPD") != std::string::npos) protocol = Protocol::Dash;
            else throw ParserException("not an HLS or DASH manifest: " + uri);
        }
        if (*protocol == Protocol::Hls) {
            auto source = std::make_shared<HlsSource>(std::move(fetcher), std::move(nowUnixTimeMs));
            source->Load(answered, content);
            return source;
        }
        auto source = std::make_shared<DashSource>(std::move(fetcher), std::move(nowUnixTimeMs));
        source->Load(answered, content);
        return source;
    }

}
