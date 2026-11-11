#include "kiln/ui/MyersDiff.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <utility>
#include <vector>

namespace kiln::ui {

    namespace {
        using SameFn = std::function<bool(int, int)>;

        // CenteredArray: indexed from -mid.
        struct Centered {
            std::vector<int> data;
            explicit Centered(int size) : data(std::size_t(size), 0) {}
            int& operator[](int i) { return data[std::size_t(i + int(data.size()) / 2)]; }
        };

        struct Snake {
            int startX = 0, startY = 0, endX = 0, endY = 0;
            bool reverse = false;
            int DiagonalSize() const { return std::min(endX - startX, endY - startY); }
            bool HasAdditionOrRemoval() const { return endY - startY != endX - startX; }
            bool IsAddition() const { return endY - startY > endX - startX; }
        };

        // A diagonal pushes (x + size, y + size, size).
        void PushDiagonal(std::vector<int>& d, int x, int y, int size) { d.insert(d.end(), { x + size, y + size, size }); }

        void AddDiagonal(const Snake& s, std::vector<int>& diagonals) {
            int x = s.startX, y = s.startY, size;
            if (s.HasAdditionOrRemoval()) {
                size = s.DiagonalSize();
                x += !(s.reverse || s.IsAddition());
                y += !(s.reverse || !s.IsAddition());
            } else {
                size = s.endX - s.startX;
            }
            PushDiagonal(diagonals, x, y, size);
        }

        bool Forward(int oldStart, int oldEnd, int newStart, int newEnd, const SameFn& cb, Centered& forward, Centered& backward, int d,
                     Snake& snake) {
            const int oldSize = oldEnd - oldStart, newSize = newEnd - newStart;
            const bool checkForSnake = (std::abs(oldSize - newSize) & 1) == 1;
            const int delta = oldSize - newSize;
            for (int k = -d; k <= d; k += 2) {
                int startX, x;
                if (k == -d || (k != d && forward[k + 1] > forward[k - 1])) {
                    startX = forward[k + 1];
                    x = startX;
                } else {
                    startX = forward[k - 1];
                    x = startX + 1;
                }
                int y = newStart + (x - oldStart) - k;
                const int startY = y - ((d != 0 && x == startX) ? 1 : 0);
                while (x < oldEnd && y < newEnd && cb(x, y)) { ++x; ++y; }
                forward[k] = x;
                if (checkForSnake) {
                    const int backwardsK = delta - k;
                    if (backwardsK >= -d + 1 && backwardsK <= d - 1 && backward[backwardsK] <= x) {
                        snake = { startX, startY, x, y, false };
                        return true;
                    }
                }
            }
            return false;
        }

        bool Backward(int oldStart, int oldEnd, int newStart, int newEnd, const SameFn& cb, Centered& forward, Centered& backward, int d,
                      Snake& snake) {
            const int oldSize = oldEnd - oldStart, newSize = newEnd - newStart;
            const bool checkForSnake = ((oldSize - newSize) & 1) == 0;
            const int delta = oldSize - newSize;
            for (int k = -d; k <= d; k += 2) {
                int startX, x;
                if (k == -d || (k != d && backward[k + 1] < backward[k - 1])) {
                    startX = backward[k + 1];
                    x = startX;
                } else {
                    startX = backward[k - 1];
                    x = startX - 1;
                }
                int y = newEnd - (oldEnd - x - k);
                const int startY = y + ((d != 0 && x == startX) ? 1 : 0);
                while (x > oldStart && y > newStart && cb(x - 1, y - 1)) { --x; --y; }
                backward[k] = x;
                if (checkForSnake) {
                    const int forwardsK = delta - k;
                    if (forwardsK >= -d && forwardsK <= d && forward[forwardsK] >= x) {
                        snake = { x, y, startX, startY, true };
                        return true;
                    }
                }
            }
            return false;
        }

        bool MidPoint(int oldStart, int oldEnd, int newStart, int newEnd, const SameFn& cb, Centered& forward, Centered& backward, Snake& snake) {
            const int oldSize = oldEnd - oldStart, newSize = newEnd - newStart;
            if (oldSize < 1 || newSize < 1) return false;
            const int max = (oldSize + newSize + 1) / 2;
            forward[1] = oldStart;
            backward[1] = oldEnd;
            for (int d = 0; d < max; ++d) {
                if (Forward(oldStart, oldEnd, newStart, newEnd, cb, forward, backward, d, snake)) return true;
                if (Backward(oldStart, oldEnd, newStart, newEnd, cb, forward, backward, d, snake)) return true;
            }
            return false;
        }
    }

    void ExecuteDiff(int oldSize, int newSize, const SameFn& areItemsTheSame, const std::function<void(int, int)>& same) {
        // calculateDiff
        const int max = (oldSize + newSize + 1) / 2;
        std::vector<int> diagonals;
        std::vector<std::array<int, 4>> stack{ { 0, oldSize, 0, newSize } };
        Centered forward(max * 2 + 1), backward(max * 2 + 1);
        Snake snake;
        while (!stack.empty()) {
            const auto [oldStart, oldEnd, newStart, newEnd] = stack.back();
            stack.pop_back();
            if (MidPoint(oldStart, oldEnd, newStart, newEnd, areItemsTheSame, forward, backward, snake)) {
                if (snake.DiagonalSize() > 0) AddDiagonal(snake, diagonals);
                stack.push_back({ oldStart, snake.startX, newStart, snake.startY });
                stack.push_back({ snake.endX, oldEnd, snake.endY, newEnd });
            }
        }
        // sortDiagonals: by end x, then end y.
        std::vector<std::array<int, 3>> sorted;
        for (std::size_t i = 0; i + 2 < diagonals.size(); i += 3) sorted.push_back({ diagonals[i], diagonals[i + 1], diagonals[i + 2] });
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) { return a[0] < b[0] || (a[0] == b[0] && a[1] < b[1]); });
        sorted.push_back({ oldSize, newSize, 0 });
        // applyDiff: only the kept pairs matter here.
        int posX = 0, posY = 0;
        for (const auto& [endX, endY, size] : sorted) {
            const int startX = endX - size, startY = endY - size;
            posX = std::max(posX, startX);
            posY = std::max(posY, startY);
            for (int len = size; len > 0; --len) same(posX++, posY++);
        }
    }

}
