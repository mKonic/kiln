// media3 exoplayer-rtsp payload readers: H.264, H.265, AAC (RFC 3640), MP4A-LATM, AC-3, Opus, PCM,
// AMR, VP8, VP9, H.263 and MPEG-4 Visual.
#include "kiln/media/rtsp/RtpReaders.h"

#include "kiln/media/codec/CodecUtil.h"

#include <algorithm>
#include <cctype>

namespace kiln::media::rtsp {

    using codec::ParsableBitArray;
    using codec::ParsableByteArray;
    namespace MimeTypes = stream::MimeTypes;

    namespace {
        void CheckArgument(bool v, const std::string& message) {
            if (!v) throw std::invalid_argument(message);
        }
        void CheckState(bool v, const char* message) {
            if (!v) throw std::logic_error(message);
        }
        TrackOutput& Out(TrackOutput* output) {
            if (!output) throw std::logic_error("no track output");
            return *output;
        }
        // Writes the H.26x start code; its length.
        int WriteStartCode(TrackOutput& output) {
            ParsableByteArray startCode(std::vector<std::uint8_t>(codec::kNalStartCode.begin(), codec::kNalStartCode.end()));
            const int n = startCode.BytesLeft();
            output.SampleData(startCode, n);
            return n;
        }
    }

    void SampleCollector::SampleData(ParsableByteArray& data, int length) {
        const std::size_t at = m_Pending.size();
        m_Pending.resize(at + std::size_t(length));
        data.ReadBytes(m_Pending.data() + at, length);
    }

    void SampleCollector::SampleMetadata(std::int64_t timeUs, int flags, int size, int offset) {
        const std::size_t end = m_Pending.size() - std::size_t(offset);
        const std::size_t start = end - std::size_t(size);
        samples.push_back({ timeUs, flags, std::vector<std::uint8_t>(m_Pending.begin() + std::ptrdiff_t(start), m_Pending.begin() + std::ptrdiff_t(end)) });
        m_Pending.erase(m_Pending.begin(), m_Pending.begin() + std::ptrdiff_t(end));
    }

    std::int64_t ToSampleTimeUs(std::int64_t startTimeOffsetUs, std::int64_t rtpTimestamp, std::int64_t firstReceivedRtpTimestamp, int mediaFrequency) {
        const std::int64_t delta = (rtpTimestamp - firstReceivedRtpTimestamp) & 0xFFFFFFFFLL;
        return startTimeOffsetUs + stream::ScaleLargeTimestamp(delta, 1'000'000, mediaFrequency);
    }

    int AmrFrameSize(int frameType, bool isWideBand) {
        static constexpr int kNb[] = { 13, 14, 16, 18, 20, 21, 27, 32, 6, 7, 6, 6, 1, 1, 1, 1 };
        static constexpr int kWb[] = { 18, 24, 33, 37, 41, 47, 51, 59, 61, 6, 1, 1, 1, 1, 1, 1 };
        CheckArgument((frameType >= 0 && frameType <= 8) || frameType == 15,
                      std::string("Illegal AMR ") + (isWideBand ? "WB" : "NB") + " frame type " + std::to_string(frameType));
        return isWideBand ? kWb[frameType] : kNb[frameType];
    }

    namespace {
        // --- H.264 (RFC 6184) and H.265 (RFC 7798) ------------------------------------------
        class H26xReader : public RtpPayloadReader {
        protected:
            explicit H26xReader(RtpPayloadFormat format) : m_PayloadFormat(std::move(format)) {}
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t, int) override {}
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_PreviousTimestamp = kTimeUnset;
                ResetForNewAccessUnit();
                m_StartTimeOffsetUs = timeUs;
            }
            void ResetForNewAccessUnit() {
                m_Corrupted = false;
                m_InFragmentationUnit = false;
                m_FragmentedSampleSizeBytes = 0;
                m_BufferFlags = 0;
            }
            // consume's frame: new access units on a timestamp change, the metadata on the marker.
            template <typename Body> void ConsumeWith(std::int64_t timestamp, int sequenceNumber, bool rtpMarker, Body&& body) {
                TrackOutput& out = Out(m_Output);
                if (m_PreviousTimestamp != kTimeUnset && timestamp != m_PreviousTimestamp) ResetForNewAccessUnit();
                if (!m_Corrupted) body();
                if (m_FirstReceivedTimestamp == kTimeUnset) m_FirstReceivedTimestamp = timestamp;
                if (rtpMarker) {
                    if (!m_Corrupted)
                        out.SampleMetadata(ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, 90'000), m_BufferFlags,
                                           m_FragmentedSampleSizeBytes, 0);
                    ResetForNewAccessUnit();
                }
                m_PreviousTimestamp = timestamp;
                m_PreviousSequenceNumber = sequenceNumber;
            }

