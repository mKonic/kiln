// ScaffoldSamples.kt
#include "ScaffoldSamples.h"
#include "Common.h"

#include <kiln/foundation/Border.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/FloatingActionButton.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Snackbar.h>
#include <kiln/runtime/Channel.h>

namespace catalog::samples {
    namespace {
        // The samples' body: centred text inside the scaffold's padding.
        void Body(const char* text, const PaddingValues& innerPadding) {
            m3::Text(text, WrapContent(FillMax(Padding({}, innerPadding), Axis::Both), Axis::Both));
        }
        void ShowSnackbarFab(std::function<void()> onClick) {
            m3::ExtendedFloatingActionButton(std::move(onClick), {}, {}, [] { m3::Text("Show snackbar"); });
        }
        // var clickCount by remember { mutableStateOf(0) }, in the FAB's own scope.
        kiln::rt::State<int>& ClickCount() { return RememberState(0); }

        kiln::rt::Task<> Show(m3::SnackbarHostState* host, std::string message, std::optional<std::string> actionLabel = std::nullopt,
                              bool withDismissAction = false, std::optional<m3::SnackbarDuration> duration = std::nullopt) {
            co_await host->ShowSnackbar(std::move(message), std::move(actionLabel), withDismissAction, duration);
        }
    }

    void ScaffoldWithSimpleSnackbar() {
        auto& snackbarHostState = m3::RememberSnackbarHostState();
        const LaunchScope scope = RememberCoroutineScope();
        m3::ScaffoldOptions s;
        s.snackbarHost = [&snackbarHostState] { m3::SnackbarHost(snackbarHostState); };
        s.floatingActionButton = [&snackbarHostState, scope] {
            auto& clickCount = ClickCount();
            ShowSnackbarFab([&snackbarHostState, &clickCount, scope] {
                clickCount.Set(clickCount.Peek() + 1);
                scope.Launch(Show(&snackbarHostState, "Snackbar # " + std::to_string(clickCount.Peek())));
            });
        };
        m3::Scaffold({}, s, [](const PaddingValues& p) { Body("Body content", p); });
    }

    void ScaffoldWithIndefiniteSnackbar() {
        auto& snackbarHostState = m3::RememberSnackbarHostState();
        const LaunchScope scope = RememberCoroutineScope();
        m3::ScaffoldOptions s;
        s.snackbarHost = [&snackbarHostState] { m3::SnackbarHost(snackbarHostState); };
        s.floatingActionButton = [&snackbarHostState, scope] {
            auto& clickCount = ClickCount();
            ShowSnackbarFab([&snackbarHostState, &clickCount, scope] {
                clickCount.Set(clickCount.Peek() + 1);
                scope.Launch(Show(&snackbarHostState, "Snackbar # " + std::to_string(clickCount.Peek()), "Action", true,
                                  m3::SnackbarDuration::Indefinite));
            });
        };
        m3::Scaffold({}, s, [](const PaddingValues& p) { Body("Body content", p); });
    }

