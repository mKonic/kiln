// The JVM bridge's byte format (kiln/jvm/Codec.h): the bytes dev.kiln.bridge.Codec writes and
// reads, and how a generated struct goes through it. tools/bridge-test checks both halves together.
#include "Check.h"

#include "kiln/jvm/Codec.h"

#include <cmath>

using namespace kiln::jvm;

namespace {
    enum class Mood : std::int32_t { Calm, Busy };
    struct Tag {
        std::string name;
        std::optional<std::int64_t> weight;
        Mood mood{};
        std::vector<Tag> children;
        bool operator==(const Tag&) const = default;
    };
}

// What tools/gen-jvm writes for a @CppStruct.
template<> struct kiln::jvm::Codec<Tag> {
    static void Write(Writer& w, const Tag& v) {
        Codec<std::string>::Write(w, v.name);
        Codec<std::optional<std::int64_t>>::Write(w, v.weight);
        Codec<Mood>::Write(w, v.mood);
        Codec<std::vector<Tag>>::Write(w, v.children);
    }
    static Tag Read(Reader& r) {
        Tag v;
        v.name = Codec<std::string>::Read(r);
        v.weight = Codec<std::optional<std::int64_t>>::Read(r);
        v.mood = Codec<Mood>::Read(r);
        v.children = Codec<std::vector<Tag>>::Read(r);
        return v;
    }
};

TEST(JvmCodecWritesJavasBytes) {
    using B = std::vector<std::uint8_t>;
    CHECK(Encode(true) == B{ 1 } && Encode(false) == B{ 0 });
    CHECK(Encode(std::int32_t(-2)) == (B{ 0xFE, 0xFF, 0xFF, 0xFF }));   // little-endian, as ByteBuffer LITTLE_ENDIAN
    CHECK(Encode(std::int64_t(0x0102030405060708)) == (B{ 8, 7, 6, 5, 4, 3, 2, 1 }));
    CHECK(Encode(1.0f) == (B{ 0, 0, 0x80, 0x3F }));                     // Float.floatToRawIntBits
    CHECK(Encode(std::string("é")) == (B{ 2, 0, 0, 0, 0xC3, 0xA9 }));  // UTF-8, length in bytes
    CHECK(Encode(std::vector<std::uint8_t>{ 9 }) == (B{ 1, 0, 0, 0, 9 }));
    CHECK(Encode(std::vector<std::int32_t>{ 1, 2 }) == (B{ 2, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0 }));
    CHECK(Encode(std::optional<bool>()) == B{ 0 } && Encode(std::optional<bool>(true)) == (B{ 1, 1 }));
    CHECK(Encode(Mood::Busy) == (B{ 1, 0, 0, 0 }));                     // the enum's ordinal
}

TEST(JvmCodecRoundTripsStructs) {
    Tag t{ "root", 7, Mood::Busy, { Tag{ "leaf", std::nullopt, Mood::Calm, {} }, Tag{ "🎉", -1, Mood::Busy, {} } } };
    CHECK(Decode<Tag>(Encode(t)) == t);
    CHECK(Decode<std::vector<std::optional<std::string>>>(Encode(std::vector<std::optional<std::string>>{ "a", std::nullopt, "" }))
          == (std::vector<std::optional<std::string>>{ "a", std::nullopt, "" }));
    CHECK(std::isnan(Decode<double>(Encode(std::nan("")))));
    CHECK(Decode<std::string>(Encode(std::string("a\0b", 3))) == std::string("a\0b", 3));
}

TEST(JvmCodecRejectsBadBytes) {
    auto throws = [](std::vector<std::uint8_t> bytes, auto type) {
        try {
            (void)Decode<decltype(type)>(bytes);
        } catch (const CodecError&) {
            return true;
        }
        return false;
    };
    CHECK(throws({ 1, 0, 0 }, std::int32_t{}));                     // ends early
    CHECK(throws({ 5, 0, 0, 0, 'a' }, std::string{}));              // a length past the end
    CHECK(throws({ 0xFF, 0xFF, 0xFF, 0xFF }, std::string{}));       // a negative length
    CHECK(throws({ 0xFF, 0xFF, 0xFF, 0x7F }, std::vector<bool>{})); // a count no bytes back
    CHECK(throws({ 1, 0, 0, 0, 0 }, std::int32_t{}));               // bytes left over
}
