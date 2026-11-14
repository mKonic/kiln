//! kiln for Rust: composable functions over the kiln C ABI.
//!
//! Composable calls take their identity from the call site (`#[track_caller]`), like Compose's
//! positional memoisation; in loops, wrap each item in [`key`]. Content closures run again on
//! their own when a state they read changes, so they are `'static` and move what they use
//! (states and handles are `Copy`).
//!
//! ```ignore
//! # use kiln::*;
//! #[no_mangle]
//! pub extern "C" fn kiln_main(app: *mut kiln::sys::kiln_app) {
//!     kiln::set_content(app, || {
//!         let count = remember_state(0i64);
//!         column(Modifier::new().padding(16.0), move || {
//!             basic_text(&format!("Count {}", count.get()), Modifier::new());
//!             box_(Modifier::new().size(48.0, 48.0).background(argb(0xFF6750A4), None)
//!                     .clickable(move || count.set(count.get() + 1)), || {});
//!         });
//!     });
//! }
//! ```

pub use kiln_sys as sys;

pub mod m3;
pub mod material3;

use std::ffi::{c_void, CStr, CString};
use std::panic::Location;

// --- Callbacks ------------------------------------------------------------------------------

/// A closure as one of the C callback structs (`kiln_fn`, `kiln_fn_bool`, ...). kiln owns it
/// from the call that takes it and drops it when done.
pub trait IntoCallback<C> {
    fn into_callback(self) -> C;
}

fn guard<R>(f: impl FnOnce() -> R) -> R {
    match std::panic::catch_unwind(std::panic::AssertUnwindSafe(f)) {
        Ok(r) => r,
        Err(_) => {
            eprintln!("kiln: a callback panicked; aborting (panics cannot cross into C)");
            std::process::abort()
        }
    }
}

#[macro_export]
#[doc(hidden)]
macro_rules! __kiln_callback {
    ($c:ty, ($($a:ident: $t:ty),*) -> $r:ty) => {
        impl<F: FnMut($($t),*) -> $r + 'static> $crate::IntoCallback<$c> for F {
            fn into_callback(self) -> $c {
                unsafe extern "C" fn call<F: FnMut($($t),*) -> $r>(u: *mut ::std::ffi::c_void, $($a: $t),*) -> $r {
                    $crate::__guard(|| unsafe { (*(u as *mut F))($($a),*) })
                }
                unsafe extern "C" fn drop_it<F>(u: *mut ::std::ffi::c_void) {
                    drop(unsafe { Box::from_raw(u as *mut F) })
                }
                let mut c: $c = Default::default();
                c.call = Some(call::<F>);
                c.user = Box::into_raw(Box::new(self)) as *mut ::std::ffi::c_void;
                c.drop = Some(drop_it::<F>);
                c
            }
        }
    };
}
#[doc(hidden)]
pub fn __guard<R>(f: impl FnOnce() -> R) -> R { guard(f) }

__kiln_callback!(sys::kiln_fn, () -> ());
__kiln_callback!(sys::kiln_fn_bool, (v: bool) -> ());
__kiln_callback!(sys::kiln_fn_int, (v: i32) -> ());
__kiln_callback!(sys::kiln_fn_float, (v: f32) -> ());
__kiln_callback!(sys::kiln_fn_lazy, (s: *mut sys::kiln_lazy_scope) -> ());
__kiln_callback!(sys::kiln_fn_item_key, (i: i32) -> sys::kiln_key);
__kiln_callback!(sys::kiln_fn_lazy_grid, (s: *mut sys::kiln_lazy_grid_scope) -> ());
__kiln_callback!(sys::kiln_fn_grid_span, (i: i32, max_current_line_span: i32, max_line_span: i32) -> i32);
__kiln_callback!(sys::kiln_fn_count, () -> i32);
__kiln_callback!(sys::kiln_fn_effect, () -> sys::kiln_fn);
__kiln_callback!(sys::kiln_fn_volition_state, (f: *mut sys::kiln_volition_fields) -> ());
__kiln_callback!(sys::kiln_fn_volition_lines, (l: *mut sys::kiln_volition_lines) -> ());
__kiln_callback!(sys::kiln_fn_volition_go, (n: *const ::std::ffi::c_char, a: *const ::std::ffi::c_char) -> *const ::std::ffi::c_char);
__kiln_callback!(sys::kiln_fn_task, (t: *mut sys::kiln_task) -> ());
__kiln_callback!(sys::kiln_fn_task_frame, (t: *mut sys::kiln_task, time: i64) -> ());
__kiln_callback!(sys::kiln_fn_constraints, (c: *const sys::kiln_constraints) -> ());
__kiln_callback!(sys::kiln_fn_view_factory, (env: *mut ::std::ffi::c_void, context: *mut ::std::ffi::c_void) -> *mut ::std::ffi::c_void);
__kiln_callback!(sys::kiln_fn_view, (env: *mut ::std::ffi::c_void, view: *mut ::std::ffi::c_void) -> ());

