// media3 exoplayer-rtsp: RtpPacket, RtpPacketReorderingQueue, RtpPayloadFormat, RtspMediaTrack.
#include "kiln/media/rtsp/Rtp.h"

#include "kiln/media/codec/CodecUtil.h"

#include <cctype>
#include <cstdlib>

namespace kiln::media::rtsp {

    using namespace detail;
    namespace MimeTypes = stream::MimeTypes;

    // --- RtpPacket ------------------------------------------------------------------------------
    void RtpPacket::SetSequenceNumber(int n) {
        if (n < MIN_SEQUENCE_NUMBER || n > MAX_SEQUENCE_NUMBER) throw std::invalid_argument("sequence number out of range");
        sequenceNumber = n & 0xFFFF;
    }

    std::optional<RtpPacket> RtpPacket::Parse(const std::uint8_t* data, int length) {
        if (length < MIN_HEADER_SIZE) return std::nullopt;
        codec::ParsableByteArray b(std::vector<std::uint8_t>(data, data + length));
        const int first = b.ReadUnsignedByte();
        const int version = first >> 6;
        RtpPacket p;
        p.padding = ((first >> 5) & 1) == 1;
        const int csrcCount = first & 0xF;
        const bool hasExtension = ((first >> 4) & 1) == 1;
        if (version != RTP_VERSION) return std::nullopt;
        const int second = b.ReadUnsignedByte();
        p.marker = ((second >> 7) & 1) == 1;
        p.payloadType = second & 0x7F;
        p.sequenceNumber = b.ReadUnsignedShort();
        p.timestamp = b.ReadUnsignedInt();
        p.ssrc = b.ReadInt();
        std::vector<std::uint8_t> csrc(std::size_t(csrcCount) * CSRC_SIZE);
        if (csrcCount > 0) b.ReadBytes(csrc.data(), int(csrc.size()));
        p.SetCsrc(std::move(csrc));
        if (hasExtension) {
            b.SkipBytes(2);
            const int extensionLength = b.ReadShort();
            if (extensionLength != 0) b.SkipBytes(extensionLength * 4);
        }
        p.payloadData.resize(std::size_t(b.BytesLeft()));
        b.ReadBytes(p.payloadData.data(), int(p.payloadData.size()));
        return p;
    }

    int RtpPacket::WriteToBuffer(std::uint8_t* target, int targetSize, int offset, int length) const {
        const int packetLength = MIN_HEADER_SIZE + CSRC_SIZE * csrcCount + int(payloadData.size());
        if (length < packetLength || targetSize - offset < packetLength) return int(stream::kLengthUnset);
        std::uint8_t* o = target + offset;
        o[0] = std::uint8_t((version << 6) | ((padding ? 1 : 0) << 5) | ((extension ? 1 : 0) << 4) | (csrcCount & 0xF));
        o[1] = std::uint8_t(((marker ? 1 : 0) << 7) | (payloadType & 0x7F));
        o[2] = std::uint8_t(sequenceNumber >> 8);
        o[3] = std::uint8_t(sequenceNumber);
        for (int i = 0; i < 4; ++i) o[4 + i] = std::uint8_t(std::uint32_t(timestamp) >> (24 - 8 * i));
        for (int i = 0; i < 4; ++i) o[8 + i] = std::uint8_t(std::uint32_t(ssrc) >> (24 - 8 * i));
        std::copy(csrc.begin(), csrc.end(), o + 12);
        std::copy(payloadData.begin(), payloadData.end(), o + 12 + csrc.size());
        return packetLength;
    }

    // --- RtpPacketReorderingQueue ---------------------------------------------------------------
    namespace {
        int SequenceNumberShift(int sequenceNumber, int previousSequenceNumber) {
            const int shift = sequenceNumber - previousSequenceNumber;
            if (std::abs(shift) > RtpPacketReorderingQueue::MAX_SEQUENCE_LEAP_ALLOWED) {
                const int wrapped = std::min(sequenceNumber, previousSequenceNumber) - std::max(sequenceNumber, previousSequenceNumber) + RtpPacket::MAX_SEQUENCE_NUMBER;
                if (wrapped < RtpPacketReorderingQueue::MAX_SEQUENCE_LEAP_ALLOWED) return sequenceNumber < previousSequenceNumber ? wrapped : -wrapped;
            }
            return shift;
        }
    }

