//! Material 3: the common components with closures. Everything else in `kiln_m3.h` is in
//! [`crate::sys`] (the `kiln_m3_*` functions), callable with [`crate::here`] for the key and
//! `IntoCallback` for closures.
use crate::{here, sys, CStrBuf, IntoCallback, Modifier, TextFieldState};
use std::ptr::null;


/// MaterialTheme with the default (light) scheme, or `scheme` when given.
#[track_caller]
pub fn material_theme(dark: bool, content: impl FnMut() + 'static) {
    let key = here();
    let mut scheme: sys::kiln_m3_color_scheme = Default::default();
    unsafe {
        sys::kiln_m3_color_scheme_init(&mut scheme);
        if dark { sys::kiln_m3_dark_color_scheme(key, crate::none(), &mut scheme) } else { sys::kiln_m3_light_color_scheme(key, crate::none(), &mut scheme) }
        let mut args: sys::kiln_m3_material_theme_args = Default::default();
        sys::kiln_m3_material_theme_args_init(&mut args);
        args.color_scheme = &scheme;
        sys::kiln_m3_material_theme(key, &args, content.into_callback());
    }
}

/// The current theme's colour scheme.
#[track_caller]
pub fn color_scheme() -> sys::kiln_m3_color_scheme {
    let mut s: sys::kiln_m3_color_scheme = Default::default();
    unsafe {
        sys::kiln_m3_color_scheme_init(&mut s);
        sys::kiln_m3_material_theme_color_scheme(here(), &mut s);
    }
    s
}

#[track_caller]
pub fn text(text: &str) { text_with(text, Modifier::new()) }
#[track_caller]
pub fn text_with(text: &str, modifier: Modifier) {
    let t = CStrBuf::new(text);
    unsafe { sys::kiln_m3_text(here(), t.ptr(), modifier.raw(), null()) }
}

#[track_caller]
pub fn button(on_click: impl FnMut() + 'static, modifier: Modifier, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_m3_button(here(), on_click.into_callback(), modifier.raw(), null(), content.into_callback()) }
}
#[track_caller]
pub fn text_button(on_click: impl FnMut() + 'static, modifier: Modifier, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_m3_text_button(here(), on_click.into_callback(), modifier.raw(), null(), content.into_callback()) }
}
#[track_caller]
pub fn checkbox(checked: bool, on_change: impl FnMut(bool) + 'static, modifier: Modifier) {
    unsafe { sys::kiln_m3_checkbox(here(), checked, on_change.into_callback(), modifier.raw(), null()) }
}
#[track_caller]
pub fn switch(checked: bool, on_change: impl FnMut(bool) + 'static, modifier: Modifier) {
    unsafe { sys::kiln_m3_switch(here(), checked, on_change.into_callback(), modifier.raw(), null()) }
}
#[track_caller]
pub fn outlined_text_field(state: TextFieldState, modifier: Modifier) {
    unsafe { sys::kiln_m3_outlined_text_field(here(), state.raw(), modifier.raw(), null()) }
}
#[track_caller]
pub fn list_item(headline: impl FnMut() + 'static, modifier: Modifier) {
    unsafe { sys::kiln_m3_list_item(here(), headline.into_callback(), modifier.raw(), null()) }
}
#[track_caller]
pub fn top_app_bar(title: impl FnMut() + 'static) {
    unsafe { sys::kiln_m3_top_app_bar(here(), title.into_callback(), null(), null()) }
}
#[track_caller]
pub fn floating_action_button(on_click: impl FnMut() + 'static, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_m3_floating_action_button(here(), on_click.into_callback(), null(), null(), content.into_callback()) }
}

/// Scaffold: an optional top bar and floating action button around content, which gets the
/// padding the bars take.
#[track_caller]
pub fn scaffold(
    top_bar: Option<Box<dyn FnMut()>>,
    fab: Option<Box<dyn FnMut()>>,
    content: impl FnMut(sys::kiln_padding) + 'static,
) {
    let mut o: sys::kiln_m3_scaffold_options = Default::default();
    unsafe { sys::kiln_m3_scaffold_options_init(&mut o) };
    if let Some(mut t) = top_bar { o.top_bar = (move || t()).into_callback(); }
    if let Some(mut f) = fab { o.floating_action_button = (move || f()).into_callback(); }
    unsafe { sys::kiln_m3_scaffold(here(), null(), &o, content.into_callback()) }
}
