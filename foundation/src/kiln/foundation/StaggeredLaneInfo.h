#pragma once

// LazyStaggeredGridLaneInfo: lane assignments of staggered grid items in a sliding window around
// the scroll position (at most kMaxCapacity items; the anchor moves with far requests), and the
// gaps above full-span items (how far each lane ended above them).

#include <algorithm>
#include <cassert>
#include <deque>
#include <vector>

namespace kiln::foundation {

    class StaggeredLaneInfo {
    public:
        static constexpr int kLaneUnset = -1;
        static constexpr int kLaneFullSpan = -2;

        void SetLane(int itemIndex, int lane) {
            assert(itemIndex >= 0);
            EnsureValidIndex(itemIndex);
            m_Lanes[std::size_t(itemIndex - m_Anchor)] = lane + 1;
        }
        int GetLane(int itemIndex) const {
            if (itemIndex < LowerBound() || itemIndex >= UpperBound()) return kLaneUnset;
            return m_Lanes[std::size_t(itemIndex - m_Anchor)] - 1;
        }
        bool AssignedToLane(int itemIndex, int targetLane) const {
            const int lane = GetLane(itemIndex);
            return lane == targetLane || lane == kLaneUnset || lane == kLaneFullSpan;
        }
        int UpperBound() const { return m_Anchor + int(m_Lanes.size()); }
        int LowerBound() const { return m_Anchor; }
        void Reset() {
            std::fill(m_Lanes.begin(), m_Lanes.end(), 0);
            m_Spanned.clear();
        }
        // The previous item that can be in `targetLane`, or -1.
        int FindPreviousItemIndex(int itemIndex, int targetLane) const {
            for (int i = itemIndex - 1; i >= 0; --i) if (AssignedToLane(i, targetLane)) return i;
            return -1;
        }
        // The next item that can be in `targetLane`, or UpperBound().
        int FindNextItemIndex(int itemIndex, int targetLane) const {
            for (int i = itemIndex + 1; i < UpperBound(); ++i) if (AssignedToLane(i, targetLane)) return i;
            return UpperBound();
        }

        void EnsureValidIndex(int requestedIndex) {
            const int requestedCapacity = requestedIndex - m_Anchor;
            if (requestedCapacity >= 0 && requestedCapacity < kMaxCapacity) {
                EnsureCapacity(requestedCapacity + 1);
            } else {
                // Beyond the window: rebase so the requested index is in its middle.
                const int oldAnchor = m_Anchor;
                const int size = int(m_Lanes.size());
                m_Anchor = std::max(requestedIndex - size / 2, 0);
                int delta = m_Anchor - oldAnchor;
                if (delta >= 0) {
                    if (delta < size) std::copy(m_Lanes.begin() + delta, m_Lanes.end(), m_Lanes.begin());
                    std::fill(m_Lanes.begin() + std::max(0, size - delta), m_Lanes.end(), 0);
                } else {
                    delta = -delta;
                    if (size + delta < kMaxCapacity) {
                        EnsureCapacity(size + delta + 1, delta);
                    } else {
                        if (delta < size) std::copy_backward(m_Lanes.begin(), m_Lanes.end() - delta, m_Lanes.end());
                        std::fill(m_Lanes.begin(), m_Lanes.begin() + std::min(size, delta), 0);
                    }
                }
            }
            // Full-span gaps outside the window are forgotten.
            while (!m_Spanned.empty() && m_Spanned.front().index < LowerBound()) m_Spanned.pop_front();
            while (!m_Spanned.empty() && m_Spanned.back().index > UpperBound()) m_Spanned.pop_back();
        }

        void SetGaps(int itemIndex, const std::vector<int>* gaps) {
            auto it = std::lower_bound(m_Spanned.begin(), m_Spanned.end(), itemIndex, [](const Spanned& s, int i) { return s.index < i; });
            const bool found = it != m_Spanned.end() && it->index == itemIndex;
            if (!found) {
                if (gaps) m_Spanned.insert(it, Spanned{ itemIndex, *gaps });
            } else if (!gaps) {
                m_Spanned.erase(it);
            } else {
                it->gaps = *gaps;
            }
        }
        const std::vector<int>* GetGaps(int itemIndex) const {
            auto it = std::lower_bound(m_Spanned.begin(), m_Spanned.end(), itemIndex, [](const Spanned& s, int i) { return s.index < i; });
            return it != m_Spanned.end() && it->index == itemIndex ? &it->gaps : nullptr;
        }

    private:
        static constexpr int kMaxCapacity = 131'072;   // 2^17, closest to 100 000
        struct Spanned { int index; std::vector<int> gaps; };

        void EnsureCapacity(int capacity, int newOffset = 0) {
            assert(capacity <= kMaxCapacity);
            if (int(m_Lanes.size()) >= capacity) return;
            std::size_t newSize = m_Lanes.size();
            while (int(newSize) < capacity) newSize *= 2;
            std::vector<int> lanes(newSize, 0);
            std::copy(m_Lanes.begin(), m_Lanes.end(), lanes.begin() + newOffset);
            m_Lanes = std::move(lanes);
        }

        int m_Anchor = 0;
        std::vector<int> m_Lanes = std::vector<int>(16, 0);
        std::deque<Spanned> m_Spanned;
    };

}
