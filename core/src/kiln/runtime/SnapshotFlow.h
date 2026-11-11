#pragma once

// snapshotFlow: a block of state reads as a stream. Collect runs the block with its reads
// tracked and hands its value to the collector, then again each time state it read changes,
// skipping values equal to the last one (distinctUntilChanged).

#include "kiln/runtime/Channel.h"
#include "kiln/runtime/State.h"
#include "kiln/runtime/Task.h"

#include <functional>
#include <memory>
#include <optional>

namespace kiln::rt {

    template<typename T>
    class SnapshotFlow {
    public:
        explicit SnapshotFlow(std::function<T()> block) : m_Block(std::move(block)) {}

        // Runs until the collecting job is cancelled.
        Task<> Collect(std::function<void(const T&)> collector) const {
            auto watcher = std::make_shared<Watcher>();
            std::optional<T> last;
            for (;;) {
                std::optional<T> value;
                Observation::Track(*watcher, [&] { value.emplace(m_Block()); });
                if (!last || !(*last == *value)) {
                    last = value;
                    Observation::Untracked([&] { collector(*last); });
                }
                if (!co_await watcher->changed.Next()) co_return;
            }
        }

    private:
        struct Watcher final : Subscriber, std::enable_shared_from_this<Watcher> {
            Channel<bool> changed{ 1 };
            // Notified from ApplyChanges: wake the collector from the dispatcher, not from
            // inside the apply.
            void OnStateChanged() override {
                std::weak_ptr<Watcher> weak = this->weak_from_this();
                Dispatcher::Main().Post([weak] { if (auto w = weak.lock()) w->changed.TrySend(true); });
            }
        };
        std::function<T()> m_Block;
    };

}
