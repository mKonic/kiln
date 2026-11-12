//! The notes app in the kiln dev window: `cargo run --example notes`. kiln's host shell has
//! the `main`; the app exports kiln_main.
#![no_main]

#[path = "shared/notes_app.rs"]
mod notes_app;
