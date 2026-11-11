#include "kiln/material3/PullToRefresh.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Animate.h"
#include "kiln/foundation/AnimatedVisibility.h"
#include "kiln/foundation/Compose.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Layouts.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/graphics/Path.h"
#include "kiln/material3/LoadingIndicator.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/ProgressIndicator.h"
#include "kiln/material3/internal/CoroutineScope.h"
#include "kiln/material3/tokens/ElevationTokens.h"
#include "kiln/material3/tokens/MotionSchemeKeyTokens.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/ui/Locals.h"

#include <algorithm>
#include <cmath>

namespace kiln::material3 {

    using namespace foundation;
    using namespace tokens;
    using ui::Color;
    using ui::Dp;

    namespace {
        constexpr float DragMultiplier = 0.5f, MaxProgressArc = 0.8f, MinAlpha = 0.3f, MaxAlpha = 1.0f;
        constexpr Dp StrokeWidth{ 2.5f }, ArcRadius{ 5.5f }, ArrowWidth{ 10 }, ArrowHeight{ 5 };

        class PullToRefreshStateImpl final : public PullToRefreshState {
        public:
            explicit PullToRefreshStateImpl(float initial = 0) : m_Anim(std::make_shared<anim::Animatable>(initial)) {}
            float DistanceFraction() const override { return m_Anim->Float(); }
            bool IsAnimating() const override { return m_Anim->IsRunning(); }
            rt::Task<> AnimateToThreshold() override { co_await m_Anim->AnimateTo(1.0f); }
            rt::Task<> AnimateToHidden() override { co_await m_Anim->AnimateTo(0.0f); }
            rt::Task<> SnapTo(float target) override {
                m_Anim->SnapTo(target);
                co_return;
            }

        private:
            std::shared_ptr<anim::Animatable> m_Anim;
        };
    }

    std::shared_ptr<PullToRefreshState> MakePullToRefreshState() { return std::make_shared<PullToRefreshStateImpl>(); }

    std::shared_ptr<PullToRefreshState> RememberPullToRefreshState() {
        rt::Saver<std::shared_ptr<PullToRefreshState>> saver{
            [](const std::shared_ptr<PullToRefreshState>& s) { return rt::SavedValue(double(s->DistanceFraction())); },
            [](const rt::SavedValue& saved) -> std::optional<std::shared_ptr<PullToRefreshState>> {
                if (auto n = rt::SavedNumber(saved)) return std::make_shared<PullToRefreshStateImpl>(float(*n));
                return std::nullopt;
            }
        };
        return rt::RememberSaveable<std::shared_ptr<PullToRefreshState>>([] { return MakePullToRefreshState(); }, saver, rt::Keys());
    }

    // --- the modifier ---------------------------------------------------------------------------

    namespace {
        rt::Task<> Ignore(rt::Task<> t) { co_await std::move(t); }

        // PullToRefreshModifierNode, as the nested scroll connection.
        struct PullToRefreshConnection final : ui::NestedScrollConnection, std::enable_shared_from_this<PullToRefreshConnection> {
            bool isRefreshing = false, enabled = true, attached = false;
            std::function<void()> onRefresh;
            std::shared_ptr<PullToRefreshState> state;
            Dp threshold;
            ui::Density density;
            std::shared_ptr<internal::CoroutineScope> scope;
            float verticalOffset = 0, distancePulled = 0;

            float Adjusted() const { return distancePulled * DragMultiplier; }
            int ThresholdPx() const { return int(std::lround(density.ToPx(threshold))); }
            float Progress() const { return Adjusted() / float(ThresholdPx()); }

            void Attach() {
                attached = true;
                scope->Launch(Ignore(state->SnapTo(isRefreshing ? 1.0f : 0.0f)));
                verticalOffset = isRefreshing ? float(ThresholdPx()) : 0.0f;
            }

