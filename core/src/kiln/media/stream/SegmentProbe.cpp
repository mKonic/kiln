#include "kiln/media/stream/SegmentProbe.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <string_view>

#include <kiln/Function.h>

namespace kiln::media::stream {

    namespace {
        constexpr std::size_t kTsPacketSize = 188;

        // The stream types ATSParser::Stream gives an ElementaryStreamQueue (private data with an
        // AC-4 / DTS descriptor aside).
        bool HasQueue(int streamType) {
            switch (streamType) {
                case 0x1b: case 0xdb:               // H.264, encrypted
                case 0x0f: case 0xcf:               // ADTS AAC, encrypted
                case 0x03: case 0x04:               // MPEG-1 / -2 audio
                case 0x01: case 0x02: case 0x10:    // MPEG-1 / -2 / -4 video
                case 0x83: case 0x81: case 0xc1:    // LPCM AC-3, AC-3, encrypted
                case 0x87: case 0x82:               // E-AC-3, DTS
                case 0x15:                          // metadata
                    return true;
                default:
                    return false;
            }
        }

        struct PesStream {
            bool payloadStarted = false;
            std::optional<std::int64_t> pts;
            int expectedContinuityCounter = -1;
        };

        // The PES header's 33-bit PTS (ATSParser::Stream::parsePES), if it has one.
        std::optional<std::int64_t> PesPts(const std::uint8_t* p, std::size_t n) {
            if (n < 9 || p[0] != 0 || p[1] != 0 || p[2] != 1) return std::nullopt;
            const int streamId = p[3];
            if (streamId == 0xbc || streamId == 0xbe || streamId == 0xbf || streamId == 0xf0 || streamId == 0xf1 || streamId == 0xff
                || streamId == 0xf2 || streamId == 0xf8)
                return std::nullopt;
            const int ptsDtsFlags = p[7] >> 6;
            if ((ptsDtsFlags != 2 && ptsDtsFlags != 3) || n < 14) return std::nullopt;
            return (std::int64_t(p[9] & 0x0e) << 29) | (std::int64_t(p[10]) << 22) | (std::int64_t(p[11] & 0xfe) << 14)
                 | (std::int64_t(p[12]) << 7) | (std::int64_t(p[13]) >> 1);
        }

        // A PSI section's start in a packet's payload (after the pointer field), or nullptr.
        const std::uint8_t* Section(const std::uint8_t* payload, std::size_t n, std::size_t& length) {
            if (n < 1 || std::size_t(payload[0]) + 1 + 3 > n) return nullptr;
            const std::uint8_t* s = payload + 1 + payload[0];
            const std::size_t left = n - 1 - payload[0];
            length = std::min<std::size_t>(left, 3 + (((s[1] & 0x0f) << 8) | s[2]));
            return s;
        }

        std::uint32_t ReadU32(const std::uint8_t* p) { return std::uint32_t(p[0]) << 24 | std::uint32_t(p[1]) << 16 | std::uint32_t(p[2]) << 8 | p[3]; }
        std::uint64_t ReadU64(const std::uint8_t* p) { return std::uint64_t(ReadU32(p)) << 32 | ReadU32(p + 4); }
    }

