#include "kiln/ui/Sensors.h"

#include "kiln/runtime/Channel.h"

namespace kiln::ui {

    namespace {
        SensorHost& Host() { static auto* h = new SensorHost; return *h; }
    }

    bool HasSensor(SensorType type) { return Host().has && Host().has(type); }

    void SensorSubscription::Reset() {
        if (m_Id >= 0 && Host().stop) Host().stop(m_Id);
        m_Id = -1;
    }

    SensorSubscription ListenToSensor(SensorType type, int periodUs, kiln::Function<void(const SensorEvent&)> listener) {
        if (!Host().start) return {};
        return SensorSubscription(Host().start(type, periodUs, std::move(listener)));
    }

    rt::Flow<SensorEvent> SensorEvents(SensorType type, int periodUs) {
        return rt::Flow<SensorEvent>::Create([type, periodUs](rt::FlowCollector<SensorEvent> emit) -> rt::Task<> {
            auto events = std::make_shared<rt::Channel<SensorEvent>>(1, rt::Channel<SensorEvent>::Overflow::DropOldest);
            SensorSubscription sub = ListenToSensor(type, periodUs, [events](const SensorEvent& e) { events->TrySend(e); });
            if (!sub) co_return;
            while (auto e = co_await events->Next()) co_await emit(*e);
        });
    }

    void SetSensorHost(SensorHost host) { Host() = std::move(host); }

}