            ui::Offset OnPreScroll(ui::Offset available, ui::NestedScrollSource source) override {
                if (state->IsAnimating() || !enabled) return {};
                // Swiping up.
                if (source == ui::NestedScrollSource::UserInput && available.y < 0) return ConsumeAvailableOffset(available);
                return {};
            }
            ui::Offset OnPostScroll(ui::Offset, ui::Offset available, ui::NestedScrollSource source) override {
                if (state->IsAnimating() || !enabled) return {};
                // Swiping down.
                if (source != ui::NestedScrollSource::UserInput) return {};
                const ui::Offset consumed = ConsumeAvailableOffset(available);
                if (!state->IsAnimating()) scope->Launch(Ignore(state->SnapTo(verticalOffset / float(ThresholdPx()))));
                return consumed;
            }
            rt::Task<ui::Offset> OnPreFling(ui::Offset available) override {
                auto keep = shared_from_this();
                const float consumed = co_await OnRelease(available.y);
                co_return ui::Offset{ 0, consumed };
            }

            void Update() {
                auto keep = shared_from_this();
                if (!isRefreshing) scope->Launch(AnimateToHiddenTask(keep));
                else scope->Launch(AnimateToThresholdTask(keep));
            }

            ui::Offset ConsumeAvailableOffset(ui::Offset available) {
                float y = 0;
                if (!isRefreshing) {
                    const float next = std::max(0.0f, distancePulled + available.y);
                    y = next - distancePulled;
                    distancePulled = next;
                    verticalOffset = CalculateVerticalOffset();
                }
                return { 0, y };
            }

            rt::Task<float> OnRelease(float velocity) {
                if (isRefreshing) co_return 0.0f;   // already refreshing
                const bool triggered = Adjusted() > float(ThresholdPx());
                // A fling without a pull (inside a list), or upwards, is not ours; a downward one
                // over the shown indicator is.
                const float consumed = distancePulled == 0 || velocity < 0 ? 0.0f : velocity;
                if (triggered) {
                    if (onRefresh) onRefresh();
                    // Let an isRefreshing update made by onRefresh reach this node first: kiln
                    // resumes frame waiters before it recomposes, so wait past the next frame's
                    // recomposition (Compose's withFrameNanos runs after it).
                    co_await rt::NextFrame{};
                    co_await rt::NextFrame{};
                }
                if (!isRefreshing) co_await AnimateToHidden();
                distancePulled = 0;
                co_return consumed;
            }

            float CalculateVerticalOffset() const {
                const float thresholdPx = float(ThresholdPx());
                if (Adjusted() <= thresholdPx) return Adjusted();
                // Past the threshold: a tension that grows ever more slowly, capped at 200%.
                const float overshoot = std::abs(Progress()) - 1.0f;
                const float linear = std::clamp(overshoot, 0.0f, 2.0f);
                const float tension = linear - linear * linear / 4;
                return thresholdPx + thresholdPx * tension;
            }

            rt::Task<> AnimateToHidden() {
                struct Finally {
                    PullToRefreshConnection* self;
                    ~Finally() { self->distancePulled = self->verticalOffset = 0; }
                } f{ this };
                co_await state->AnimateToHidden();
            }
            static rt::Task<> AnimateToHiddenTask(std::shared_ptr<PullToRefreshConnection> self) { co_await self->AnimateToHidden(); }
            static rt::Task<> AnimateToThresholdTask(std::shared_ptr<PullToRefreshConnection> self) {
                struct Finally {
                    PullToRefreshConnection* self;
                    ~Finally() {
                        if (!self->attached) return;   // updated when it attaches again
                        self->distancePulled = self->verticalOffset = float(self->ThresholdPx());
                    }
                } f{ self.get() };
                co_await self->state->AnimateToThreshold();
            }
        };
    }

