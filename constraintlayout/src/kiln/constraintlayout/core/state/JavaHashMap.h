#pragma once

// A map that iterates in java.util.HashMap's order, for the String keys constraintlayout-core
// keeps its references under. State builds the widget tree by iterating its maps, so the
// children's order, and with it which solution the solver settles on when constraints tie,
// follows that order. Keys hash as Java's String.hashCode over UTF-16; buckets, appending
// within a bucket, growth at 0.75 load and clear() keeping the table size are HashMap's.
// (HashMap turns crowded buckets into trees; that keeps a bucket's order only for keys that
// compare equal by hash, which constraint ids practically never reach.)

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace kiln::constraintlayout::core {

    // Java's String.hashCode of a UTF-8 string, over its UTF-16 code units.
    inline int32_t javaStringHash(const std::string& s) {
        uint32_t h = 0;
        auto add = [&](uint32_t unit) { h = 31 * h + unit; };
        for (std::size_t i = 0; i < s.size();) {
            const auto c = static_cast<unsigned char>(s[i]);
            uint32_t cp = c;
            std::size_t n = 1;
            if (c >= 0xF0 && i + 3 < s.size()) {
                cp = ((c & 0x07u) << 18) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 12) |
                     ((static_cast<unsigned char>(s[i + 2]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 3]) & 0x3Fu);
                n = 4;
            } else if (c >= 0xE0 && i + 2 < s.size()) {
                cp = ((c & 0x0Fu) << 12) | ((static_cast<unsigned char>(s[i + 1]) & 0x3Fu) << 6) | (static_cast<unsigned char>(s[i + 2]) & 0x3Fu);
                n = 3;
            } else if (c >= 0xC0 && i + 1 < s.size()) {
                cp = ((c & 0x1Fu) << 6) | (static_cast<unsigned char>(s[i + 1]) & 0x3Fu);
                n = 2;
            }
            if (cp >= 0x10000) {
                cp -= 0x10000;
                add(0xD800 + (cp >> 10));
                add(0xDC00 + (cp & 0x3FF));
            } else {
                add(cp);
            }
            i += n;
        }
        return static_cast<int32_t>(h);
    }

    struct JavaStringKeyHash {
        int32_t operator()(const std::string& key) const { return javaStringHash(key); }
    };

    template<typename V, typename Hash = JavaStringKeyHash>
    class JavaHashMap {
    public:
        using Entry = std::pair<std::string, V>;

        V* find(const std::string& key) {
            if (mTable.empty()) return nullptr;
            for (Entry& e : mTable[indexFor(key, mTable.size())])
                if (e.first == key) return &e.second;
            return nullptr;
        }
        const V* find(const std::string& key) const { return const_cast<JavaHashMap*>(this)->find(key); }
        bool contains(const std::string& key) const { return find(key) != nullptr; }

        V& put(const std::string& key, V value) {
            if (mTable.empty()) mTable.resize(16);
            auto& bucket = mTable[indexFor(key, mTable.size())];
            for (Entry& e : bucket)
                if (e.first == key) {
                    e.second = std::move(value);
                    return e.second;
                }
            bucket.emplace_back(key, std::move(value));
            if (++mSize > mTable.size() * 3 / 4) {
                resize();
                return *find(key);
            }
            return bucket.back().second;
        }

        bool remove(const std::string& key) {
            if (mTable.empty()) return false;
            auto& bucket = mTable[indexFor(key, mTable.size())];
            for (auto it = bucket.begin(); it != bucket.end(); ++it)
                if (it->first == key) {
                    bucket.erase(it);
                    mSize--;
                    return true;
                }
            return false;
        }

        void clear() {
            for (auto& bucket : mTable) bucket.clear();
            mSize = 0;
        }

        std::size_t size() const { return mSize; }
        bool empty() const { return mSize == 0; }

        // Keys in iteration order (a snapshot, as iterating a keySet while not modifying it).
        std::vector<std::string> keys() const {
            std::vector<std::string> out;
            out.reserve(mSize);
            for (const auto& bucket : mTable)
                for (const Entry& e : bucket) out.push_back(e.first);
            return out;
        }

        template<typename F>
        void forEach(F&& f) const {
            for (const auto& bucket : mTable)
                for (const Entry& e : bucket) f(e.first, e.second);
        }

    private:
        static std::size_t indexFor(const std::string& key, std::size_t n) {
            const auto h = static_cast<uint32_t>(Hash{}(key));
            return (h ^ (h >> 16)) & (n - 1);
        }

        void resize() {
            std::vector<std::vector<Entry>> table(mTable.size() * 2);
            for (auto& bucket : mTable)
                for (Entry& e : bucket) table[indexFor(e.first, table.size())].push_back(std::move(e));
            mTable = std::move(table);
        }

        std::vector<std::vector<Entry>> mTable;
        std::size_t mSize = 0;
    };

    // HashSet<String>: a JavaHashMap without values, iterating in the same order.
    class JavaHashSet {
    public:
        bool add(const std::string& key) {
            if (mMap.contains(key)) return false;
            mMap.put(key, true);
            return true;
        }
        bool contains(const std::string& key) const { return mMap.contains(key); }
        bool remove(const std::string& key) { return mMap.remove(key); }
        void clear() { mMap.clear(); }
        std::size_t size() const { return mMap.size(); }
        bool empty() const { return mMap.empty(); }
        std::vector<std::string> keys() const { return mMap.keys(); }

    private:
        JavaHashMap<bool> mMap;
    };

}
