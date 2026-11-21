// A movie box for SmoothStreaming fragments, from the manifest's track (DefaultSsChunkSource's
// Track: the stream's timescale, the format, 4-byte NAL lengths for video).
#include "kiln/media/smoothstreaming/SsInitSegment.h"

#include "kiln/media/stream/StreamUtil.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>

namespace kiln::media::ss {

    namespace {
        namespace MimeTypes = stream::MimeTypes;

        // A box writer: Begin writes a size placeholder and the type, End fills the size in.
        struct Writer {
            std::vector<std::uint8_t> out;
            std::vector<std::size_t> open;
            void U8(unsigned v) { out.push_back(std::uint8_t(v)); }
            void U16(unsigned v) { U8(v >> 8); U8(v); }
            void U24(std::uint32_t v) { U8(v >> 16); U8(v >> 8); U8(v); }
            void U32(std::uint32_t v) { U16(v >> 16); U16(v & 0xffff); }
            void Zeros(std::size_t n) { out.insert(out.end(), n, 0); }
            void Bytes(const std::vector<std::uint8_t>& b) { out.insert(out.end(), b.begin(), b.end()); }
            void Type(const char* t) { out.insert(out.end(), t, t + 4); }
            void Begin(const char* type) {
                open.push_back(out.size());
                U32(0);
                Type(type);
            }
            void BeginFull(const char* type, unsigned version, std::uint32_t flags) {
                Begin(type);
                U8(version);
                U24(flags);
            }
            void End() {
                const std::size_t at = open.back();
                open.pop_back();
                const std::uint32_t size = std::uint32_t(out.size() - at);
                out[at] = std::uint8_t(size >> 24);
                out[at + 1] = std::uint8_t(size >> 16);
                out[at + 2] = std::uint8_t(size >> 8);
                out[at + 3] = std::uint8_t(size);
            }
            void Matrix() {
                for (std::uint32_t v : { 0x00010000u, 0u, 0u, 0u, 0x00010000u, 0u, 0u, 0u, 0x40000000u }) U32(v);
            }
            // An MPEG-4 descriptor with a 4-byte length (ISO 14496-1 expandable size).
            void Descriptor(unsigned tag, const std::vector<std::uint8_t>& body) {
                U8(tag);
                const std::size_t n = body.size();
                U8(0x80 | ((n >> 21) & 0x7f));
                U8(0x80 | ((n >> 14) & 0x7f));
                U8(0x80 | ((n >> 7) & 0x7f));
                U8(n & 0x7f);
                Bytes(body);
            }
        };

        // A NAL unit without its start code.
        std::vector<std::uint8_t> StripStartCode(const std::vector<std::uint8_t>& nal) {
            std::size_t at = 0;
            if (nal.size() >= 4 && nal[0] == 0 && nal[1] == 0 && nal[2] == 0 && nal[3] == 1) at = 4;
            else if (nal.size() >= 3 && nal[0] == 0 && nal[1] == 0 && nal[2] == 1) at = 3;
            return { nal.begin() + std::ptrdiff_t(at), nal.end() };
        }

        bool WriteAvc(Writer& w, const Format& f) {
            std::vector<std::vector<std::uint8_t>> sps, pps;
            for (const auto& unit : f.initializationData) {
                std::vector<std::uint8_t> nal = StripStartCode(unit);
                if (nal.empty()) continue;
                const int type = nal[0] & 0x1f;
                if (type == 7) sps.push_back(std::move(nal));
                else if (type == 8) pps.push_back(std::move(nal));
            }
            if (sps.empty() || sps[0].size() < 4) return false;
            w.Begin("avc1");
            w.Zeros(6);
            w.U16(1);           // data_reference_index
            w.Zeros(16);        // pre_defined, reserved
            w.U16(std::max(f.width, 0));
            w.U16(std::max(f.height, 0));
            w.U32(0x00480000);  // 72 dpi
            w.U32(0x00480000);
            w.U32(0);
            w.U16(1);           // frame_count
            w.Zeros(32);        // compressorname
            w.U16(0x0018);
            w.U16(0xffff);
            w.Begin("avcC");
            w.U8(1);
            w.U8(sps[0][1]);    // profile, compatibility, level
            w.U8(sps[0][2]);
            w.U8(sps[0][3]);
            w.U8(0xfc | 3);     // 4-byte NAL lengths (nalUnitLengthFieldLength)
            w.U8(0xe0 | unsigned(sps.size()));
            for (const auto& s : sps) {
                w.U16(unsigned(s.size()));
                w.Bytes(s);
            }
            w.U8(unsigned(pps.size()));
            for (const auto& p : pps) {
                w.U16(unsigned(p.size()));
                w.Bytes(p);
            }
            w.End();
            w.End();
            return true;
        }