    ui::Modifier PullToRefresh(ui::Modifier m, bool isRefreshing, std::shared_ptr<PullToRefreshState> state, std::function<void()> onRefresh, bool enabled,
                               Dp threshold) {
        auto& c = *rt::Composition::Current();
        const auto scope = internal::RememberCoroutineScope();
        const ui::Density density = c.Read(ui::LocalDensity);
        const auto conn = c.Remember<std::shared_ptr<PullToRefreshConnection>>([] { return std::make_shared<PullToRefreshConnection>(); });
        c.SideEffect([conn, isRefreshing, state, onRefresh, enabled, threshold, density, scope] {
            conn->onRefresh = onRefresh;
            conn->enabled = enabled;
            conn->state = state;
            conn->threshold = threshold;
            conn->density = density;
            conn->scope = scope;
            if (!conn->attached) {
                conn->isRefreshing = isRefreshing;
                conn->Attach();
            } else if (conn->isRefreshing != isRefreshing) {
                conn->isRefreshing = isRefreshing;
                conn->Update();
            }
        });
        c.DisposableEffect(rt::Keys(reinterpret_cast<std::uintptr_t>(conn.get())), [conn]() -> std::function<void()> {
            return [conn] { conn->attached = false; };
        });
        return NestedScroll(std::move(m), conn);
    }

    // --- defaults and indicators ----------------------------------------------------------------

    namespace PullToRefreshDefaults {
        const Dp PositionalThreshold{ 80 };
        const Dp IndicatorMaxDistance{ 80 };
        const Dp Elevation = ElevationTokens::Level2;
        const Dp LoadingIndicatorElevation = ElevationTokens::Level0;
        ShapePtr indicatorShape() { return CircleShape(); }
        Color indicatorContainerColor() { return MaterialTheme::colorScheme().surfaceContainerHigh; }
        Color loadingIndicatorContainerColor() { return LoadingIndicatorDefaults::containedContainerColor(); }
        Color indicatorColor() { return MaterialTheme::colorScheme().onSurfaceVariant; }
        Color loadingIndicatorColor() { return LoadingIndicatorDefaults::containedIndicatorColor(); }

        void IndicatorBox(std::shared_ptr<PullToRefreshState> state, bool isRefreshing, const ui::Modifier& modifier, const IndicatorBoxOptions& o,
                          std::function<void()> content) {
            const ui::Density d = rt::Composition::Current()->Read(ui::LocalDensity);
            const float maxDistancePx = float(std::lround(d.ToPx(o.maxDistance)));
            const float elevationPx = d.ToPx(o.elevation);
            const ShapePtr shape = o.shape ? o.shape : indicatorShape();
            ui::Modifier m = SizeMod(modifier, internal::SpinnerContainerSize, internal::SpinnerContainerSize);
            // Nothing shows above where the indicator rests.
            m = DrawWithContent(std::move(m), [](ui::ContentDrawScope& s) {
                s.Save();
                s.ClipRect(ui::Rect{ -1e7f, 0, 1e7f, 1e7f });
                s.DrawContent();
                s.Restore();
            });
            m = GraphicsLayer(std::move(m), [state, isRefreshing, maxDistancePx, elevationPx, shape](GraphicsLayerParams& l) {
                const bool showElevation = state->DistanceFraction() > 0 || isRefreshing;
                l.translationY = state->DistanceFraction() * maxDistancePx - l.size.height;
                l.shadowElevation = showElevation ? elevationPx : 0.0f;
                l.clipShape = shape;
                l.clip = true;
            });
            m = Background(std::move(m), o.containerColor, shape);
            Box(m, ui::Alignment::Center(), false, std::move(content));
        }