            RtpPayloadFormat m_PayloadFormat;
            TrackOutput* m_Output = nullptr;
            ParsableByteArray m_FuScratch;
            int m_BufferFlags = 0;
            std::int64_t m_FirstReceivedTimestamp = kTimeUnset;
            int m_PreviousSequenceNumber = -1;
            std::int64_t m_PreviousTimestamp = kTimeUnset;
            int m_FragmentedSampleSizeBytes = 0;
            std::int64_t m_StartTimeOffsetUs = 0;
            bool m_Corrupted = false, m_InFragmentationUnit = false;
        };

        class RtpH264Reader final : public H26xReader {
        public:
            explicit RtpH264Reader(RtpPayloadFormat format) : H26xReader(std::move(format)) {}
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) override {
                ConsumeWith(timestamp, sequenceNumber, rtpMarker, [&] {
                    if (data.GetData().empty()) throw stream::ParserException("malformed RTP H264 packet");
                    const int mode = data.GetData()[0] & 0x1F;
                    if (m_InFragmentationUnit && mode != 28) m_Corrupted = true;
                    if (m_Corrupted) return;
                    if (mode > 0 && mode < 24) SingleNalUnit(data);
                    else if (mode == 24) SingleTimeAggregation(data);
                    else if (mode == 28) FragmentationUnit(data, sequenceNumber);
                    else throw stream::ParserException("RTP H264 packetization mode [" + std::to_string(mode) + "] not supported.");
                });
            }

        private:
            static int Flags(int nalType) { return nalType == 5 ? kBufferFlagKeyFrame : 0; }
            void SingleNalUnit(ParsableByteArray& data) {
                TrackOutput& out = Out(m_Output);
                const int n = data.BytesLeft();
                m_FragmentedSampleSizeBytes += WriteStartCode(out);
                out.SampleData(data, n);
                m_FragmentedSampleSizeBytes += n;
                m_BufferFlags = Flags(data.GetData()[0] & 0x1F);
            }
            void SingleTimeAggregation(ParsableByteArray& data) {
                TrackOutput& out = Out(m_Output);
                data.ReadUnsignedByte();
                while (data.BytesLeft() > 4) {
                    const int length = data.ReadUnsignedShort();
                    m_FragmentedSampleSizeBytes += WriteStartCode(out);
                    out.SampleData(data, length);
                    m_FragmentedSampleSizeBytes += length;
                }
                m_BufferFlags = 0;
            }
            void FragmentationUnit(ParsableByteArray& data, int sequenceNumber) {
                TrackOutput& out = Out(m_Output);
                const int fuIndicator = std::int8_t(data.GetData()[0]);
                const int fuHeader = std::int8_t(data.GetData()[1]);
                const int nalHeader = (fuIndicator & 0xE0) | (fuHeader & 0x1F);
                const bool first = (fuHeader & 0x80) > 0, last = (fuHeader & 0x40) > 0;
                if (first) {
                    if (m_InFragmentationUnit) {
                        m_Corrupted = true;
                        m_FragmentedSampleSizeBytes = 0;
                    }
                    m_InFragmentationUnit = true;
                    m_FragmentedSampleSizeBytes += WriteStartCode(out);
                    data.GetData()[1] = std::uint8_t(nalHeader);
                    m_FuScratch.Reset(data.GetData());
                    m_FuScratch.SetPosition(1);
                } else {
                    if (m_Corrupted) return;
                    if (sequenceNumber != RtpPacket::GetNextSequenceNumber(m_PreviousSequenceNumber)) {
                        m_Corrupted = true;   // "Received RTP packet with unexpected sequence number"
                        return;
                    }
                    m_FuScratch.Reset(data.GetData());
                    m_FuScratch.SetPosition(2);
                }
                const int size = m_FuScratch.BytesLeft();
                out.SampleData(m_FuScratch, size);
                m_FragmentedSampleSizeBytes += size;
                if (last) {
                    m_InFragmentationUnit = false;
                    m_BufferFlags = Flags(nalHeader & 0x1F);
                }
            }
        };

        class RtpH265Reader final : public H26xReader {
        public:
            explicit RtpH265Reader(RtpPayloadFormat format) : H26xReader(std::move(format)) {}
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) override {
                if (data.GetData().empty()) throw stream::ParserException("Empty RTP data packet.");
                ConsumeWith(timestamp, sequenceNumber, rtpMarker, [&] {
                    const int type = (data.GetData()[0] >> 1) & 0x3F;
                    if (m_InFragmentationUnit && type != 49) m_Corrupted = true;
                    if (m_Corrupted) return;
                    if (type >= 0 && type < 48) SingleNalUnit(data);
                    else if (type == 48) Aggregation(data);
                    else if (type == 49) FragmentationUnit(data, sequenceNumber);
                    else throw stream::ParserException("RTP H265 payload type [" + std::to_string(type) + "] not supported.");
                });
            }

        private:
            static int Flags(int nalType) { return nalType == 19 || nalType == 20 ? kBufferFlagKeyFrame : 0; }
            void SingleNalUnit(ParsableByteArray& data) {
                TrackOutput& out = Out(m_Output);
                const int n = data.BytesLeft();
                m_FragmentedSampleSizeBytes += WriteStartCode(out);
                out.SampleData(data, n);
                m_FragmentedSampleSizeBytes += n;
                m_BufferFlags = Flags((data.GetData()[0] >> 1) & 0x3F);
            }
            void Aggregation(ParsableByteArray& data) {
                TrackOutput& out = Out(m_Output);
                int count = 0;
                data.SetPosition(2);
                while (data.BytesLeft() > 2) {
                    const int size = data.ReadUnsignedShort();
                    const int nalType = codec::NalUnitUtil::GetH265NalUnitType(data.GetData().data(), data.GetPosition() - 3);
                    if (data.BytesLeft() < size) throw stream::ParserException("Malformed Aggregation Packet. NAL unit size exceeds packet size.");
                    m_FragmentedSampleSizeBytes += WriteStartCode(out);
                    out.SampleData(data, size);
                    m_FragmentedSampleSizeBytes += size;
                    m_BufferFlags |= Flags(nalType);
                    ++count;
                }
                if (data.BytesLeft() > 0) throw stream::ParserException("Malformed Aggregation Packet. Packet size exceeds NAL unit size.");
                if (count < 2) throw stream::ParserException("Aggregation Packet must contain at least 2 NAL units.");
            }
            void FragmentationUnit(ParsableByteArray& data, int sequenceNumber) {
                TrackOutput& out = Out(m_Output);
                if (data.GetData().size() < 3) throw stream::ParserException("Malformed FU header.");
                const int tid = data.GetData()[1] & 0x7;
                const int fuHeader = std::int8_t(data.GetData()[2]);
                const int nalUnitType = fuHeader & 0x3F;
                const bool first = (fuHeader & 0x80) > 0, last = (fuHeader & 0x40) > 0;
                if (first) {
                    if (m_InFragmentationUnit) {
                        m_Corrupted = true;
                        m_FragmentedSampleSizeBytes = 0;
                    }
                    m_InFragmentationUnit = true;
                    m_FragmentedSampleSizeBytes += WriteStartCode(out);
                    m_FuScratch.Reset(data.GetData());   // a copy (clone)
                    m_FuScratch.GetData()[1] = std::uint8_t((nalUnitType << 1) & 0x7F);
                    m_FuScratch.GetData()[2] = std::uint8_t(tid);
                    m_FuScratch.SetPosition(1);
                } else {
                    if (m_Corrupted) return;
                    // As upstream: modulo MAX_SEQUENCE_NUMBER.
                    if (sequenceNumber != (m_PreviousSequenceNumber + 1) % RtpPacket::MAX_SEQUENCE_NUMBER) {
                        m_Corrupted = true;
                        return;
                    }
                    m_FuScratch.Reset(data.GetData());
                    m_FuScratch.SetPosition(3);
                }
                const int size = m_FuScratch.BytesLeft();
                out.SampleData(m_FuScratch, size);
                m_FragmentedSampleSizeBytes += size;
                if (last) {
                    m_InFragmentationUnit = false;
                    m_BufferFlags = Flags(nalUnitType);
                }
            }
        };

        // --- AAC (RFC 3640 mpeg4-generic) ---------------------------------------------------
        class RtpAacReader final : public RtpPayloadReader {
        public:
            explicit RtpAacReader(RtpPayloadFormat format) : m_PayloadFormat(std::move(format)), m_SampleRate(m_PayloadFormat.clockRate) {
                auto it = m_PayloadFormat.fmtpParameters.find("mode");
                if (it == m_PayloadFormat.fmtpParameters.end()) throw std::logic_error("missing mode");
                if (detail::EqualsIgnoreCase(it->second, "AAC-hbr")) {
                    m_AuSizeBits = 13;
                    m_AuIndexBits = 3;
                } else if (detail::EqualsIgnoreCase(it->second, "AAC-lbr")) {
                    m_AuSizeBits = 6;
                    m_AuIndexBits = 2;
                } else {
                    throw std::logic_error("AAC mode not supported");
                }
                m_AuHeaderBits = m_AuIndexBits + m_AuSizeBits;
            }
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t timestamp, int) override { m_FirstReceivedTimestamp = timestamp; }
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int, bool rtpMarker) override {
                TrackOutput& out = Out(m_Output);
                const int headersBitLength = data.ReadShort();
                const int count = headersBitLength / m_AuHeaderBits;
                std::int64_t sampleTimeUs = ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, m_SampleRate);
                m_AuHeaders.Reset(data);
                if (count == 1) {
                    const int auSize = m_AuHeaders.ReadBits(m_AuSizeBits);
                    m_AuHeaders.SkipBits(m_AuIndexBits);
                    out.SampleData(data, data.BytesLeft());
                    if (rtpMarker) out.SampleMetadata(sampleTimeUs, kBufferFlagKeyFrame, auSize, 0);
                } else {
                    data.SkipBytes((headersBitLength + 7) / 8);
                    for (int i = 0; i < count; ++i) {
                        const int auSize = m_AuHeaders.ReadBits(m_AuSizeBits);
                        m_AuHeaders.SkipBits(m_AuIndexBits);
                        out.SampleData(data, auSize);
                        out.SampleMetadata(sampleTimeUs, kBufferFlagKeyFrame, auSize, 0);
                        sampleTimeUs += stream::ScaleLargeTimestamp(count, 1'000'000, m_SampleRate);   // as upstream
                    }
                }
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_StartTimeOffsetUs = timeUs;
            }

        private:
            RtpPayloadFormat m_PayloadFormat;
            ParsableBitArray m_AuHeaders;
            int m_SampleRate, m_AuSizeBits = 0, m_AuIndexBits = 0, m_AuHeaderBits = 0;
            std::int64_t m_FirstReceivedTimestamp = 0, m_StartTimeOffsetUs = 0;
            TrackOutput* m_Output = nullptr;
        };

        // --- MP4A-LATM (RFC 6416) -----------------------------------------------------------
        class RtpMp4aReader final : public RtpPayloadReader {
        public:
            explicit RtpMp4aReader(RtpPayloadFormat format) : m_PayloadFormat(std::move(format)) {
                int subframes = 0;
                auto it = m_PayloadFormat.fmtpParameters.find("config");
                if (it != m_PayloadFormat.fmtpParameters.end() && it->second.size() % 2 == 0) {
                    ParsableBitArray bits(codec::BytesFromHexString(it->second));
                    const int version = bits.ReadBits(1);
                    if (version != 0) throw std::invalid_argument("unsupported audio mux version: " + std::to_string(version));
                    CheckArgument(bits.ReadBits(1) == 1, "Only supports allStreamsSameTimeFraming.");
                    subframes = bits.ReadBits(6);
                    CheckArgument(bits.ReadBits(4) == 0, "Only suppors one program.");
                    CheckArgument(bits.ReadBits(3) == 0, "Only suppors one layer.");
                }
                m_Subframes = subframes + 1;
            }
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t timestamp, int) override {
                CheckState(m_FirstReceivedTimestamp == kTimeUnset, "first packet twice");
                m_FirstReceivedTimestamp = timestamp;
            }
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) override {
                TrackOutput& out = Out(m_Output);
                const int expected = RtpPacket::GetNextSequenceNumber(m_PreviousSequenceNumber);
                if (m_FragmentedSampleSizeBytes > 0 && expected < sequenceNumber) OutputFragmented();
                for (int i = 0; i < m_Subframes; ++i) {
                    int length = 0;
                    while (data.GetPosition() < data.Limit()) {
                        const int muxLength = data.ReadUnsignedByte();
                        length += muxLength;
                        if (muxLength != 0xff) break;
                    }
                    out.SampleData(data, length);
                    m_FragmentedSampleSizeBytes += length;
                }
                m_FragmentedSampleTimeUs = ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, m_PayloadFormat.clockRate);
                if (rtpMarker) OutputFragmented();
                m_PreviousSequenceNumber = sequenceNumber;
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_FragmentedSampleSizeBytes = 0;
                m_StartTimeOffsetUs = timeUs;
            }

        private:
            void OutputFragmented() {
                Out(m_Output).SampleMetadata(m_FragmentedSampleTimeUs, kBufferFlagKeyFrame, m_FragmentedSampleSizeBytes, 0);
                m_FragmentedSampleSizeBytes = 0;
                m_FragmentedSampleTimeUs = kTimeUnset;
            }
            RtpPayloadFormat m_PayloadFormat;
            int m_Subframes = 1;
            TrackOutput* m_Output = nullptr;
            std::int64_t m_FirstReceivedTimestamp = kTimeUnset;
            int m_PreviousSequenceNumber = -1, m_FragmentedSampleSizeBytes = 0;
            std::int64_t m_StartTimeOffsetUs = 0, m_FragmentedSampleTimeUs = kTimeUnset;
        };

        // --- AC-3 (RFC 4184) ----------------------------------------------------------------
        class RtpAc3Reader final : public RtpPayloadReader {
        public:
            explicit RtpAc3Reader(RtpPayloadFormat format) : m_PayloadFormat(std::move(format)) {}
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t timestamp, int) override {
                CheckState(m_FirstReceivedTimestamp == kTimeUnset, "first packet twice");
                m_FirstReceivedTimestamp = timestamp;
            }
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int, bool rtpMarker) override {
                const int frameType = data.ReadUnsignedByte() & 0x3;
                const int frames = data.ReadUnsignedByte() & 0xFF;
                const std::int64_t sampleTimeUs = ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, m_PayloadFormat.clockRate);
                switch (frameType) {
                    case 0:
                        MaybeOutputPending();
                        if (frames == 1) SingleFrame(data, sampleTimeUs);
                        else MultiFrame(data, frames, sampleTimeUs);
                        break;
                    case 1:
                    case 2:
                        MaybeOutputPending();
                        [[fallthrough]];
                    case 3:
                        Fragment(data, rtpMarker, frameType, sampleTimeUs);
                        break;
                    default:
                        throw std::invalid_argument(std::to_string(frameType));
                }
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_StartTimeOffsetUs = timeUs;
            }

        private:
            void SingleFrame(ParsableByteArray& data, std::int64_t timeUs) {
                const int size = data.BytesLeft();
                Out(m_Output).SampleData(data, size);
                Out(m_Output).SampleMetadata(timeUs, kBufferFlagKeyFrame, size, 0);
            }
            void MultiFrame(ParsableByteArray& data, int frames, std::int64_t timeUs) {
                m_Scratch.Reset(data.GetData());
                m_Scratch.SkipBytes(2);
                for (int i = 0; i < frames; ++i) {
                    const codec::Ac3Util::SyncFrameInfo info = codec::Ac3Util::ParseAc3SyncframeInfo(m_Scratch);
                    Out(m_Output).SampleData(data, info.frameSize);
                    Out(m_Output).SampleMetadata(timeUs, kBufferFlagKeyFrame, info.frameSize, 0);
                    timeUs += std::int64_t(info.sampleCount / info.sampleRate) * 1'000'000;   // integer division, as upstream
                    m_Scratch.SkipBytes(info.frameSize);
                }
            }
            void Fragment(ParsableByteArray& data, bool frameBoundary, int frameType, std::int64_t timeUs) {
                const int n = data.BytesLeft();
                Out(m_Output).SampleData(data, n);
                m_PendingBytes += n;
                m_PendingTimeUs = timeUs;
                if (frameBoundary && frameType == 3) OutputPending();
            }
            void MaybeOutputPending() {
                if (m_PendingBytes > 0) OutputPending();
            }
            void OutputPending() {
                Out(m_Output).SampleMetadata(m_PendingTimeUs, kBufferFlagKeyFrame, m_PendingBytes, 0);
                m_PendingBytes = 0;
            }
            RtpPayloadFormat m_PayloadFormat;
            ParsableBitArray m_Scratch;
            TrackOutput* m_Output = nullptr;
            int m_PendingBytes = 0;
            std::int64_t m_FirstReceivedTimestamp = kTimeUnset, m_PendingTimeUs = 0, m_StartTimeOffsetUs = 0;
        };

        // --- Opus (RFC 7587) ----------------------------------------------------------------
        class RtpOpusReader final : public RtpPayloadReader {
        public:
            explicit RtpOpusReader(RtpPayloadFormat format) : m_PayloadFormat(std::move(format)) {}
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t timestamp, int) override { m_FirstReceivedTimestamp = timestamp; }
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool) override {
                TrackOutput& out = Out(m_Output);
                if (!m_FoundIdHeader) {
                    const int position = data.GetPosition();
                    CheckArgument(data.Limit() > 18, "ID Header has insufficient data");
                    CheckArgument(data.ReadString(8) == "OpusHead", "ID Header missing");
                    CheckArgument(data.ReadUnsignedByte() == 1, "version number must always be 1");
                    data.SetPosition(position);
                    Format f = m_PayloadFormat.format;
                    f.initializationData = codec::OpusUtil::BuildInitializationData(data.GetData());
                    out.OnFormat(f);
                    m_FoundIdHeader = true;
                } else if (!m_FoundCommentHeader) {
                    CheckArgument(data.Limit() >= 8, "Comment Header has insufficient data");
                    CheckArgument(data.ReadString(8) == "OpusTags", "Comment Header should follow ID Header");
                    m_FoundCommentHeader = true;
                } else {
                    const int size = data.BytesLeft();
                    out.SampleData(data, size);
                    out.SampleMetadata(ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, 48'000), kBufferFlagKeyFrame, size, 0);
                }
                m_PreviousSequenceNumber = sequenceNumber;
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_StartTimeOffsetUs = timeUs;
            }

        private:
            RtpPayloadFormat m_PayloadFormat;
            TrackOutput* m_Output = nullptr;
            std::int64_t m_FirstReceivedTimestamp = -1, m_StartTimeOffsetUs = 0;
            int m_PreviousSequenceNumber = -1;
            bool m_FoundIdHeader = false, m_FoundCommentHeader = false;
        };

        // --- PCM (L8, L16, PCMA, PCMU) and AMR ----------------------------------------------
        class RtpPcmReader final : public RtpPayloadReader {
        public:
            explicit RtpPcmReader(RtpPayloadFormat format) : m_PayloadFormat(std::move(format)) {}
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t timestamp, int) override { m_FirstReceivedTimestamp = timestamp; }
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool) override {
                const std::int64_t timeUs = ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, m_PayloadFormat.clockRate);
                const int size = data.BytesLeft();
                Out(m_Output).SampleData(data, size);
                Out(m_Output).SampleMetadata(timeUs, kBufferFlagKeyFrame, size, 0);
                m_PreviousSequenceNumber = sequenceNumber;
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_StartTimeOffsetUs = timeUs;
            }

        private:
            RtpPayloadFormat m_PayloadFormat;
            TrackOutput* m_Output = nullptr;
            std::int64_t m_FirstReceivedTimestamp = kTimeUnset, m_StartTimeOffsetUs = 0;
            int m_PreviousSequenceNumber = -1;
        };

        class RtpAmrReader final : public RtpPayloadReader {
        public:
            explicit RtpAmrReader(RtpPayloadFormat format)
                : m_PayloadFormat(std::move(format)), m_WideBand(m_PayloadFormat.format.sampleMimeType == kMimeAudioAmrWb), m_SampleRate(m_PayloadFormat.clockRate) {}
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t timestamp, int) override { m_FirstReceivedTimestamp = timestamp; }
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool) override {
                data.SkipBytes(1);
                const int frameType = (data.PeekUnsignedByte() >> 3) & 0x0f;
                const int frameSize = AmrFrameSize(frameType, m_WideBand);
                const int size = data.BytesLeft();
                CheckArgument(size == frameSize, "compound payload not supported currently");
                Out(m_Output).SampleData(data, size);
                Out(m_Output).SampleMetadata(ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, m_SampleRate), kBufferFlagKeyFrame, size, 0);
                m_PreviousSequenceNumber = sequenceNumber;
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_StartTimeOffsetUs = timeUs;
            }

        private:
            RtpPayloadFormat m_PayloadFormat;
            bool m_WideBand;
            int m_SampleRate;
            TrackOutput* m_Output = nullptr;
            std::int64_t m_FirstReceivedTimestamp = kTimeUnset, m_StartTimeOffsetUs = 0;
            int m_PreviousSequenceNumber = -1;
        };

        // --- fragmented video frames: VP8, VP9, H.263 ---------------------------------------
        class FramedVideoReader : public RtpPayloadReader {
        protected:
            explicit FramedVideoReader(RtpPayloadFormat format, int emptySize) : m_PayloadFormat(std::move(format)), m_EmptySize(emptySize), m_FragmentedSampleSizeBytes(emptySize) {}
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t timestamp, int) override {
                CheckState(m_FirstReceivedTimestamp == kTimeUnset, "first packet twice");
                m_FirstReceivedTimestamp = timestamp;
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_FragmentedSampleSizeBytes = m_EmptySize;
                m_StartTimeOffsetUs = timeUs;
            }
            void OutputFragmented() {
                Out(m_Output).SampleMetadata(m_FragmentedSampleTimeUs, m_KeyFrame ? kBufferFlagKeyFrame : 0, m_FragmentedSampleSizeBytes, 0);
                m_FragmentedSampleSizeBytes = m_EmptySize;
                m_FragmentedSampleTimeUs = kTimeUnset;
                if (m_EmptySize == 0) m_KeyFrame = false;   // H.263 only
                m_GotFirstPacket = false;
            }
            void AppendFragment(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) {
                const int size = data.BytesLeft();
                Out(m_Output).SampleData(data, size);
                m_FragmentedSampleSizeBytes = (m_FragmentedSampleSizeBytes == m_EmptySize ? 0 : m_FragmentedSampleSizeBytes) + size;
                m_FragmentedSampleTimeUs = ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, 90'000);
                if (rtpMarker) OutputFragmented();
                m_PreviousSequenceNumber = sequenceNumber;
            }
            void ReportSize(int width, int height) {
                if (width != m_PayloadFormat.format.width || height != m_PayloadFormat.format.height) {
                    Format f = m_PayloadFormat.format;
                    f.width = width;
                    f.height = height;
                    Out(m_Output).OnFormat(f);
                }
            }

            RtpPayloadFormat m_PayloadFormat;
            const int m_EmptySize;   // C.LENGTH_UNSET for VP8 / VP9, 0 for H.263
            TrackOutput* m_Output = nullptr;
            std::int64_t m_FirstReceivedTimestamp = kTimeUnset, m_FragmentedSampleTimeUs = kTimeUnset, m_StartTimeOffsetUs = 0;
            int m_PreviousSequenceNumber = -1, m_FragmentedSampleSizeBytes;
            bool m_GotFirstPacket = false, m_KeyFrame = false, m_OutputFormatSet = false;
        };

        class RtpVp8Reader final : public FramedVideoReader {
        public:
            explicit RtpVp8Reader(RtpPayloadFormat format) : FramedVideoReader(std::move(format), -1) {}
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) override {
                Out(m_Output);
                if (!ValidateDescriptor(data, sequenceNumber)) return;
                if (m_FragmentedSampleSizeBytes == -1 && m_GotFirstPacket) m_KeyFrame = (data.PeekUnsignedByte() & 0x01) == 0;
                if (!m_OutputFormatSet) {
                    const int position = data.GetPosition();
                    data.SetPosition(position + 6);
                    const int width = data.ReadLittleEndianUnsignedShort() & 0x3fff;
                    const int height = data.ReadLittleEndianUnsignedShort() & 0x3fff;
                    data.SetPosition(position);
                    ReportSize(width, height);
                    m_OutputFormatSet = true;
                }
                AppendFragment(data, timestamp, sequenceNumber, rtpMarker);
            }

        private:
            bool ValidateDescriptor(ParsableByteArray& payload, int sequenceNumber) {
                const int header = payload.ReadUnsignedByte();
                if ((header & 0x10) == 0x10 && (header & 0x07) == 0) {
                    if (m_GotFirstPacket && m_FragmentedSampleSizeBytes > 0) OutputFragmented();
                    m_GotFirstPacket = true;
                } else if (m_GotFirstPacket) {
                    if (sequenceNumber < RtpPacket::GetNextSequenceNumber(m_PreviousSequenceNumber)) return false;
                } else {
                    return false;   // not the start of a new VP8 partition
                }
                if ((header & 0x80) != 0) {
                    const int x = payload.ReadUnsignedByte();
                    if ((x & 0x80) != 0) {
                        const int i = payload.ReadUnsignedByte();
                        if ((i & 0x80) != 0) payload.SkipBytes(1);
                    }
                    if ((x & 0x40) != 0) payload.SkipBytes(1);
                    if ((x & 0x20) != 0 || (x & 0x10) != 0) payload.SkipBytes(1);
                }
                return true;
            }
        };

        class RtpVp9Reader final : public FramedVideoReader {
        public:
            explicit RtpVp9Reader(RtpPayloadFormat format) : FramedVideoReader(std::move(format), -1) {}
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) override {
                Out(m_Output);
                if (!ValidateDescriptor(data, sequenceNumber)) return;
                if (m_FragmentedSampleSizeBytes == -1 && m_GotFirstPacket) m_KeyFrame = (data.PeekUnsignedByte() & 0x04) == 0;
                if (!m_OutputFormatSet && m_Width != -1 && m_Height != -1) {
                    ReportSize(m_Width, m_Height);
                    m_OutputFormatSet = true;
                }
                AppendFragment(data, timestamp, sequenceNumber, rtpMarker);
            }

        private:
            bool ValidateDescriptor(ParsableByteArray& payload, int sequenceNumber) {
                const int header = payload.ReadUnsignedByte();
                if ((header & 0x08) == 0x08) {
                    if (m_GotFirstPacket && m_FragmentedSampleSizeBytes > 0) OutputFragmented();
                    m_GotFirstPacket = true;
                } else if (m_GotFirstPacket) {
                    if (sequenceNumber < RtpPacket::GetNextSequenceNumber(m_PreviousSequenceNumber)) return false;
                } else {
                    return false;
                }
                if ((header & 0x80) != 0) {
                    const int optional = payload.ReadUnsignedByte();
                    if ((optional & 0x80) != 0 && payload.BytesLeft() < 1) return false;
                }
                CheckArgument((header & 0x10) == 0, "VP9 flexible mode is not supported.");
                if ((header & 0x20) != 0) {
                    payload.SkipBytes(1);
                    if (payload.BytesLeft() < 1) return false;
                    if ((header & 0x10) == 0) payload.SkipBytes(1);
                }
                if ((header & 0x02) != 0) {
                    const int ss = payload.ReadUnsignedByte();
                    const int spatialLayers = (ss >> 5) & 0x7;
                    if ((ss & 0x10) != 0) {
                        const int count = spatialLayers + 1;
                        if (payload.BytesLeft() < count * 4) return false;
                        for (int i = 0; i < count; ++i) {
                            m_Width = payload.ReadUnsignedShort();
                            m_Height = payload.ReadUnsignedShort();
                        }
                    }
                    if ((ss & 0x08) != 0) {
                        const int pictures = payload.ReadUnsignedByte();
                        if (payload.BytesLeft() < pictures) return false;
                        for (int p = 0; p < pictures; ++p) {
                            const int picture = payload.ReadUnsignedShort();
                            const int references = (picture & 0x0C) >> 2;
                            if (payload.BytesLeft() < references) return false;
                            payload.SkipBytes(references);
                        }
                    }
                }
                return true;
            }
            int m_Width = -1, m_Height = -1;
        };

        class RtpH263Reader final : public FramedVideoReader {
        public:
            explicit RtpH263Reader(RtpPayloadFormat format) : FramedVideoReader(std::move(format), 0) {}
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) override {
                Out(m_Output);
                const int position = data.GetPosition();
                const int header = data.ReadUnsignedShort();
                const bool pBit = (header & 0x400) > 0;
                if ((header & 0x200) != 0 || (header & 0x1F8) != 0 || (header & 0x7) != 0) return;   // VRC, PLEN or PEBIT
                if (pBit) {
                    if (m_GotFirstPacket && m_FragmentedSampleSizeBytes > 0) OutputFragmented();
                    m_GotFirstPacket = true;
                    if ((data.PeekUnsignedByte() & 0xFC) < 128) return;   // no picture start code
                    data.GetData()[std::size_t(position)] = 0;
                    data.GetData()[std::size_t(position) + 1] = 0;
                    data.SetPosition(position);
                } else if (m_GotFirstPacket) {
                    if (sequenceNumber < RtpPacket::GetNextSequenceNumber(m_PreviousSequenceNumber)) return;
                } else {
                    return;
                }
                if (m_FragmentedSampleSizeBytes == 0) {
                    ParseVopHeader(data, m_OutputFormatSet);
                    if (!m_OutputFormatSet && m_KeyFrame) {
                        ReportSize(m_Width, m_Height);
                        m_OutputFormatSet = true;
                    }
                }
                AppendFragment(data, timestamp, sequenceNumber, rtpMarker);
            }

        private:
            void ParseVopHeader(ParsableByteArray& data, bool gotResolution) {
                const int position = data.GetPosition();
                const std::int64_t shortVideoHeader = data.ReadUnsignedInt();
                if (((shortVideoHeader >> 10) & 0x3F) == 0x20) {
                    const int header = data.PeekUnsignedByte();
                    const int vopType = (header >> 1) & 0x1;
                    if (!gotResolution && vopType == 0) {
                        const int sourceFormat = (header >> 2) & 0x07;
                        if (sourceFormat == 1) {
                            m_Width = 128;
                            m_Height = 96;
                        } else {
                            m_Width = 176 << (sourceFormat - 2);
                            m_Height = 144 << (sourceFormat - 2);
                        }
                    }
                    data.SetPosition(position);
                    m_KeyFrame = vopType == 0;
                    return;
                }
                data.SetPosition(position);
                m_KeyFrame = false;
            }
            int m_Width = 0, m_Height = 0;
        };

        // --- MPEG-4 Visual (RFC 6416) -------------------------------------------------------
        class RtpMpeg4Reader final : public RtpPayloadReader {
        public:
            explicit RtpMpeg4Reader(RtpPayloadFormat format) : m_PayloadFormat(std::move(format)) {}
            void CreateTracks(TrackOutput& output) override {
                m_Output = &output;
                output.OnFormat(m_PayloadFormat.format);
            }
            void OnReceivingFirstPacket(std::int64_t, int) override {}
            void Consume(ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) override {
                TrackOutput& out = Out(m_Output);
                const int limit = data.BytesLeft();
                out.SampleData(data, limit);
                if (m_SampleLength == 0) m_BufferFlags = FlagsFromVop(data);
                m_SampleLength += limit;
                if (rtpMarker) {
                    if (m_FirstReceivedTimestamp == kTimeUnset) m_FirstReceivedTimestamp = timestamp;
                    out.SampleMetadata(ToSampleTimeUs(m_StartTimeOffsetUs, timestamp, m_FirstReceivedTimestamp, 90'000), m_BufferFlags, m_SampleLength, 0);
                    m_SampleLength = 0;
                }
                m_PreviousSequenceNumber = sequenceNumber;
            }
            void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) override {
                m_FirstReceivedTimestamp = nextRtpTimestamp;
                m_StartTimeOffsetUs = timeUs;
                m_SampleLength = 0;
            }

        private:
            // The VOP start code's coding type, searched from the data's start (Bytes.indexOf).
            static int FlagsFromVop(ParsableByteArray& data) {
                const auto& bytes = data.GetData();
                static constexpr std::uint8_t kStart[] = { 0, 0, 1, 0xB6 };
                auto it = std::search(bytes.begin(), bytes.end(), std::begin(kStart), std::end(kStart));
                if (it == bytes.end()) return 0;
                data.SetPosition(int(it - bytes.begin()) + 4);
                return (data.PeekUnsignedByte() >> 6) == 0 ? kBufferFlagKeyFrame : 0;
            }
            RtpPayloadFormat m_PayloadFormat;
            TrackOutput* m_Output = nullptr;
            int m_BufferFlags = 0, m_PreviousSequenceNumber = -1, m_SampleLength = 0;
            std::int64_t m_FirstReceivedTimestamp = kTimeUnset, m_StartTimeOffsetUs = 0;
        };
    }

    std::unique_ptr<RtpPayloadReader> RtpPayloadReader::Create(const RtpPayloadFormat& f) {
        const std::string& mime = f.format.sampleMimeType;
        if (mime == MimeTypes::AUDIO_AC3) return std::make_unique<RtpAc3Reader>(f);
        if (mime == MimeTypes::AUDIO_AAC) {
            if (f.mediaEncoding == RtpPayloadFormat::RTP_MEDIA_MPEG4_LATM_AUDIO) return std::make_unique<RtpMp4aReader>(f);
            return std::make_unique<RtpAacReader>(f);
        }
        if (mime == kMimeAudioAmrNb || mime == kMimeAudioAmrWb) return std::make_unique<RtpAmrReader>(f);
        if (mime == MimeTypes::AUDIO_OPUS) return std::make_unique<RtpOpusReader>(f);
        if (mime == kMimeAudioRaw || mime == kMimeAudioAlaw || mime == kMimeAudioMlaw) return std::make_unique<RtpPcmReader>(f);
        if (mime == kMimeVideoH263) return std::make_unique<RtpH263Reader>(f);
        if (mime == MimeTypes::VIDEO_H264) return std::make_unique<RtpH264Reader>(f);
        if (mime == MimeTypes::VIDEO_H265) return std::make_unique<RtpH265Reader>(f);
        if (mime == MimeTypes::VIDEO_MP4V) return std::make_unique<RtpMpeg4Reader>(f);
        if (mime == MimeTypes::VIDEO_VP8) return std::make_unique<RtpVp8Reader>(f);
        if (mime == MimeTypes::VIDEO_VP9) return std::make_unique<RtpVp9Reader>(f);
        return nullptr;
    }

}