/// No callback.
pub fn none<C: Default>() -> C { C::default() }

// --- Values ---------------------------------------------------------------------------------

/// A colour from 0xAARRGGBB.
pub const fn argb(v: u32) -> sys::kiln_color { 0x1_0000_0000 | v as u64 }
pub const UNSPECIFIED: sys::kiln_color = 0;

pub fn alignment(horizontal: f32, vertical: f32) -> sys::kiln_alignment { sys::kiln_alignment { horizontal, vertical } }
pub fn spaced_by(dp: f32) -> sys::kiln_arrangement {
    sys::kiln_arrangement { kind: sys::kiln_arrangement_kind::KILN_ARRANGE_SPACED_BY, spacing: dp, align_bias: f32::NAN }
}

pub(crate) struct CStrBuf(CString);
impl CStrBuf {
    pub(crate) fn new(s: &str) -> Self { CStrBuf(CString::new(s.replace('\0', "")).unwrap()) }
    pub(crate) fn ptr(&self) -> *const std::ffi::c_char { self.0.as_ptr() }
}
pub(crate) fn from_c(s: *const std::ffi::c_char) -> String {
    if s.is_null() { String::new() } else { unsafe { CStr::from_ptr(s) }.to_string_lossy().into_owned() }
}

// --- Keys -----------------------------------------------------------------------------------

fn fnv(s: &str) -> u64 {
    let mut h: u64 = 0xcbf29ce484222325;
    for b in s.bytes() {
        h ^= b as u64;
        h = h.wrapping_mul(0x100000001b3);
    }
    h
}

/// The key of the calling code's location (what KILN_HERE gives in C).
#[track_caller]
pub fn here() -> sys::kiln_key {
    let l = Location::caller();
    sys::kiln_key { value: (fnv(l.file()) ^ ((l.line() as u64) << 20) ^ l.column() as u64) | 1, explicit_key: false }
}
pub fn key_int(v: i64) -> sys::kiln_key { sys::kiln_key { value: (v as u64).wrapping_mul(0x9E3779B97F4A7C15).wrapping_add(1), explicit_key: true } }
pub fn key_str(s: &str) -> sys::kiln_key { sys::kiln_key { value: fnv(s) | 1, explicit_key: true } }

/// `key(k) { content }`: content with its own identity (items in a loop).
pub fn key(k: sys::kiln_key, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_group(k, content.into_callback()) }
}

// --- App ------------------------------------------------------------------------------------

/// The app's content (call from `kiln_main`).
pub fn set_content(app: *mut sys::kiln_app, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_app_set_content(app, content.into_callback()) }
}

// --- Modifiers ------------------------------------------------------------------------------

/// A modifier chain, valid inside the content call that built it.
#[derive(Clone, Copy)]
pub struct Modifier(*mut sys::kiln_modifier);

impl Default for Modifier { fn default() -> Self { Modifier::new() } }

macro_rules! modifier_fns {
    ($($name:ident($($a:ident: $t:ty),*) => $f:ident;)*) => {
        $(pub fn $name(self, $($a: $t),*) -> Self { Modifier(unsafe { sys::$f(self.0, $($a),*) }) })*
    };
}

