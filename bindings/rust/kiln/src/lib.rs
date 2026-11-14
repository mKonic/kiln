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
__kiln_callback!(sys::kiln_fn_http, (r: *const sys::kiln_http_response) -> ());
__kiln_callback!(sys::kiln_fn_int, (v: i32) -> ());
__kiln_callback!(sys::kiln_fn_float, (v: f32) -> ());
__kiln_callback!(sys::kiln_fn_lazy, (s: *mut sys::kiln_lazy_scope) -> ());
__kiln_callback!(sys::kiln_fn_item_key, (i: i32) -> sys::kiln_key);
__kiln_callback!(sys::kiln_fn_layout_coordinates, (c: *const sys::kiln_layout_coordinates) -> ());
__kiln_callback!(sys::kiln_fn_size, (s: sys::kiln_int_size) -> ());
__kiln_callback!(sys::kiln_fn_draw, (d: *mut sys::kiln_draw_scope) -> ());
__kiln_callback!(sys::kiln_fn_place, (s: *mut sys::kiln_placement_scope) -> ());
__kiln_callback!(sys::kiln_fn_measure, (s: *mut sys::kiln_measure_scope, m: *const *mut sys::kiln_measurable, n: usize, c: sys::kiln_int_constraints) -> *mut sys::kiln_measure_result);
__kiln_callback!(sys::kiln_fn_layout_modifier, (s: *mut sys::kiln_measure_scope, m: *mut sys::kiln_measurable, c: sys::kiln_int_constraints) -> *mut sys::kiln_measure_result);
__kiln_callback!(sys::kiln_fn_intent, (i: *const sys::kiln_intent) -> ());
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
    /// `f(coordinates)` after any move in the window.
    pub fn on_globally_positioned(self, mut f: impl FnMut(&LayoutCoordinates) + 'static) -> Self {
        let c = move |p: *const sys::kiln_layout_coordinates| f(&LayoutCoordinates(p));
        Modifier(unsafe { sys::kiln_mod_on_globally_positioned(self.0, c.into_callback()) })
    }
    pub fn on_placed(self, mut f: impl FnMut(&LayoutCoordinates) + 'static) -> Self {
        let c = move |p: *const sys::kiln_layout_coordinates| f(&LayoutCoordinates(p));
        Modifier(unsafe { sys::kiln_mod_on_placed(self.0, c.into_callback()) })
    }
    /// `f(width, height)` in px.
    pub fn on_size_changed(self, mut f: impl FnMut(i32, i32) + 'static) -> Self {
        let c = move |s: sys::kiln_int_size| f(s.width, s.height);
        Modifier(unsafe { sys::kiln_mod_on_size_changed(self.0, c.into_callback()) })
    }
    pub fn draw_behind(self, mut on_draw: impl FnMut(&mut DrawScope) + 'static) -> Self {
        let c = move |d: *mut sys::kiln_draw_scope| on_draw(&mut DrawScope(d));
        Modifier(unsafe { sys::kiln_mod_draw_behind(self.0, c.into_callback()) })
    }
    /// `on_draw` draws the content itself with `DrawScope::draw_content`.
    pub fn draw_with_content(self, mut on_draw: impl FnMut(&mut DrawScope) + 'static) -> Self {
        let c = move |d: *mut sys::kiln_draw_scope| on_draw(&mut DrawScope(d));
        Modifier(unsafe { sys::kiln_mod_draw_with_content(self.0, c.into_callback()) })
    }
    /// Modifier.layout: `measure(scope, measurable, constraints)` measures and places what it wraps.
    pub fn layout(self, mut measure: impl FnMut(&MeasureScope, Measurable, sys::kiln_int_constraints) -> MeasureResult + 'static) -> Self {
        let c = move |s: *mut sys::kiln_measure_scope, m: *mut sys::kiln_measurable, k: sys::kiln_int_constraints| {
            measure(&MeasureScope(s), Measurable(m), k).0
        };
        Modifier(unsafe { sys::kiln_mod_layout(self.0, c.into_callback()) })
    }
    pub fn layout_id(self, id: &str) -> Self {
        let t = CStrBuf::new(id);
        Modifier(unsafe { sys::kiln_mod_layout_id(self.0, t.ptr()) })
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

/// A measure pass (px); only valid inside the measure callback.
pub struct MeasureScope(*mut sys::kiln_measure_scope);
/// What a measure callback returns.
pub struct MeasureResult(*mut sys::kiln_measure_result);
#[derive(Clone, Copy)]
pub struct Measurable(*mut sys::kiln_measurable);
#[derive(Clone, Copy)]
pub struct Placeable(*mut sys::kiln_placeable);
pub struct PlacementScope(*mut sys::kiln_placement_scope);
impl MeasureScope {
    pub fn density(&self) -> f32 { unsafe { sys::kiln_measure_scope_density(self.0) } }
    pub fn is_rtl(&self) -> bool { unsafe { sys::kiln_measure_scope_is_rtl(self.0) } }
    /// layout(width, height) { place }.
    pub fn layout(&self, width: i32, height: i32, mut place: impl FnMut(&PlacementScope) + 'static) -> MeasureResult {
        let c = move |s: *mut sys::kiln_placement_scope| place(&PlacementScope(s));
        MeasureResult(unsafe { sys::kiln_measure_scope_layout(self.0, width, height, c.into_callback()) })
    }
}
impl Measurable {
    pub fn measure(&self, c: sys::kiln_int_constraints) -> Placeable { Placeable(unsafe { sys::kiln_measurable_measure(self.0, c) }) }
    pub fn layout_id(&self) -> Option<String> {
        let p = unsafe { sys::kiln_measurable_layout_id(self.0) };
        if p.is_null() { None } else { Some(crate::from_c(p)) }
    }
}
impl Placeable {
    pub fn width(&self) -> i32 { unsafe { sys::kiln_placeable_width(self.0) } }
    pub fn height(&self) -> i32 { unsafe { sys::kiln_placeable_height(self.0) } }
}
impl PlacementScope {
    pub fn place(&self, p: Placeable, x: i32, y: i32) { unsafe { sys::kiln_place(self.0, p.0, x, y) } }
    pub fn place_relative(&self, p: Placeable, x: i32, y: i32) { unsafe { sys::kiln_place_relative(self.0, p.0, x, y) } }
}
/// Layout: `measure(scope, children, constraints)` measures and places the content's children.
#[track_caller]
pub fn layout(modifier: Modifier, content: impl FnMut() + 'static,
              mut measure: impl FnMut(&MeasureScope, &[Measurable], sys::kiln_int_constraints) -> MeasureResult + 'static) {
    let c = move |s: *mut sys::kiln_measure_scope, m: *const *mut sys::kiln_measurable, n: usize, k: sys::kiln_int_constraints| {
        let children: Vec<Measurable> = (0..n).map(|i| Measurable(unsafe { *m.add(i) })).collect();
        measure(&MeasureScope(s), &children, k).0
    };
    unsafe { sys::kiln_layout(here(), modifier.0, content.into_callback(), c.into_callback()) }
}

/// Content from pickers and shares (content:// URIs; files on the desktop): the bytes.
pub fn read_content(uri: &str) -> Option<Vec<u8>> {
    let u = CStrBuf::new(uri);
    let mut n: usize = 0;
    let p = unsafe { sys::kiln_content_read(u.ptr(), &mut n) };
    if p.is_null() { return None; }
    let v = unsafe { std::slice::from_raw_parts(p, n) }.to_vec();
    unsafe { sys::kiln_content_free(p) };
    Some(v)
}
/// Writes the bytes ("wt" replaces, "wa" appends).
pub fn write_content(uri: &str, bytes: &[u8], mode: &str) -> bool {
    let u = CStrBuf::new(uri);
    let m = CStrBuf::new(mode);
    unsafe { sys::kiln_content_write(u.ptr(), bytes.as_ptr(), bytes.len(), m.ptr()) }
}
/// OpenableColumns' display name.
pub fn content_display_name(uri: &str) -> Option<String> {
    let u = CStrBuf::new(uri);
    opt_c(unsafe { sys::kiln_content_display_name(u.ptr()) })
}

/// The app's private files directory (Context.getFilesDir).
pub fn files_dir() -> String { crate::from_c(unsafe { sys::kiln_files_dir() }) }
/// The app's cache directory (Context.getCacheDir).
pub fn cache_dir() -> String { crate::from_c(unsafe { sys::kiln_cache_dir() }) }
/// Files left out of backups (Context.getNoBackupFilesDir).
pub fn no_backup_files_dir() -> String { crate::from_c(unsafe { sys::kiln_no_backup_files_dir() }) }
/// Where a database file goes (Context.getDatabasePath).
pub fn database_path(name: &str) -> String { let n = CStrBuf::new(name); crate::from_c(unsafe { sys::kiln_database_path(n.ptr()) }) }

/// An image loaded in the background (a URL, content or file URI, path, `asset:///path` or
/// `data:` URI) at the size its layout gives it; see `kiln_async_image`. `options` may set the
/// placeholder and error colours, content scale and alignment; `on_state` gets 0 (loading),
/// 1 (loaded) or 2 (failed).
#[track_caller]
pub fn async_image(model: &str, content_description: Option<&str>, modifier: Modifier, options: Option<sys::kiln_async_image_options>,
                   on_state: Option<Box<dyn FnMut(i32)>>) {
    let m = CStrBuf::new(model);
    let d = content_description.map(CStrBuf::new);
    let mut o: sys::kiln_async_image_options = match options {
        Some(o) => o,
        None => { let mut o = unsafe { std::mem::zeroed() }; unsafe { sys::kiln_async_image_options_init(&mut o) }; o }
    };
    if let Some(mut f) = on_state { o.on_state = (move |v: i32| f(v)).into_callback(); }
    unsafe { sys::kiln_async_image(here(), m.ptr(), d.as_ref().map_or(std::ptr::null(), |d| d.ptr()), modifier.0, &o) }
}
/// The default async image options, to change before passing to [`async_image`].
pub fn async_image_options() -> sys::kiln_async_image_options {
    let mut o = unsafe { std::mem::zeroed() };
    unsafe { sys::kiln_async_image_options_init(&mut o) };
    o
}

/// An HTTP request for [`http_fetch`]. Timeouts of 0 mean 10 s; negative, no limit.
#[derive(Clone, Default)]
pub struct HttpRequest {
    pub url: String,
    /// Empty: GET.
    pub method: String,
    pub headers: Vec<(String, String)>,
    pub body: Vec<u8>,
    pub connect_timeout_ms: i32,
    pub read_timeout_ms: i32,
    pub no_redirects: bool,
}
/// What a request ended with: a response (any status), or `status` 0 and an `error`.
#[derive(Clone, Default, Debug)]
pub struct HttpResponse {
    pub status: i32,
    pub status_text: String,
    pub url: String,
    pub body: Vec<u8>,
    pub headers: Vec<(String, String)>,
    pub error: Option<String>,
}
impl HttpResponse {
    pub fn ok(&self) -> bool { (200..300).contains(&self.status) }
    pub fn text(&self) -> String { String::from_utf8_lossy(&self.body).into_owned() }
    /// The first value of the header, names compared without case.
    pub fn header(&self, name: &str) -> Option<&str> {
        self.headers.iter().find(|(n, _)| n.eq_ignore_ascii_case(name)).map(|(_, v)| v.as_str())
    }
}
/// Runs the request in the background through the platform (HttpURLConnection on Android,
/// libcurl on the desktop); `done` runs once on the main thread. `wanted_headers` names the
/// response headers to copy into the result. Returns an id for [`http_cancel`].
pub fn http_fetch(request: &HttpRequest, wanted_headers: &[&str], mut done: impl FnMut(HttpResponse) + 'static) -> u64 {
    let url = CStrBuf::new(&request.url);
    let method = CStrBuf::new(if request.method.is_empty() { "GET" } else { &request.method });
    let fields: Vec<CStrBuf> = request.headers.iter().flat_map(|(n, v)| [CStrBuf::new(n), CStrBuf::new(v)]).collect();
    let pointers: Vec<*const std::ffi::c_char> = fields.iter().map(|f| f.ptr()).collect();
    let wanted: Vec<String> = wanted_headers.iter().map(|s| s.to_string()).collect();
    let mut r: sys::kiln_http_request = unsafe { std::mem::zeroed() };
    r.size = std::mem::size_of::<sys::kiln_http_request>();
    r.url = url.ptr();
    r.method = method.ptr();
    r.headers = pointers.as_ptr();
    r.header_count = request.headers.len();
    r.body = request.body.as_ptr() as *const std::ffi::c_void;
    r.body_size = request.body.len();
    r.connect_timeout_ms = request.connect_timeout_ms;
    r.read_timeout_ms = request.read_timeout_ms;
    r.no_redirects = request.no_redirects;
    let callback = move |c: *const sys::kiln_http_response| {
        let c = unsafe { &*c };
        let mut out = HttpResponse {
            status: c.status,
            status_text: crate::from_c(c.status_text),
            url: crate::from_c(c.url),
            body: if c.body.is_null() { Vec::new() } else { unsafe { std::slice::from_raw_parts(c.body as *const u8, c.body_size) }.to_vec() },
            headers: Vec::new(),
            error: if c.error.is_null() { None } else { Some(crate::from_c(c.error)) },
        };
        for name in &wanted {
            let n = CStrBuf::new(name);
            let v = unsafe { sys::kiln_http_response_header(c, n.ptr()) };
            if !v.is_null() { out.headers.push((name.clone(), crate::from_c(v))); }
        }
        done(out);
    };
    unsafe { sys::kiln_http_fetch(&r, IntoCallback::<sys::kiln_fn_http>::into_callback(callback)) }
}
/// Shows the system's share sheet for text and/or content:// streams.
pub fn share(text: &str, subject: &str, streams: &[&str], mime_type: &str, chooser_title: Option<&str>) -> bool {
    let t = CStrBuf::new(text);
    let s = CStrBuf::new(subject);
    let m = CStrBuf::new(mime_type);
    let bufs: Vec<CStrBuf> = streams.iter().map(|u| CStrBuf::new(u)).collect();
    let ptrs: Vec<*const std::ffi::c_char> = bufs.iter().map(|b| b.ptr()).collect();
    let title = chooser_title.map(CStrBuf::new);
    unsafe { sys::kiln_share(t.ptr(), s.ptr(), ptrs.as_ptr(), ptrs.len(), m.ptr(), title.as_ref().map_or(std::ptr::null(), |b| b.ptr())) }
}
/// Opens a link in the browser or the app that handles it.
pub fn open_uri(uri: &str) -> bool { let u = CStrBuf::new(uri); unsafe { sys::kiln_open_uri(u.ptr()) } }
/// Whether to explain a permission before asking again (Activity.shouldShowRequestPermissionRationale).
pub fn should_show_permission_rationale(permission: &str) -> bool {
    let p = CStrBuf::new(permission);
    unsafe { sys::kiln_should_show_permission_rationale(p.ptr()) }
}

/// Keeps the launch splash screen up while true (after the first frame it would go).
pub fn keep_splash_screen(keep: bool) { unsafe { sys::kiln_keep_splash_screen(keep) } }

/// Log levels (android.util.Log priorities).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum LogLevel { Verbose = 2, Debug = 3, Info = 4, Warn = 5, Error = 6 }
/// Writes to logcat on Android, stderr elsewhere. Below Info only for tags enabled with
/// `setprop log.tag.<tag> D`.
pub fn log(level: LogLevel, tag: &str, message: &str) {
    let t = CStrBuf::new(tag);
    let lvl = match level {
        LogLevel::Verbose => sys::kiln_log_level::KILN_LOG_VERBOSE,
        LogLevel::Debug => sys::kiln_log_level::KILN_LOG_DEBUG,
        LogLevel::Info => sys::kiln_log_level::KILN_LOG_INFO,
        LogLevel::Warn => sys::kiln_log_level::KILN_LOG_WARN,
        LogLevel::Error => sys::kiln_log_level::KILN_LOG_ERROR,
    };
    if unsafe { sys::kiln_log_is_loggable(lvl.clone(), t.ptr()) } {
        let m = CStrBuf::new(message);
        unsafe { sys::kiln_log(lvl, t.ptr(), m.ptr()) }
    }
}
/// `log` with a format string: `kiln::log!(Info, "Notes", "loaded {} notes", n)`.
#[macro_export]
macro_rules! log {
    ($level:ident, $tag:expr, $($arg:tt)*) => { $crate::log($crate::LogLevel::$level, $tag, &format!($($arg)*)) };
}

/// A system trace section (Perfetto, systrace) that ends when dropped.
pub struct TraceSection(());
impl TraceSection {
    pub fn new(name: &str) -> Self { let n = CStrBuf::new(name); unsafe { sys::kiln_trace_begin(n.ptr()) }; TraceSection(()) }
}
impl Drop for TraceSection {
    fn drop(&mut self) { unsafe { sys::kiln_trace_end() } }
}
/// A value plotted over time in a system trace.
pub fn trace_counter(name: &str, value: i64) { let n = CStrBuf::new(name); unsafe { sys::kiln_trace_counter(n.ptr(), value) } }

/// Enters picture-in-picture (the activity declares `android:supportsPictureInPicture`);
/// an aspect of 0 is the platform's.
pub fn enter_picture_in_picture(aspect_width: i32, aspect_height: i32) -> bool { unsafe { sys::kiln_enter_picture_in_picture(aspect_width, aspect_height) } }
/// The params for later entries and auto-enter.
pub fn set_picture_in_picture_params(aspect_width: i32, aspect_height: i32, auto_enter: bool) -> bool {
    unsafe { sys::kiln_set_picture_in_picture_params(aspect_width, aspect_height, auto_enter) }
}
/// Whether the activity is in picture-in-picture; content recomposes when it changes.
pub fn is_in_picture_in_picture() -> bool { unsafe { sys::kiln_is_in_picture_in_picture() } }
/// Activity.setRequestedOrientation (ActivityInfo.SCREEN_ORIENTATION_*).
pub fn set_requested_orientation(orientation: i32) { unsafe { sys::kiln_set_requested_orientation(orientation) } }

/// Aborts a request still in progress; its callback is not called.
pub fn http_cancel(request: u64) { unsafe { sys::kiln_http_cancel(request) } }
/// Whether the default network is connected. For content: recomposes on a change.
#[track_caller]
pub fn network_connected() -> bool { unsafe { sys::kiln_network_connected(here()) } }

/// Preferences (DataStore): typed key-value data kept in the app's files directory. The getters
/// are for content and recompose it when the value changes; the setters write in the background.
#[derive(Clone, Copy)]
pub struct Preferences(*mut sys::kiln_preferences);
impl Preferences {
    pub fn open(name: &str) -> Self { let n = CStrBuf::new(name); Preferences(unsafe { sys::kiln_preferences_open(n.ptr()) }) }
    #[track_caller]
    pub fn bool(&self, name: &str, fallback: bool) -> bool { let n = CStrBuf::new(name); unsafe { sys::kiln_preferences_bool(here(), self.0, n.ptr(), fallback) } }
    #[track_caller]
    pub fn i64(&self, name: &str, fallback: i64) -> i64 { let n = CStrBuf::new(name); unsafe { sys::kiln_preferences_i64(here(), self.0, n.ptr(), fallback) } }
    #[track_caller]
    pub fn f64(&self, name: &str, fallback: f64) -> f64 { let n = CStrBuf::new(name); unsafe { sys::kiln_preferences_f64(here(), self.0, n.ptr(), fallback) } }
    #[track_caller]
    pub fn string(&self, name: &str, fallback: &str) -> String {
        let n = CStrBuf::new(name);
        let p = unsafe { sys::kiln_preferences_str(here(), self.0, n.ptr(), std::ptr::null()) };
        if p.is_null() { fallback.to_string() } else { crate::from_c(p) }
    }
    pub fn set_bool(&self, name: &str, v: bool) { let n = CStrBuf::new(name); unsafe { sys::kiln_preferences_set_bool(self.0, n.ptr(), v) } }
    pub fn set_i64(&self, name: &str, v: i64) { let n = CStrBuf::new(name); unsafe { sys::kiln_preferences_set_i64(self.0, n.ptr(), v) } }
    pub fn set_f64(&self, name: &str, v: f64) { let n = CStrBuf::new(name); unsafe { sys::kiln_preferences_set_f64(self.0, n.ptr(), v) } }
    pub fn set_string(&self, name: &str, v: &str) { let n = CStrBuf::new(name); let s = CStrBuf::new(v); unsafe { sys::kiln_preferences_set_str(self.0, n.ptr(), s.ptr()) } }
    pub fn remove(&self, name: &str) { let n = CStrBuf::new(name); unsafe { sys::kiln_preferences_remove(self.0, n.ptr()) } }
    pub fn clear(&self) { unsafe { sys::kiln_preferences_clear(self.0) } }
}

/// An intent (a deep link, a share): only valid inside the call or callback that gave it.
pub struct Intent(*const sys::kiln_intent);
fn opt_c(p: *const std::ffi::c_char) -> Option<String> { if p.is_null() { None } else { Some(crate::from_c(p)) } }
impl Intent {
    pub fn action(&self) -> Option<String> { opt_c(unsafe { sys::kiln_intent_action(self.0) }) }
    pub fn data(&self) -> Option<String> { opt_c(unsafe { sys::kiln_intent_data(self.0) }) }
    pub fn mime_type(&self) -> Option<String> { opt_c(unsafe { sys::kiln_intent_type(self.0) }) }
    pub fn flags(&self) -> i32 { unsafe { sys::kiln_intent_flags(self.0) } }
    pub fn extra_string(&self, name: &str) -> Option<String> {
        let n = CStrBuf::new(name);
        opt_c(unsafe { sys::kiln_intent_extra_string(self.0, n.ptr()) })
    }
    pub fn clip_uris(&self) -> Vec<String> {
        (0..unsafe { sys::kiln_intent_clip_uri_count(self.0) }).filter_map(|i| opt_c(unsafe { sys::kiln_intent_clip_uri(self.0, i) })).collect()
    }
}
/// The activity's launch intent, read now (None off Android).
pub fn with_activity_intent<R>(f: impl FnOnce(&Intent) -> R) -> Option<R> {
    let p = unsafe { sys::kiln_activity_intent() };
    if p.is_null() { None } else { Some(f(&Intent(p))) }
}
/// OnNewIntent: `f` gets each intent the activity receives while this is composed.
#[track_caller]
pub fn on_new_intent(mut f: impl FnMut(&Intent) + 'static) {
    let c = move |i: *const sys::kiln_intent| f(&Intent(i));
    unsafe { sys::kiln_on_new_intent(here(), c.into_callback()) }
}

/// A format argument for resource strings.
pub enum FormatArg<'a> { Str(&'a str), Int(i64), Double(f64) }
fn with_format_args<R>(args: &[FormatArg], f: impl FnOnce(*const sys::kiln_format_arg, usize) -> R) -> R {
    let strings: Vec<CStrBuf> = args.iter().map(|a| match a { FormatArg::Str(s) => CStrBuf::new(s), _ => CStrBuf::new("") }).collect();
    let c: Vec<sys::kiln_format_arg> = args.iter().zip(&strings).map(|(a, s)| match a {
        FormatArg::Str(_) => sys::kiln_format_arg { kind: sys::kiln_format_arg_kind::KILN_FORMAT_STRING, s: s.ptr(), i: 0, d: 0.0 },
        FormatArg::Int(i) => sys::kiln_format_arg { kind: sys::kiln_format_arg_kind::KILN_FORMAT_INT, s: std::ptr::null(), i: *i, d: 0.0 },
        FormatArg::Double(d) => sys::kiln_format_arg { kind: sys::kiln_format_arg_kind::KILN_FORMAT_DOUBLE, s: std::ptr::null(), i: 0, d: *d },
    }).collect();
    f(c.as_ptr(), c.len())
}
/// stringResource(name, args): the app's res/ string, formatted (kiln_res_c.h names them).
pub fn string_resource(name: &str, args: &[FormatArg]) -> Option<String> {
    let n = CStrBuf::new(name);
    with_format_args(args, |a, len| {
        let p = unsafe { sys::kiln_string_resource(n.ptr(), a, len) };
        if p.is_null() { None } else { Some(crate::from_c(p)) }
    })
}
/// pluralStringResource(name, quantity, args).
pub fn plural_string_resource(name: &str, quantity: i32, args: &[FormatArg]) -> Option<String> {
    let n = CStrBuf::new(name);
    with_format_args(args, |a, len| {
        let p = unsafe { sys::kiln_plural_string_resource(n.ptr(), quantity, a, len) };
        if p.is_null() { None } else { Some(crate::from_c(p)) }
    })
}
pub fn color_resource(name: &str) -> Option<sys::kiln_color> {
    let n = CStrBuf::new(name);
    let mut c: sys::kiln_color = 0;
    if unsafe { sys::kiln_color_resource(n.ptr(), &mut c) } { Some(c) } else { None }
}
pub fn dimension_resource(name: &str) -> Option<f32> {
    let n = CStrBuf::new(name);
    let mut d = 0.0f32;
    if unsafe { sys::kiln_dimension_resource(n.ptr(), &mut d) } { Some(d) } else { None }
}

