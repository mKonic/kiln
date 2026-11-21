#pragma once

// Ports of media3's codec helpers: NalUnitUtil (start codes, emulation prevention, the H.264
// and H.265 SPS parsers), AacUtil (AudioSpecificConfig), Ac3Util (sync frame info), OpusUtil
// (initialization data), ColorInfo's ISO code mapping and CodecSpecificDataUtil (codec
// strings, MPEG-4 Visual's resolution).

#include "kiln/media/codec/Parsable.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kiln::media::codec {

    inline constexpr std::array<std::uint8_t, 4> kNalStartCode = { 0, 0, 0, 1 };

    namespace NalUnitUtil {
        inline constexpr int H264_NAL_UNIT_TYPE_IDR = 5;
        inline constexpr int H264_NAL_UNIT_TYPE_SPS = 7;
        inline constexpr int H264_NAL_UNIT_TYPE_PPS = 8;
        inline constexpr int H265_NAL_UNIT_TYPE_VPS = 32;
        inline constexpr int H265_NAL_UNIT_TYPE_SPS = 33;
        inline constexpr int H265_NAL_UNIT_TYPE_PPS = 34;

        struct SpsData {
            int profileIdc = 0, constraintsFlagsAndReservedZero2Bits = 0, levelIdc = 0, seqParameterSetId = 0, maxNumRefFrames = 0;
            int width = 0, height = 0;
            float pixelWidthHeightRatio = 1;
            int bitDepthLumaMinus8 = 0, bitDepthChromaMinus8 = 0;
            bool separateColorPlaneFlag = false, frameMbsOnlyFlag = false;
            int frameNumLength = 0, picOrderCountType = 0, picOrderCntLsbLength = 0;
            bool deltaPicOrderAlwaysZeroFlag = false;
            int colorSpace = -1, colorRange = -1, colorTransfer = -1;
            int maxNumReorderFrames = 0;
        };
        struct H265NalHeader {
            int nalUnitType = 0, layerId = 0, temporalId = 0;
        };
        struct H265ProfileTierLevel {
            int generalProfileSpace = 0;
            bool generalTierFlag = false;
            int generalProfileIdc = 0, generalProfileCompatibilityFlags = 0;
            std::array<int, 6> constraintBytes{};
            int generalLevelIdc = 0;
        };
        struct H265SpsData {
            H265NalHeader nalHeader;
            int maxSubLayersMinus1 = 0;
            std::optional<H265ProfileTierLevel> profileTierLevel;
            int chromaFormatIdc = 0, bitDepthLumaMinus8 = 0, bitDepthChromaMinus8 = 0, seqParameterSetId = 0;
            int width = 0, height = 0, decodedWidth = 0, decodedHeight = 0;
            float pixelWidthHeightRatio = 1;
            int maxNumReorderPics = -1;
            int colorSpace = -1, colorRange = -1, colorTransfer = -1;
        };

        // Removes emulation prevention bytes in place; the new length.
        int UnescapeStream(std::uint8_t* data, int limit);
        // The NAL unit's type from the byte after a start code at offset (H.264 / H.265).
        inline int GetNalUnitType(const std::uint8_t* data, int offset) { return data[offset + 3] & 0x1F; }
        inline int GetH265NalUnitType(const std::uint8_t* data, int offset) { return (data[offset + 3] & 0x7E) >> 1; }
        // An SPS NAL unit from its header byte (offset) to limit.
        SpsData ParseSpsNalUnit(const std::uint8_t* nal, int offset, int limit);
        SpsData ParseSpsNalUnitPayload(const std::uint8_t* nal, int offset, int limit);
        // An H.265 SPS without a VPS (the multi-layer fields it would give stay unset).
        H265SpsData ParseH265SpsNalUnit(const std::uint8_t* nal, int offset, int limit);
    }

    namespace AacUtil {
        struct Config {
            int sampleRateHz = 0, channelCount = 0;
            std::string codecs;
        };
        Config ParseAudioSpecificConfig(ParsableBitArray& bits, bool forceReadToEnd);
        Config ParseAudioSpecificConfig(const std::vector<std::uint8_t>& config);
        std::vector<std::uint8_t> BuildAacLcAudioSpecificConfig(int sampleRate, int channelCount);
        std::vector<std::uint8_t> BuildAudioSpecificConfig(int audioObjectType, int sampleRateIndex, int channelConfig);
    }

    namespace Ac3Util {
        struct SyncFrameInfo {
            std::string mimeType;   // "" when unknown
            int streamType = -1, channelCount = 0, sampleRate = -1, frameSize = -1, sampleCount = 0, bitrate = 0;
        };
        SyncFrameInfo ParseAc3SyncframeInfo(ParsableBitArray& data);
    }

    namespace OpusUtil {
        inline constexpr int SAMPLE_RATE = 48'000;
        // The header, then the pre-skip and the seek pre-roll in nanoseconds (native byte order).
        std::vector<std::vector<std::uint8_t>> BuildInitializationData(const std::vector<std::uint8_t>& header);
    }

    namespace ColorInfo {
        int IsoColorPrimariesToColorSpace(int isoColorPrimaries);
        int IsoTransferCharacteristicsToColorTransfer(int isoTransferCharacteristics);
    }

    namespace CodecSpecificDataUtil {
        std::string BuildAvcCodecString(int profileIdc, int constraintsFlagsAndReservedZero2Bits, int levelIdc);
        std::string BuildHevcCodecString(int generalProfileSpace, bool generalTierFlag, int generalProfileIdc, int generalProfileCompatibilityFlags,
                                         const std::array<int, 6>& constraintBytes, int generalLevelIdc);
        // The width and height in an MPEG-4 Visual configuration (throws std::invalid_argument).
        std::pair<int, int> GetVideoResolutionFromMpeg4VideoConfig(const std::vector<std::uint8_t>& config);
    }

    // Util.getBytesFromHexString.
    std::vector<std::uint8_t> BytesFromHexString(const std::string& hex);

}