impl Modifier {
    pub fn new() -> Self { Modifier(std::ptr::null_mut()) }
    pub fn raw(self) -> *const sys::kiln_modifier { self.0 }
    modifier_fns! {
        padding(all: f32) => kiln_mod_padding_all;
        padding_hv(horizontal: f32, vertical: f32) => kiln_mod_padding_hv;
        padding_each(start: f32, top: f32, end: f32, bottom: f32) => kiln_mod_padding;
        size(width: f32, height: f32) => kiln_mod_size;
        width(width: f32) => kiln_mod_width;
        height(height: f32) => kiln_mod_height;
        size_in(min_w: f32, min_h: f32, max_w: f32, max_h: f32) => kiln_mod_size_in;
        fill_max_width(fraction: f32) => kiln_mod_fill_max_width;
        fill_max_height(fraction: f32) => kiln_mod_fill_max_height;
        fill_max_size(fraction: f32) => kiln_mod_fill_max_size;
        aspect_ratio(ratio: f32) => kiln_mod_aspect_ratio;
        offset(x: f32, y: f32) => kiln_mod_offset;
        weight(weight: f32, fill: bool) => kiln_mod_weight;
        align(a: sys::kiln_alignment) => kiln_mod_align_in_box;
        alpha(a: f32) => kiln_mod_alpha;
        rotate(degrees: f32) => kiln_mod_rotate;
        scale(sx: f32, sy: f32) => kiln_mod_scale;
        z_index(z: f32) => kiln_mod_z_index;
    }
    /// alignBy: aligns a Row / Column child on the line with its siblings that use it.
    pub fn align_by(self, line: AlignmentLine) -> Self { Modifier(unsafe { sys::kiln_mod_align_by(self.0, line.0) }) }
    /// paddingFrom: `before` / `after` are dp, `f32::NAN` to leave a side alone.
    pub fn padding_from(self, line: AlignmentLine, before: f32, after: f32) -> Self {
        Modifier(unsafe { sys::kiln_mod_padding_from(self.0, line.0, before, after) })
    }
    pub fn focus_requester(self, r: FocusRequester) -> Self { Modifier(unsafe { sys::kiln_mod_focus_requester(self.0, r.0) }) }
    pub fn focusable(self, enabled: bool) -> Self { Modifier(unsafe { sys::kiln_mod_focusable(self.0, enabled) }) }
    /// `on_change(focused)` for the first focus target after it.
    pub fn on_focus_changed(self, on_change: impl FnMut(bool) + 'static) -> Self {
        Modifier(unsafe { sys::kiln_mod_on_focus_changed(self.0, on_change.into_callback()) })
    }
    pub fn safe_drawing_padding(self) -> Self { Modifier(unsafe { sys::kiln_mod_safe_drawing_padding(self.0) }) }
    pub fn system_bars_padding(self) -> Self { Modifier(unsafe { sys::kiln_mod_system_bars_padding(self.0) }) }
    pub fn ime_padding(self) -> Self { Modifier(unsafe { sys::kiln_mod_ime_padding(self.0) }) }
    pub fn background(self, color: sys::kiln_color, shape: Option<&Shape>) -> Self {
        Modifier(unsafe { sys::kiln_mod_background(self.0, color, shape.map_or(std::ptr::null(), |s| s.0)) })
    }
    pub fn border(self, width: f32, color: sys::kiln_color, shape: Option<&Shape>) -> Self {
        Modifier(unsafe { sys::kiln_mod_border(self.0, width, color, shape.map_or(std::ptr::null(), |s| s.0)) })
    }
    pub fn clip(self, shape: Option<&Shape>) -> Self { Modifier(unsafe { sys::kiln_mod_clip(self.0, shape.map_or(std::ptr::null(), |s| s.0)) }) }
    pub fn clickable(self, on_click: impl FnMut() + 'static) -> Self {
        Modifier(unsafe { sys::kiln_mod_clickable(self.0, true, on_click.into_callback()) })
    }
    pub fn toggleable(self, value: bool, on_change: impl FnMut(bool) + 'static) -> Self {
        Modifier(unsafe { sys::kiln_mod_toggleable(self.0, value, true, on_change.into_callback()) })
    }
    pub fn vertical_scroll(self, state: ScrollState) -> Self { Modifier(unsafe { sys::kiln_mod_vertical_scroll(self.0, state.0, true) }) }
    pub fn horizontal_scroll(self, state: ScrollState) -> Self { Modifier(unsafe { sys::kiln_mod_horizontal_scroll(self.0, state.0, true) }) }
    pub fn test_tag(self, tag: &str) -> Self {
        let t = CStrBuf::new(tag);
        Modifier(unsafe { sys::kiln_mod_test_tag(self.0, t.ptr()) })
    }
    pub fn content_description(self, d: &str) -> Self {
        let t = CStrBuf::new(d);
        Modifier(unsafe { sys::kiln_mod_content_description(self.0, t.ptr()) })
    }
}

/// An alignment line (a baseline, a component's line). Static, compared by identity.
#[derive(Clone, Copy)]
pub struct AlignmentLine(pub *const sys::kiln_alignment_line);
impl AlignmentLine {
    pub fn first_baseline() -> Self { AlignmentLine(unsafe { sys::kiln_first_baseline() }) }
    pub fn last_baseline() -> Self { AlignmentLine(unsafe { sys::kiln_last_baseline() }) }
}

// --- Refcounted handles ---------------------------------------------------------------------

