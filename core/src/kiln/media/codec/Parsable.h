#pragma once

// media3's ParsableByteArray, ParsableBitArray and ParsableNalUnitBitArray: big-endian readers
// over bytes, by byte, by bit, and by bit skipping H.26x emulation prevention bytes. Reads past
// the limit throw std::out_of_range (Java's IndexOutOfBounds / IllegalStateException).

#include <cstdint>
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace kiln::media::codec {

    class ParsableByteArray {
    public:
        ParsableByteArray() = default;
        explicit ParsableByteArray(int limit) : m_Data(std::size_t(limit)), m_Limit(limit) {}
        explicit ParsableByteArray(std::vector<std::uint8_t> data) : m_Data(std::move(data)), m_Limit(int(m_Data.size())) {}
        ParsableByteArray(std::vector<std::uint8_t> data, int limit) : m_Data(std::move(data)), m_Limit(limit) {}

        // reset(limit): the position to 0, the limit to limit, growing the data as needed.
        void Reset(int limit) {
            if (int(m_Data.size()) < limit) m_Data.resize(std::size_t(limit));
            m_Position = 0;
            m_Limit = limit;
        }
        void Reset(std::vector<std::uint8_t> data) { Reset(std::move(data), -1); }
        void Reset(std::vector<std::uint8_t> data, int limit) {
            m_Data = std::move(data);
            m_Limit = limit < 0 ? int(m_Data.size()) : limit;
            m_Position = 0;
        }
        void EnsureCapacity(int capacity) {
            if (int(m_Data.size()) < capacity) m_Data.resize(std::size_t(capacity));
        }
        int BytesLeft() const { return std::max(m_Limit - m_Position, 0); }
        int Limit() const { return m_Limit; }
        void SetLimit(int limit) {
            if (limit < 0 || limit > int(m_Data.size())) throw std::out_of_range("limit");
            m_Limit = limit;
        }
        int GetPosition() const { return m_Position; }
        void SetPosition(int position) {
            if (position < 0 || position > m_Limit) throw std::out_of_range("position");
            m_Position = position;
        }
        std::vector<std::uint8_t>& GetData() { return m_Data; }
        const std::vector<std::uint8_t>& GetData() const { return m_Data; }
        int Capacity() const { return int(m_Data.size()); }
        void SkipBytes(int n) { SetPosition(m_Position + n); }
        void ReadBytes(std::uint8_t* out, int length) {
            Need(length);
            if (length > 0) std::memcpy(out, m_Data.data() + m_Position, std::size_t(length));
            m_Position += length;
        }
        int PeekUnsignedByte() const {
            if (m_Position >= m_Limit) throw std::out_of_range("peek");
            return m_Data[std::size_t(m_Position)];
        }
        int ReadUnsignedByte() {
            Need(1);
            return m_Data[std::size_t(m_Position++)];
        }
        int ReadUnsignedShort() {
            Need(2);
            const int v = m_Data[std::size_t(m_Position)] << 8 | m_Data[std::size_t(m_Position) + 1];
            m_Position += 2;
            return v;
        }
        int ReadLittleEndianUnsignedShort() {
            Need(2);
            const int v = m_Data[std::size_t(m_Position)] | m_Data[std::size_t(m_Position) + 1] << 8;
            m_Position += 2;
            return v;
        }
        int ReadShort() { return std::int16_t(ReadUnsignedShort()); }
        int ReadUnsignedInt24() {
            Need(3);
            const int v = m_Data[std::size_t(m_Position)] << 16 | m_Data[std::size_t(m_Position) + 1] << 8 | m_Data[std::size_t(m_Position) + 2];
            m_Position += 3;
            return v;
        }
        std::int64_t ReadUnsignedInt() {
            Need(4);
            const std::uint32_t v = std::uint32_t(m_Data[std::size_t(m_Position)]) << 24 | std::uint32_t(m_Data[std::size_t(m_Position) + 1]) << 16 |
                                    std::uint32_t(m_Data[std::size_t(m_Position) + 2]) << 8 | m_Data[std::size_t(m_Position) + 3];
            m_Position += 4;
            return std::int64_t(v);
        }
        int ReadInt() { return int(std::uint32_t(ReadUnsignedInt())); }
        std::string ReadString(int length) {
            Need(length);
            std::string s(reinterpret_cast<const char*>(m_Data.data()) + m_Position, std::size_t(length));
            m_Position += length;
            return s;
        }

    private:
        void Need(int n) const {
            if (n < 0 || m_Position + n > m_Limit || m_Position + n > int(m_Data.size())) throw std::out_of_range("read past the limit");
        }
        std::vector<std::uint8_t> m_Data;
        int m_Position = 0, m_Limit = 0;
    };

    class ParsableBitArray {
    public:
        ParsableBitArray() = default;
        explicit ParsableBitArray(std::vector<std::uint8_t> data) { Reset(std::move(data)); }
        ParsableBitArray(std::vector<std::uint8_t> data, int limit) { Reset(std::move(data), limit); }

        void Reset(std::vector<std::uint8_t> data) {
            const int n = int(data.size());
            Reset(std::move(data), n);
        }
        void Reset(std::vector<std::uint8_t> data, int limit) {
            m_Data = std::move(data);
            m_ByteOffset = m_BitOffset = 0;
            m_ByteLimit = limit;
        }
        void Reset(const ParsableByteArray& bytes) {
            Reset(bytes.GetData(), bytes.Limit());
            SetPosition(bytes.GetPosition() * 8);
        }
        std::vector<std::uint8_t>& Data() { return m_Data; }
        int BitsLeft() const { return (m_ByteLimit - m_ByteOffset) * 8 - m_BitOffset; }
        int GetPosition() const { return m_ByteOffset * 8 + m_BitOffset; }
        int GetBytePosition() const {
            if (m_BitOffset != 0) throw std::logic_error("not byte aligned");
            return m_ByteOffset;
        }
        void SetPosition(int position) {
            m_ByteOffset = position / 8;
            m_BitOffset = position - m_ByteOffset * 8;
            Check();
        }
        void SkipBit() {
            if (++m_BitOffset == 8) {
                m_BitOffset = 0;
                ++m_ByteOffset;
            }
            Check();
        }
        void SkipBits(int numBits) {
            const int numBytes = numBits / 8;
            m_ByteOffset += numBytes;
            m_BitOffset += numBits - numBytes * 8;
            if (m_BitOffset > 7) {
                ++m_ByteOffset;
                m_BitOffset -= 8;
            }
            Check();
        }
        bool ReadBit() {
            const bool v = (At(m_ByteOffset) & (0x80 >> m_BitOffset)) != 0;
            SkipBit();
            return v;
        }
        int ReadBits(int numBits) {
            if (numBits == 0) return 0;
            std::uint32_t v = 0;
            m_BitOffset += numBits;
            while (m_BitOffset > 8) {
                m_BitOffset -= 8;
                v |= std::uint32_t(At(m_ByteOffset++)) << m_BitOffset;
            }
            v |= std::uint32_t(At(m_ByteOffset)) >> (8 - m_BitOffset);
            v &= 0xFFFFFFFFu >> (32 - numBits);
            if (m_BitOffset == 8) {
                m_BitOffset = 0;
                ++m_ByteOffset;
            }
            Check();
            return int(v);
        }
        std::int64_t ReadBitsToLong(int numBits) {
            if (numBits <= 32) return std::int64_t(std::uint32_t(ReadBits(numBits)));
            const std::uint32_t high = std::uint32_t(ReadBits(numBits - 32));
            return std::int64_t(std::uint64_t(high) << 32 | std::uint32_t(ReadBits(32)));
        }
        void ByteAlign() {
            if (m_BitOffset == 0) return;
            m_BitOffset = 0;
            ++m_ByteOffset;
            Check();
        }
        void ReadBytes(std::uint8_t* out, int length) {
            if (m_BitOffset != 0) throw std::logic_error("not byte aligned");
            if (m_ByteOffset + length > int(m_Data.size())) throw std::out_of_range("read past the data");
            if (length > 0) std::memcpy(out, m_Data.data() + m_ByteOffset, std::size_t(length));
            m_ByteOffset += length;
            Check();
        }
        void SkipBytes(int length) {
            if (m_BitOffset != 0) throw std::logic_error("not byte aligned");
            m_ByteOffset += length;
            Check();
        }

    private:
        int At(int i) const {
            if (i < 0 || i >= int(m_Data.size())) throw std::out_of_range("read past the data");
            return m_Data[std::size_t(i)];
        }
        void Check() const {
            if (!(m_ByteOffset >= 0 && (m_ByteOffset < m_ByteLimit || (m_ByteOffset == m_ByteLimit && m_BitOffset == 0)))) throw std::out_of_range("bit offset past the limit");
        }
        std::vector<std::uint8_t> m_Data;
        int m_ByteOffset = 0, m_BitOffset = 0, m_ByteLimit = 0;
    };

    // Reads bits from a NAL unit, skipping the 0x03 of each 00 00 03.
    class ParsableNalUnitBitArray {
    public:
        ParsableNalUnitBitArray(const std::uint8_t* data, int offset, int limit) { Reset(data, offset, limit); }
        void Reset(const std::uint8_t* data, int offset, int limit) {
            m_Data = data;
            m_StartOffset = m_ByteOffset = offset;
            m_ByteLimit = limit;
            m_BitOffset = 0;
            Check();
        }
        void SkipBit() {
            if (++m_BitOffset == 8) {
                m_BitOffset = 0;
                m_ByteOffset += ShouldSkipByte(m_ByteOffset + 1) ? 2 : 1;
            }
            Check();
        }
        void SkipBits(int numBits) {
            const int oldByteOffset = m_ByteOffset;
            const int numBytes = numBits / 8;
            m_ByteOffset += numBytes;
            m_BitOffset += numBits - numBytes * 8;
            if (m_BitOffset > 7) {
                ++m_ByteOffset;
                m_BitOffset -= 8;
            }
            for (int i = oldByteOffset + 1; i <= m_ByteOffset; ++i)
                if (ShouldSkipByte(i)) {
                    ++m_ByteOffset;
                    i += 2;
                }
            Check();
        }
        bool ReadBit() {
            const bool v = (At(m_ByteOffset) & (0x80 >> m_BitOffset)) != 0;
            SkipBit();
            return v;
        }
        int ReadBits(int numBits) {
            std::uint32_t v = 0;
            m_BitOffset += numBits;
            while (m_BitOffset > 8) {
                m_BitOffset -= 8;
                v |= std::uint32_t(At(m_ByteOffset)) << m_BitOffset;
                m_ByteOffset += ShouldSkipByte(m_ByteOffset + 1) ? 2 : 1;
            }
            v |= std::uint32_t(At(m_ByteOffset)) >> (8 - m_BitOffset);
            v &= 0xFFFFFFFFu >> (32 - numBits);
            if (m_BitOffset == 8) {
                m_BitOffset = 0;
                m_ByteOffset += ShouldSkipByte(m_ByteOffset + 1) ? 2 : 1;
            }
            Check();
            return int(v);
        }
        int ReadUnsignedExpGolombCodedInt() { return ReadExpGolombCodeNum(); }
        int ReadSignedExpGolombCodedInt() {
            const int codeNum = ReadExpGolombCodeNum();
            return (codeNum % 2 == 0 ? -1 : 1) * ((codeNum + 1) / 2);
        }

    private:
        int ReadExpGolombCodeNum() {
            int leadingZeros = 0;
            while (!ReadBit()) ++leadingZeros;
            return int((1u << leadingZeros) - 1) + (leadingZeros > 0 ? ReadBits(leadingZeros) : 0);
        }
        bool ShouldSkipByte(int offset) const {
            return m_StartOffset <= offset - 2 && offset < m_ByteLimit && m_Data[offset] == 0x03 && m_Data[offset - 2] == 0x00 && m_Data[offset - 1] == 0x00;
        }
        int At(int i) const {
            if (i < 0 || i >= m_ByteLimit) throw std::out_of_range("read past the NAL unit");
            return m_Data[i];
        }
        void Check() const {
            if (!(m_ByteOffset >= 0 && (m_ByteOffset < m_ByteLimit || (m_ByteOffset == m_ByteLimit && m_BitOffset == 0)))) throw std::out_of_range("bit offset past the NAL unit");
        }
        const std::uint8_t* m_Data = nullptr;
        int m_StartOffset = 0, m_ByteOffset = 0, m_BitOffset = 0, m_ByteLimit = 0;
    };

}