        namespace {
            struct ArrowValues {
                float rotation, startAngle, endAngle, scale;
            };
            ArrowValues MakeArrowValues(float progress) {
                // The first 40% of the progress is dropped; the rest scales to the full range.
                const float adjusted = std::max(std::min(1.0f, progress) - 0.4f, 0.0f) * 5 / 3;
                const float overshoot = std::abs(progress) - 1.0f;
                const float linear = std::clamp(overshoot, 0.0f, 2.0f);
                const float tension = linear - linear * linear / 4;
                // SwipeRefreshLayout's figures.
                const float endTrim = adjusted * MaxProgressArc;
                const float rotation = (-0.25f + 0.4f * adjusted + tension) * 0.5f;
                return { rotation, rotation * 360, (rotation + endTrim) * 360, std::min(1.0f, adjusted) };
            }

            void CircularArrowProgressIndicator(std::shared_ptr<PullToRefreshState> state, Color color) {
                auto& c = *rt::Composition::Current();
                // Read in its own scope: crossing the threshold recomposes only this.
                const auto alpha = c.Remember<std::shared_ptr<rt::State<float>>>([] { return std::make_shared<rt::State<float>>(MinAlpha); });
                c.Child([state, alpha] {
                    const float target = state->DistanceFraction() >= 1 ? MaxAlpha : MinAlpha;
                    const float animated = AnimateFloatAsState(target, Value(MotionSchemeKeyTokens::DefaultEffects));
                    if (alpha->Peek() != animated) alpha->Set(animated);
                });
                ui::Modifier m = ClearAndSetSemantics({}, [state](ui::SemanticsConfiguration& s) {
                    const float p = state->DistanceFraction();
                    if (p > 0) s.progressBarRangeInfo = ui::ProgressBarRangeInfo{ p, 0, 1, 0 };
                });
                m = SizeMod(std::move(m), internal::SpinnerSize, internal::SpinnerSize);
                Canvas(m, [state, alpha, color](ui::DrawScope& s) {
                    const ArrowValues v = MakeArrowValues(state->DistanceFraction());
                    const float a = alpha->Get();
                    const ui::Size size = s.GetSize();
                    const ui::Offset center{ size.width / 2, size.height / 2 };
                    const float strokePx = s.ToPx(StrokeWidth);
                    s.Save();
                    s.Rotate(v.rotation, center);
                    const float arcRadius = s.ToPx(ArcRadius) + strokePx / 2;
                    const ui::Rect bounds{ center.x - arcRadius, center.y - arcRadius, center.x + arcRadius, center.y + arcRadius };
                    gfx::Path arc;
                    arc.AddArc(bounds, v.startAngle, v.endAngle - v.startAngle);
                    gfx::Stroke stroke;
                    stroke.width = strokePx;
                    stroke.cap = gfx::StrokeCap::Butt;
                    s.DrawPath(arc, gfx::Brush::SolidColor(color), &stroke, a);
                    // The arrow at the arc's end.
                    const float arrowW = s.ToPx(ArrowWidth) * v.scale, arrowH = s.ToPx(ArrowHeight) * v.scale;
                    const float radius = std::min(bounds.Width(), bounds.Height()) / 2;
                    const float dx = radius + center.x - arrowW / 2, dy = center.y - strokePx;
                    gfx::Path arrow;
                    arrow.fillType = gfx::FillType::EvenOdd;
                    arrow.MoveTo(dx, dy);
                    arrow.LineTo(dx + arrowW / 2, dy + arrowH);
                    arrow.LineTo(dx + arrowW, dy);
                    s.Save();
                    s.Rotate(v.endAngle - strokePx, center);
                    gfx::Stroke arrowStroke;
                    arrowStroke.width = strokePx;
                    s.DrawPath(arrow, gfx::Brush::SolidColor(color), &arrowStroke, a);
                    s.Restore();
                    s.Restore();
                });
            }
        }

