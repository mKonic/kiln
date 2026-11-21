// media3 NalUnitUtil, AacUtil, Ac3Util, OpusUtil, ColorInfo and CodecSpecificDataUtil ports.
#include "kiln/media/codec/CodecUtil.h"

#include "kiln/media/stream/StreamUtil.h"

#include <algorithm>
#include <cstdio>

namespace kiln::media::codec {

    namespace {
        constexpr int kExtendedSar = 0xFF;
        constexpr float kAspectRatioIdcValues[] = { 1.f, 1.f, 12.f / 11.f, 10.f / 11.f, 16.f / 11.f, 40.f / 33.f, 24.f / 11.f, 20.f / 11.f, 32.f / 11.f,
                                                    80.f / 33.f, 18.f / 11.f, 15.f / 11.f, 64.f / 33.f, 160.f / 99.f, 4.f / 3.f, 3.f / 2.f, 2.f };
        constexpr int kColorRangeFull = 1, kColorRangeLimited = 2;

        void SkipScalingList(ParsableNalUnitBitArray& bits, int size) {
            int lastScale = 8, nextScale = 8;
            for (int i = 0; i < size; ++i) {
                if (nextScale != 0) {
                    const int deltaScale = bits.ReadSignedExpGolombCodedInt();
                    nextScale = (lastScale + deltaScale + 256) % 256;
                }
                lastScale = nextScale == 0 ? lastScale : nextScale;
            }
        }
        void SkipHrdParameters(ParsableNalUnitBitArray& data) {
            const int count = data.ReadUnsignedExpGolombCodedInt() + 1;
            data.SkipBits(8);
            for (int i = 0; i < count; ++i) {
                data.ReadUnsignedExpGolombCodedInt();
                data.ReadUnsignedExpGolombCodedInt();
                data.SkipBit();
            }
            data.SkipBits(20);
        }
        // The VUI's aspect ratio, video signal type, chroma location (H.264 and H.265 share the
        // start).
        void ReadAspectRatio(ParsableNalUnitBitArray& data, float& ratio) {
            const int idc = data.ReadBits(8);
            if (idc == kExtendedSar) {
                const int sarWidth = data.ReadBits(16), sarHeight = data.ReadBits(16);
                if (sarWidth != 0 && sarHeight != 0) ratio = float(sarWidth) / float(sarHeight);
            } else if (idc < int(std::size(kAspectRatioIdcValues))) {
                ratio = kAspectRatioIdcValues[idc];
            }
        }
        void ReadVideoSignalType(ParsableNalUnitBitArray& data, int& space, int& range, int& transfer) {
            data.SkipBits(3);
            range = data.ReadBit() ? kColorRangeFull : kColorRangeLimited;
            if (data.ReadBit()) {
                const int primaries = data.ReadBits(8), characteristics = data.ReadBits(8);
                data.SkipBits(8);
                space = ColorInfo::IsoColorPrimariesToColorSpace(primaries);
                transfer = ColorInfo::IsoTransferCharacteristicsToColorTransfer(characteristics);
            }
        }
        NalUnitUtil::H265ProfileTierLevel ParseH265ProfileTierLevel(ParsableNalUnitBitArray& data, int maxSubLayersMinus1) {
            NalUnitUtil::H265ProfileTierLevel p;
            p.generalProfileSpace = data.ReadBits(2);
            p.generalTierFlag = data.ReadBit();
            p.generalProfileIdc = data.ReadBits(5);
            for (int i = 0; i < 32; ++i)
                if (data.ReadBit()) p.generalProfileCompatibilityFlags |= int(1u << i);
            for (int& b : p.constraintBytes) b = data.ReadBits(8);
            p.generalLevelIdc = data.ReadBits(8);
            int toSkip = 0;
            for (int i = 0; i < maxSubLayersMinus1; ++i) {
                if (data.ReadBit()) toSkip += 88;
                if (data.ReadBit()) toSkip += 8;
            }
            data.SkipBits(toSkip);
            if (maxSubLayersMinus1 > 0) data.SkipBits(2 * (8 - maxSubLayersMinus1));
            return p;
        }
        void SkipH265ScalingList(ParsableNalUnitBitArray& bits) {
            for (int sizeId = 0; sizeId < 4; ++sizeId)
                for (int matrixId = 0; matrixId < 6; matrixId += sizeId == 3 ? 3 : 1) {
                    if (!bits.ReadBit()) {
                        bits.ReadUnsignedExpGolombCodedInt();
                    } else {
                        const int coefNum = std::min(64, 1 << (4 + (sizeId << 1)));
                        if (sizeId > 1) bits.ReadSignedExpGolombCodedInt();
                        for (int i = 0; i < coefNum; ++i) bits.ReadSignedExpGolombCodedInt();
                    }
                }
        }
        void SkipH265ShortTermReferencePictureSets(ParsableNalUnitBitArray& bits) {
            const int count = bits.ReadUnsignedExpGolombCodedInt();
            int prevNeg = -1, prevPos = -1;
            std::vector<int> prevS0, prevS1;
            for (int idx = 0; idx < count; ++idx) {
                int numNeg, numPos;
                std::vector<int> s0, s1;
                const bool interRps = idx != 0 && bits.ReadBit();
                if (interRps) {
                    const int prevNumDeltaPocs = prevNeg + prevPos;
                    const int deltaRpsSign = bits.ReadBit() ? 1 : 0;
                    const int absDeltaRps = bits.ReadUnsignedExpGolombCodedInt() + 1;
                    const int deltaRps = (1 - 2 * deltaRpsSign) * absDeltaRps;
                    std::vector<bool> use(std::size_t(prevNumDeltaPocs + 1));
                    for (int j = 0; j <= prevNumDeltaPocs; ++j) use[std::size_t(j)] = !bits.ReadBit() ? bits.ReadBit() : true;
                    for (int j = prevPos - 1; j >= 0; --j) {
                        const int d = prevS1[std::size_t(j)] + deltaRps;
                        if (d < 0 && use[std::size_t(prevNeg + j)]) s0.push_back(d);
                    }
                    if (deltaRps < 0 && use[std::size_t(prevNumDeltaPocs)]) s0.push_back(deltaRps);
                    for (int j = 0; j < prevNeg; ++j) {
                        const int d = prevS0[std::size_t(j)] + deltaRps;
                        if (d < 0 && use[std::size_t(j)]) s0.push_back(d);
                    }
                    numNeg = int(s0.size());
                    for (int j = prevNeg - 1; j >= 0; --j) {
                        const int d = prevS0[std::size_t(j)] + deltaRps;
                        if (d > 0 && use[std::size_t(j)]) s1.push_back(d);
                    }
                    if (deltaRps > 0 && use[std::size_t(prevNumDeltaPocs)]) s1.push_back(deltaRps);
                    for (int j = 0; j < prevPos; ++j) {
                        const int d = prevS1[std::size_t(j)] + deltaRps;
                        if (d > 0 && use[std::size_t(prevNeg + j)]) s1.push_back(d);
                    }
                    numPos = int(s1.size());
                } else {
                    numNeg = bits.ReadUnsignedExpGolombCodedInt();
                    numPos = bits.ReadUnsignedExpGolombCodedInt();
                    for (int i = 0; i < numNeg; ++i) {
                        s0.push_back((i > 0 ? s0[std::size_t(i) - 1] : 0) - (bits.ReadUnsignedExpGolombCodedInt() + 1));
                        bits.SkipBit();
                    }
                    for (int i = 0; i < numPos; ++i) {
                        s1.push_back((i > 0 ? s1[std::size_t(i) - 1] : 0) + (bits.ReadUnsignedExpGolombCodedInt() + 1));
                        bits.SkipBit();
                    }
                }
                prevNeg = numNeg;
                prevPos = numPos;
                prevS0 = std::move(s0);
                prevS1 = std::move(s1);
            }
        }
    }