    bool RtpPacketReorderingQueue::Compare::operator()(const Container& a, const Container& b) const {
        return SequenceNumberShift(a.packet.sequenceNumber, b.packet.sequenceNumber) < 0;
    }

    void RtpPacketReorderingQueue::Reset() {
        std::lock_guard lock(m_Mutex);
        m_Queue.clear();
        m_Started = false;
        m_LastDequeuedSequenceNumber = m_LastReceivedSequenceNumber = -1;
    }

    void RtpPacketReorderingQueue::AddToQueue(Container c) {
        m_LastReceivedSequenceNumber = c.packet.sequenceNumber;
        m_Queue.insert(std::move(c));   // a duplicate sequence number is dropped (TreeSet.add)
    }

    bool RtpPacketReorderingQueue::Offer(const RtpPacket& packet, std::int64_t receivedTimestampMs) {
        std::lock_guard lock(m_Mutex);
        if (m_Queue.size() >= 5000) throw std::logic_error("Queue size limit of 5000 reached.");
        const int sequenceNumber = packet.sequenceNumber;
        if (!m_Started) {
            m_Queue.clear();
            m_LastDequeuedSequenceNumber = RtpPacket::GetPreviousSequenceNumber(sequenceNumber);
            m_Started = true;
            AddToQueue({ packet, receivedTimestampMs });
            return true;
        }
        const int expected = RtpPacket::GetNextSequenceNumber(m_LastReceivedSequenceNumber);
        const int shift = SequenceNumberShift(sequenceNumber, expected);
        if (std::abs(shift) < MAX_SEQUENCE_LEAP_ALLOWED) {
            if (SequenceNumberShift(sequenceNumber, m_LastDequeuedSequenceNumber) > 0) {
                AddToQueue({ packet, receivedTimestampMs });
                return true;
            }
        } else {
            m_LastDequeuedSequenceNumber = RtpPacket::GetPreviousSequenceNumber(sequenceNumber);
            m_Queue.clear();
            AddToQueue({ packet, receivedTimestampMs });
            return true;
        }
        return false;
    }

    std::optional<RtpPacket> RtpPacketReorderingQueue::Poll(std::int64_t cutoffTimestampMs) {
        std::lock_guard lock(m_Mutex);
        if (m_Queue.empty()) return std::nullopt;
        const Container& first = *m_Queue.begin();
        const int sequenceNumber = first.packet.sequenceNumber;
        if (sequenceNumber == RtpPacket::GetNextSequenceNumber(m_LastDequeuedSequenceNumber) || cutoffTimestampMs >= first.receivedTimestampMs) {
            RtpPacket packet = first.packet;
            m_Queue.erase(m_Queue.begin());
            m_LastDequeuedSequenceNumber = sequenceNumber;
            return packet;
        }
        return std::nullopt;
    }

    // --- RtpPayloadFormat -----------------------------------------------------------------------
    namespace {
        std::string Upper(std::string s) {
            for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
            return s;
        }
    }

    bool RtpPayloadFormat::IsFormatSupported(const MediaDescription& d) {
        const std::string e = Upper(d.rtpMapAttribute.mediaEncoding);
        for (const char* s : { RTP_MEDIA_AC3, RTP_MEDIA_AMR, RTP_MEDIA_AMR_WB, RTP_MEDIA_H263_1998, RTP_MEDIA_H263_2000, RTP_MEDIA_H264, RTP_MEDIA_H265,
                               RTP_MEDIA_MPEG4_GENERIC, RTP_MEDIA_MPEG4_LATM_AUDIO, RTP_MEDIA_MPEG4_VIDEO, RTP_MEDIA_OPUS, RTP_MEDIA_PCM_L8, RTP_MEDIA_PCM_L16,
                               RTP_MEDIA_PCMA, RTP_MEDIA_PCMU, RTP_MEDIA_VP8, RTP_MEDIA_VP9 })
            if (e == s) return true;
        return false;
    }

