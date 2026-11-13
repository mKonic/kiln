// PullToRefreshSamples.kt
#include "PullToRefreshSamples.h"
#include <kiln/Function.h>
#include "Common.h"

#include <kiln/animation/Animatable.h>
#include <kiln/animation/Easing.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/IconButton.h>
#include <kiln/material3/ListItem.h>
#include <kiln/material3/ProgressIndicator.h>
#include <kiln/material3/PullToRefresh.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/runtime/Channel.h>
#include <kiln/runtime/CoroutineScope.h>

#include <algorithm>
#include <memory>

namespace catalog::samples {
    namespace PD = m3::PullToRefreshDefaults;
    using StatePtr = std::shared_ptr<m3::PullToRefreshState>;

    namespace {
        kiln::rt::Task<> Refresh(kiln::rt::State<int>& itemCount, kiln::rt::State<bool>& isRefreshing, int millis) {
            co_await kiln::rt::Delay(millis);
            itemCount.Set(itemCount.Peek() + 5);
            isRefreshing.Set(false);
        }

        // The samples' state: 15 items, five more after a refresh of `millis`.
        struct Refreshing {
            kiln::rt::State<int>& itemCount;
            kiln::rt::State<bool>& isRefreshing;
            kiln::Function<void()> onRefresh;
        };
        Refreshing RememberRefreshing(int millis = 5000) {
            auto& itemCount = RememberState(15);
            auto& isRefreshing = RememberState(false);
            const LaunchScope coroutineScope = RememberCoroutineScope();
            return { itemCount, isRefreshing, [&itemCount, &isRefreshing, coroutineScope, millis] {
                        isRefreshing.Set(true);
                        coroutineScope.Launch(Refresh(itemCount, isRefreshing, millis));
                    } };
        }

        void Bar(const char* title, kiln::Function<void()> onRefresh, bool enabled = true) {
            m3::TopAppBarOptions bar;
            // Provide an accessible alternative to trigger refresh.
            bar.actions = [onRefresh, enabled] {
                m3::IconButtonOptions o;
                o.enabled = enabled;
                m3::IconButton(onRefresh, {}, o, [] { m3::Icon(Icons::Filled::Refresh(), std::string("Trigger Refresh")); });
            };
            m3::TopAppBar([title] { m3::Text(title); }, {}, bar);
        }

        // LazyColumn(Modifier.fillMaxSize()) { items(itemCount) { ListItem { Text("Item ${itemCount - it}") } } }
        void Items(kiln::Function<int()> count) {
            LazyColumn(FillMax({}, Axis::Both), RememberLazyListState(), [count](LazyListScope& s) {
                const int n = count();
                s.Items(n, [n](int it) { m3::ListItem({}, {}, [n, it] { m3::Text("Item " + std::to_string(n - it)); }); });
            });
        }

        // The PullToRefreshBox samples: the list in the box, under a top bar.
        void BoxSample(const char* title, const Refreshing& r, StatePtr state, bool hideWhileRefreshing,
                       kiln::Function<void(StatePtr, bool)> indicator = {}) {
            m3::ScaffoldOptions o;
            o.topBar = [title, onRefresh = r.onRefresh] { Bar(title, onRefresh); };
            m3::Scaffold({}, o, [r, state, hideWhileRefreshing, indicator](const PaddingValues& padding) {
                m3::PullToRefreshBoxOptions b;
                b.state = state;
                if (indicator) b.indicator = [indicator, state, &isRefreshing = r.isRefreshing] { indicator(state, isRefreshing.Get()); };
                m3::PullToRefreshBox(r.isRefreshing.Get(), r.onRefresh, Padding({}, padding), b, [r, hideWhileRefreshing] {
                    Items([r, hideWhileRefreshing] { return hideWhileRefreshing && r.isRefreshing.Get() ? 0 : r.itemCount.Get(); });
                });
            });
        }

        // A custom state: an Animatable with a high-bouncy spring to the threshold.
        class BouncyState : public m3::PullToRefreshState {
        public:
            float DistanceFraction() const override { return m_Anim.Float(); }
            bool IsAnimating() const override { return m_Anim.IsRunning(); }
            kiln::rt::Task<> AnimateToThreshold() override {
                co_await m_Anim.AnimateTo(1.0f, kiln::anim::Spring(kiln::anim::spring::DampingRatioHighBouncy));
            }
            kiln::rt::Task<> AnimateToHidden() override { co_await m_Anim.AnimateTo(0.0f); }
            kiln::rt::Task<> SnapTo(float targetValue) override {
                m_Anim.SnapTo(targetValue);
                co_return;
            }

        private:
            kiln::anim::Animatable m_Anim{ 0.0f };
        };