    // --- NalUnitUtil ----------------------------------------------------------------------------
    namespace NalUnitUtil {
        int UnescapeStream(std::uint8_t* data, int limit) {
            std::vector<int> escapes;
            for (int position = 0; position < limit;) {
                int next = limit;
                for (int i = position; i < limit - 2; ++i)
                    if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 3) {
                        next = i;
                        break;
                    }
                position = next;
                if (position < limit) {
                    escapes.push_back(position);
                    position += 3;
                }
            }
            const int unescapedLength = limit - int(escapes.size());
            int from = 0, to = 0;
            for (int escape : escapes) {
                const int copy = escape - from;
                std::memmove(data + to, data + from, std::size_t(copy));
                to += copy;
                data[to++] = 0;
                data[to++] = 0;
                from += copy + 3;
            }
            std::memmove(data + to, data + from, std::size_t(unescapedLength - to));
            return unescapedLength;
        }

        SpsData ParseSpsNalUnit(const std::uint8_t* nal, int offset, int limit) { return ParseSpsNalUnitPayload(nal, offset + 1, limit); }

        SpsData ParseSpsNalUnitPayload(const std::uint8_t* nal, int offset, int limit) {
            ParsableNalUnitBitArray data(nal, offset, limit);
            SpsData s;
            s.profileIdc = data.ReadBits(8);
            s.constraintsFlagsAndReservedZero2Bits = data.ReadBits(8);
            s.levelIdc = data.ReadBits(8);
            s.seqParameterSetId = data.ReadUnsignedExpGolombCodedInt();
            int chromaFormatIdc = 1;
            const int p = s.profileIdc;
            if (p == 100 || p == 110 || p == 122 || p == 244 || p == 44 || p == 83 || p == 86 || p == 118 || p == 128 || p == 138) {
                chromaFormatIdc = data.ReadUnsignedExpGolombCodedInt();
                if (chromaFormatIdc == 3) s.separateColorPlaneFlag = data.ReadBit();
                s.bitDepthLumaMinus8 = data.ReadUnsignedExpGolombCodedInt();
                s.bitDepthChromaMinus8 = data.ReadUnsignedExpGolombCodedInt();
                data.SkipBit();
                if (data.ReadBit()) {
                    const int lists = chromaFormatIdc != 3 ? 8 : 12;
                    for (int i = 0; i < lists; ++i)
                        if (data.ReadBit()) SkipScalingList(data, i < 6 ? 16 : 64);
                }
            }
            s.frameNumLength = data.ReadUnsignedExpGolombCodedInt() + 4;
            s.picOrderCountType = data.ReadUnsignedExpGolombCodedInt();
            if (s.picOrderCountType == 0) {
                s.picOrderCntLsbLength = data.ReadUnsignedExpGolombCodedInt() + 4;
            } else if (s.picOrderCountType == 1) {
                s.deltaPicOrderAlwaysZeroFlag = data.ReadBit();
                data.ReadSignedExpGolombCodedInt();
                data.ReadSignedExpGolombCodedInt();
                const std::int64_t cycle = data.ReadUnsignedExpGolombCodedInt();
                for (std::int64_t i = 0; i < cycle; ++i) data.ReadUnsignedExpGolombCodedInt();
            }
            s.maxNumRefFrames = data.ReadUnsignedExpGolombCodedInt();
            data.SkipBit();
            const int picWidthInMbs = data.ReadUnsignedExpGolombCodedInt() + 1;
            const int picHeightInMapUnits = data.ReadUnsignedExpGolombCodedInt() + 1;
            s.frameMbsOnlyFlag = data.ReadBit();
            const int frameHeightInMbs = (2 - (s.frameMbsOnlyFlag ? 1 : 0)) * picHeightInMapUnits;
            if (!s.frameMbsOnlyFlag) data.SkipBit();
            data.SkipBit();
            int width = picWidthInMbs * 16, height = frameHeightInMbs * 16;
            if (data.ReadBit()) {
                const int left = data.ReadUnsignedExpGolombCodedInt(), right = data.ReadUnsignedExpGolombCodedInt();
                const int top = data.ReadUnsignedExpGolombCodedInt(), bottom = data.ReadUnsignedExpGolombCodedInt();
                int cropUnitX, cropUnitY;
                if (chromaFormatIdc == 0) {
                    cropUnitX = 1;
                    cropUnitY = 2 - (s.frameMbsOnlyFlag ? 1 : 0);
                } else {
                    const int subWidthC = chromaFormatIdc == 3 ? 1 : 2, subHeightC = chromaFormatIdc == 1 ? 2 : 1;
                    cropUnitX = subWidthC;
                    cropUnitY = subHeightC * (2 - (s.frameMbsOnlyFlag ? 1 : 0));
                }
                width -= (left + right) * cropUnitX;
                height -= (top + bottom) * cropUnitY;
            }
            s.width = width;
            s.height = height;
            s.maxNumReorderFrames =
                (p == 44 || p == 86 || p == 100 || p == 110 || p == 122 || p == 244) && (s.constraintsFlagsAndReservedZero2Bits & 0x10) != 0 ? 0 : 16;
            if (data.ReadBit()) {   // vui_parameters_present_flag
                if (data.ReadBit()) ReadAspectRatio(data, s.pixelWidthHeightRatio);
                if (data.ReadBit()) data.SkipBit();
                if (data.ReadBit()) ReadVideoSignalType(data, s.colorSpace, s.colorRange, s.colorTransfer);
                if (data.ReadBit()) {
                    data.ReadUnsignedExpGolombCodedInt();
                    data.ReadUnsignedExpGolombCodedInt();
                }
                if (data.ReadBit()) data.SkipBits(65);
                const bool nalHrd = data.ReadBit();
                if (nalHrd) SkipHrdParameters(data);
                const bool vclHrd = data.ReadBit();
                if (vclHrd) SkipHrdParameters(data);
                if (nalHrd || vclHrd) data.SkipBit();
                data.SkipBit();
                if (data.ReadBit()) {
                    data.SkipBit();
                    for (int i = 0; i < 4; ++i) data.ReadUnsignedExpGolombCodedInt();
                    s.maxNumReorderFrames = data.ReadUnsignedExpGolombCodedInt();
                    data.ReadUnsignedExpGolombCodedInt();
                }
            }
            return s;
        }