    std::optional<std::int64_t> FirstTsTimestampBase(std::span<const std::uint8_t> ts) {
        int pmtPid = -1;
        std::map<int, PesStream> streams;
        std::optional<std::int64_t> firstPending;
        for (std::size_t at = 0; at + kTsPacketSize <= ts.size(); at += kTsPacketSize) {
            const std::uint8_t* p = ts.data() + at;
            if (p[0] != 0x47) continue;
            const bool unitStart = (p[1] & 0x40) != 0;
            const int pid = ((p[1] & 0x1f) << 8) | p[2];
            const int adaptationFieldControl = (p[3] >> 4) & 3;
            const int continuityCounter = p[3] & 0x0f;
            std::size_t offset = 4;
            if (adaptationFieldControl & 2) offset += 1 + std::size_t(p[4]);
            if (!(adaptationFieldControl & 1) || offset >= kTsPacketSize) continue;
            const std::uint8_t* payload = p + offset;
            const std::size_t payloadSize = kTsPacketSize - offset;
            if (pid == 0) {
                std::size_t length = 0;
                const std::uint8_t* s = unitStart ? Section(payload, payloadSize, length) : nullptr;
                if (!s || s[0] != 0 || length < 12) continue;
                for (std::size_t i = 8; i + 4 <= length - 4; i += 4) {
                    const int program = (s[i] << 8) | s[i + 1];
                    if (program != 0) {
                        pmtPid = ((s[i + 2] & 0x1f) << 8) | s[i + 3];
                        break;
                    }
                }
                continue;
            }
            if (pid == pmtPid) {
                std::size_t length = 0;
                const std::uint8_t* s = unitStart ? Section(payload, payloadSize, length) : nullptr;
                if (!s || s[0] != 2 || length < 16) continue;
                const std::size_t programInfoLength = ((s[10] & 0x0f) << 8) | s[11];
                for (std::size_t i = 12 + programInfoLength; i + 5 <= length - 4;) {
                    const int streamType = s[i];
                    const int elementaryPid = ((s[i + 1] & 0x1f) << 8) | s[i + 2];
                    const std::size_t esInfoLength = ((s[i + 3] & 0x0f) << 8) | s[i + 4];
                    if (HasQueue(streamType)) streams.try_emplace(elementaryPid);
                    i += 5 + esInfoLength;
                }
                continue;
            }
            auto it = streams.find(pid);
            if (it == streams.end()) continue;
            PesStream& stream = it->second;
            if (stream.expectedContinuityCounter >= 0 && stream.expectedContinuityCounter != continuityCounter) {
                stream.payloadStarted = false;
                stream.expectedContinuityCounter = -1;
                if (!unitStart) continue;
            }
            stream.expectedContinuityCounter = (continuityCounter + 1) & 0x0f;
            if (!unitStart) continue;
            // flush(): the previous PES of this stream is complete.
            if (stream.payloadStarted && stream.pts) return stream.pts;
            stream.payloadStarted = true;
            stream.pts = PesPts(payload, payloadSize);
            if (stream.pts && !firstPending) firstPending = stream.pts;
        }
        // The data ended with no PES complete (the extractor's end of stream flushes them).
        return firstPending;
    }

    std::size_t Id3TagsLength(std::span<const std::uint8_t> data) {
        std::size_t at = 0;
        while (at + 10 <= data.size() && data[at] == 'I' && data[at + 1] == 'D' && data[at + 2] == '3') {
            const std::size_t size = std::size_t(data[at + 6] & 0x7f) << 21 | std::size_t(data[at + 7] & 0x7f) << 14 | std::size_t(data[at + 8] & 0x7f) << 7
                                   | std::size_t(data[at + 9] & 0x7f);
            const bool footer = (data[at + 5] & 0x10) != 0;
            at += 10 + size + (footer ? 10 : 0);
        }
        return std::min(at, data.size());
    }

    std::int64_t PeekId3PrivTimestamp(std::span<const std::uint8_t> data) {
        if (data.size() < 10 || data[0] != 'I' || data[1] != 'D' || data[2] != '3') return kTimeUnset;
        const int majorVersion = data[3];
        const int flags = data[5];
        const std::size_t size = std::size_t(data[6] & 0x7f) << 21 | std::size_t(data[7] & 0x7f) << 14 | std::size_t(data[8] & 0x7f) << 7 | std::size_t(data[9] & 0x7f);
        if (data.size() < 10 + size || (majorVersion != 3 && majorVersion != 4)) return kTimeUnset;
        std::size_t at = 10;
        const std::size_t end = 10 + size;
        if (flags & 0x40) {   // extended header
            if (at + 4 > end) return kTimeUnset;
            const std::size_t extended = majorVersion == 4 ? (std::size_t(data[at] & 0x7f) << 21 | std::size_t(data[at + 1] & 0x7f) << 14
                                                                | std::size_t(data[at + 2] & 0x7f) << 7 | std::size_t(data[at + 3] & 0x7f))
                                                             : ReadU32(&data[at]) + 4;
            at += extended;
        }
        while (at + 10 <= end) {
            const std::string_view id(reinterpret_cast<const char*>(&data[at]), 4);
            if (data[at] == 0) break;   // padding
            const std::size_t frameSize = majorVersion == 4 ? (std::size_t(data[at + 4] & 0x7f) << 21 | std::size_t(data[at + 5] & 0x7f) << 14
                                                                 | std::size_t(data[at + 6] & 0x7f) << 7 | std::size_t(data[at + 7] & 0x7f))
                                                              : ReadU32(&data[at + 4]);
            const std::size_t body = at + 10;
            if (body + frameSize > end) break;
            if (id == "PRIV") {
                const auto* owner = reinterpret_cast<const char*>(&data[body]);
                const std::size_t ownerLength = strnlen(owner, frameSize);
                if (std::string_view(owner, ownerLength) == PRIV_TIMESTAMP_FRAME_OWNER && ownerLength + 1 + 8 <= frameSize)
                    return std::int64_t(ReadU64(&data[body + ownerLength + 1]) & 0x1FFFFFFFFULL);
            }
            at = body + frameSize;
        }
        return kTimeUnset;
    }

