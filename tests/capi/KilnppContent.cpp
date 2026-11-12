// Content written with kiln++ (kilnpp.hpp, kiln_m3.hpp) over the C ABI only: CApiTests.cpp
// composes and checks it.
#include <kiln/kiln_m3.hpp>

#include <string>

int kpp_effect_runs = 0;
int kpp_disposals = 0;
std::string kpp_field_text;

void kpp_content() {
    auto clicks = kpp::remember_state<int64_t>(0);
    auto field = kpp::remember_text_field_state("hello");
    kpp_field_text = field.text();
    kpp::m3::material_theme(nullptr, [=] {
        kpp::column(kpp::Modifier().padding(8), [=] {
            kpp::m3::button([=] { clicks.set(clicks.get() + 1); }, kpp::Modifier().test_tag("kpp-button"), nullptr,
                            [=] { kpp::m3::text("Taps " + std::to_string(clicks.get())); });
            if (clicks.get() == 1) {
                kpp::disposable_effect({}, [] {
                    ++kpp_effect_runs;
                    return [] { ++kpp_disposals; };
                });
            }
            kpp::lazy_column(kpp::Modifier().height(100), [](kpp::LazyScope s) {
                s.items(50, [](int32_t i) { kpp::basic_text("row " + std::to_string(i), kpp::Modifier().height(20).test_tag(("row " + std::to_string(i)).c_str())); },
                        [](int32_t i) { return i; });
            });
            kpp::m3::outlined_text_field(field, kpp::Modifier().test_tag("kpp-field"));
        }, { kpp::spaced_by(4) });
    });
}
