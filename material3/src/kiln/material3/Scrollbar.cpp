#include "kiln/material3/Scrollbar.h"

#include "kiln/animation/Animatable.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/ui/LayoutNode.h"

#include <algorithm>
#include <stdexcept>

namespace kiln::material3 {

    ui::Color NonInteractiveScrollbarDefaults::thumbColor() {
        return MaterialTheme::colorScheme().outline.WithAlpha(ThumbOpacity);
    }

    namespace {
        struct ScrollbarParams {
            const foundation::ScrollIndicatorState* state;
            foundation::Orientation orientation;
            ui::Color thumbColor, trackColor;
            ui::Dp thickness, thumbMinLength;
            float thumbMaxLengthFraction;
            bool isFadeEnabled;
            int fadeDurationMillis, fadeDelayMillis;
            ui::Dp mainAxisTrackInset, crossAxisTrackInset;
            bool operator==(const ScrollbarParams&) const = default;
        };

        // NonInteractiveScrollbarNode. An update recreates Compose's inner node; here the node
        // starts over the same way (alpha, fade job and offset baseline).
        struct ScrollbarNode final : ui::ModifierNode, ui::DrawModifier {
            ScrollbarParams params;
            anim::Animatable alpha;
            int lastOffset = -1;
            rt::Job fadeJob;
            // When the last scroll event came (the fade waits fadeDelayMillis past it: the
            // withTimeoutOrNull loop over the scroll events channel).
            rt::Dispatcher::Clock::time_point lastEvent{};
            internal::ScrollbarFrame frame;

            explicit ScrollbarNode(ScrollbarParams p) : params(p), alpha(p.isFadeEnabled ? 0.0f : 1.0f) {}
            ui::DrawModifier* AsDraw() override { return this; }

            void OnAttach() override {
                // Baseline the offset on (re-)attach.
                rt::Observation::Untracked([&] { lastOffset = params.state->ScrollOffset(); });
            }
            void OnDetach() override { fadeJob.Cancel(); }
            void OnParamsChanged() {
                fadeJob.Cancel();
                rt::Observation::Untracked([&] { alpha.SnapTo(params.isFadeEnabled ? 0.0f : 1.0f); });
                if (Owner() && Owner()->GetOwner()) OnAttach();
                InvalidateDraw();
            }

            static rt::Task<> Fade(ScrollbarNode* node) {
                node->alpha.SnapTo(1.0f);
                rt::Dispatcher& d = rt::Dispatcher::Main();
                for (;;) {
                    const auto due = node->lastEvent + std::chrono::milliseconds(node->params.fadeDelayMillis);
                    const auto wait = std::chrono::duration_cast<std::chrono::milliseconds>(due - d.Now()).count();
                    if (wait <= 0) break;
                    co_await rt::Delay(wait);
                }
                co_await node->alpha.AnimateTo(0.0f, anim::Tween(node->params.fadeDurationMillis, 0, anim::FastOutSlowIn()));
            }