/// A shape (refcounted).
pub struct Shape(*mut sys::kiln_shape);
impl Shape {
    pub fn rectangle() -> Self { Shape(unsafe { sys::kiln_shape_rectangle() }) }
    pub fn circle() -> Self { Shape(unsafe { sys::kiln_shape_circle() }) }
    pub fn rounded(dp: f32) -> Self { Shape(unsafe { sys::kiln_shape_rounded(dp) }) }
    pub fn rounded_percent(p: f32) -> Self { Shape(unsafe { sys::kiln_shape_rounded_percent(p) }) }
    pub fn cut(dp: f32) -> Self { Shape(unsafe { sys::kiln_shape_cut(dp) }) }
    pub fn raw(&self) -> *const sys::kiln_shape { self.0 }
}
impl Clone for Shape { fn clone(&self) -> Self { Shape(unsafe { sys::kiln_shape_retain(self.0) }) } }
impl Drop for Shape { fn drop(&mut self) { unsafe { sys::kiln_shape_release(self.0) } } }

/// A text style (refcounted).
pub struct TextStyle(*mut sys::kiln_text_style);
impl TextStyle {
    pub fn new(size_sp: f32, color: sys::kiln_color, weight: i32) -> Self {
        let mut o: sys::kiln_text_style_options = Default::default();
        unsafe { sys::kiln_text_style_options_init(&mut o) };
        o.font_size_sp = size_sp;
        o.color = color;
        o.font_weight = weight;
        TextStyle(unsafe { sys::kiln_text_style_new(&o) })
    }
    pub fn raw(&self) -> *const sys::kiln_text_style { self.0 }
}
impl Clone for TextStyle { fn clone(&self) -> Self { TextStyle(unsafe { sys::kiln_text_style_retain(self.0) }) } }
impl Drop for TextStyle { fn drop(&mut self) { unsafe { sys::kiln_text_style_release(self.0) } } }

// --- State ----------------------------------------------------------------------------------

/// Values a [`State`] can hold.
pub trait StateValue: Sized {
    type Handle;
    fn remember(key: sys::kiln_key, initial: Self, saveable: bool) -> *mut Self::Handle;
    fn get(h: *mut Self::Handle) -> Self;
    fn set(h: *mut Self::Handle, v: Self);
}
impl StateValue for i64 {
    type Handle = sys::kiln_state_i64;
    fn remember(k: sys::kiln_key, v: i64, s: bool) -> *mut Self::Handle {
        unsafe { if s { sys::kiln_remember_saveable_i64(k, v) } else { sys::kiln_remember_state_i64(k, v) } }
    }
    fn get(h: *mut Self::Handle) -> i64 { unsafe { sys::kiln_state_i64_get(h) } }
    fn set(h: *mut Self::Handle, v: i64) { unsafe { sys::kiln_state_i64_set(h, v) } }
}
impl StateValue for bool {
    type Handle = sys::kiln_state_i64;
    fn remember(k: sys::kiln_key, v: bool, s: bool) -> *mut Self::Handle { i64::remember(k, v as i64, s) }
    fn get(h: *mut Self::Handle) -> bool { i64::get(h) != 0 }
    fn set(h: *mut Self::Handle, v: bool) { i64::set(h, v as i64) }
}
impl StateValue for f64 {
    type Handle = sys::kiln_state_f64;
    fn remember(k: sys::kiln_key, v: f64, s: bool) -> *mut Self::Handle {
        unsafe { if s { sys::kiln_remember_saveable_f64(k, v) } else { sys::kiln_remember_state_f64(k, v) } }
    }
    fn get(h: *mut Self::Handle) -> f64 { unsafe { sys::kiln_state_f64_get(h) } }
    fn set(h: *mut Self::Handle, v: f64) { unsafe { sys::kiln_state_f64_set(h, v) } }
}
impl StateValue for String {
    type Handle = sys::kiln_state_str;
    fn remember(k: sys::kiln_key, v: String, s: bool) -> *mut Self::Handle {
        let c = CStrBuf::new(&v);
        unsafe { if s { sys::kiln_remember_saveable_str(k, c.ptr()) } else { sys::kiln_remember_state_str(k, c.ptr()) } }
    }
    fn get(h: *mut Self::Handle) -> String { from_c(unsafe { sys::kiln_state_str_get(h) }) }
    fn set(h: *mut Self::Handle, v: String) {
        let c = CStrBuf::new(&v);
        unsafe { sys::kiln_state_str_set(h, c.ptr()) }
    }
}

/// An observable value remembered at its call. Reading it in content subscribes that content.
pub struct State<T: StateValue>(*mut T::Handle);
impl<T: StateValue> Clone for State<T> { fn clone(&self) -> Self { *self } }
impl<T: StateValue> Copy for State<T> {}
impl<T: StateValue> State<T> {
    pub fn get(&self) -> T { T::get(self.0) }
    pub fn set(&self, v: T) { T::set(self.0, v) }
}