        H265SpsData ParseH265SpsNalUnit(const std::uint8_t* nal, int offset, int limit) {
            H265SpsData s;
            {
                ParsableNalUnitBitArray header(nal, offset, limit);
                header.SkipBit();
                s.nalHeader.nalUnitType = header.ReadBits(6);
                s.nalHeader.layerId = header.ReadBits(6);
                s.nalHeader.temporalId = header.ReadBits(3) - 1;
            }
            ParsableNalUnitBitArray data(nal, offset + 2, limit);
            data.SkipBits(4);
            s.maxSubLayersMinus1 = data.ReadBits(3);
            const bool multiLayerExtSpsFlag = s.nalHeader.layerId != 0 && s.maxSubLayersMinus1 == 7;
            if (!multiLayerExtSpsFlag) {
                data.SkipBit();
                s.profileTierLevel = ParseH265ProfileTierLevel(data, s.maxSubLayersMinus1);
            }
            s.seqParameterSetId = data.ReadUnsignedExpGolombCodedInt();
            if (multiLayerExtSpsFlag) {
                if (data.ReadBit()) data.ReadBits(8);   // update_rep_format_flag, sps_rep_format_idx (needs the VPS)
            } else {
                s.chromaFormatIdc = data.ReadUnsignedExpGolombCodedInt();
                if (s.chromaFormatIdc == 3) data.SkipBit();
                s.decodedWidth = data.ReadUnsignedExpGolombCodedInt();
                s.decodedHeight = data.ReadUnsignedExpGolombCodedInt();
                if (data.ReadBit()) {
                    const int left = data.ReadUnsignedExpGolombCodedInt(), right = data.ReadUnsignedExpGolombCodedInt();
                    const int top = data.ReadUnsignedExpGolombCodedInt(), bottom = data.ReadUnsignedExpGolombCodedInt();
                    const int subWidthC = s.chromaFormatIdc == 1 || s.chromaFormatIdc == 2 ? 2 : 1;
                    const int subHeightC = s.chromaFormatIdc == 1 ? 2 : 1;
                    s.width = s.decodedWidth - subWidthC * (left + right);
                    s.height = s.decodedHeight - subHeightC * (top + bottom);
                } else {
                    s.width = s.decodedWidth;
                    s.height = s.decodedHeight;
                }
                s.bitDepthLumaMinus8 = data.ReadUnsignedExpGolombCodedInt();
                s.bitDepthChromaMinus8 = data.ReadUnsignedExpGolombCodedInt();
            }
            const int log2MaxPicOrderCntLsbMinus4 = data.ReadUnsignedExpGolombCodedInt();
            if (!multiLayerExtSpsFlag) {
                for (int i = data.ReadBit() ? 0 : s.maxSubLayersMinus1; i <= s.maxSubLayersMinus1; ++i) {
                    data.ReadUnsignedExpGolombCodedInt();
                    s.maxNumReorderPics = std::max(data.ReadUnsignedExpGolombCodedInt(), s.maxNumReorderPics);
                    data.ReadUnsignedExpGolombCodedInt();
                }
            }
            for (int i = 0; i < 6; ++i) data.ReadUnsignedExpGolombCodedInt();
            if (data.ReadBit()) {   // scaling_list_enabled_flag
                const bool infer = multiLayerExtSpsFlag && data.ReadBit();
                if (infer) data.SkipBits(6);
                else if (data.ReadBit()) SkipH265ScalingList(data);
            }
            data.SkipBits(2);
            if (data.ReadBit()) {   // pcm_enabled_flag
                data.SkipBits(8);
                data.ReadUnsignedExpGolombCodedInt();
                data.ReadUnsignedExpGolombCodedInt();
                data.SkipBit();
            }
            SkipH265ShortTermReferencePictureSets(data);
            if (data.ReadBit()) {
                const int n = data.ReadUnsignedExpGolombCodedInt();
                for (int i = 0; i < n; ++i) data.SkipBits(log2MaxPicOrderCntLsbMinus4 + 4 + 1);
            }
            data.SkipBits(2);
            if (data.ReadBit()) {   // vui_parameters_present_flag
                if (data.ReadBit()) ReadAspectRatio(data, s.pixelWidthHeightRatio);
                if (data.ReadBit()) data.SkipBit();
                if (data.ReadBit()) ReadVideoSignalType(data, s.colorSpace, s.colorRange, s.colorTransfer);
                if (data.ReadBit()) {
                    data.ReadUnsignedExpGolombCodedInt();
                    data.ReadUnsignedExpGolombCodedInt();
                }
                data.SkipBit();
                if (data.ReadBit()) s.height *= 2;   // field_seq_flag
            }
            return s;
        }
    }

    // --- AacUtil --------------------------------------------------------------------------------
    namespace AacUtil {
        namespace {
            constexpr int kSampleRates[] = { 96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350 };
            constexpr int kChannelCounts[] = { 0, 1, 2, 3, 4, 5, 6, 8, -1, -1, -1, 7, 8, -1, 8, -1 };
            int AudioObjectType(ParsableBitArray& bits) {
                int type = bits.ReadBits(5);
                if (type == 31) type = 32 + bits.ReadBits(6);
                return type;
            }
            int SamplingFrequency(ParsableBitArray& bits) {
                const int index = bits.ReadBits(4);
                if (index == 0xF) {
                    if (bits.BitsLeft() < 24) throw stream::ParserException("AAC header insufficient data");
                    return bits.ReadBits(24);
                }
                if (index < 13) return kSampleRates[index];
                throw stream::ParserException("AAC header wrong Sampling Frequency Index");
            }
            void ParseGaSpecificConfig(ParsableBitArray& bits, int type, int channelConfiguration) {
                bits.ReadBit();   // frameLengthFlag
                if (bits.ReadBit()) bits.SkipBits(14);
                const bool extensionFlag = bits.ReadBit();
                if (channelConfiguration == 0) throw std::logic_error("programConfigElement is not supported");
                if (type == 6 || type == 20) bits.SkipBits(3);
                if (extensionFlag) {
                    if (type == 22) bits.SkipBits(16);
                    if (type == 17 || type == 19 || type == 20 || type == 23) bits.SkipBits(3);
                    bits.SkipBits(1);
                }
            }
        }

        Config ParseAudioSpecificConfig(ParsableBitArray& bits, bool forceReadToEnd) {
            int type = AudioObjectType(bits);
            int sampleRate = SamplingFrequency(bits);
            int channelConfiguration = bits.ReadBits(4);
            const std::string codecs = "mp4a.40." + std::to_string(type);
            if (type == 5 || type == 29) {
                sampleRate = SamplingFrequency(bits);
                type = AudioObjectType(bits);
                if (type == 22) channelConfiguration = bits.ReadBits(4);
            }
            if (forceReadToEnd) {
                switch (type) {
                    case 1: case 2: case 3: case 4: case 6: case 7: case 17: case 19: case 20: case 21: case 22: case 23:
                        ParseGaSpecificConfig(bits, type, channelConfiguration);
                        break;
                    default:
                        throw stream::ParserException("Unsupported audio object type: " + std::to_string(type));
                }
                switch (type) {
                    case 17: case 19: case 20: case 21: case 22: case 23: {
                        const int epConfig = bits.ReadBits(2);
                        if (epConfig == 2 || epConfig == 3) throw stream::ParserException("Unsupported epConfig: " + std::to_string(epConfig));
                        break;
                    }
                    default:
                        break;
                }
            }
            const int channelCount = kChannelCounts[channelConfiguration];
            if (channelCount == -1) throw stream::ParserException("malformed AAC channel configuration");
            return { sampleRate, channelCount, codecs };
        }

        Config ParseAudioSpecificConfig(const std::vector<std::uint8_t>& config) {
            ParsableBitArray bits(config);
            return ParseAudioSpecificConfig(bits, false);
        }

        std::vector<std::uint8_t> BuildAacLcAudioSpecificConfig(int sampleRate, int channelCount) {
            int rateIndex = -1, channelConfig = -1;
            for (int i = 0; i < int(std::size(kSampleRates)); ++i)
                if (sampleRate == kSampleRates[i]) rateIndex = i;
            for (int i = 0; i < int(std::size(kChannelCounts)); ++i)
                if (channelCount == kChannelCounts[i]) channelConfig = i;
            if (sampleRate == -1 || channelConfig == -1)
                throw std::invalid_argument("Invalid sample rate or number of channels: " + std::to_string(sampleRate) + ", " + std::to_string(channelCount));
            return BuildAudioSpecificConfig(2, rateIndex, channelConfig);
        }

        std::vector<std::uint8_t> BuildAudioSpecificConfig(int type, int rateIndex, int channelConfig) {
            return { std::uint8_t(((type << 3) & 0xF8) | ((rateIndex >> 1) & 0x07)), std::uint8_t(((rateIndex << 7) & 0x80) | ((channelConfig << 3) & 0x78)) };
        }
    }

    // --- Ac3Util --------------------------------------------------------------------------------
    namespace Ac3Util {
        namespace {
            constexpr int kSamplesPerBlock = 256;
            constexpr int kBlocksByNumblkscod[] = { 1, 2, 3, 6 };
            constexpr int kSampleRateByFscod[] = { 48000, 44100, 32000 };
            constexpr int kSampleRateByFscod2[] = { 24000, 22050, 16000 };
            constexpr int kChannelCountByAcmod[] = { 2, 1, 2, 3, 3, 4, 4, 5 };
            constexpr int kBitrateByHalfFrmsizecod[] = { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640 };
            constexpr int kWords44_1[] = { 69, 87, 104, 121, 139, 174, 208, 243, 278, 348, 417, 487, 557, 696, 835, 975, 1114, 1253, 1393 };
            int SyncframeSize(int fscod, int frmsizecod) {
                const int half = frmsizecod / 2;
                if (fscod < 0 || fscod >= 3 || frmsizecod < 0 || half >= int(std::size(kWords44_1))) return int(stream::kLengthUnset);
                const int sampleRate = kSampleRateByFscod[fscod];
                if (sampleRate == 44100) return 2 * (kWords44_1[half] + (frmsizecod % 2));
                const int bitrate = kBitrateByHalfFrmsizecod[half];
                return sampleRate == 32000 ? 6 * bitrate : 4 * bitrate;
            }
        }

        SyncFrameInfo ParseAc3SyncframeInfo(ParsableBitArray& data) {
            const int initial = data.GetPosition();
            data.SkipBits(40);
            const bool isEac3 = data.ReadBits(5) > 10;
            data.SetPosition(initial);
            SyncFrameInfo info;
            if (isEac3) {
                data.SkipBits(16);
                const int strmtyp = data.ReadBits(2);
                info.streamType = strmtyp <= 2 ? strmtyp : -1;
                data.SkipBits(3);
                info.frameSize = (data.ReadBits(11) + 1) * 2;
                const int fscod = data.ReadBits(2);
                int audioBlocks, numblkscod;
                if (fscod == 3) {
                    numblkscod = 3;
                    info.sampleRate = kSampleRateByFscod2[data.ReadBits(2)];
                    audioBlocks = 6;
                } else {
                    numblkscod = data.ReadBits(2);
                    audioBlocks = kBlocksByNumblkscod[numblkscod];
                    info.sampleRate = kSampleRateByFscod[fscod];
                }
                info.sampleCount = kSamplesPerBlock * audioBlocks;
                info.bitrate = (info.frameSize * info.sampleRate) / (audioBlocks * 32);
                const int acmod = data.ReadBits(3);
                const bool lfeon = data.ReadBit();
                info.channelCount = kChannelCountByAcmod[acmod] + (lfeon ? 1 : 0);
                data.SkipBits(10);
                if (data.ReadBit()) data.SkipBits(8);
                if (acmod == 0) {
                    data.SkipBits(5);
                    if (data.ReadBit()) data.SkipBits(8);
                }
                if (info.streamType == 1 && data.ReadBit()) data.SkipBits(16);
                if (data.ReadBit()) {   // mixmdate
                    if (acmod > 2) data.SkipBits(2);
                    if ((acmod & 1) != 0 && acmod > 2) data.SkipBits(6);
                    if ((acmod & 4) != 0) data.SkipBits(6);
                    if (lfeon && data.ReadBit()) data.SkipBits(5);
                    if (info.streamType == 0) {
                        if (data.ReadBit()) data.SkipBits(6);
                        if (acmod == 0 && data.ReadBit()) data.SkipBits(6);
                        if (data.ReadBit()) data.SkipBits(6);
                        const int mixdef = data.ReadBits(2);
                        if (mixdef == 1) {
                            data.SkipBits(5);
                        } else if (mixdef == 2) {
                            data.SkipBits(12);
                        } else if (mixdef == 3) {
                            const int mixdeflen = data.ReadBits(5);
                            if (data.ReadBit()) {
                                data.SkipBits(5);
                                for (int i = 0; i < 7; ++i)
                                    if (data.ReadBit()) data.SkipBits(4);
                                if (data.ReadBit()) {   // addche
                                    if (data.ReadBit()) data.SkipBits(4);
                                    if (data.ReadBit()) data.SkipBits(4);
                                }
                            }
                            if (data.ReadBit()) {
                                data.SkipBits(5);
                                if (data.ReadBit()) {
                                    data.SkipBits(7);
                                    if (data.ReadBit()) data.SkipBits(8);
                                }
                            }
                            data.SkipBits(8 * (mixdeflen + 2));
                            data.ByteAlign();
                        }
                        if (acmod < 2) {
                            if (data.ReadBit()) data.SkipBits(14);
                            if (acmod == 0 && data.ReadBit()) data.SkipBits(14);
                        }
                        if (data.ReadBit()) {
                            if (numblkscod == 0) {
                                data.SkipBits(5);
                            } else {
                                for (int blk = 0; blk < audioBlocks; ++blk)
                                    if (data.ReadBit()) data.SkipBits(5);
                            }
                        }
                    }
                }
                if (data.ReadBit()) {   // infomdate
                    data.SkipBits(5);
                    if (acmod == 2) data.SkipBits(4);
                    if (acmod >= 6) data.SkipBits(2);
                    if (data.ReadBit()) data.SkipBits(8);
                    if (acmod == 0 && data.ReadBit()) data.SkipBits(8);
                    if (fscod < 3) data.SkipBit();
                }
                if (info.streamType == 0 && numblkscod != 3) data.SkipBit();
                if (info.streamType == 2 && (numblkscod == 3 || data.ReadBit())) data.SkipBits(6);
                info.mimeType = stream::MimeTypes::AUDIO_E_AC3;
                if (data.ReadBit()) {
                    const int addbsil = data.ReadBits(6);
                    if (addbsil == 1 && data.ReadBits(8) == 1) info.mimeType = stream::MimeTypes::AUDIO_E_AC3_JOC;
                }
            } else {
                info.mimeType = stream::MimeTypes::AUDIO_AC3;
                data.SkipBits(32);
                const int fscod = data.ReadBits(2);
                if (fscod == 3) info.mimeType.clear();
                const int frmsizecod = data.ReadBits(6);
                info.bitrate = kBitrateByHalfFrmsizecod[std::min(frmsizecod / 2, int(std::size(kBitrateByHalfFrmsizecod)) - 1)] * 1000;
                info.frameSize = SyncframeSize(fscod, frmsizecod);
                data.SkipBits(8);
                const int acmod = data.ReadBits(3);
                if ((acmod & 1) != 0 && acmod != 1) data.SkipBits(2);
                if ((acmod & 4) != 0) data.SkipBits(2);
                if (acmod == 2) data.SkipBits(2);
                info.sampleRate = fscod < 3 ? kSampleRateByFscod[fscod] : -1;
                info.sampleCount = 6 * kSamplesPerBlock;
                const bool lfeon = data.ReadBit();
                info.channelCount = kChannelCountByAcmod[acmod] + (lfeon ? 1 : 0);
            }
            return info;
        }
    }

    // --- OpusUtil -------------------------------------------------------------------------------
    std::vector<std::vector<std::uint8_t>> OpusUtil::BuildInitializationData(const std::vector<std::uint8_t>& header) {
        const int preSkipSamples = header.size() >= 12 ? (header[11] << 8 | header[10]) : 0;
        auto nanos = [](std::int64_t samples) { return samples * 1'000'000'000 / SAMPLE_RATE; };
        auto native = [](std::int64_t v) {
            std::vector<std::uint8_t> out(8);
            std::memcpy(out.data(), &v, 8);   // ByteOrder.nativeOrder()
            return out;
        };
        return { header, native(nanos(preSkipSamples)), native(nanos(3840)) };
    }

    // --- ColorInfo ------------------------------------------------------------------------------
    int ColorInfo::IsoColorPrimariesToColorSpace(int iso) {
        switch (iso) {
            case 1: return 1;                          // BT.709
            case 4: case 5: case 6: case 7: return 2;  // BT.601
            case 9: return 6;                          // BT.2020
            default: return -1;
        }
    }
    int ColorInfo::IsoTransferCharacteristicsToColorTransfer(int iso) {
        switch (iso) {
            case 1: case 6: case 7: return 3;   // SDR
            case 4: return 10;                  // gamma 2.2
            case 13: return 2;                  // sRGB
            case 16: return 6;                  // ST 2084
            case 18: return 7;                  // HLG
            default: return -1;
        }
    }

    // --- CodecSpecificDataUtil ------------------------------------------------------------------
    std::string CodecSpecificDataUtil::BuildAvcCodecString(int profileIdc, int constraints, int levelIdc) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "avc1.%02X%02X%02X", profileIdc, constraints, levelIdc);
        return buf;
    }

    std::string CodecSpecificDataUtil::BuildHevcCodecString(int space, bool tier, int profileIdc, int compatibilityFlags, const std::array<int, 6>& constraintBytes,
                                                            int levelIdc) {
        static constexpr const char* kSpaces[] = { "", "A", "B", "C" };
        char buf[64];
        std::snprintf(buf, sizeof buf, "hvc1.%s%d.%X.%c%d", kSpaces[space & 3], profileIdc, unsigned(compatibilityFlags), tier ? 'H' : 'L', levelIdc);
        std::string out = buf;
        std::size_t trailing = constraintBytes.size();
        while (trailing > 0 && constraintBytes[trailing - 1] == 0) --trailing;
        for (std::size_t i = 0; i < trailing; ++i) {
            std::snprintf(buf, sizeof buf, ".%02X", constraintBytes[i]);
            out += buf;
        }
        return out;
    }

    std::pair<int, int> CodecSpecificDataUtil::GetVideoResolutionFromMpeg4VideoConfig(const std::vector<std::uint8_t>& config) {
        int offset = 0;
        bool found = false;
        ParsableByteArray scratch(config);
        while (offset + 3 < int(config.size())) {
            if (scratch.ReadUnsignedInt24() != 1 || (config[std::size_t(offset) + 3] & 0xF0) != 0x20) {
                scratch.SetPosition(scratch.GetPosition() - 2);
                ++offset;
                continue;
            }
            found = true;
            break;
        }
        auto check = [](bool v, const char* message) {
            if (!v) throw std::invalid_argument(message);
        };
        check(found, "Invalid input: VOL not found.");
        ParsableBitArray bits(config);
        bits.SkipBits((offset + 4) * 8);
        bits.SkipBits(1);
        bits.SkipBits(8);
        if (bits.ReadBit()) bits.SkipBits(7);
        if (bits.ReadBits(4) == 0x0F) bits.SkipBits(16);
        if (bits.ReadBit()) {
            bits.SkipBits(3);
            if (bits.ReadBit()) bits.SkipBits(79);
        }
        check(bits.ReadBits(2) == 0, "Only supports rectangular video object layer shape.");
        check(bits.ReadBit(), "marker_bit");
        int resolution = bits.ReadBits(16);
        check(bits.ReadBit(), "marker_bit");
        if (bits.ReadBit()) {
            check(resolution > 0, "vop_time_increment_resolution");
            --resolution;
            int skip = 0;
            while (resolution > 0) {
                ++skip;
                resolution >>= 1;
            }
            bits.SkipBits(skip);
        }
        check(bits.ReadBit(), "marker_bit");
        const int width = bits.ReadBits(13);
        check(bits.ReadBit(), "marker_bit");
        const int height = bits.ReadBits(13);
        check(bits.ReadBit(), "marker_bit");
        bits.SkipBits(1);
        return { width, height };
    }

    std::vector<std::uint8_t> BytesFromHexString(const std::string& hex) {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        std::vector<std::uint8_t> out(hex.size() / 2);
        for (std::size_t i = 0; i < out.size(); ++i) out[i] = std::uint8_t((digit(hex[i * 2]) << 4) + digit(hex[i * 2 + 1]));
        return out;
    }

}
