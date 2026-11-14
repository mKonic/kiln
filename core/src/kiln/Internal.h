#pragma once

// KILN_INTERNAL marks a declaration that is public in C++ only because kiln's own components
// reach it, where Compose keeps the same thing `internal`. tools/gen-capi leaves it out of the
// C API, kiln++ and the Rust bindings.
#if defined(__clang__)
#define KILN_INTERNAL [[clang::annotate("kiln_internal")]]
#else
#define KILN_INTERNAL
#endif