    void ScaffoldWithCustomSnackbar() {
        struct SnackbarVisualsWithError final : m3::SnackbarVisuals {
            bool isError;
            SnackbarVisualsWithError(std::string message, bool error)
                : m3::SnackbarVisuals(std::move(message), error ? "Error" : "OK", false, m3::SnackbarDuration::Indefinite), isError(error) {}
        };
        auto& snackbarHostState = m3::RememberSnackbarHostState();
        const LaunchScope scope = RememberCoroutineScope();
        m3::ScaffoldOptions s;
        s.snackbarHost = [&snackbarHostState] {
            m3::SnackbarHost(snackbarHostState, {}, [](std::shared_ptr<m3::SnackbarData> data) {
                const auto* withError = dynamic_cast<const SnackbarVisualsWithError*>(&data->Visuals());
                const bool isError = withError && withError->isError;
                const auto& cs = m3::MaterialTheme::colorScheme();
                const m3::ButtonColors buttonColor = isError ? m3::ButtonDefaults::textButtonColors(cs.errorContainer, cs.error)
                                                             : m3::ButtonDefaults::textButtonColors(Color::Unspecified(), cs.inversePrimary);
                m3::SnackbarOptions o;
                o.action = [data, isError, buttonColor] {
                    m3::ButtonOptions b;
                    b.colors = buttonColor;
                    m3::TextButton([data, isError] { if (isError) data->Dismiss(); else data->PerformAction(); }, {}, b,
                                   [data] { m3::Text(data->Visuals().actionLabel.value_or("")); });
                };
                m3::Snackbar(Padding(Border({}, 2_dp, cs.secondary), 12_dp), o, [data] { m3::Text(data->Visuals().message); });
            });
        };
        s.floatingActionButton = [&snackbarHostState, scope] {
            auto& clickCount = ClickCount();
            ShowSnackbarFab([&snackbarHostState, &clickCount, scope] {
                clickCount.Set(clickCount.Peek() + 1);
                const int count = clickCount.Peek();
                auto visuals = std::make_shared<const SnackbarVisualsWithError>("Snackbar # " + std::to_string(count), count % 2 != 0);
                scope.Launch([](m3::SnackbarHostState* host, std::shared_ptr<const SnackbarVisualsWithError> v) -> kiln::rt::Task<> {
                    co_await host->ShowSnackbar(v);
                }(&snackbarHostState, visuals));
            });
        };
        m3::Scaffold({}, s, [](const PaddingValues& p) { Body("Custom Snackbar Demo", p); });
    }

    void ScaffoldWithCoroutinesSnackbar() {
        auto& snackbarHostState = m3::RememberSnackbarHostState();
        // Channel<Int>(Channel.CONFLATED): a new value replaces one not yet received.
        auto& channel = Here().Remember<std::shared_ptr<kiln::rt::Channel<int>>>([] { return std::shared_ptr<kiln::rt::Channel<int>>(kiln::rt::Channel<int>::Conflated()); });
        Here().LaunchedEffect(kiln::rt::Keys(channel.get()), [channel, &snackbarHostState]() -> kiln::rt::Task<> {
            while (auto index = co_await channel->Next()) {
                const m3::SnackbarResult result = co_await snackbarHostState.ShowSnackbar("Snackbar # " + std::to_string(*index), "Action on " + std::to_string(*index));
                switch (result) {
                    case m3::SnackbarResult::ActionPerformed: /* action has been performed */ break;
                    case m3::SnackbarResult::Dismissed: /* dismissed, no action needed */ break;
                }
            }
        });
        m3::ScaffoldOptions s;
        s.snackbarHost = [&snackbarHostState] { m3::SnackbarHost(snackbarHostState); };
        s.floatingActionButton = [channel] {
            auto& clickCount = ClickCount();
            ShowSnackbarFab([channel, &clickCount] {
                clickCount.Set(clickCount.Peek() + 1);
                channel->TrySend(clickCount.Peek());
            });
        };
        m3::Scaffold({}, s, [](const PaddingValues& p) { Body("Snackbar demo", p); });
    }

    void ScaffoldWithMultilineSnackbar() {
        auto& snackbarHostState = m3::RememberSnackbarHostState();
        const LaunchScope scope = RememberCoroutineScope();
        m3::ScaffoldOptions s;
        s.snackbarHost = [&snackbarHostState] {
            m3::SnackbarHost(snackbarHostState, {}, [](std::shared_ptr<m3::SnackbarData> data) {
                m3::Snackbar({}, {}, [data] {
                    m3::TextOptions o;
                    o.maxLines = 2;
                    o.overflow = kiln::text::TextOverflow::Ellipsis;
                    m3::Text(data->Visuals().message, {}, o);
                });
            });
        };
        s.floatingActionButton = [&snackbarHostState, scope] {
            ShowSnackbarFab([&snackbarHostState, scope] {
                const std::string longMessage = "Very very very very very very very very very very very very very "
                                                "very very very very very very very very very very very very "
                                                "very very very very very very very very very very long message";
                scope.Launch(Show(&snackbarHostState, longMessage));
            });
        };
        m3::Scaffold({}, s, [](const PaddingValues& p) { Body("Multiline Snackbar Demo", p); });
    }
}