/// `remember { mutableStateOf(initial) }`
#[track_caller]
pub fn remember_state<T: StateValue>(initial: T) -> State<T> { State(T::remember(here(), initial, false)) }
/// `rememberSaveable { mutableStateOf(initial) }`
#[track_caller]
pub fn remember_saveable<T: StateValue>(initial: T) -> State<T> { State(T::remember(here(), initial, true)) }

/// `remember { init() }`: a value of any type kept while the call stays. The reference is valid
/// for the current composition of this call.
#[track_caller]
pub fn remember<T: 'static, I: FnOnce() -> T>(init: I) -> &'static mut T {
    struct Ctx<I>(Option<I>);
    unsafe extern "C" fn make<T, I: FnOnce() -> T>(u: *mut c_void) -> *mut c_void {
        let ctx = unsafe { &mut *(u as *mut Ctx<I>) };
        Box::into_raw(Box::new((ctx.0.take().unwrap())())) as *mut c_void
    }
    unsafe extern "C" fn drop_value<T>(v: *mut c_void) { drop(unsafe { Box::from_raw(v as *mut T) }) }
    let mut ctx = Ctx(Some(init));
    let ri = sys::kiln_remember_init {
        call: Some(make::<T, I>),
        user: &mut ctx as *mut _ as *mut c_void,
        drop_value: Some(drop_value::<T>),
    };
    unsafe { &mut *(sys::kiln_remember(here(), std::ptr::null(), 0, ri) as *mut T) }
}

// --- Effects --------------------------------------------------------------------------------

#[track_caller]
pub fn side_effect(effect: impl FnMut() + 'static) { unsafe { sys::kiln_side_effect(here(), effect.into_callback()) } }

/// `DisposableEffect(keys)`: `effect` returns the closure run when the call leaves.
#[track_caller]
pub fn disposable_effect<D: FnMut() + 'static>(keys: &[u64], mut effect: impl FnMut() -> D + 'static) {
    let wrapped = move || -> sys::kiln_fn { effect().into_callback() };
    unsafe { sys::kiln_disposable_effect(here(), keys.as_ptr(), keys.len(), wrapped.into_callback()) }
}

// --- Layouts --------------------------------------------------------------------------------