    std::string RtpPayloadFormat::GetMimeTypeFromRtpMediaType(const std::string& mediaType) {
        const std::string e = Upper(mediaType);
        if (e == RTP_MEDIA_AC3) return MimeTypes::AUDIO_AC3;
        if (e == RTP_MEDIA_AMR) return kMimeAudioAmrNb;
        if (e == RTP_MEDIA_AMR_WB) return kMimeAudioAmrWb;
        if (e == RTP_MEDIA_MPEG4_GENERIC || e == RTP_MEDIA_MPEG4_LATM_AUDIO) return MimeTypes::AUDIO_AAC;
        if (e == RTP_MEDIA_OPUS) return MimeTypes::AUDIO_OPUS;
        if (e == RTP_MEDIA_PCM_L8 || e == RTP_MEDIA_PCM_L16) return kMimeAudioRaw;
        if (e == RTP_MEDIA_PCMA) return kMimeAudioAlaw;
        if (e == RTP_MEDIA_PCMU) return kMimeAudioMlaw;
        if (e == RTP_MEDIA_H263_1998 || e == RTP_MEDIA_H263_2000) return kMimeVideoH263;
        if (e == RTP_MEDIA_H264) return MimeTypes::VIDEO_H264;
        if (e == RTP_MEDIA_H265) return MimeTypes::VIDEO_H265;
        if (e == RTP_MEDIA_MPEG4_VIDEO) return MimeTypes::VIDEO_MP4V;
        if (e == RTP_MEDIA_VP8) return MimeTypes::VIDEO_VP8;
        if (e == RTP_MEDIA_VP9) return MimeTypes::VIDEO_VP9;
        throw std::invalid_argument(mediaType);
    }

    int RtpPayloadFormat::GetRawPcmEncodingType(const std::string& e) {
        if (e != RTP_MEDIA_PCM_L8 && e != RTP_MEDIA_PCM_L16) throw std::invalid_argument(e);
        return e == RTP_MEDIA_PCM_L8 ? kEncodingPcm8Bit : kEncodingPcm16BitBigEndian;
    }

    // --- RtspMediaTrack -------------------------------------------------------------------------
    namespace {
        void Require(bool v, const std::string& message) {
            if (!v) throw std::invalid_argument(message);
        }
        std::vector<std::uint8_t> ParameterSetWithStartCode(const std::string& parameterSet) {
            std::vector<std::uint8_t> nal(codec::kNalStartCode.begin(), codec::kNalStartCode.end());
            const std::vector<std::uint8_t> decoded = stream::Base64Decode(parameterSet);
            nal.insert(nal.end(), decoded.begin(), decoded.end());
            return nal;
        }
        const std::string* Find(const std::map<std::string, std::string>& m, const char* key) {
            auto it = m.find(key);
            return it == m.end() ? nullptr : &it->second;
        }
    }

    RtspMediaTrack::RtspMediaTrack(const RtspHeaders& headers, const MediaDescription& d, const std::string& sessionUri) {
        auto control = d.attributes.find(SessionDescription::ATTR_CONTROL);
        Require(control != d.attributes.end(), "missing attribute control");
        payloadFormat = GeneratePayloadFormat(d);
        uri = ExtractTrackUri(headers, sessionUri, control->second);
    }

