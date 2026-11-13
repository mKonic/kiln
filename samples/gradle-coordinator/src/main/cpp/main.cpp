// kiln's part of the CoordinatorLayout sample: a list whose scrolling also scrolls the View app
// bar above it (rememberNestedScrollInteropConnection).
#include <kiln/kiln_m3.hpp>

#include <string>

extern "C" void kiln_main(kiln_app* app) {
    kpp::set_content(app, [] {
        kpp::m3::material_theme(nullptr, [] {
            const kiln_nested_scroll_connection* interop = kpp::remember_nested_scroll_interop_connection();
            kpp::lazy_column(kpp::Modifier().fill_max_size().nested_scroll(interop), [](kpp::LazyScope s) {
                s.items(60, [](int32_t i) {
                    kpp::m3::list_item([i] { kpp::m3::text("Row " + std::to_string(i + 1) + " from kiln"); });
                });
            });
        });
    });
}