        // The view model: refresh requests queue on a channel and a loop serves them.
        struct ViewModel {
            std::unique_ptr<kiln::rt::Channel<bool>> refreshRequests = std::make_unique<kiln::rt::Channel<bool>>(1);
            kiln::rt::State<bool> isRefreshing{ false };
            kiln::rt::State<int> itemCount{ 15 };
            kiln::rt::CoroutineScope viewModelScope;

            ViewModel() { viewModelScope.Launch(Serve(this)); }
            static kiln::rt::Task<> Serve(ViewModel* self) {
                while (co_await self->refreshRequests->Next()) {
                    self->isRefreshing.Set(true);
                    self->itemCount.Set(self->itemCount.Peek() + 5);
                    co_await kiln::rt::Delay(5000);   // simulate doing real work
                    self->isRefreshing.Set(false);
                }
            }
            void Refresh() { refreshRequests->TrySend(true); }
        };
    }

    void PullToRefreshSample() {
        const Refreshing r = RememberRefreshing();
        BoxSample("Title", r, m3::RememberPullToRefreshState(), false);
    }

    void PullToRefreshWithLoadingIndicatorSample() {
        const Refreshing r = RememberRefreshing();
        BoxSample("Title", r, m3::RememberPullToRefreshState(), false,
                  [](StatePtr state, bool isRefreshing) { PD::LoadingIndicator(state, isRefreshing, AlignInBox({}, Alignment::TopCenter())); });
    }

    void PullToRefreshViewModelSample() {
        const auto viewModel = Here().Remember<std::shared_ptr<ViewModel>>([] { return std::make_shared<ViewModel>(); });
        m3::ScaffoldOptions o;
        o.topBar = [viewModel] { Bar("Title", [viewModel] { viewModel->Refresh(); }, !viewModel->isRefreshing.Get()); };
        m3::Scaffold({}, o, [viewModel](const PaddingValues& padding) {
            m3::PullToRefreshBox(viewModel->isRefreshing.Get(), [viewModel] { viewModel->Refresh(); }, Padding({}, padding), {}, [viewModel] {
                Items([viewModel] { return viewModel->isRefreshing.Get() ? 0 : viewModel->itemCount.Get(); });
            });
        });
    }

    void PullToRefreshScalingSample() {
        const Refreshing r = RememberRefreshing();
        const StatePtr state = m3::RememberPullToRefreshState();
        const auto scaleFraction = [&isRefreshing = r.isRefreshing, state] {
            if (isRefreshing.Get()) return 1.0f;
            return std::clamp(kiln::anim::LinearOutSlowIn().Transform(state->DistanceFraction()), 0.0f, 1.0f);
        };
        m3::ScaffoldOptions o;
        o.topBar = [onRefresh = r.onRefresh] { Bar("TopAppBar", onRefresh); };
        m3::Scaffold(m3::PullToRefresh({}, r.isRefreshing.Get(), state, r.onRefresh), o, [r, state, scaleFraction](const PaddingValues& padding) {
            Box(Padding({}, padding), Alignment::TopStart(), false, [r, state, scaleFraction] {
                Items([r] { return r.isRefreshing.Get() ? 0 : r.itemCount.Get(); });
                Box(GraphicsLayer(AlignInBox({}, Alignment::TopCenter()), [scaleFraction](GraphicsLayerParams& l) { l.scaleX = l.scaleY = scaleFraction(); }),
                    Alignment::TopStart(), false, [&isRefreshing = r.isRefreshing, state] { PD::Indicator(state, isRefreshing.Get()); });
            });
        });
    }

    void PullToRefreshSampleCustomState() {
        const Refreshing r = RememberRefreshing();
        const StatePtr state = Here().Remember<StatePtr>([] { return std::make_shared<BouncyState>(); });
        BoxSample("TopAppBar", r, state, true);
    }

    void PullToRefreshCustomIndicatorWithDefaultTransform() {
        const Refreshing r = RememberRefreshing(1500);
        BoxSample("Title", r, m3::RememberPullToRefreshState(), false, [](StatePtr state, bool isRefreshing) {
            PD::IndicatorBoxOptions o;
            o.elevation = 0_dp;
            PD::IndicatorBox(state, isRefreshing, AlignInBox({}, Alignment::TopCenter()), o, [state, isRefreshing] {
                if (isRefreshing) {
                    m3::CircularProgressIndicator();
                } else {
                    m3::CircularProgressIndicatorOptions c;
                    c.trackColor = m3::ProgressIndicatorDefaults::circularIndeterminateTrackColor();
                    m3::CircularProgressIndicator([state] { return state->DistanceFraction(); }, {}, c);
                }
            });
        });
    }
}