    int ChunkIndex::GetChunkIndex(std::int64_t timeUs) const {
        // Util.binarySearchFloor(inclusive, stayInBounds).
        auto it = std::upper_bound(timesUs.begin(), timesUs.end(), timeUs);
        return std::max(0, int(it - timesUs.begin()) - 1);
    }

    std::optional<ChunkIndex> ParseSidx(std::span<const std::uint8_t> data, std::int64_t inputPosition) {
        std::size_t at = 0;
        while (at + 8 <= data.size()) {
            std::uint64_t size = ReadU32(&data[at]);
            std::size_t header = 8;
            if (size == 1) {
                if (at + 16 > data.size()) return std::nullopt;
                size = ReadU64(&data[at + 8]);
                header = 16;
            } else if (size == 0) {
                size = data.size() - at;
            }
            if (size < header) return std::nullopt;
            if (std::memcmp(&data[at + 4], "sidx", 4) == 0) {
                if (at + size > data.size()) return std::nullopt;
                const std::uint8_t* p = &data[at + header];
                const std::uint8_t* end = &data[at] + size;
                auto need = [&](std::size_t n) {
                    if (std::size_t(end - p) < n) throw ParserException("truncated sidx");
                };
                need(12);
                const int version = p[0];
                const std::int64_t timescale = ReadU32(p + 8);
                p += 12;
                std::int64_t earliestPresentationTime = 0;
                std::int64_t offset = inputPosition + std::int64_t(at + size);
                if (version == 0) {
                    need(8);
                    earliestPresentationTime = ReadU32(p);
                    offset += ReadU32(p + 4);
                    p += 8;
                } else {
                    need(16);
                    earliestPresentationTime = std::int64_t(ReadU64(p));
                    offset += std::int64_t(ReadU64(p + 8));
                    p += 16;
                }
                need(4);
                const int referenceCount = (p[2] << 8) | p[3];
                p += 4;
                ChunkIndex index;
                std::int64_t time = earliestPresentationTime;
                std::int64_t timeUs = ScaleLargeTimestamp(earliestPresentationTime, kMicrosPerSecond, timescale);
                for (int i = 0; i < referenceCount; ++i) {
                    need(12);
                    const std::uint32_t firstInt = ReadU32(p);
                    if (firstInt & 0x80000000u) throw ParserException("Unhandled indirect reference");
                    const std::int64_t referenceDuration = ReadU32(p + 4);
                    const int chunkSize = int(firstInt & 0x7fffffffu);
                    index.sizes.push_back(chunkSize);
                    index.offsets.push_back(offset);
                    index.timesUs.push_back(timeUs);
                    time += referenceDuration;
                    timeUs = ScaleLargeTimestamp(time, kMicrosPerSecond, timescale);
                    index.durationsUs.push_back(timeUs - index.timesUs.back());
                    offset += chunkSize;
                    p += 12;
                }
                return index;
            }
            at += std::size_t(size);
        }
        return std::nullopt;
    }

    namespace {
        // The boxes directly inside [data, data + size): type -> body (first of each type).
        void ForEachBox(std::span<const std::uint8_t> data, const kiln::Function<bool(std::string_view type, std::span<const std::uint8_t> body)>& f) {
            std::size_t at = 0;
            while (at + 8 <= data.size()) {
                std::uint64_t size = ReadU32(&data[at]);
                std::size_t header = 8;
                if (size == 1) {
                    if (at + 16 > data.size()) return;
                    size = ReadU64(&data[at + 8]);
                    header = 16;
                } else if (size == 0) {
                    size = data.size() - at;
                }
                if (size < header || at + size > data.size()) return;
                if (!f(std::string_view(reinterpret_cast<const char*>(&data[at + 4]), 4), data.subspan(at + header, std::size_t(size) - header))) return;
                at += std::size_t(size);
            }
        }
    }