        // The AudioSampleEntry fields before the codec's box.
        void BeginAudioEntry(Writer& w, const char* type, const Format& f) {
            w.Begin(type);
            w.Zeros(6);
            w.U16(1);   // data_reference_index
            w.Zeros(8);
            w.U16(unsigned(std::max(f.channelCount, 1)));
            w.U16(16);  // sample size
            w.U32(0);
            const std::uint32_t rate = std::uint32_t(std::max(f.sampleRate, 0));
            w.U32(rate > 0xffff ? 0 : rate << 16);
        }

        // ATSC A/52 Annex F: fscod for the sample rate, acmod / lfeon for the channels, and the
        // nominal bit rate's code.
        int Fscod(int sampleRate) { return sampleRate == 48000 ? 0 : sampleRate == 44100 ? 1 : sampleRate == 32000 ? 2 : 0; }
        void ChannelMode(int channels, int& acmod, int& lfeon) {
            lfeon = channels == 6 ? 1 : 0;
            switch (channels) {
                case 1: acmod = 1; break;
                case 3: acmod = 3; break;
                case 4: acmod = 6; break;
                case 5:
                case 6: acmod = 7; break;
                default: acmod = 2; break;
            }
        }
        int BitRateCode(int bitrate) {
            static constexpr int kKbps[] = { 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640 };
            const int kbps = bitrate > 0 ? bitrate / 1000 : 384;
            for (int i = 0; i < int(std::size(kKbps)); ++i)
                if (kKbps[i] >= kbps) return i;
            return int(std::size(kKbps)) - 1;
        }