/// A draw scope (px, +y down); only valid inside the draw callback.
pub struct DrawScope(*mut sys::kiln_draw_scope);
fn style_ptr(style: Option<&sys::kiln_draw_style>) -> *const sys::kiln_draw_style { style.map_or(std::ptr::null(), |s| s as *const _) }
/// A stroke of `width` px (butt caps, miter joins); `None` as a style fills.
pub fn stroke(width: f32) -> sys::kiln_draw_style {
    sys::kiln_draw_style { width, miter: 4.0, cap: sys::kiln_stroke_cap::KILN_STROKE_CAP_BUTT, join: sys::kiln_stroke_join::KILN_STROKE_JOIN_MITER }
}
fn off(x: f32, y: f32) -> sys::kiln_offset { sys::kiln_offset { x, y } }
fn sz(w: f32, h: f32) -> sys::kiln_size { sys::kiln_size { width: w, height: h } }
impl DrawScope {
    pub fn size(&self) -> (f32, f32) { let s = unsafe { sys::kiln_draw_scope_size(self.0) }; (s.width, s.height) }
    pub fn density(&self) -> f32 { unsafe { sys::kiln_draw_scope_density(self.0) } }
    pub fn is_rtl(&self) -> bool { unsafe { sys::kiln_draw_scope_is_rtl(self.0) } }
    pub fn draw_rect(&mut self, color: sys::kiln_color, x: f32, y: f32, w: f32, h: f32, style: Option<&sys::kiln_draw_style>) {
        unsafe { sys::kiln_draw_rect(self.0, color, off(x, y), sz(w, h), style_ptr(style)) }
    }
    pub fn draw_round_rect(&mut self, color: sys::kiln_color, x: f32, y: f32, w: f32, h: f32, radius: f32, style: Option<&sys::kiln_draw_style>) {
        unsafe { sys::kiln_draw_round_rect(self.0, color, off(x, y), sz(w, h), radius, style_ptr(style)) }
    }
    pub fn draw_circle(&mut self, color: sys::kiln_color, cx: f32, cy: f32, radius: f32, style: Option<&sys::kiln_draw_style>) {
        unsafe { sys::kiln_draw_circle(self.0, color, off(cx, cy), radius, style_ptr(style)) }
    }
    pub fn draw_oval(&mut self, color: sys::kiln_color, x: f32, y: f32, w: f32, h: f32, style: Option<&sys::kiln_draw_style>) {
        unsafe { sys::kiln_draw_oval(self.0, color, off(x, y), sz(w, h), style_ptr(style)) }
    }
    pub fn draw_arc(&mut self, color: sys::kiln_color, start: f32, sweep: f32, use_center: bool, x: f32, y: f32, w: f32, h: f32,
                    style: Option<&sys::kiln_draw_style>) {
        unsafe { sys::kiln_draw_arc(self.0, color, start, sweep, use_center, off(x, y), sz(w, h), style_ptr(style)) }
    }
    pub fn draw_line(&mut self, color: sys::kiln_color, x0: f32, y0: f32, x1: f32, y1: f32, width: f32) {
        unsafe { sys::kiln_draw_line(self.0, color, off(x0, y0), off(x1, y1), width, sys::kiln_stroke_cap::KILN_STROKE_CAP_BUTT) }
    }
    pub fn draw_path(&mut self, path: &Path, color: sys::kiln_color, style: Option<&sys::kiln_draw_style>) {
        unsafe { sys::kiln_draw_path(self.0, path.0, color, style_ptr(style)) }
    }
    pub fn save(&mut self) { unsafe { sys::kiln_draw_save(self.0) } }
    pub fn restore(&mut self) { unsafe { sys::kiln_draw_restore(self.0) } }
    pub fn translate(&mut self, dx: f32, dy: f32) { unsafe { sys::kiln_draw_translate(self.0, dx, dy) } }
    pub fn rotate(&mut self, degrees: f32, px: f32, py: f32) { unsafe { sys::kiln_draw_rotate(self.0, degrees, off(px, py)) } }
    pub fn scale(&mut self, sx: f32, sy: f32, px: f32, py: f32) { unsafe { sys::kiln_draw_scale(self.0, sx, sy, off(px, py)) } }
    pub fn clip_rect(&mut self, left: f32, top: f32, right: f32, bottom: f32, radius: f32) {
        unsafe { sys::kiln_draw_clip_rect(self.0, sys::kiln_rect { left, top, right, bottom }, radius) }
    }
    pub fn clip_path(&mut self, path: &Path) { unsafe { sys::kiln_draw_clip_path(self.0, path.0) } }
    pub fn multiply_alpha(&mut self, alpha: f32) { unsafe { sys::kiln_draw_multiply_alpha(self.0, alpha) } }
    /// In `draw_with_content`: draws what the modifier wraps.
    pub fn draw_content(&mut self) { unsafe { sys::kiln_draw_content(self.0) } }
}

