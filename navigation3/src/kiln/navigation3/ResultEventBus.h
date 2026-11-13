#pragma once

// navigation3-runtime result passing (result/ResultEventBus.kt, ResultEffect.kt,
// ResultEventBusNavEntryDecorator.kt): an entry sends a result under a key and the entry that
// waits for it receives it. Each key has a buffered channel (64 results; more are dropped until
// some are received). A key defaults to the result type's name.

#include "kiln/navigation3/NavEntry.h"
#include <kiln/Function.h>
#include "kiln/runtime/Channel.h"
#include "kiln/runtime/Composition.h"
#include "kiln/runtime/State.h"

#include <any>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <typeinfo>

namespace kiln::navigation3 {

    class ResultEventBus {
    public:
        using Channel = rt::Channel<std::any>;

        template<typename T> static std::string KeyOf() { return typeid(T).name(); }

        // sendResult: queues `result` for whoever waits on `resultKey` (now or later).
        void SendResult(const std::string& resultKey, std::any result) { ChannelFor(resultKey)->TrySend(std::move(result)); }
        template<typename T> void SendResult(T result) { SendResult(KeyOf<T>(), std::any(std::move(result))); }

        // removeResult: drops the key's queued results; a waiting ResultEffect starts over.
        void RemoveResult(const std::string& resultKey) {
            auto it = m_Channels.find(resultKey);
            if (it == m_Channels.end()) return;
            auto channel = std::move(it->second);
            m_Channels.erase(it);
            channel->Close();   // a receiver waiting on it drains what is buffered and stops
            m_Generation.Set(m_Generation.Peek() + 1);
        }
        template<typename T> void RemoveResult() { RemoveResult(KeyOf<T>()); }

        // The key's channel, created on first use (getResultFlow).
        const std::shared_ptr<Channel>& ChannelFor(const std::string& resultKey) {
            auto& ch = m_Channels[resultKey];
            if (!ch) ch = std::make_shared<Channel>(64);
            return ch;
        }
        // Changes when a key's channel is removed (observable: effects restart on it).
        int Generation() const { return m_Generation.Get(); }

    private:
        std::map<std::string, std::shared_ptr<Channel>> m_Channels;
        rt::State<int> m_Generation{ 0 };
    };
    using ResultEventBusPtr = std::shared_ptr<ResultEventBus>;

    // LocalResultEventBus: the bus a ResultEventBusNavEntryDecorator provides. Reading it with
    // none provided is an error.
    inline rt::Local<ResultEventBusPtr> LocalResultEventBus{ nullptr };
    inline const ResultEventBusPtr& CurrentResultEventBus() {
        const auto& bus = rt::Composition::Current()->Read(LocalResultEventBus);
        if (!bus) throw std::logic_error("No ResultEventBus has been provided");
        return bus;
    }

    // rememberResultEventBus
    inline ResultEventBusPtr RememberResultEventBus(rt::Key key = {}) {
        return rt::Composition::Current()->Remember<ResultEventBusPtr>([] { return std::make_shared<ResultEventBus>(); }, {}, key);
    }

    // ResultEffect: calls onResult with each result sent under `resultKey`, in order, while this
    // is composed (the latest onResult). `bus` null: LocalResultEventBus.
    template<typename T>
    void ResultEffect(const std::string& resultKey, kiln::Function<void(const T&)> onResult, ResultEventBusPtr bus = nullptr, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        if (!bus) bus = CurrentResultEventBus();
        auto& current = c.Remember<std::shared_ptr<kiln::Function<void(const T&)>>>([] { return std::make_shared<kiln::Function<void(const T&)>>(); }, {}, key);
        *current = std::move(onResult);   // rememberUpdatedState
        c.LaunchedEffect(rt::Keys(resultKey, bus->Generation(), static_cast<const void*>(bus.get())), [bus, resultKey, current]() -> rt::Task<> {
            const auto channel = bus->ChannelFor(resultKey);
            while (auto result = co_await channel->Next()) (*current)(std::any_cast<const T&>(*result));
        }, key);
    }
    template<typename T>
    void ResultEffect(kiln::Function<void(const T&)> onResult, ResultEventBusPtr bus = nullptr, rt::Key key = {}) {
        ResultEffect<T>(ResultEventBus::KeyOf<T>(), std::move(onResult), std::move(bus), key);
    }

    // conflateAsState: the latest result sent under `resultKey`, `defaultValue` before any.
    template<typename T>
    const T& ConflateAsState(const ResultEventBusPtr& bus, const std::string& resultKey, T defaultValue, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        auto& state = c.Remember<std::shared_ptr<rt::State<T>>>([&] { return std::make_shared<rt::State<T>>(std::move(defaultValue)); }, rt::Keys(resultKey), key);
        c.LaunchedEffect(rt::Keys(resultKey, bus->Generation(), static_cast<const void*>(bus.get())), [bus, resultKey, state]() -> rt::Task<> {
            const auto channel = bus->ChannelFor(resultKey);
            while (auto result = co_await channel->Next()) state->Set(std::any_cast<const T&>(*result));
        }, key);
        return state->Get();
    }
    template<typename T>
    const T& ConflateAsState(const ResultEventBusPtr& bus, T defaultValue, rt::Key key = {}) {
        return ConflateAsState<T>(bus, ResultEventBus::KeyOf<T>(), std::move(defaultValue), key);
    }

    // ResultEventBusNavEntryDecorator: provides `bus` (null: a remembered one) to every entry.
    template<typename T>
    NavEntryDecorator<T> RememberResultEventBusNavEntryDecorator(ResultEventBusPtr bus = nullptr, rt::Key key = {}) {
        auto& c = *rt::Composition::Current();
        if (!bus) bus = RememberResultEventBus(key);
        auto& decorator = c.Remember<std::shared_ptr<NavEntryDecorator<T>>>([&] {
            return std::make_shared<NavEntryDecorator<T>>([bus](const NavEntry<T>& entry) {
                rt::Composition::Current()->Provide(LocalResultEventBus, bus, [&entry] { entry.Content(); });
            });
        }, {}, key);
        return *decorator;
    }

}
