#include "kiln/ui/Location.h"

#include "kiln/runtime/Channel.h"

namespace kiln::ui {

    namespace {
        LocationHost& Host() { static auto* h = new LocationHost; return *h; }
        // Cancels the host request when the task or flow goes.
        struct Request {
            int id = -1;
            ~Request() { if (id >= 0 && Host().cancel) Host().cancel(id); }
        };
    }

    std::optional<Location> LastKnownLocation() { return Host().lastKnown ? Host().lastKnown() : std::nullopt; }
    bool IsLocationEnabled() { return Host().enabled && Host().enabled(); }

    rt::Task<std::optional<Location>> CurrentLocation(std::string provider) {
        if (!Host().current) co_return std::nullopt;
        auto result = std::make_shared<rt::Channel<std::optional<Location>>>(1);
        Request r;
        r.id = Host().current(provider, [result](std::optional<Location> l) { result->TrySend(std::move(l)); });
        if (r.id < 0) co_return std::nullopt;
        auto got = co_await result->Next();
        r.id = -1;   // answered: nothing to cancel
        co_return got ? *got : std::nullopt;
    }

    rt::Flow<Location> LocationUpdates(std::string provider, std::int64_t intervalMs, float minDistance) {
        return rt::Flow<Location>::Create([provider, intervalMs, minDistance](rt::FlowCollector<Location> emit) -> rt::Task<> {
            if (!Host().updates) co_return;
            auto fixes = std::make_shared<rt::Channel<Location>>(1, rt::Channel<Location>::Overflow::DropOldest);
            Request r;
            r.id = Host().updates(provider, intervalMs, minDistance, [fixes](const Location& l) { fixes->TrySend(l); });
            if (r.id < 0) co_return;
            while (auto l = co_await fixes->Next()) co_await emit(*l);
        });
    }

    void SetLocationHost(LocationHost host) { Host() = std::move(host); }

}
