#include "kiln/ui/ScrollCapture.h"

#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace kiln::ui {

    namespace {
        bool CanScrollVertically(const SemanticsConfiguration& c) {
            return c.scrollByOffset && c.verticalScrollAxisRange && c.verticalScrollAxisRange->maxValue &&
                   c.verticalScrollAxisRange->maxValue() > 0;
        }
        IntRect Round(const Rect& r) {
            return { int(std::lround(r.left)), int(std::lround(r.top)), int(std::lround(r.right)), int(std::lround(r.bottom)) };
        }
    }

    std::optional<ScrollCapture::Candidate> ScrollCapture::Search() {
        AccessibilityDelegate& a11y = m_Owner.Accessibility();
        const auto& nodes = a11y.Nodes();
        std::vector<Candidate> candidates;
        // visitScrollCaptureCandidates: depth-first from a node with a stack; a scrollable node
        // is a candidate and its own descendants are searched one level deeper.
        std::function<void(int, int)> visit = [&](int fromId, int depth) {
            const auto from = nodes.find(fromId);
            if (from == nodes.end()) return;
            std::vector<int> stack(from->second.children.begin(), from->second.children.end());
            while (!stack.empty()) {
                const int id = stack.back();
                stack.pop_back();
                const auto it = nodes.find(id);
                if (it == nodes.end() || it->second.fake) continue;
                const AccessibilityNode& n = it->second;
                if (n.hidden || n.config.disabled) continue;
                const IntRect viewport = Round(n.boundsInRoot);
                if (viewport.Empty()) continue;
                if (!CanScrollVertically(n.config)) {
                    stack.insert(stack.end(), n.children.begin(), n.children.end());
                    continue;
                }
                candidates.push_back({ id, depth + 1, viewport });
                visit(id, depth + 1);
            }
        };
        visit(a11y::kHostViewId, 0);
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate& a, const Candidate& b) {
            if (a.depth != b.depth) return a.depth < b.depth;
            return (a.viewportBounds.bottom - a.viewportBounds.top) < (b.viewportBounds.bottom - b.viewportBounds.top);
        });
        if (candidates.empty()) return std::nullopt;
        return candidates.back();
    }

    void ScrollCapture::Start(const Candidate& candidate) {
        m_Candidate = candidate;
        m_ScrollAmount = 0;
        m_InProgress = true;
        m_Owner.SetScrollCaptureInProgress(true);
    }

    // RelativeScroller.scrollBy: through the node's ScrollByOffset, reversed for a reversed range.
    rt::Task<> ScrollCapture::ScrollBy(float delta) {
        const AccessibilityNode* n = m_Owner.Accessibility().FindAnyNode(m_Candidate.id);
        if (!n || !n->config.scrollByOffset) co_return;
        const bool reverse = n->config.verticalScrollAxisRange && n->config.verticalScrollAxisRange->reverseScrolling;
        const auto action = n->config.scrollByOffset;   // the node may change while scrolling
        const Offset consumed = co_await action(Offset{ 0, reverse ? -delta : delta });
        m_ScrollAmount += reverse ? -consumed.y : consumed.y;
    }

    rt::Task<> ScrollCapture::ScrollTo(float offset) { co_await ScrollBy(offset - m_ScrollAmount); }

    int ScrollCapture::MapOffsetToViewport(int offset) const {
        const int viewportSize = m_Candidate.viewportBounds.bottom - m_Candidate.viewportBounds.top;
        return std::clamp(offset - int(std::lround(m_ScrollAmount)), 0, viewportSize);
    }

    rt::Task<ScrollCapture::Image> ScrollCapture::ImageRequest(IntRect captureArea) {
        const int viewportSize = m_Candidate.viewportBounds.bottom - m_Candidate.viewportBounds.top;
        const int targetMin = captureArea.top, targetMax = captureArea.bottom;
        if (targetMin > targetMax || targetMax - targetMin > viewportSize) throw std::invalid_argument("capture range larger than the viewport");
        // scrollRangeToCenter (isAlwaysScrollDuringScrollCaptureEnabled: always scroll).
        co_await ScrollTo(float(targetMin + (targetMax - targetMin) / 2 - viewportSize / 2));
        co_await rt::NextFrame{};
        m_Owner.MeasureAndLayout(m_Owner.WindowSize());
        Image out;
        const int clippedMin = MapOffsetToViewport(targetMin), clippedMax = MapOffsetToViewport(targetMax);
        if (clippedMin == clippedMax) co_return out;
        const IntRect clipped{ captureArea.left, clippedMin, captureArea.right, clippedMax };
        out.width = captureArea.right - captureArea.left;
        out.height = captureArea.bottom - captureArea.top;
        const IntRect viewport = m_Candidate.viewportBounds;
        LayoutNode& root = m_Owner.Root();
        out.rgba = m_Owner.RenderToPixels(out.width, out.height, [&](DrawScope& d) {
            d.Translate({ -float(clipped.left + viewport.left), -float(clipped.top + viewport.top) });
            root.Draw(static_cast<ContentDrawScope&>(d));
        });
        const int scrolled = int(std::lround(m_ScrollAmount));
        out.rect = { clipped.left, clipped.top + scrolled, clipped.right, clipped.bottom + scrolled };
        co_return out;
    }

    rt::Task<> ScrollCapture::End() {
        co_await ScrollTo(0);
        m_InProgress = false;
        m_Owner.SetScrollCaptureInProgress(false);
    }

}