/// A path (freed on drop).
pub struct Path(*mut sys::kiln_path);
impl Path {
    pub fn new() -> Self { Path(unsafe { sys::kiln_path_new() }) }
    pub fn move_to(&mut self, x: f32, y: f32) -> &mut Self { unsafe { sys::kiln_path_move_to(self.0, x, y) }; self }
    pub fn line_to(&mut self, x: f32, y: f32) -> &mut Self { unsafe { sys::kiln_path_line_to(self.0, x, y) }; self }
    pub fn quadratic_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32) -> &mut Self { unsafe { sys::kiln_path_quadratic_to(self.0, x1, y1, x2, y2) }; self }
    pub fn cubic_to(&mut self, x1: f32, y1: f32, x2: f32, y2: f32, x3: f32, y3: f32) -> &mut Self {
        unsafe { sys::kiln_path_cubic_to(self.0, x1, y1, x2, y2, x3, y3) }; self
    }
    pub fn add_rect(&mut self, left: f32, top: f32, right: f32, bottom: f32) -> &mut Self {
        unsafe { sys::kiln_path_add_rect(self.0, sys::kiln_rect { left, top, right, bottom }) }; self
    }
    pub fn add_oval(&mut self, left: f32, top: f32, right: f32, bottom: f32) -> &mut Self {
        unsafe { sys::kiln_path_add_oval(self.0, sys::kiln_rect { left, top, right, bottom }) }; self
    }
    pub fn close(&mut self) -> &mut Self { unsafe { sys::kiln_path_close(self.0) }; self }
}
impl Default for Path { fn default() -> Self { Path::new() } }
impl Drop for Path { fn drop(&mut self) { unsafe { sys::kiln_path_free(self.0) } } }