        bool WriteAudio(Writer& w, const Format& f) {
            const std::string& mime = f.sampleMimeType;
            if (mime == MimeTypes::AUDIO_AAC) {
                std::vector<std::uint8_t> asc = f.initializationData.empty() ? std::vector<std::uint8_t>{} : f.initializationData[0];
                BeginAudioEntry(w, "mp4a", f);
                w.BeginFull("esds", 0, 0);
                Writer dcd;   // DecoderConfigDescriptor's body
                dcd.U8(0x40);                 // Audio ISO/IEC 14496-3
                dcd.U8((0x05 << 2) | 1);      // audio stream
                dcd.U24(0);
                dcd.U32(std::uint32_t(std::max(f.bitrate, 0)));
                dcd.U32(std::uint32_t(std::max(f.bitrate, 0)));
                if (!asc.empty()) dcd.Descriptor(0x05, asc);
                Writer es;    // ES_Descriptor's body
                es.U16(0);    // ES_ID
                es.U8(0);
                es.Descriptor(0x04, dcd.out);
                es.Descriptor(0x06, { 0x02 });
                w.Descriptor(0x03, es.out);
                w.End();
                w.End();
                return true;
            }
            int acmod = 2, lfeon = 0;
            ChannelMode(f.channelCount, acmod, lfeon);
            if (mime == MimeTypes::AUDIO_AC3) {
                BeginAudioEntry(w, "ac-3", f);
                w.Begin("dac3");
                const std::uint32_t v = std::uint32_t(Fscod(f.sampleRate)) << 22 | 8u << 17 | 0u << 14 | std::uint32_t(acmod) << 11 | std::uint32_t(lfeon) << 10 |
                                        std::uint32_t(BitRateCode(f.bitrate)) << 5;
                w.U24(v);
                w.End();
                w.End();
                return true;
            }
            if (mime == MimeTypes::AUDIO_E_AC3) {
                BeginAudioEntry(w, "ec-3", f);
                w.Begin("dec3");
                const unsigned dataRate = unsigned(std::max(f.bitrate, 0) / 1000);
                w.U16((dataRate & 0x1fff) << 3 | 0);   // data_rate, num_ind_sub - 1 = 0
                // fscod(2) bsid(5)=16 reserved(1) asvc(1) bsmod(3) acmod(3) lfeon(1) reserved(3)
                // num_dep_sub(4)=0 reserved(1)
                const std::uint32_t v = std::uint32_t(Fscod(f.sampleRate)) << 22 | 16u << 17 | std::uint32_t(acmod) << 9 | std::uint32_t(lfeon) << 8;
                w.U24(v);
                w.End();
                w.End();
                return true;
            }
            if (mime == MimeTypes::AUDIO_OPUS) {
                BeginAudioEntry(w, "Opus", f);
                w.Begin("dOps");
                // From an OpusHead (little-endian) when the private data is one; else stereo
                // defaults with the declared channels and rate.
                const std::vector<std::uint8_t>* head = nullptr;
                for (const auto& d : f.initializationData)
                    if (d.size() >= 19 && std::memcmp(d.data(), "OpusHead", 8) == 0) head = &d;
                if (head) {
                    const auto& h = *head;
                    w.U8(0);                                   // Version
                    w.U8(h[9]);                                // OutputChannelCount
                    w.U16(unsigned(h[10] | h[11] << 8));       // PreSkip
                    w.U32(std::uint32_t(h[12] | h[13] << 8 | h[14] << 16 | std::uint32_t(h[15]) << 24));
                    w.U16(unsigned(h[16] | h[17] << 8));       // OutputGain
                    w.U8(h[18]);                               // ChannelMappingFamily
                    if (h[18] != 0 && h.size() >= 21u + h[9]) {
                        w.U8(h[19]);
                        w.U8(h[20]);
                        for (int i = 0; i < h[9]; ++i) w.U8(h[21 + std::size_t(i)]);
                    }
                } else {
                    w.U8(0);
                    w.U8(unsigned(std::max(f.channelCount, 1)));
                    w.U16(312);   // 6.5 ms at 48 kHz, libopus's usual pre-skip
                    w.U32(std::uint32_t(std::max(f.sampleRate, 0)));
                    w.U16(0);
                    w.U8(0);
                }
                w.End();
                w.End();
                return true;
            }
            const char* dts = mime == MimeTypes::AUDIO_DTS ? "dtsc" : mime == MimeTypes::AUDIO_DTS_HD ? "dtsh" : mime == MimeTypes::AUDIO_DTS_EXPRESS ? "dtse" : nullptr;
            if (dts) {
                BeginAudioEntry(w, dts, f);
                w.End();
                return true;
            }
            return false;
        }
    }

