//! The notes list in Rust: a text field and an add button, notes in a lazy list, tap one to
//! remove it. Host: `cargo run --example notes` (the kiln dev window).
#![no_main]

use kiln::*;
use std::cell::RefCell;
use std::rc::Rc;

#[derive(Default)]
struct Notes {
    items: Vec<(i64, String)>,
    next_id: i64,
}

#[no_mangle]
pub extern "C" fn kiln_main(app: *mut sys::kiln_app) {
    set_content(app, || {
        m3::material_theme(false, || {
            let notes: Rc<RefCell<Notes>> = remember(|| Rc::new(RefCell::new(Notes::default()))).clone();
            let revision = remember_state(0i64);
            let field = remember_text_field_state("");
            let bump = move || revision.set(revision.get() + 1);
            m3::scaffold(Some(Box::new(|| m3::top_app_bar(|| m3::text("Notes (Rust)")))), None, move |p| {
                let notes = notes.clone();
                column_with(Modifier::new().fill_max_size(1.0).padding_each(p.start, p.top, p.end, p.bottom), Some(spaced_by(8.0)), -1.0, move || {
                    let add = notes.clone();
                    row(Modifier::new().fill_max_width(1.0).padding_hv(16.0, 0.0), Some(spaced_by(8.0)), 0.0, move || {
                        m3::outlined_text_field(field, Modifier::new().weight(1.0, true));
                        let add = add.clone();
                        m3::button(move || {
                            let text = field.text();
                            if text.is_empty() { return; }
                            let mut n = add.borrow_mut();
                            let id = n.next_id;
                            n.next_id += 1;
                            n.items.push((id, text));
                            field.set_text("");
                            bump();
                        }, Modifier::new(), || m3::text("Add"));
                    });
                    revision.get();   // the list recomposes when the notes change
                    let list = notes.clone();
                    lazy_column(Modifier::new().fill_max_width(1.0).weight(1.0, true), move |s| {
                        let count = list.borrow().items.len();
                        let (items, keys) = (list.clone(), list.clone());
                        s.items(count, move |i| {
                            let (id, text) = items.borrow().items[i as usize].clone();
                            let remove = items.clone();
                            m3::list_item(move || m3::text(&text), Modifier::new().clickable(move || {
                                remove.borrow_mut().items.retain(|(x, _)| *x != id);
                                bump();
                            }));
                        }, move |i| keys.borrow().items[i as usize].0);
                    });
                });
            });
        });
    });
}