/// Canvas: a box that only draws.
#[track_caller]
pub fn canvas(modifier: Modifier, mut on_draw: impl FnMut(&mut DrawScope) + 'static) {
    let c = move |d: *mut sys::kiln_draw_scope| on_draw(&mut DrawScope(d));
    unsafe { sys::kiln_canvas(here(), modifier.0, c.into_callback()) }
}

/// Where an element sits, in px; only valid inside the callback that gave it.
pub struct LayoutCoordinates(pub *const sys::kiln_layout_coordinates);
impl LayoutCoordinates {
    pub fn is_attached(&self) -> bool { unsafe { sys::kiln_layout_coordinates_is_attached(self.0) } }
    pub fn size(&self) -> sys::kiln_int_size { unsafe { sys::kiln_layout_coordinates_size(self.0) } }
    pub fn position_in_root(&self) -> sys::kiln_offset { unsafe { sys::kiln_layout_coordinates_position_in_root(self.0) } }
    pub fn bounds_in_root(&self) -> sys::kiln_rect { unsafe { sys::kiln_layout_coordinates_bounds_in_root(self.0) } }
    pub fn position_in_window(&self) -> sys::kiln_offset { unsafe { sys::kiln_layout_coordinates_position_in_window(self.0) } }
    pub fn bounds_in_window(&self) -> sys::kiln_rect { unsafe { sys::kiln_layout_coordinates_bounds_in_window(self.0) } }
    pub fn position_in_parent(&self) -> sys::kiln_offset { unsafe { sys::kiln_layout_coordinates_position_in_parent(self.0) } }
    pub fn bounds_in_parent(&self) -> sys::kiln_rect { unsafe { sys::kiln_layout_coordinates_bounds_in_parent(self.0) } }
    pub fn local_position_of(&self, source: &LayoutCoordinates, relative: sys::kiln_offset) -> sys::kiln_offset {
        unsafe { sys::kiln_layout_coordinates_local_position_of(self.0, source.0, relative) }
    }
    pub fn local_bounding_box_of(&self, source: &LayoutCoordinates) -> sys::kiln_rect {
        unsafe { sys::kiln_layout_coordinates_local_bounding_box_of(self.0, source.0) }
    }
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
        // `answer` is assigned to keep the returned pointer's string alive, not to be read.
        #[allow(unused_assignments)]
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