    RtpPayloadFormat RtspMediaTrack::GeneratePayloadFormat(const MediaDescription& d) {
        Format f;
        if (d.bitrate > 0) f.SetBitrates(d.bitrate, f.peakBitrate);
        const int payloadType = d.rtpMapAttribute.payloadType;
        const std::string encoding = d.rtpMapAttribute.mediaEncoding;
        const std::string mime = RtpPayloadFormat::GetMimeTypeFromRtpMediaType(encoding);
        f.sampleMimeType = mime;
        const int clockRate = d.rtpMapAttribute.clockRate;
        int channelCount = -1;
        if (d.mediaType == MediaDescription::MEDIA_TYPE_AUDIO) {
            channelCount = d.rtpMapAttribute.encodingParameters != -1 ? d.rtpMapAttribute.encodingParameters : mime == MimeTypes::AUDIO_AC3 ? 6 : 1;
            f.sampleRate = clockRate;
            f.channelCount = channelCount;
        }
        const std::map<std::string, std::string> fmtp = d.GetFmtpParametersAsMap();
        if (mime == MimeTypes::AUDIO_AAC) {
            Require(channelCount != -1, "channel count");
            Require(!fmtp.empty(), "missing attribute fmtp");
            if (encoding == RtpPayloadFormat::RTP_MEDIA_MPEG4_LATM_AUDIO) {
                const std::string* cpresent = Find(fmtp, "cpresent");
                Require(cpresent && *cpresent == "0", "Only supports cpresent=0 in AAC audio.");
                const std::string* config = Find(fmtp, "config");
                if (!config) throw std::invalid_argument("AAC audio stream must include config fmtp parameter");
                Require(config->size() % 2 == 0, "Malformat MPEG4 config: " + *config);
                codec::ParsableBitArray bits(codec::BytesFromHexString(*config));
                Require(bits.ReadBits(1) == 0, "Only supports audio mux version 0.");
                Require(bits.ReadBits(1) == 1, "Only supports allStreamsSameTimeFraming.");
                bits.SkipBits(6);
                Require(bits.ReadBits(4) == 0, "Only supports one program.");
                Require(bits.ReadBits(3) == 0, "Only supports one numLayer.");
                codec::AacUtil::Config aac;
                try {
                    aac = codec::AacUtil::ParseAudioSpecificConfig(bits, false);
                } catch (const stream::ParserException& e) {
                    throw std::invalid_argument(e.what());
                }
                f.sampleRate = aac.sampleRateHz;
                f.channelCount = aac.channelCount;
                f.codecs = aac.codecs;
            }
            // processAacFmtpAttribute.
            const std::string* profile = Find(fmtp, "profile-level-id");
            std::string profileLevel = profile ? *profile : "";
            if (!profile && encoding == RtpPayloadFormat::RTP_MEDIA_MPEG4_LATM_AUDIO) profileLevel = "30";
            Require(!profileLevel.empty(), "missing profile-level-id param");
            f.codecs = "mp4a.40." + profileLevel;
            f.initializationData = { codec::AacUtil::BuildAacLcAudioSpecificConfig(clockRate, channelCount) };
        } else if (mime == kMimeAudioAmrNb || mime == kMimeAudioAmrWb) {
            Require(channelCount == 1, "Multi channel AMR is not currently supported.");
            Require(!fmtp.empty(), "fmtp parameters must include octet-align.");
            Require(Find(fmtp, "octet-align") != nullptr, "Only octet aligned mode is currently supported.");
            Require(Find(fmtp, "interleaving") == nullptr, "Interleaving mode is not currently supported.");
        } else if (mime == MimeTypes::AUDIO_OPUS) {
            Require(channelCount != -1, "channel count");
            Require(clockRate == 48'000, "Invalid OPUS clock rate.");
        } else if (mime == MimeTypes::VIDEO_MP4V) {
            Require(!fmtp.empty(), "missing attribute fmtp");
            if (const std::string* config = Find(fmtp, "config")) {
                const std::vector<std::uint8_t> buffer = codec::BytesFromHexString(*config);
                f.initializationData = { buffer };
                const auto [w, h] = codec::CodecSpecificDataUtil::GetVideoResolutionFromMpeg4VideoConfig(buffer);
                f.width = w;
                f.height = h;
            } else {
                f.width = 352;
                f.height = 288;
            }
            const std::string* profile = Find(fmtp, "profile-level-id");
            f.codecs = "mp4v." + (profile ? *profile : std::string("1"));
        } else if (mime == kMimeVideoH263) {
            f.width = 352;
            f.height = 288;
        } else if (mime == MimeTypes::VIDEO_H264) {
            Require(!fmtp.empty(), "missing attribute fmtp");
            const std::string* sprop = Find(fmtp, "sprop-parameter-sets");
            Require(sprop != nullptr, "missing sprop parameter");
            const auto sets = Split(*sprop, ",");
            Require(sets.size() == 2, "empty sprop value");
            f.initializationData = { ParameterSetWithStartCode(sets[0]), ParameterSetWithStartCode(sets[1]) };
            const auto& sps = f.initializationData[0];
            const auto data = codec::NalUnitUtil::ParseSpsNalUnit(sps.data(), int(codec::kNalStartCode.size()), int(sps.size()));
            f.pixelWidthHeightRatio = data.pixelWidthHeightRatio;
            f.height = data.height;
            f.width = data.width;
            f.colorSpace = data.colorSpace;
            f.colorRange = data.colorRange;
            f.colorTransfer = data.colorTransfer;
            const std::string* profile = Find(fmtp, "profile-level-id");
            f.codecs = profile ? "avc1." + *profile
                               : codec::CodecSpecificDataUtil::BuildAvcCodecString(data.profileIdc, data.constraintsFlagsAndReservedZero2Bits, data.levelIdc);
        } else if (mime == MimeTypes::VIDEO_H265) {
            Require(!fmtp.empty(), "missing attribute fmtp");
            if (const std::string* maxDonDiff = Find(fmtp, "sprop-max-don-diff")) {
                const int v = std::stoi(*maxDonDiff);
                Require(v == 0, "non-zero sprop-max-don-diff " + *maxDonDiff + " is not supported");
            }
            const std::string* vps = Find(fmtp, "sprop-vps");
            Require(vps != nullptr, "missing sprop-vps parameter");
            const std::string* sps = Find(fmtp, "sprop-sps");
            Require(sps != nullptr, "missing sprop-sps parameter");
            const std::string* pps = Find(fmtp, "sprop-pps");
            Require(pps != nullptr, "missing sprop-pps parameter");
            f.initializationData = { ParameterSetWithStartCode(*vps), ParameterSetWithStartCode(*sps), ParameterSetWithStartCode(*pps) };
            const auto& spsNal = f.initializationData[1];
            const auto data = codec::NalUnitUtil::ParseH265SpsNalUnit(spsNal.data(), int(codec::kNalStartCode.size()), int(spsNal.size()));
            f.pixelWidthHeightRatio = data.pixelWidthHeightRatio;
            f.height = data.height;
            f.width = data.width;
            f.colorSpace = data.colorSpace;
            f.colorRange = data.colorRange;
            f.colorTransfer = data.colorTransfer;
            if (data.profileTierLevel) {
                const auto& p = *data.profileTierLevel;
                f.codecs = codec::CodecSpecificDataUtil::BuildHevcCodecString(p.generalProfileSpace, p.generalTierFlag, p.generalProfileIdc,
                                                                              p.generalProfileCompatibilityFlags, p.constraintBytes, p.generalLevelIdc);
            }
        } else if (mime == MimeTypes::VIDEO_VP8 || mime == MimeTypes::VIDEO_VP9) {
            f.width = 320;
            f.height = 240;
        } else if (mime == kMimeAudioRaw) {
            f.pcmEncoding = RtpPayloadFormat::GetRawPcmEncodingType(encoding);
        }
        Require(clockRate > 0, "clock rate");
        return { f, payloadType, clockRate, fmtp, encoding };
    }

    std::string RtspMediaTrack::ExtractTrackUri(const RtspHeaders& headers, const std::string& sessionUri, const std::string& control) {
        if (UriScheme(control)) return control;   // absolute
        std::string base = sessionUri;
        if (auto contentBase = headers.Get(RtspHeaders::CONTENT_BASE); contentBase && !contentBase->empty()) base = *contentBase;
        else if (auto location = headers.Get(RtspHeaders::CONTENT_LOCATION); location && !location->empty()) base = *location;
        if (control == "*") return base;
        // Uri.Builder.appendEncodedPath: the segment after the path, before any query.
        const std::size_t cut = base.find_first_of("?#");
        std::string path = base.substr(0, cut), rest = cut == std::string::npos ? "" : base.substr(cut);
        const std::size_t authority = path.find("//");
        const bool hasPath = authority != std::string::npos && path.find('/', authority + 2) != std::string::npos;
        if (!hasPath || path.back() != '/') path += '/';
        std::string segment = control;
        while (!segment.empty() && segment.front() == '/') segment.erase(segment.begin());
        return path + segment + rest;
    }

}