        void Indicator(std::shared_ptr<PullToRefreshState> state, bool isRefreshing, const ui::Modifier& modifier, const IndicatorOptions& o) {
            IndicatorBoxOptions box;
            box.containerColor = o.containerColor.value_or(indicatorContainerColor());
            box.maxDistance = o.maxDistance;
            const Color color = o.color.value_or(indicatorColor());
            IndicatorBox(state, isRefreshing, modifier, box, [state, isRefreshing, color] {
                Crossfade(rt::Key(isRefreshing ? 1 : 0), {}, [state, color](rt::Key k) {
                    if (k.value == rt::Key(1).value) {
                        CircularProgressIndicatorOptions p;
                        p.strokeWidth = StrokeWidth;
                        p.color = color;
                        CircularProgressIndicator(SizeMod({}, internal::SpinnerSize, internal::SpinnerSize), p);
                    } else {
                        CircularArrowProgressIndicator(state, color);
                    }
                }, Value(MotionSchemeKeyTokens::DefaultEffects));
            });
        }

        void LoadingIndicator(std::shared_ptr<PullToRefreshState> state, bool isRefreshing, const ui::Modifier& modifier, const IndicatorOptions& o) {
            IndicatorBoxOptions box;
            box.containerColor = o.containerColor.value_or(loadingIndicatorContainerColor());
            box.elevation = o.elevation.value_or(LoadingIndicatorElevation);
            box.maxDistance = o.maxDistance;
            const Dp w = LoadingIndicatorDefaults::ContainerWidth, h = LoadingIndicatorDefaults::ContainerHeight;
            ContainedLoadingIndicatorOptions li;
            li.containerColor = box.containerColor;
            li.indicatorColor = o.color.value_or(loadingIndicatorColor());
            IndicatorBox(state, isRefreshing, SizeMod(modifier, w, h), box, [state, isRefreshing, w, h, li] {
                Crossfade(rt::Key(isRefreshing ? 1 : 0), {}, [state, w, h, li](rt::Key k) {
                    if (k.value == rt::Key(1).value) {
                        ContainedLoadingIndicator(RequiredSize({}, w, h), li);
                        return;
                    }
                    // Morphs with the pull; past the threshold the whole indicator turns. The
                    // fraction is read only while drawing and building semantics, so a pull does
                    // not recompose it; at rest it is hidden from accessibility services.
                    ui::Modifier m = ClearAndSetSemantics({}, [state](ui::SemanticsConfiguration& s) {
                        const float p = state->DistanceFraction();
                        if (p > 0) s.progressBarRangeInfo = ui::ProgressBarRangeInfo{ std::clamp(p, 0.0f, 1.0f), 0, 1 };
                    });
                    m = RequiredSize(std::move(m), w, h);
                    m = DrawWithContent(std::move(m), [state](ui::ContentDrawScope& s) {
                        const float p = state->DistanceFraction();
                        if (p > 1) {
                            // From progress - 1, so the turn starts without a jump.
                            const ui::Size size = s.GetSize();
                            s.Save();
                            s.Rotate(-(p - 1) * 180, ui::Offset{ size.width / 2, size.height / 2 });
                            s.DrawContent();
                            s.Restore();
                        } else if (p > 0) {
                            s.DrawContent();
                        }
                    });
                    ContainedLoadingIndicator([state] { return state->DistanceFraction(); }, m, li);
                }, Value(MotionSchemeKeyTokens::DefaultEffects));
            });
        }
    }

    void PullToRefreshBox(bool isRefreshing, std::function<void()> onRefresh, const ui::Modifier& modifier, const PullToRefreshBoxOptions& o,
                          std::function<void()> content) {
        const auto state = o.state ? o.state : RememberPullToRefreshState();
        std::function<void()> indicator = o.indicator;
        if (!indicator)
            indicator = [state, isRefreshing] { PullToRefreshDefaults::Indicator(state, isRefreshing, AlignInBox({}, ui::Alignment::TopCenter())); };
        Box(PullToRefresh(modifier, isRefreshing, state, std::move(onRefresh), o.enabled, o.threshold), o.contentAlignment, false, [content, indicator] {
            content();
            indicator();
        });
    }

}
