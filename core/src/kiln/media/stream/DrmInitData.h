#pragma once

// media3-common's DrmInitData: the protection schemes of a stream, with their PSSH data.

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kiln::media::stream {

    // A 128-bit UUID, most significant byte first (java.util.UUID's order).
    using Uuid = std::array<std::uint8_t, 16>;
    Uuid ParseUuid(const std::string& text);
    inline constexpr Uuid kWidevineUuid = { 0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed };
    inline constexpr Uuid kPlayreadyUuid = { 0x9a, 0x04, 0xf0, 0x79, 0x98, 0x40, 0x42, 0x86, 0xab, 0x92, 0xe6, 0x5b, 0xe0, 0x88, 0x5f, 0x95 };
    inline constexpr Uuid kClearkeyUuid = { 0xe2, 0x71, 0x9d, 0x58, 0xa9, 0x85, 0xb3, 0xc9, 0x78, 0x1a, 0xb0, 0x30, 0xaf, 0x78, 0xd3, 0x0e };
    inline constexpr Uuid kCommonPsshUuid = { 0x10, 0x77, 0xef, 0xec, 0xc0, 0xb2, 0x4d, 0x02, 0xac, 0xe3, 0x3c, 0x1e, 0x52, 0xe2, 0xfb, 0x4b };
    inline constexpr Uuid kUuidNil = {};

    // DrmInitData: the protection schemes of a stream (C.CENC_TYPE_* scheme type), sorted as
    // media3 sorts them (by UUID, with C.UUID_NIL last).
    struct SchemeData {
        Uuid uuid{};
        std::string licenseServerUrl;
        std::string mimeType;
        std::optional<std::vector<std::uint8_t>> data;
        SchemeData copyWithData(std::optional<std::vector<std::uint8_t>> d) const {
            SchemeData s = *this;
            s.data = std::move(d);
            return s;
        }
        // Whether this applies to the scheme (a C.UUID_NIL scheme applies to all of them).
        bool Matches(const Uuid& schemeUuid) const { return uuid == kUuidNil || uuid == schemeUuid; }
        bool HasData() const { return data.has_value(); }
        // Whether this, with data, can stand in for the other, without.
        bool CanReplace(const SchemeData& other) const { return HasData() && !other.HasData() && Matches(other.uuid); }
        bool operator==(const SchemeData&) const = default;
    };
    struct DrmInitData {
        std::string schemeType;   // "" for Java's null
        std::vector<SchemeData> schemeDatas;
        DrmInitData() = default;
        DrmInitData(std::string schemeType, std::vector<SchemeData> schemeDatas);
        bool operator==(const DrmInitData&) const = default;
    };
    inline constexpr const char* kCencTypeCenc = "cenc";
    inline constexpr const char* kCencTypeCbcs = "cbcs";

}
