#include "kiln/foundation/Back.h"

#include <cassert>

namespace kiln::foundation {

    namespace {
        struct SimpleHandler final : ui::BackCallback {
            std::function<void()> onBack;
            void OnBack() override { if (onBack) onBack(); }
        };

        // ComposePredictiveBackHandler: a channel and job per gesture.
        struct PredictiveHandler final : ui::BackCallback {
            std::function<rt::Task<>(BackProgress&)> onBack;
            rt::Dispatcher* dispatcher = nullptr;
            std::shared_ptr<BackProgress> channel;
            rt::Job job;
            bool predictive = false;

            static rt::Task<> Run(std::function<rt::Task<>(BackProgress&)> fn, std::shared_ptr<BackProgress> ch) {
                co_await fn(*ch);
                assert(ch->Closed() && "You must collect the progress flow");
            }
            void Launch() {
                channel = std::make_shared<BackProgress>();
                job = rt::Launch(*dispatcher, Run(onBack, channel));
            }
            void OnBackStarted(const BackEvent&) override {
                OnBackCancelled();
                if (!enabled) return;
                predictive = true;
                Launch();
            }
            void OnBackProgressed(const BackEvent& e) override { if (channel) channel->TrySend(e); }
            void OnBack() override {
                if (channel && !predictive) OnBackCancelled();
                if (!channel) { predictive = false; Launch(); }
                // The job may finish (and drop its reference) inside Close.
                auto ch = channel;
                if (ch) ch->Close();
                predictive = false;
            }
            void OnBackCancelled() override {
                job.Cancel();
                channel.reset();
                predictive = false;
            }
            void SetEnabled(bool value) {
                if (!value && enabled && channel && job.Done()) OnBackCancelled();
                enabled = value;
            }
        };

        template<typename H>
        H& Register(bool enabled) {
            auto& c = *rt::Composition::Current();
            ui::BackDispatcher* d = c.Read(LocalBackDispatcher);
            // Disabled until the side effect below applies `enabled`.
            auto& h = c.Remember<std::shared_ptr<H>>([] { auto p = std::make_shared<H>(); p->enabled = false; return p; });
            H* raw = h.get();
            // The dispose owns the handler: remembered values may be freed before effects run.
            std::shared_ptr<H> keep = h;
            c.DisposableEffect(rt::Keys(d), [d, keep] {
                d->Add(keep.get());
                return [d, keep] { d->Remove(keep.get()); };
            });
            c.SideEffect([d, raw, enabled] {
                if constexpr (std::is_same_v<H, PredictiveHandler>) raw->SetEnabled(enabled);
                else raw->enabled = enabled;
                d->EnabledChanged();
            });
            return *raw;
        }
    }

    void BackHandler(bool enabled, std::function<void()> onBack) {
        Register<SimpleHandler>(enabled).onBack = std::move(onBack);
    }

    void PredictiveBackHandler(bool enabled, std::function<rt::Task<>(BackProgress&)> onBack) {
        auto& h = Register<PredictiveHandler>(enabled);
        h.onBack = std::move(onBack);
        h.dispatcher = &rt::Composition::Current()->GetDispatcher();
    }

}
