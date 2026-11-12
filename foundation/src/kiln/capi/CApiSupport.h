#pragma once

// Shared by the C ABI glue (FoundationCApi.cpp, the generated material3 glue): conversions from
// the C types to kiln's, callback ownership and the transient modifier arena.

#include <kiln/kiln.h>

#include "kiln/foundation/Shapes.h"
#include "kiln/runtime/Composition.h"
#include "kiln/text/TextStyle.h"
#include "kiln/ui/Modifier.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <cmath>
#include <functional>
#include <memory>
#include <string>

struct kiln_modifier { kiln::ui::Modifier m; };

namespace kiln::capi {

    void LogError(const char* function, const char* what);

    // Every exported function body: exceptions stop here and are logged.
#define KILN_CAPI_TRY try
#define KILN_CAPI_CATCH(fallback)                                                           \
    catch (const std::exception& e) { ::kiln::capi::LogError(__func__, e.what()); return fallback; } \
    catch (...) { ::kiln::capi::LogError(__func__, "unknown exception"); return fallback; }

    // Marks a call into C: modifiers made inside live until the outermost callback returns.
    struct CallbackScope {
        CallbackScope();
        ~CallbackScope();
        CallbackScope(const CallbackScope&) = delete;
        CallbackScope& operator=(const CallbackScope&) = delete;
    };

    // A kiln_fn* owned by kiln: drop runs when the last copy goes.
    template<typename F>
    struct Owned {
        F f;
        explicit Owned(F fn) : f(fn) {}
        ~Owned() { if (f.drop) f.drop(f.user); }
        Owned(const Owned&) = delete;
        Owned& operator=(const Owned&) = delete;
    };
    // Null when the callback is absent (its drop, if any, has run).
    template<typename F>
    std::shared_ptr<Owned<F>> Own(F f) {
        if (!f.call) {
            if (f.drop) f.drop(f.user);
            return nullptr;
        }
        return std::make_shared<Owned<F>>(f);
    }

    // C callbacks as std::function (empty when absent).
    std::function<void()> Fn(kiln_fn f);
    std::function<void(bool)> Fn(kiln_fn_bool f);
    std::function<void(int)> Fn(kiln_fn_int f);
    std::function<void(float)> Fn(kiln_fn_float f);
    std::function<void(const std::string&)> Fn(kiln_fn_str f);

    // Values.
    inline rt::Key ToKey(kiln_key k) {
        rt::Key r = rt::Key::Positional();
        r.value = k.value;
        r.explicitKey = k.explicit_key;
        return r;
    }
    inline rt::Keys ToKeys(const uint64_t* keys, size_t count) {
        rt::Keys r;
        for (size_t i = 0; keys && i < count; ++i) r.hashes.push_back(std::size_t(keys[i]));
        return r;
    }
    inline ui::Color ToColor(kiln_color c) { return c == KILN_COLOR_UNSPECIFIED ? ui::Color::Unspecified() : ui::Color(std::uint32_t(c)); }
    inline kiln_color FromColor(ui::Color c) { return c == ui::Color::Unspecified() ? KILN_COLOR_UNSPECIFIED : KILN_ARGB(c.argb); }
    inline ui::Dp ToDp(float v) { return std::isnan(v) ? ui::Dp::Unspecified() : ui::Dp(v); }
    inline float FromDp(ui::Dp d) { return d.value; }
    inline ui::Alignment ToAlignment(kiln_alignment a) { return { a.horizontal, a.vertical }; }
    inline std::string ToString(const char* s) { return s ? std::string(s) : std::string(); }
    inline std::optional<std::string> ToOptString(const char* s) { return s ? std::optional<std::string>(s) : std::nullopt; }
    // A string handed back to C: valid until the next string returned on this thread.
    const char* ReturnString(std::string s);

    inline const ui::Modifier& ToModifier(const kiln_modifier* m) {
        static const ui::Modifier empty;
        return m ? m->m : empty;
    }
    kiln_modifier* NewModifier(ui::Modifier m);

    // Options structs: a struct from an older header is shorter; read what it has and take the
    // defaults for the rest.
    template<typename T>
    T ReadOptions(const T* in, void (*init)(T*)) {
        T out;
        init(&out);
        if (in && in->size > sizeof(size_t)) {
            const size_t n = std::min(in->size, sizeof(T));
            std::memcpy(reinterpret_cast<char*>(&out) + sizeof(size_t), reinterpret_cast<const char*>(in) + sizeof(size_t), n - sizeof(size_t));
        }
        out.size = sizeof(T);
        return out;
    }

    foundation::ShapePtr ToShape(const kiln_shape* s);   // null: null
    const text::TextStyle* ToTextStyle(const kiln_text_style* s);   // null: null
    kiln_shape* NewShape(foundation::ShapePtr shape);
    kiln_text_style* NewTextStyle(text::TextStyle style);

}