#[track_caller]
pub fn box_(modifier: Modifier, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_box(here(), modifier.0, std::ptr::null(), content.into_callback()) }
}
#[track_caller]
pub fn box_aligned(modifier: Modifier, content_alignment: sys::kiln_alignment, content: impl FnMut() + 'static) {
    let mut o: sys::kiln_box_options = Default::default();
    unsafe { sys::kiln_box_options_init(&mut o) };
    o.content_alignment = content_alignment;
    unsafe { sys::kiln_box(here(), modifier.0, &o, content.into_callback()) }
}
#[track_caller]
pub fn row(modifier: Modifier, arrangement: Option<sys::kiln_arrangement>, vertical_alignment: f32, content: impl FnMut() + 'static) {
    let mut o: sys::kiln_row_options = Default::default();
    unsafe { sys::kiln_row_options_init(&mut o) };
    if let Some(a) = arrangement { o.horizontal_arrangement = a; }
    o.vertical_alignment = vertical_alignment;
    unsafe { sys::kiln_row(here(), modifier.0, &o, content.into_callback()) }
}
#[track_caller]
pub fn column(modifier: Modifier, content: impl FnMut() + 'static) { column_with(modifier, None, -1.0, content) }
#[track_caller]
pub fn column_with(modifier: Modifier, arrangement: Option<sys::kiln_arrangement>, horizontal_alignment: f32, content: impl FnMut() + 'static) {
    let mut o: sys::kiln_column_options = Default::default();
    unsafe { sys::kiln_column_options_init(&mut o) };
    if let Some(a) = arrangement { o.vertical_arrangement = a; }
    o.horizontal_alignment = horizontal_alignment;
    unsafe { sys::kiln_column(here(), modifier.0, &o, content.into_callback()) }
}
#[track_caller]
pub fn spacer(modifier: Modifier) { unsafe { sys::kiln_spacer(here(), modifier.0) } }

/// AndroidView: an Android View inside kiln content, made by `factory(env, context)` (a JNIEnv*
/// and the window's Context; it returns a local reference). `update(env, view)` runs after the
/// View is made and after each recomposition.
#[track_caller]
pub fn android_view(modifier: Modifier, factory: impl IntoCallback<sys::kiln_fn_view_factory>, update: impl IntoCallback<sys::kiln_fn_view>) {
    let o = sys::kiln_android_view_options { update: update.into_callback(), ..Default::default() };
    unsafe { sys::kiln_android_view(here(), modifier.0, factory.into_callback(), &o) }
}
/// The same for a View class with a (Context) constructor, such as "android.webkit.WebView".
#[track_caller]
pub fn android_view_class(class_name: &str, modifier: Modifier, update: impl IntoCallback<sys::kiln_fn_view>) {
    let name = CStrBuf::new(class_name);
    let o = sys::kiln_android_view_options { update: update.into_callback(), ..Default::default() };
    unsafe { sys::kiln_android_view_class(here(), modifier.0, name.ptr(), &o) }
}

#[track_caller]
pub fn basic_text(text: &str, modifier: Modifier) { basic_text_styled(text, modifier, None) }
#[track_caller]
pub fn basic_text_styled(text: &str, modifier: Modifier, style: Option<&TextStyle>) {
    let t = CStrBuf::new(text);
    unsafe { sys::kiln_basic_text(here(), t.ptr(), modifier.0, style.map_or(std::ptr::null(), |s| s.0), std::ptr::null()) }
}

/// A remembered text field state.
#[derive(Clone, Copy)]
pub struct TextFieldState(pub(crate) *mut sys::kiln_text_field_state);
impl TextFieldState {
    pub fn text(&self) -> String { from_c(unsafe { sys::kiln_text_field_state_text(self.0) }) }
    pub fn set_text(&self, t: &str) {
        let c = CStrBuf::new(t);
        unsafe { sys::kiln_text_field_state_set_text(self.0, c.ptr()) }
    }
    pub fn raw(&self) -> *mut sys::kiln_text_field_state { self.0 }
}
#[track_caller]
pub fn remember_text_field_state(initial: &str) -> TextFieldState {
    let c = CStrBuf::new(initial);
    TextFieldState(unsafe { sys::kiln_remember_text_field_state(here(), c.ptr()) })
}

// --- Scrolling and lazy lists ---------------------------------------------------------------

#[derive(Clone, Copy)]
pub struct ScrollState(*mut sys::kiln_scroll_state);
impl ScrollState {
    pub fn value(&self) -> i32 { unsafe { sys::kiln_scroll_state_value(self.0) } }
    pub fn max_value(&self) -> i32 { unsafe { sys::kiln_scroll_state_max_value(self.0) } }
    pub fn animate_to(&self, v: i32) { unsafe { sys::kiln_scroll_state_animate_to(self.0, v, none()) } }
    /// What a scrollbar draws of it (material3::non_interactive_scrollbar).
    pub fn scroll_indicator(&self) -> *const sys::kiln_scroll_indicator_state {
        unsafe { sys::kiln_scrollable_state_scroll_indicator(sys::kiln_scroll_state_scrollable(self.0)) }
    }
}
#[track_caller]
pub fn remember_scroll_state() -> ScrollState { ScrollState(unsafe { sys::kiln_remember_scroll_state(here(), 0) }) }

/// The items of a lazy list.
pub struct LazyScope(*mut sys::kiln_lazy_scope);
impl LazyScope {
    pub fn item(&self, content: impl FnMut() + 'static) { unsafe { sys::kiln_lazy_item(self.0, std::ptr::null(), content.into_callback()) } }
    /// `count` items; `key(index)` gives each one's identity.
    pub fn items(&self, count: usize, content: impl FnMut(i32) + 'static, mut key: impl FnMut(i32) -> i64 + 'static) {
        let k = move |i: i32| key_int(key(i));
        unsafe { sys::kiln_lazy_items(self.0, count as i32, content.into_callback(), k.into_callback()) }
    }
}
/// How a lazy grid's cross axis splits into slots.
pub mod grid_cells {
    use super::sys;
    pub fn fixed(count: i32) -> sys::kiln_grid_cells { sys::kiln_grid_cells { kind: sys::kiln_grid_cells_kind::KILN_GRID_CELLS_FIXED, count, size: 0.0 } }
    pub fn adaptive(min_size: f32) -> sys::kiln_grid_cells { sys::kiln_grid_cells { kind: sys::kiln_grid_cells_kind::KILN_GRID_CELLS_ADAPTIVE, count: 1, size: min_size } }
    pub fn fixed_size(size: f32) -> sys::kiln_grid_cells { sys::kiln_grid_cells { kind: sys::kiln_grid_cells_kind::KILN_GRID_CELLS_FIXED_SIZE, count: 1, size } }
}

/// The items of a lazy grid.
pub struct LazyGridScope(*mut sys::kiln_lazy_grid_scope);
impl LazyGridScope {
    pub fn item(&self, content: impl FnMut() + 'static) {
        unsafe { sys::kiln_lazy_grid_item(self.0, std::ptr::null(), Default::default(), content.into_callback()) }
    }
    /// An item across the whole line (a header).
    pub fn full_line_item(&self, content: impl FnMut() + 'static) {
        let span = |_: i32, _: i32, max_line_span: i32| max_line_span;
        unsafe { sys::kiln_lazy_grid_item(self.0, std::ptr::null(), span.into_callback(), content.into_callback()) }
    }
    /// `count` items; `key(index)` gives each one's identity.
    pub fn items(&self, count: usize, content: impl FnMut(i32) + 'static, mut key: impl FnMut(i32) -> i64 + 'static) {
        let k = move |i: i32| key_int(key(i));
        unsafe { sys::kiln_lazy_grid_items(self.0, count as i32, content.into_callback(), k.into_callback(), Default::default()) }
    }
    /// Items with `span(index, max_current_line_span, max_line_span)` slots each.
    pub fn items_spanned(&self, count: usize, content: impl FnMut(i32) + 'static, mut key: impl FnMut(i32) -> i64 + 'static,
                         span: impl FnMut(i32, i32, i32) -> i32 + 'static) {
        let k = move |i: i32| key_int(key(i));
        unsafe { sys::kiln_lazy_grid_items(self.0, count as i32, content.into_callback(), k.into_callback(), span.into_callback()) }
    }
}
#[track_caller]
pub fn lazy_vertical_grid(modifier: Modifier, columns: sys::kiln_grid_cells, mut content: impl FnMut(&LazyGridScope) + 'static) {
    let c = move |s: *mut sys::kiln_lazy_grid_scope| content(&LazyGridScope(s));
    unsafe { sys::kiln_lazy_vertical_grid(here(), modifier.0, columns, std::ptr::null_mut(), std::ptr::null(), c.into_callback()) }
}
#[track_caller]
pub fn lazy_horizontal_grid(modifier: Modifier, rows: sys::kiln_grid_cells, mut content: impl FnMut(&LazyGridScope) + 'static) {
    let c = move |s: *mut sys::kiln_lazy_grid_scope| content(&LazyGridScope(s));
    unsafe { sys::kiln_lazy_horizontal_grid(here(), modifier.0, rows, std::ptr::null_mut(), std::ptr::null(), c.into_callback()) }
}

#[track_caller]
pub fn lazy_column(modifier: Modifier, mut content: impl FnMut(&LazyScope) + 'static) {
    let c = move |s: *mut sys::kiln_lazy_scope| content(&LazyScope(s));
    unsafe { sys::kiln_lazy_column(here(), modifier.0, std::ptr::null_mut(), std::ptr::null(), c.into_callback()) }
}
#[track_caller]
pub fn lazy_row(modifier: Modifier, mut content: impl FnMut(&LazyScope) + 'static) {
    let c = move |s: *mut sys::kiln_lazy_scope| content(&LazyScope(s));
    unsafe { sys::kiln_lazy_row(here(), modifier.0, std::ptr::null_mut(), std::ptr::null(), c.into_callback()) }
}

// --- Animation ------------------------------------------------------------------------------

#[track_caller]
pub fn animate_float(target: f32) -> f32 { unsafe { sys::kiln_animate_float(here(), target, std::ptr::null()) } }
#[track_caller]
pub fn animate_color(target: sys::kiln_color) -> sys::kiln_color { unsafe { sys::kiln_animate_color(here(), target, std::ptr::null()) } }
#[track_caller]
pub fn animated_visibility(visible: bool, modifier: Modifier, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_animated_visibility(here(), visible, modifier.0, content.into_callback()) }
}

/// FocusRequester: `request_focus` moves focus to the focusable after `.focus_requester(r)`.
#[derive(Clone, Copy)]
pub struct FocusRequester(pub *mut sys::kiln_focus_requester);
impl FocusRequester {
    pub fn request_focus(&self) -> bool { unsafe { sys::kiln_focus_requester_request_focus(self.0) } }
}
#[track_caller]
pub fn remember_focus_requester() -> FocusRequester { FocusRequester(unsafe { sys::kiln_remember_focus_requester(here()) }) }
pub fn clear_focus(force: bool) { unsafe { sys::kiln_clear_focus(force) } }

/// MutableTransitionState<Boolean>: a target set by hand, a current state that follows.
#[derive(Clone, Copy)]
pub struct TransitionState(pub *mut sys::kiln_transition_state);
impl TransitionState {
    pub fn current(&self) -> bool { unsafe { sys::kiln_transition_state_current(self.0) } }
    pub fn target(&self) -> bool { unsafe { sys::kiln_transition_state_target(self.0) } }
    pub fn set_target(&self, target: bool) { unsafe { sys::kiln_transition_state_set_target(self.0, target) } }
    pub fn is_idle(&self) -> bool { unsafe { sys::kiln_transition_state_is_idle(self.0) } }
}
#[track_caller]
pub fn remember_transition_state(initial: bool) -> TransitionState { TransitionState(unsafe { sys::kiln_remember_transition_state(here(), initial) }) }
#[track_caller]
pub fn animated_visibility_state(state: TransitionState, modifier: Modifier, content: impl FnMut() + 'static) {
    unsafe { sys::kiln_animated_visibility_state(here(), state.0, modifier.0, content.into_callback()) }
}

// --- Android --------------------------------------------------------------------------------

/// The library's JNI entry: kiln registers its natives. A Rust cdylib exports only Rust symbols,
/// so the C one is reached through this (it replaces kiln's weak JNI_OnLoad).
#[cfg(target_os = "android")]
#[no_mangle]
pub extern "C" fn JNI_OnLoad(vm: *mut c_void, _reserved: *mut c_void) -> i32 {
    extern "C" {
        fn kiln_jni_onload(vm: *mut c_void) -> i32;
    }
    unsafe { kiln_jni_onload(vm) }
}

// --- Volition ---------------------------------------------------------------------------------

/// Volition (github.com/mKonic/volition): what the app tells `where`, and where `go` can send
/// it. Debug builds answer it; release builds keep the registration and answer nothing.
pub mod volition {
    use super::{sys, IntoCallback};
    use std::ffi::{c_char, CStr, CString};

    /// `where`'s fields.
    pub struct Fields(*mut sys::kiln_volition_fields);
    impl Fields {
        fn c(s: &str) -> CString { CString::new(s.replace('\0', "")).unwrap() }
        pub fn string(&mut self, key: &str, value: &str) {
            let (k, v) = (Self::c(key), Self::c(value));
            unsafe { sys::kiln_volition_field_string(self.0, k.as_ptr(), v.as_ptr()) }
        }
        pub fn number(&mut self, key: &str, value: f64) {
            let k = Self::c(key);
            unsafe { sys::kiln_volition_field_number(self.0, k.as_ptr(), value) }
        }
        pub fn boolean(&mut self, key: &str, value: bool) {
            let k = Self::c(key);
            unsafe { sys::kiln_volition_field_bool(self.0, k.as_ptr(), value) }
        }
        pub fn strings(&mut self, key: &str, values: &[&str]) {
            let k = Self::c(key);
            let owned: Vec<CString> = values.iter().map(|v| Self::c(v)).collect();
            let ptrs: Vec<*const c_char> = owned.iter().map(|v| v.as_ptr()).collect();
            unsafe { sys::kiln_volition_field_strings(self.0, k.as_ptr(), ptrs.as_ptr(), ptrs.len()) }
        }
    }

    /// What the app answers; any part may be left out.
    #[derive(Default)]
    pub struct Adapter {
        pub state: Option<Box<dyn FnMut(&mut Fields)>>,
        /// One line each: `name` or `name:<argument>`, two spaces, what it does.
        pub destinations: Option<Box<dyn FnMut() -> Vec<String>>>,
        /// `go name[:argument]`: the answer, or `None` when `name` is not a destination here.
        pub go: Option<Box<dyn FnMut(&str, Option<&str>) -> Option<String>>>,
    }

    /// Registered while it lives.
    pub struct Registration(*mut sys::kiln_volition_registration);
    impl Drop for Registration {
        fn drop(&mut self) { unsafe { sys::kiln_volition_unregister(self.0) } }
    }

    pub fn register(adapter: Adapter) -> Registration {
        let mut a = sys::kiln_volition_adapter::default();
        unsafe { sys::kiln_volition_adapter_init(&mut a) };
        if let Some(mut state) = adapter.state {
            a.state = (move |f: *mut sys::kiln_volition_fields| state(&mut Fields(f))).into_callback();
        }
        if let Some(mut lines) = adapter.destinations {
            a.destinations = (move |l: *mut sys::kiln_volition_lines| {
                for line in lines() {
                    let c = Fields::c(&line);
                    unsafe { sys::kiln_volition_line(l, c.as_ptr()) }
                }
            }).into_callback();
        }
        if let Some(mut go) = adapter.go {
            let mut answer = CString::default();   // kiln copies it before the next call
            a.go = (move |n: *const c_char, arg: *const c_char| -> *const c_char {
                let name = unsafe { CStr::from_ptr(n) }.to_string_lossy();
                let argument = if arg.is_null() { None } else { Some(unsafe { CStr::from_ptr(arg) }.to_string_lossy().into_owned()) };
                match go(&name, argument.as_deref()) {
                    Some(r) => { answer = Fields::c(&r); answer.as_ptr() }
                    None => std::ptr::null(),
                }
            }).into_callback();
        }
        Registration(unsafe { sys::kiln_volition_register(&a) })
    }
}