            void Draw(ui::ContentDrawScope& s) override {
                s.DrawContent();
                frame = {};
                const auto& state = *params.state;
                const float contentSize = float(state.ContentSize());
                const float viewportSize = float(state.ViewportSize());
                // Nothing to show (and scrollOffset left unread, so scrolling does not redraw).
                if (contentSize <= viewportSize || viewportSize == 0 || contentSize == 0) return;

                const int currentOffset = state.ScrollOffset();
                if (params.isFadeEnabled && currentOffset != lastOffset) {
                    lastOffset = currentOffset;
                    lastEvent = rt::Dispatcher::Main().Now();
                    bool fadingOut = false;
                    rt::Observation::Untracked([&] { fadingOut = alpha.Target()[0] == 0.0f; });
                    if (fadingOut || !fadeJob.Active()) {
                        fadeJob.Cancel();
                        fadeJob = rt::Launch(rt::Dispatcher::Main(), Fade(this));
                    }
                }

                const float currentAlpha = params.isFadeEnabled ? alpha.Float() : 1.0f;
                const auto withAlpha = [&](ui::Color c) { return currentAlpha == 1 ? c : c.WithAlpha(c.Alpha() * currentAlpha); };
                const ui::Color thumbColor = withAlpha(params.thumbColor), trackColor = withAlpha(params.trackColor);
                const bool trackVisible = trackColor.Alpha() > 0, thumbVisible = thumbColor.Alpha() > 0;
                if (!trackVisible && !thumbVisible) return;

                const ui::Size size = s.GetSize();
                const bool vertical = params.orientation == foundation::Orientation::Vertical;
                const float offset = float(currentOffset);
                const float mainInset = s.ToPx(params.mainAxisTrackInset), crossInset = s.ToPx(params.crossAxisTrackInset);
                const float trackLength = (vertical ? size.height : size.width) - 2 * mainInset;
                const float minThumbLength = s.ToPx(params.thumbMinLength);
                // Too short for the shortest thumb.
                if (trackLength <= 0 || trackLength < minThumbLength) return;
                const float maxThumbLength = trackLength * params.thumbMaxLengthFraction;
                const float thickness = s.ToPx(params.thickness);
                // coerceIn: the minimum wins over a smaller maximum, as Kotlin's would throw instead.
                const float thumbLength = std::max(minThumbLength, std::min(trackLength * (viewportSize / contentSize), maxThumbLength));
                const float maxOffset = contentSize - viewportSize;
                const float thumbOffset = maxOffset > 0 ? (offset / maxOffset) * (trackLength - thumbLength) : 0.0f;
                const float radius = thickness / 2;

                ui::Rect track, thumb;
                if (vertical) {
                    const float x = s.Direction() == ui::LayoutDirection::Ltr ? size.width - crossInset - thickness : crossInset;
                    track = { x, mainInset, x + thickness, mainInset + trackLength };
                    thumb = { x, mainInset + thumbOffset, x + thickness, mainInset + thumbOffset + thumbLength };
                } else {
                    const float y = size.height - crossInset - thickness;
                    const float thumbX = s.Direction() == ui::LayoutDirection::Ltr ? mainInset + thumbOffset
                                                                                   : size.width - mainInset - thumbLength - thumbOffset;
                    track = { mainInset, y, mainInset + trackLength, y + thickness };
                    thumb = { thumbX, y, thumbX + thumbLength, y + thickness };
                }
                if (trackVisible) s.DrawRoundRect(trackColor, { track.left, track.top }, { track.Width(), track.Height() }, 0);
                if (thumbVisible) s.DrawRoundRect(thumbColor, { thumb.left, thumb.top }, { thumb.Width(), thumb.Height() }, radius);
                frame = { true, thumbVisible ? thumbColor : ui::Color::Transparent(), trackVisible ? trackColor : ui::Color::Transparent(), thumb, track };
            }
        };
    }

    ui::Modifier NonInteractiveScrollbar(ui::Modifier m, const foundation::ScrollIndicatorState& state, foundation::Orientation orientation,
                                         const NonInteractiveScrollbarOptions& o) {
        if (!(o.thumbMaxLengthFraction >= 0 && o.thumbMaxLengthFraction <= 1))
            throw std::invalid_argument("thumbMaxLengthFraction must be between 0f and 1f");
        const ui::Color thumb = o.thumbColor.Specified() ? o.thumbColor : NonInteractiveScrollbarDefaults::thumbColor();
        return m.With<ScrollbarNode>(ScrollbarParams{ &state, orientation, thumb, o.trackColor, o.thickness, o.thumbMinLength,
                                                       o.thumbMaxLengthFraction, o.isFadeEnabled, o.fadeDurationMillis, o.fadeDelayMillis,
                                                       o.mainAxisTrackInset, o.crossAxisTrackInset });
    }

    std::optional<internal::ScrollbarFrame> internal::LastScrollbarFrame(const ui::LayoutNode& node) {
        for (const ui::ModifierNode* m : node.ModifierNodes())
            if (const auto* s = dynamic_cast<const ScrollbarNode*>(m)) return s->frame;
        return std::nullopt;
    }

}
