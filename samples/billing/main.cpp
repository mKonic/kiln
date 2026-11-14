// kiln billing: Google Play Billing from C++. The Play Billing Library is Kotlin's to call, so
// src/main/kotlin/.../Bridge.kt declares a small Billing interface that PlayBilling.kt implements
// over it, and tools/gen-jvm writes kiln_bridge.h for this file: C++ connects, lists products,
// opens the purchase screen and settles purchases, and hears about new ones through
// PurchaseUpdates.
//
// The product ids are the app's own, set up in the Play Console; a purchase needs the app
// uploaded to a testing track and a Play account on the device. Elsewhere (an emulator without
// Google Play) Connect answers BILLING_UNAVAILABLE, which the screen shows.

#include <kiln/kiln.hpp>
#include <kiln/foundation/Compose.h>
#include <kiln/foundation/Layouts.h>
#include <kiln/foundation/Lazy.h>
#include <kiln/material3/AppBar.h>
#include <kiln/material3/Button.h>
#include <kiln/material3/MaterialTheme.h>
#include <kiln/material3/Scaffold.h>
#include <kiln/material3/Text.h>
#include <kiln/runtime/Composition.h>
#include <kiln/runtime/Flow.h>

#include <algorithm>
#include <string>
#include <vector>

#include "kiln_bridge.h"

using namespace kiln::foundation;
using namespace kiln::material3;
using namespace kiln::ui;
namespace billing = dev::kiln::billingsample;
namespace rt = kiln::rt;

namespace {

    // The upstream sample's rule: products whose id starts with consumable_ are used up when
    // bought (consumed, so they can be bought again); the rest are kept (acknowledged).
    constexpr const char* kConsumablePrefix = "consumable_";
    const std::vector<std::string> kProducts = { "premium_upgrade", "consumable_coins" };

    bool Consumable(const billing::Purchase& p) {
        return std::all_of(p.products.begin(), p.products.end(), [](const std::string& id) { return id.starts_with(kConsumablePrefix); });
    }

    struct Store {
        rt::MutableStateFlow<std::string> status{ std::string("Connecting to Google Play...") };
        rt::MutableStateFlow<std::vector<billing::Product>> products{ {} };
        rt::MutableStateFlow<std::vector<billing::Purchase>> owned{ {} };
        std::vector<rt::Job> settling;
    };
    Store& Shared() {
        static Store s;
        return s;
    }

    std::string Describe(const billing::Outcome& o) {
        switch (o.code) {
            case -1: return "Google Play's billing service is not connected (no Play account on the device?)";
            case 0: return "OK";
            case 1: return "Cancelled";
            case 2: return "Google Play is not reachable";
            case 3: return "Billing is not available on this device (no Google Play or no account)";
            case 4: return "This product is not available";
            case 7: return "You already own this";
            default: return "Error " + std::to_string(o.code) + (o.message.empty() ? "" : ": " + o.message);
        }
    }

    // Verifying a purchase belongs on the app's server (developer.android.com/google/play/billing/security);
    // this sample settles it on the device, as the upstream one does.
    rt::Task<> Settle(billing::Billing play, std::vector<billing::Purchase> purchases) {
        Store& s = Shared();
        for (const auto& p : purchases) {
            if (p.state != billing::PurchaseState::Purchased || p.acknowledged) continue;
            billing::Outcome o;
            if (Consumable(p)) o = co_await play.Consume(p.token);
            else o = co_await play.Acknowledge(p.token);
            s.status.SetValue(o.code == 0 ? "Thank you for your purchase" : Describe(o));
        }
        billing::Purchases now = co_await play.Owned(billing::ProductType::InApp);
        if (now.outcome.code == 0) s.owned.SetValue(std::move(now.purchases));
    }

    void SettleLater(std::vector<billing::Purchase> purchases) {
        auto play = billing::Billing::Get();
        if (!play) return;
        Store& s = Shared();
        std::erase_if(s.settling, [](const rt::Job& j) { return j.Done(); });
        s.settling.push_back(rt::Launch(rt::Dispatcher::Main(), Settle(*play, std::move(purchases))));
    }

    class Updates : public billing::PurchaseUpdates {
    public:
        // From the purchase screen, on the main thread.
        void Updated(billing::Outcome outcome, std::vector<billing::Purchase> purchases) override {
            if (outcome.code != 0) {
                Shared().status.SetValue(Describe(outcome));
                return;
            }
            const bool pending = std::any_of(purchases.begin(), purchases.end(), [](const billing::Purchase& p) { return p.state == billing::PurchaseState::Pending; });
            if (pending) Shared().status.SetValue("Your purchase is pending: it completes when it is paid for");
            SettleLater(std::move(purchases));
        }
    };

    rt::Task<> Open() {
        Store& s = Shared();
        auto play = billing::Billing::Get();
        if (!play) {
            s.status.SetValue("No Billing registered (App.kt)");
            co_return;
        }
        billing::Outcome o = co_await play->Connect();
        if (o.code != 0) {
            s.status.SetValue(Describe(o));
            co_return;
        }
        billing::Products products = co_await play->Products(kProducts, billing::ProductType::InApp);
        if (products.outcome.code != 0) {
            s.status.SetValue(Describe(products.outcome));
            co_return;
        }
        s.status.SetValue(products.products.empty() ? "No products: add them in the Play Console" : "");
        s.products.SetValue(std::move(products.products));
        // Purchases made while the app was away (a pending one completed, another device) settle now.
        billing::Purchases owned = co_await play->Owned(billing::ProductType::InApp);
        if (owned.outcome.code != 0) co_return;
        s.owned.SetValue(owned.purchases);
        co_await Settle(*play, std::move(owned.purchases));
    }

    void Screen() {
        auto& c = *rt::Composition::Current();
        c.LaunchedEffect(rt::Keys{}, [] { return Open(); });
        Store& s = Shared();
        const std::string status = rt::CollectAsState(s.status).Get();
        const std::vector<billing::Product> products = rt::CollectAsState(s.products).Get();
        const std::vector<billing::Purchase> owned = rt::CollectAsState(s.owned).Get();
        ScaffoldOptions options;
        options.topBar = [] { TopAppBar([] { Text("kiln billing"); }, {}); };
        // Scaffold composes its content later (in layout): it holds copies.
        Scaffold({}, options, [status, products, owned](const PaddingValues& padding) {
            Column(Padding(Padding(FillMax(Modifier{}, Axis::Both), padding), 16_dp), Arrangement::SpacedBy(12_dp), AlignmentH::Start(),
                   [status, products, owned] {
                if (!status.empty()) Text(status);
                for (const auto& p : products) {
                    const bool has = std::any_of(owned.begin(), owned.end(), [&](const billing::Purchase& o) {
                        return std::find(o.products.begin(), o.products.end(), p.id) != o.products.end();
                    });
                    Text(p.title, {}, { .style = MaterialTheme::typography().titleMedium });
                    if (!p.description.empty()) Text(p.description);
                    Button([id = p.id] {
                        if (auto play = billing::Billing::Get()) {
                            billing::Outcome o = play->Buy(id);
                            if (o.code != 0) Shared().status.SetValue(Describe(o));
                        }
                    }, {}, { .enabled = !has }, [has, price = p.price] { Text(has ? std::string("Owned") : "Buy for " + price); });
                }
            });
        });
    }

}

std::unique_ptr<billing::PurchaseUpdates> billing::MakePurchaseUpdates() { return std::make_unique<Updates>(); }

extern "C" void kiln_main(kiln_app* app) {
    kiln::SetContent(app, [] { MaterialTheme([] { Screen(); }); });
}
