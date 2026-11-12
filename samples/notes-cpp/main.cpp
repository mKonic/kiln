// The notes list again, in C++ with kiln++ (kilnpp.hpp, kiln_m3.hpp) over the C ABI.
#include <kiln/kiln_m3.hpp>

#include <memory>
#include <string>
#include <vector>

namespace {
    struct Note { int64_t id; std::string text; };
    struct Notes {
        std::vector<Note> items;
        int64_t nextId = 0;
    };
}

extern "C" void kiln_main(kiln_app* app) {
    kpp::set_content(app, [] {
        kpp::m3::material_theme(nullptr, [] {
            // The notes live in a remembered object; `revision` tells kiln they changed.
            auto& notes = kpp::remember<std::shared_ptr<Notes>>([] { return std::make_shared<Notes>(); });
            auto revision = kpp::remember_state<int64_t>(0);
            auto field = kpp::remember_text_field_state();
            std::shared_ptr<Notes> n = notes;
            auto changed = [revision] { revision.set(revision.get() + 1); };

            kiln_m3_scaffold_options options;
            kiln_m3_scaffold_options_init(&options);
            options.top_bar = kpp::fn<kiln_fn>([] { kpp::m3::top_app_bar([] { kpp::m3::text("Notes (C++)"); }); });
            kpp::m3::scaffold({}, &options, [=](kiln_padding p) {
                kpp::column(kpp::Modifier().fill_max_size().padding(p.start, p.top, p.end, p.bottom), [=] {
                    kpp::row(kpp::Modifier().fill_max_width().padding(16, 0), [=] {
                        kpp::m3::outlined_text_field(field, kpp::Modifier().weight(1));
                        kpp::m3::button([=] {
                            const std::string text = field.text();
                            if (text.empty()) return;
                            n->items.push_back({ n->nextId++, text });
                            field.set_text("");
                            changed();
                        }, {}, nullptr, [] { kpp::m3::text("Add"); });
                    }, { kpp::spaced_by(8), KILN_ALIGN_MIDDLE });
                    revision.get();   // the list recomposes when the notes change
                    kpp::lazy_column(kpp::Modifier().fill_max_width().weight(1), [=](kpp::LazyScope s) {
                        s.items(int32_t(n->items.size()), [=](int32_t i) {
                            const Note note = n->items[size_t(i)];
                            kpp::m3::list_item([=] { kpp::m3::text(note.text); }, kpp::Modifier().clickable([=] {
                                std::erase_if(n->items, [&](const Note& x) { return x.id == note.id; });
                                changed();
                            }));
                        }, [=](int32_t i) { return n->items[size_t(i)].id; });
                    });
                }, { kpp::spaced_by(8) });
            });
        });
    });
}
