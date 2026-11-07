#pragma once

// Scroll capture (ScrollCapture.android.kt, ComposeScrollCaptureCallback.android.kt): the long
// screenshot's target is the deepest (then tallest) vertically scrollable semantics node; each
// capture request scrolls the requested strip to the middle of its viewport and renders it.

#include "kiln/runtime/Task.h"
#include "kiln/ui/Accessibility.h"

#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace kiln::ui {

    class Owner;

    class ScrollCapture {
    public:
        explicit ScrollCapture(Owner& owner) : m_Owner(owner) {}

        struct Candidate {
            int id = 0;
            int depth = 0;
            IntRect viewportBounds;    // in root pixels
        };
        // onScrollCaptureSearch's candidate, if any.
        std::optional<Candidate> Search();

        // onScrollCaptureStart: the session for the node (a candidate from Search).
        void Start(const Candidate& candidate);
        // onScrollCaptureImageRequest: the captured rect (relative to the scroll start) and its
        // pixels (captureArea's size, RGBA premultiplied; empty for an empty rect).
        struct Image {
            IntRect rect;
            int width = 0, height = 0;
            std::vector<std::uint8_t> rgba;
        };
        rt::Task<Image> ImageRequest(IntRect captureArea);
        // onScrollCaptureEnd: back to where it started.
        rt::Task<> End();
        bool InProgress() const { return m_InProgress; }

    private:
        rt::Task<> ScrollTo(float offset);
        rt::Task<> ScrollBy(float delta);
        int MapOffsetToViewport(int offset) const;
        Owner& m_Owner;
        Candidate m_Candidate;
        float m_ScrollAmount = 0;
        bool m_InProgress = false;
    };

}
