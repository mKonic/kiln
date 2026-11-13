// LoadingIndicatorSamples.kt
#include "LoadingIndicatorSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/animation/Easing.h>
#include <kiln/foundation/Animate.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/ListItem.h>
#include <kiln/material3/LoadingIndicator.h>
#include <kiln/material3/PullToRefresh.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Slider.h>

#include <algorithm>

namespace catalog::samples {
    namespace {
        void Determinate(bool contained) {
            auto& sliderState = m3::RememberSliderState();
            const float animatedProgress = AnimateFloatAsState(
                sliderState.Value(), kiln::anim::Spring(kiln::anim::spring::DampingRatioNoBouncy, kiln::anim::spring::StiffnessVeryLow, 1 / 1000.0f));
            Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), [&sliderState, animatedProgress, contained] {
                const auto progress = [animatedProgress] { return animatedProgress; };
                if (contained) m3::ContainedLoadingIndicator(progress);
                else m3::LoadingIndicator(progress);
                Spacer(SizeIn({}, Dp::Unspecified(), 30_dp, Dp::Unspecified(), 30_dp, false));
                m3::Text("Set loading progress:");
                m3::Slider(sliderState, [&sliderState](float v) { sliderState.SetValue(v); }, Width({}, 300_dp));
            });
        }

        // fetch something
        kiln::rt::Task<> Refresh(kiln::rt::State<int>& itemCount, kiln::rt::State<bool>& isRefreshing) {
            co_await kiln::rt::Delay(5000);
            itemCount.Set(itemCount.Peek() + 5);
            isRefreshing.Set(false);
        }
    }

    void LoadingIndicatorSample() { Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), [] { m3::LoadingIndicator(); }); }
    void ContainedLoadingIndicatorSample() {
        Column({}, Arrangement::Top(), AlignmentH::CenterHorizontally(), [] { m3::ContainedLoadingIndicator(); });
    }
    void DeterminateLoadingIndicatorSample() { Determinate(false); }
    void DeterminateContainedLoadingIndicatorSample() { Determinate(true); }

    void LoadingIndicatorPullToRefreshSample() {
        auto& itemCount = RememberState(15);
        auto& isRefreshing = RememberState(false);
        const auto state = m3::RememberPullToRefreshState();
        const LaunchScope coroutineScope = RememberCoroutineScope();
        const kiln::Function<void()> onRefresh = [&itemCount, &isRefreshing, coroutineScope] {
            isRefreshing.Set(true);
            coroutineScope.Launch(Refresh(itemCount, isRefreshing));
        };
        const auto scaleFraction = [&isRefreshing, state] {
            if (isRefreshing.Get()) return 1.0f;
            return std::clamp(kiln::anim::LinearOutSlowIn().Transform(state->DistanceFraction()), 0.0f, 1.0f);
        };
        m3::ScaffoldOptions o;
        o.topBar = [onRefresh] {
            m3::TopAppBarOptions bar;
            // Provide an accessible alternative to trigger refresh.
            bar.actions = [onRefresh] {
                m3::IconButton(onRefresh, {}, {}, [] { m3::Icon(Icons::Filled::Refresh(), std::string("Trigger Refresh")); });
            };
            m3::TopAppBar([] { m3::Text("TopAppBar"); }, {}, bar);
        };
        m3::Scaffold(m3::PullToRefresh({}, isRefreshing.Get(), state, onRefresh), o,
                     [&itemCount, &isRefreshing, state, scaleFraction](const PaddingValues& padding) {
            Box(Padding({}, padding), Alignment::TopStart(), false, [&itemCount, &isRefreshing, state, scaleFraction] {
                LazyColumn(FillMax({}, Axis::Both), RememberLazyListState(), [&itemCount, &isRefreshing](LazyListScope& s) {
                    if (isRefreshing.Get()) return;
                    const int count = itemCount.Get();
                    s.Items(count, [count](int it) { m3::ListItem({}, {}, [count, it] { m3::Text("Item " + std::to_string(count - it)); }); });
                });
                Box(GraphicsLayer(AlignInBox({}, Alignment::TopCenter()), [scaleFraction](GraphicsLayerParams& l) {
                        l.scaleX = l.scaleY = scaleFraction();
                    }),
                    Alignment::TopStart(), false, [&isRefreshing, state] { m3::PullToRefreshDefaults::LoadingIndicator(state, isRefreshing.Get()); });
            });
        });
    }
}