    std::optional<std::int64_t> FirstFragmentDecodeTimeUs(std::span<const std::uint8_t> init, std::span<const std::uint8_t> segment) {
        // Track ids and their timescales (moov/trak: tkhd, mdia/mdhd).
        std::map<std::uint32_t, std::uint32_t> timescales;
        auto scan = [&](std::span<const std::uint8_t> bytes) {
            ForEachBox(bytes, [&](std::string_view type, std::span<const std::uint8_t> moov) {
                if (type != "moov") return true;
                ForEachBox(moov, [&](std::string_view t, std::span<const std::uint8_t> trak) {
                    if (t != "trak") return true;
                    std::uint32_t id = 0, timescale = 0;
                    ForEachBox(trak, [&](std::string_view b, std::span<const std::uint8_t> body) {
                        if (b == "tkhd" && body.size() >= 24) id = ReadU32(&body[body[0] == 1 ? 20 : 12]);
                        if (b == "mdia")
                            ForEachBox(body, [&](std::string_view m, std::span<const std::uint8_t> mdhd) {
                                if (m == "mdhd" && mdhd.size() >= 24) timescale = ReadU32(&mdhd[mdhd[0] == 1 ? 20 : 12]);
                                return true;
                            });
                        return true;
                    });
                    if (id && timescale) timescales[id] = timescale;
                    return true;
                });
                return false;
            });
        };
        scan(init);
        if (timescales.empty()) scan(segment);   // an initialization in the segment itself
        std::optional<std::int64_t> result;
        ForEachBox(segment, [&](std::string_view type, std::span<const std::uint8_t> moof) {
            if (type != "moof") return true;
            ForEachBox(moof, [&](std::string_view t, std::span<const std::uint8_t> traf) {
                if (t != "traf") return true;
                std::uint32_t id = 0;
                std::optional<std::uint64_t> decodeTime;
                ForEachBox(traf, [&](std::string_view b, std::span<const std::uint8_t> body) {
                    if (b == "tfhd" && body.size() >= 8) id = ReadU32(&body[4]);
                    if (b == "tfdt" && body.size() >= 8) decodeTime = body[0] == 1 && body.size() >= 12 ? ReadU64(&body[4]) : ReadU32(&body[4]);
                    return true;
                });
                auto it = timescales.find(id);
                if (decodeTime && it != timescales.end()) result = ScaleLargeTimestamp(std::int64_t(*decodeTime), kMicrosPerSecond, it->second);
                return false;
            });
            return false;
        });
        return result;
    }

    // --- AES-128 (FIPS-197), decryption only -----------------------------------------------------
    namespace {
        constexpr std::array<std::uint8_t, 256> kSbox = [] {
            std::array<std::uint8_t, 256> box{};
            std::uint8_t p = 1, q = 1;
            do {
                p = std::uint8_t(p ^ (p << 1) ^ (p & 0x80 ? 0x1b : 0));   // p * 3
                q ^= std::uint8_t(q << 1);
                q ^= std::uint8_t(q << 2);
                q ^= std::uint8_t(q << 4);
                if (q & 0x80) q ^= 0x09;                                     // q / 3
                const auto rotl = [](std::uint8_t x, int s) { return std::uint8_t((x << s) | (x >> (8 - s))); };
                box[p] = std::uint8_t(q ^ rotl(q, 1) ^ rotl(q, 2) ^ rotl(q, 3) ^ rotl(q, 4) ^ 0x63);
            } while (p != 1);
            box[0] = 0x63;
            return box;
        }();
        constexpr std::array<std::uint8_t, 256> kInvSbox = [] {
            std::array<std::uint8_t, 256> inv{};
            for (int i = 0; i < 256; ++i) inv[kSbox[std::size_t(i)]] = std::uint8_t(i);
            return inv;
        }();

        std::uint8_t XTime(std::uint8_t x) { return std::uint8_t((x << 1) ^ (x & 0x80 ? 0x1b : 0)); }
        std::uint8_t Mul(std::uint8_t a, std::uint8_t b) {
            std::uint8_t r = 0;
            while (b) {
                if (b & 1) r ^= a;
                a = XTime(a);
                b >>= 1;
            }
            return r;
        }

