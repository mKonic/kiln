#pragma once

// The byte format values cross the JVM bridge in (kiln/jvm/Jvm.h, tools/gen-jvm) when they are not
// a primitive, a String or a ByteArray: lists, nullable values, @CppStruct classes and enums. The
// Java half is dev.kiln.bridge.Codec; both write the same bytes.
//
//   Boolean          one byte, 0 or 1
//   Int, Long        4 and 8 bytes, little-endian two's complement
//   Float, Double    the IEEE 754 bits as Int / Long
//   String           an Int of UTF-8 bytes, then the bytes
//   ByteArray        an Int of bytes, then the bytes
//   List<T>          an Int count, then each element
//   T?               a Boolean (present), then T when present
//   enum             its ordinal as an Int
//   @CppStruct       its fields in declaration order
//
// A generated struct specializes Codec<T> with Write and Read.

#include <bit>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace kiln::jvm {

    // Bytes that end early or hold an impossible length.
    class CodecError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    class Writer {
    public:
        void Bool(bool v) { m_Bytes.push_back(v ? 1 : 0); }
        void I32(std::int32_t v) { Le(std::uint32_t(v)); }
        void I64(std::int64_t v) { Le(std::uint64_t(v)); }
        void F32(float v) { Le(std::bit_cast<std::uint32_t>(v)); }
        void F64(double v) { Le(std::bit_cast<std::uint64_t>(v)); }
        void Bytes(std::span<const std::uint8_t> v) {
            I32(std::int32_t(v.size()));
            m_Bytes.insert(m_Bytes.end(), v.begin(), v.end());
        }
        void Str(std::string_view v) { Bytes({ reinterpret_cast<const std::uint8_t*>(v.data()), v.size() }); }
        std::vector<std::uint8_t>& Data() { return m_Bytes; }
        std::vector<std::uint8_t> Take() { return std::move(m_Bytes); }

    private:
        template<typename U>
        void Le(U v) {
            for (std::size_t i = 0; i < sizeof(U); ++i) m_Bytes.push_back(std::uint8_t(v >> (8 * i)));
        }
        std::vector<std::uint8_t> m_Bytes;
    };

    class Reader {
    public:
        explicit Reader(std::span<const std::uint8_t> bytes) : m_P(bytes.data()), m_End(bytes.data() + bytes.size()) {}
        bool Bool() { return Take(1)[0] != 0; }
        std::int32_t I32() { return std::int32_t(Le<std::uint32_t>()); }
        std::int64_t I64() { return std::int64_t(Le<std::uint64_t>()); }
        float F32() { return std::bit_cast<float>(Le<std::uint32_t>()); }
        double F64() { return std::bit_cast<double>(Le<std::uint64_t>()); }
        std::vector<std::uint8_t> Bytes() {
            const std::uint8_t* p = Take(Length());
            return { p, m_P };
        }
        std::string Str() {
            std::size_t n = Length();
            return { reinterpret_cast<const char*>(Take(n)), n };
        }
        // A count of elements that each take at least one byte.
        std::size_t Count() { return Length(); }
        std::size_t Remaining() const { return std::size_t(m_End - m_P); }

    private:
        std::size_t Length() {
            std::int32_t n = I32();
            if (n < 0 || std::size_t(n) > Remaining()) throw CodecError("kiln bridge: a length past the end of the bytes");
            return std::size_t(n);
        }
        const std::uint8_t* Take(std::size_t n) {
            if (n > Remaining()) throw CodecError("kiln bridge: the bytes end early");
            const std::uint8_t* p = m_P;
            m_P += n;
            return p;
        }
        template<typename U>
        U Le() {
            const std::uint8_t* p = Take(sizeof(U));
            U v = 0;
            for (std::size_t i = 0; i < sizeof(U); ++i) v |= U(p[i]) << (8 * i);
            return v;
        }
        const std::uint8_t* m_P;
        const std::uint8_t* m_End;
    };

    template<typename T, typename = void>
    struct Codec;

    template<> struct Codec<bool> {
        static void Write(Writer& w, bool v) { w.Bool(v); }
        static bool Read(Reader& r) { return r.Bool(); }
    };
    template<> struct Codec<std::int32_t> {
        static void Write(Writer& w, std::int32_t v) { w.I32(v); }
        static std::int32_t Read(Reader& r) { return r.I32(); }
    };
    template<> struct Codec<std::int64_t> {
        static void Write(Writer& w, std::int64_t v) { w.I64(v); }
        static std::int64_t Read(Reader& r) { return r.I64(); }
    };
    template<> struct Codec<float> {
        static void Write(Writer& w, float v) { w.F32(v); }
        static float Read(Reader& r) { return r.F32(); }
    };
    template<> struct Codec<double> {
        static void Write(Writer& w, double v) { w.F64(v); }
        static double Read(Reader& r) { return r.F64(); }
    };
    template<> struct Codec<std::string> {
        static void Write(Writer& w, const std::string& v) { w.Str(v); }
        static std::string Read(Reader& r) { return r.Str(); }
    };
    template<> struct Codec<std::vector<std::uint8_t>> {
        static void Write(Writer& w, const std::vector<std::uint8_t>& v) { w.Bytes(v); }
        static std::vector<std::uint8_t> Read(Reader& r) { return r.Bytes(); }
    };
    template<typename T>
    struct Codec<std::vector<T>, std::enable_if_t<!std::is_same_v<T, std::uint8_t>>> {
        static void Write(Writer& w, const std::vector<T>& v) {
            w.I32(std::int32_t(v.size()));
            for (const T& e : v) Codec<T>::Write(w, e);
        }
        static std::vector<T> Read(Reader& r) {
            std::size_t n = r.Count();
            std::vector<T> v;
            v.reserve(n);
            for (std::size_t i = 0; i < n; ++i) v.push_back(Codec<T>::Read(r));
            return v;
        }
    };
    template<typename T>
    struct Codec<std::optional<T>> {
        static void Write(Writer& w, const std::optional<T>& v) {
            w.Bool(v.has_value());
            if (v) Codec<T>::Write(w, *v);
        }
        static std::optional<T> Read(Reader& r) {
            if (!r.Bool()) return std::nullopt;
            return Codec<T>::Read(r);
        }
    };
    template<typename T>
    struct Codec<T, std::enable_if_t<std::is_enum_v<T>>> {
        static void Write(Writer& w, T v) { w.I32(std::int32_t(v)); }
        static T Read(Reader& r) { return T(r.I32()); }
    };

    template<typename T>
    std::vector<std::uint8_t> Encode(const T& value) {
        Writer w;
        Codec<T>::Write(w, value);
        return w.Take();
    }
    // Throws CodecError when the bytes do not hold a T, or hold more than one.
    template<typename T>
    T Decode(std::span<const std::uint8_t> bytes) {
        Reader r(bytes);
        T value = Codec<T>::Read(r);
        if (r.Remaining() != 0) throw CodecError("kiln bridge: bytes left after the value");
        return value;
    }

}
