#pragma once

// media3 exoplayer-rtsp's payload readers (rtsp.reader): RTP payloads to samples, through a
// TrackOutput as media3's extractors write them.

#include "kiln/media/codec/Parsable.h"
#include "kiln/media/rtsp/Rtp.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace kiln::media::rtsp {

    inline constexpr int kBufferFlagKeyFrame = 1;   // C.BUFFER_FLAG_KEY_FRAME

    // TrackOutput: a format, sample bytes, then each sample's metadata (the last `size` bytes
    // written before the last `offset` bytes; bytes written before a sample and never committed
    // are dropped, as SampleQueue drops them).
    class TrackOutput {
    public:
        virtual ~TrackOutput() = default;
        virtual void OnFormat(const Format& format) = 0;
        // Takes length bytes from data.
        virtual void SampleData(codec::ParsableByteArray& data, int length) = 0;
        virtual void SampleMetadata(std::int64_t timeUs, int flags, int size, int offset) = 0;
    };

    // A TrackOutput that keeps what it is given (FakeTrackOutput): the formats and the samples.
    class SampleCollector : public TrackOutput {
    public:
        struct Sample {
            std::int64_t timeUs = 0;
            int flags = 0;
            std::vector<std::uint8_t> data;
        };
        void OnFormat(const Format& format) override { formats.push_back(format); }
        void SampleData(codec::ParsableByteArray& data, int length) override;
        void SampleMetadata(std::int64_t timeUs, int flags, int size, int offset) override;
        std::vector<Format> formats;
        std::vector<Sample> samples;

    private:
        std::vector<std::uint8_t> m_Pending;
    };

    // RtpReaderUtils.toSampleTimeUs: the RTP timestamp's distance from the first (32-bit
    // wrapping) in microseconds, after the start offset.
    std::int64_t ToSampleTimeUs(std::int64_t startTimeOffsetUs, std::int64_t rtpTimestamp, std::int64_t firstReceivedRtpTimestamp, int mediaFrequency);

    class RtpPayloadReader {
    public:
        virtual ~RtpPayloadReader() = default;
        // The reader's track (ExtractorOutput.track) with its first format.
        virtual void CreateTracks(TrackOutput& output) = 0;
        virtual void OnReceivingFirstPacket(std::int64_t timestamp, int sequenceNumber) = 0;
        // A packet's payload. Throws stream::ParserException for one it cannot read.
        virtual void Consume(codec::ParsableByteArray& data, std::int64_t timestamp, int sequenceNumber, bool rtpMarker) = 0;
        virtual void Seek(std::int64_t nextRtpTimestamp, std::int64_t timeUs) = 0;
        // DefaultRtpPayloadReaderFactory: nullptr for a format without a reader.
        static std::unique_ptr<RtpPayloadReader> Create(const RtpPayloadFormat& payloadFormat);
    };

    // AMR frame sizes by frame type (RtpAmrReader.getFrameSize).
    int AmrFrameSize(int frameType, bool isWideBand);

}
