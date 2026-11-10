#include "kiln/material3/LoadingIndicator.h"

#include "kiln/animation/Animatable.h"
#include "kiln/foundation/Draw.h"
#include "kiln/foundation/Semantics.h"
#include "kiln/graphics/shapes/Morph.h"
#include "kiln/material3/MaterialShapes.h"
#include "kiln/material3/MaterialTheme.h"
#include "kiln/material3/tokens/LoadingIndicatorTokens.h"

#include <chrono>
#include <list>
#include <numbers>
#include <stdexcept>

namespace kiln::material3 {
    using namespace foundation;
    using namespace ui;
    using gfx::shapes::Morph;
    using gfx::shapes::RoundedPolygon;

    namespace {
        using Polygons = std::vector<RoundedPolygon>;
        struct Sequence {
            Polygons polygons;
            bool circular;
            std::vector<Morph> morphs;
            float scale = 1;
        };
        std::shared_ptr<const Sequence> MorphSequence(const Polygons& polygons, bool circular) {
            // LoadingIndicator.kt caches eight value-equal sequences, shared between indicators.
            static std::list<std::shared_ptr<const Sequence>> cache;
            for (auto it = cache.begin(); it != cache.end(); ++it) if ((*it)->circular == circular && (*it)->polygons == polygons) {
                const auto result = *it;
                cache.splice(cache.begin(), cache, it);
                return result;
            }
            if (polygons.size() < 2) throw std::invalid_argument("indicatorPolygons should have, at least, two RoundedPolygons");
            auto sequence = std::make_shared<Sequence>(Sequence{ polygons, circular, {} });
            for (std::size_t i = 0; i < polygons.size(); ++i) {
                if (i + 1 < polygons.size()) sequence->morphs.emplace_back(polygons[i].Normalized(), polygons[i + 1].Normalized());
                else if (circular) sequence->morphs.emplace_back(polygons[i].Normalized(), polygons[0].Normalized());
                const auto bounds = polygons[i].CalculateBounds(), maxBounds = polygons[i].CalculateMaxBounds();
                sequence->scale = std::min(sequence->scale, std::max((bounds[2] - bounds[0]) / (maxBounds[2] - maxBounds[0]), (bounds[3] - bounds[1]) / (maxBounds[3] - maxBounds[1])));
            }
            sequence->scale *= LoadingIndicatorDefaults::IndicatorSize.value / std::min(LoadingIndicatorDefaults::ContainerWidth.value, LoadingIndicatorDefaults::ContainerHeight.value);
            cache.push_front(sequence);
            if (cache.size() > 8) cache.pop_back();
            return sequence;
        }
        struct AnimationState {
            anim::Animatable morph{0}, rotation{0};
            rt::State<std::size_t> index{0};
            rt::State<float> rotationTarget{90};
            std::shared_ptr<const Sequence> sequence;
        };
        rt::Task<> RunMorphs(std::shared_ptr<AnimationState> state, std::shared_ptr<const Sequence> sequence) {
            const auto spec = anim::Spring(0.6f, 200, 0.1f);
            for (;;) {
                const auto start = rt::Dispatcher::Main().Now();
                const auto result = co_await state->morph.AnimateTo(1, spec);
                if (result.reason == anim::EndReason::Finished) {
                    state->index.Set((state->index.Peek() + 1) % sequence->morphs.size());
                    state->morph.SnapTo(0);
                    state->rotationTarget.Set(std::fmod(state->rotationTarget.Peek() + 90, 360.0f));
                }
                const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(rt::Dispatcher::Main().Now() - start).count();
                if (elapsed < 650) co_await rt::Delay(650 - elapsed);
            }
        }
        rt::Task<> RunRotation(std::shared_ptr<AnimationState> state) {
            co_await state->rotation.AnimateTo(360, anim::InfiniteRepeatable(anim::Tween(4666, 0, anim::Linear())));
        }
        void DrawMorph(DrawScope& s, const Sequence& sequence, std::size_t index, float progress, float rotation, Color color) {
            auto path = sequence.morphs[index].ToPath(progress);
            const auto size = s.GetSize();
            path.Transform(gfx::Matrix::Scale(size.width * sequence.scale, size.height * sequence.scale));
            const auto bounds = path.Bounds();
            const Offset center{ size.width / 2, size.height / 2 };
            path.Translate({ center.x - (bounds.left + bounds.right) / 2, center.y - (bounds.top + bounds.bottom) / 2 });
            s.Save();
            s.Rotate(rotation, center);
            s.DrawPath(path, color);
            s.Restore();
        }
        void Impl(std::function<float()> progress, const Modifier& modifier, Color container, Color indicator, ShapePtr shape, const Polygons& polygons) {
            const auto sequence = MorphSequence(polygons, !progress);
            std::shared_ptr<AnimationState> state;
            if (!progress) {
                auto& c = *rt::Composition::Current();
                state = c.Remember<std::shared_ptr<AnimationState>>([] { return std::make_shared<AnimationState>(); });
                if (state->sequence != sequence) { state->index.Set(0); state->sequence = sequence; }
                c.LaunchedEffect(rt::Keys(std::hash<const void*>{}(sequence.get())), [state, sequence] { return RunMorphs(state, sequence); });
                c.LaunchedEffect(rt::Keys(std::hash<const void*>{}(sequence.get())), [state] { return RunRotation(state); });
            }
            auto m = Semantics(modifier, [progress](SemanticsConfiguration& config) {
                const float value = progress ? std::clamp(progress(), 0.0f, 1.0f) : 0;
                config.progressBarRangeInfo = progress ? ProgressBarRangeInfo{ std::isnan(value) ? 0 : value, 0, 1 } : ProgressBarRangeInfo::Indeterminate();
            }, true);
            m = Clip(FillMax(SizeMod(m, LoadingIndicatorDefaults::ContainerWidth, LoadingIndicatorDefaults::ContainerHeight), Axis::Both), shape);
            if (container.Specified()) m = Background(m, container);
            Box(m, Alignment::Center(), false, [=] {
                Canvas(AspectRatio({}, 1, true), [=](DrawScope& s) {
                    if (progress) {
                        const float value = std::clamp(progress(), 0.0f, 1.0f);
                        if (!std::isfinite(value)) return;
                        const auto index = std::min(std::size_t(float(sequence->morphs.size()) * value), sequence->morphs.size() - 1);
                        const float adjusted = value == 1 && index == sequence->morphs.size() - 1 ? 1 : std::fmod(value * sequence->morphs.size(), 1.0f);
                        DrawMorph(s, *sequence, index, adjusted, -value * 180, indicator);
                    } else {
                        const float value = state->morph.Float();
                        DrawMorph(s, *sequence, state->index.Get(), value, value * 90 + state->rotationTarget.Get() + state->rotation.Float(), indicator);
                    }
                });
            });
        }
    }
    void LoadingIndicator(std::function<float()> progress, const Modifier& modifier, const LoadingIndicatorOptions& o) {
        Impl(std::move(progress), modifier, Color::Unspecified(), o.color.Specified() ? o.color : LoadingIndicatorDefaults::indicatorColor(),
             LoadingIndicatorDefaults::containerShape(), o.polygons ? *o.polygons : LoadingIndicatorDefaults::determinateIndicatorPolygons());
    }
    void LoadingIndicator(const Modifier& modifier, const LoadingIndicatorOptions& o) {
        Impl({}, modifier, Color::Unspecified(), o.color.Specified() ? o.color : LoadingIndicatorDefaults::indicatorColor(),
             LoadingIndicatorDefaults::containerShape(), o.polygons ? *o.polygons : LoadingIndicatorDefaults::indeterminateIndicatorPolygons());
    }
    void ContainedLoadingIndicator(std::function<float()> progress, const Modifier& modifier, const ContainedLoadingIndicatorOptions& o) {
        Impl(std::move(progress), modifier, o.containerColor.Specified() ? o.containerColor : LoadingIndicatorDefaults::containedContainerColor(),
             o.indicatorColor.Specified() ? o.indicatorColor : LoadingIndicatorDefaults::containedIndicatorColor(), o.containerShape ? o.containerShape : LoadingIndicatorDefaults::containerShape(),
             o.polygons ? *o.polygons : LoadingIndicatorDefaults::determinateIndicatorPolygons());
    }
    void ContainedLoadingIndicator(const Modifier& modifier, const ContainedLoadingIndicatorOptions& o) {
        Impl({}, modifier, o.containerColor.Specified() ? o.containerColor : LoadingIndicatorDefaults::containedContainerColor(),
             o.indicatorColor.Specified() ? o.indicatorColor : LoadingIndicatorDefaults::containedIndicatorColor(), o.containerShape ? o.containerShape : LoadingIndicatorDefaults::containerShape(),
             o.polygons ? *o.polygons : LoadingIndicatorDefaults::indeterminateIndicatorPolygons());
    }
    namespace LoadingIndicatorDefaults {
        ShapePtr containerShape() { return Value(tokens::LoadingIndicatorTokens::ContainerShape); }
        Color indicatorColor() { return Value(tokens::LoadingIndicatorTokens::ActiveIndicatorColor); }
        Color containedIndicatorColor() { return Value(tokens::LoadingIndicatorTokens::ContainedActiveColor); }
        Color containedContainerColor() { return Value(tokens::LoadingIndicatorTokens::ContainedContainerColor); }
        const Polygons& indeterminateIndicatorPolygons() {
            static const Polygons polygons{ MaterialShapes::SoftBurst(), MaterialShapes::Cookie9Sided(), MaterialShapes::Pentagon(), MaterialShapes::Pill(), MaterialShapes::Sunny(), MaterialShapes::Cookie4Sided(), MaterialShapes::Oval() };
            return polygons;
        }
        const Polygons& determinateIndicatorPolygons() {
            static const Polygons polygons{ MaterialShapes::Circle().Transformed([](float x, float y) {
                constexpr float angle = 18 * std::numbers::pi_v<float> / 180;
                return gfx::shapes::Point{ x * std::cos(angle) - y * std::sin(angle), x * std::sin(angle) + y * std::cos(angle) };
            }), MaterialShapes::SoftBurst() };
            return polygons;
        }
    }
}