        using RoundKeys = std::array<std::uint8_t, 176>;
        RoundKeys ExpandKey(const std::uint8_t* key) {
            RoundKeys w{};
            std::memcpy(w.data(), key, 16);
            std::uint8_t rcon = 1;
            for (std::size_t i = 16; i < 176; i += 4) {
                std::uint8_t t[4] = { w[i - 4], w[i - 3], w[i - 2], w[i - 1] };
                if (i % 16 == 0) {
                    const std::uint8_t first = t[0];
                    t[0] = std::uint8_t(kSbox[t[1]] ^ rcon);
                    t[1] = kSbox[t[2]];
                    t[2] = kSbox[t[3]];
                    t[3] = kSbox[first];
                    rcon = XTime(rcon);
                }
                for (std::size_t j = 0; j < 4; ++j) w[i + j] = std::uint8_t(w[i - 16 + j] ^ t[j]);
            }
            return w;
        }

        void DecryptBlock(const RoundKeys& w, std::uint8_t* s) {
            auto addRoundKey = [&](int round) { for (int i = 0; i < 16; ++i) s[i] ^= w[std::size_t(round * 16 + i)]; };
            addRoundKey(10);
            for (int round = 9; round >= 0; --round) {
                // InvShiftRows (state is column-major: s[row + 4 * column]).
                std::uint8_t t[16];
                for (int c = 0; c < 4; ++c)
                    for (int r = 0; r < 4; ++r) t[r + 4 * ((c + r) % 4)] = s[r + 4 * c];
                for (int i = 0; i < 16; ++i) s[i] = kInvSbox[t[i]];   // InvSubBytes
                addRoundKey(round);
                if (round == 0) break;
                for (int c = 0; c < 4; ++c) {   // InvMixColumns
                    std::uint8_t* col = s + 4 * c;
                    const std::uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                    col[0] = std::uint8_t(Mul(a0, 14) ^ Mul(a1, 11) ^ Mul(a2, 13) ^ Mul(a3, 9));
                    col[1] = std::uint8_t(Mul(a0, 9) ^ Mul(a1, 14) ^ Mul(a2, 11) ^ Mul(a3, 13));
                    col[2] = std::uint8_t(Mul(a0, 13) ^ Mul(a1, 9) ^ Mul(a2, 14) ^ Mul(a3, 11));
                    col[3] = std::uint8_t(Mul(a0, 11) ^ Mul(a1, 13) ^ Mul(a2, 9) ^ Mul(a3, 14));
                }
            }
        }
    }

    std::vector<std::uint8_t> Aes128CbcDecrypt(std::span<const std::uint8_t> key, std::span<const std::uint8_t> iv, std::span<const std::uint8_t> data) {
        if (key.size() != 16 || iv.size() != 16) throw ParserException("AES-128 needs a 16-byte key and IV");
        if (data.size() % 16 != 0 || data.empty()) throw ParserException("AES-128 data is not whole blocks");
        const RoundKeys w = ExpandKey(key.data());
        std::vector<std::uint8_t> out(data.begin(), data.end());
        std::uint8_t previous[16];
        std::memcpy(previous, iv.data(), 16);
        for (std::size_t at = 0; at < out.size(); at += 16) {
            std::uint8_t cipher[16];
            std::memcpy(cipher, &out[at], 16);
            DecryptBlock(w, &out[at]);
            for (int i = 0; i < 16; ++i) out[at + std::size_t(i)] ^= previous[i];
            std::memcpy(previous, cipher, 16);
        }
        const std::uint8_t pad = out.back();
        if (pad == 0 || pad > 16) throw ParserException("bad AES-128 padding");
        for (std::size_t i = out.size() - pad; i < out.size(); ++i)
            if (out[i] != pad) throw ParserException("bad AES-128 padding");
        out.resize(out.size() - pad);
        return out;
    }

    std::vector<std::uint8_t> HlsEncryptionIv(const std::string& iv) {
        std::string_view hex = iv;
        if (hex.size() >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) hex.remove_prefix(2);
        // BigInteger(hex, 16) right-aligned in 16 bytes, keeping the low 16 when longer.
        std::vector<std::uint8_t> out(16, 0);
        int byte = 15;
        bool low = true;
        for (std::size_t i = hex.size(); i-- > 0 && byte >= 0;) {
            const char c = hex[i];
            const int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (v < 0) throw ParserException("bad IV " + iv);
            if (low) {
                out[std::size_t(byte)] = std::uint8_t(v);
            } else {
                out[std::size_t(byte)] = std::uint8_t(out[std::size_t(byte)] | (v << 4));
                --byte;
            }
            low = !low;
        }
        return out;
    }

}