    std::optional<std::vector<std::uint8_t>> BuildInitSegment(const Format& f, TrackType type, std::int64_t timescale, std::uint32_t trackId) {
        if (type != TrackType::Video && type != TrackType::Audio) return std::nullopt;
        Writer entry;
        if (type == TrackType::Video) {
            if (f.sampleMimeType != MimeTypes::VIDEO_H264 || !WriteAvc(entry, f)) return std::nullopt;
        } else if (!WriteAudio(entry, f)) {
            return std::nullopt;
        }
        const std::uint32_t scale = std::uint32_t(std::clamp<std::int64_t>(timescale, 1, 0xffffffff));
        Writer w;
        w.Begin("ftyp");
        w.Type("iso6");
        w.U32(1);
        w.Type("isom");
        w.Type("iso6");
        w.Type("dash");
        w.End();
        w.Begin("moov");
        w.BeginFull("mvhd", 0, 0);
        w.U32(0);
        w.U32(0);
        w.U32(scale);
        w.U32(0);            // duration: the fragments say
        w.U32(0x00010000);   // rate
        w.U16(0x0100);       // volume
        w.Zeros(10);
        w.Matrix();
        w.Zeros(24);
        w.U32(trackId + 1);
        w.End();
        w.Begin("trak");
        w.BeginFull("tkhd", 0, 0x7);
        w.U32(0);
        w.U32(0);
        w.U32(trackId);
        w.U32(0);
        w.U32(0);            // duration
        w.Zeros(8);
        w.U16(0);            // layer
        w.U16(0);            // alternate_group
        w.U16(type == TrackType::Audio ? 0x0100 : 0);
        w.U16(0);
        w.Matrix();
        w.U32(type == TrackType::Video ? std::uint32_t(std::max(f.width, 0)) << 16 : 0);
        w.U32(type == TrackType::Video ? std::uint32_t(std::max(f.height, 0)) << 16 : 0);
        w.End();
        w.Begin("mdia");
        w.BeginFull("mdhd", 0, 0);
        w.U32(0);
        w.U32(0);
        w.U32(scale);
        w.U32(0);
        // ISO 639-2/T as three 5-bit letters ('und' when unknown).
        std::string lang = f.language.size() == 3 ? f.language : "und";
        unsigned packed = 0;
        for (char c : lang) packed = packed << 5 | (unsigned(std::tolower(static_cast<unsigned char>(c)) - 0x60) & 0x1f);
        w.U16(packed);
        w.U16(0);
        w.End();
        w.BeginFull("hdlr", 0, 0);
        w.U32(0);
        w.Type(type == TrackType::Video ? "vide" : "soun");
        w.Zeros(12);
        w.U8(0);
        w.End();
        w.Begin("minf");
        if (type == TrackType::Video) {
            w.BeginFull("vmhd", 0, 1);
            w.Zeros(8);
        } else {
            w.BeginFull("smhd", 0, 0);
            w.Zeros(4);
        }
        w.End();
        w.Begin("dinf");
        w.BeginFull("dref", 0, 0);
        w.U32(1);
        w.BeginFull("url ", 0, 1);
        w.End();
        w.End();
        w.End();
        w.Begin("stbl");
        w.BeginFull("stsd", 0, 0);
        w.U32(1);
        w.Bytes(entry.out);
        w.End();
        for (const char* box : { "stts", "stsc", "stco" }) {
            w.BeginFull(box, 0, 0);
            w.U32(0);
            w.End();
        }
        w.BeginFull("stsz", 0, 0);
        w.U32(0);
        w.U32(0);
        w.End();
        w.End();   // stbl
        w.End();   // minf
        w.End();   // mdia
        w.End();   // trak
        w.Begin("mvex");
        w.BeginFull("trex", 0, 0);
        w.U32(trackId);
        w.U32(1);   // sample description
        w.U32(0);
        w.U32(0);
        w.U32(0);   // flags: sync samples unless the fragments say otherwise
        w.End();
        w.End();
        w.End();   // moov
        return std::move(w.out);
    }

    void SetFragmentTrackIds(std::vector<std::uint8_t>& bytes, std::uint32_t trackId) {
        auto u32 = [&](std::size_t at) {
            return std::uint64_t(bytes[at]) << 24 | std::uint64_t(bytes[at + 1]) << 16 | std::uint64_t(bytes[at + 2]) << 8 | bytes[at + 3];
        };
        // The children of [from, to) named type, as (start, size).
        auto children = [&](std::size_t from, std::size_t to, const char* type, auto&& visit) {
            std::size_t at = from;
            while (at + 8 <= to) {
                std::uint64_t size = u32(at);
                std::size_t header = 8;
                if (size == 1 && at + 16 <= to) {
                    size = u32(at + 8) << 32 | u32(at + 12);
                    header = 16;
                } else if (size == 0) {
                    size = to - at;
                }
                if (size < header || at + size > to) return;
                if (std::memcmp(&bytes[at + 4], type, 4) == 0) visit(at + header, at + std::size_t(size));
                at += std::size_t(size);
            }
        };
        children(0, bytes.size(), "moof", [&](std::size_t moofStart, std::size_t moofEnd) {
            children(moofStart, moofEnd, "traf", [&](std::size_t trafStart, std::size_t trafEnd) {
                children(trafStart, trafEnd, "tfhd", [&](std::size_t body, std::size_t end) {
                    if (body + 8 > end) return;   // version/flags, then track_ID
                    bytes[body + 4] = std::uint8_t(trackId >> 24);
                    bytes[body + 5] = std::uint8_t(trackId >> 16);
                    bytes[body + 6] = std::uint8_t(trackId >> 8);
                    bytes[body + 7] = std::uint8_t(trackId);
                });
            });
        });
    }

}
