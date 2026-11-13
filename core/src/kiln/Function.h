#pragma once
// kiln::Function: std::function without what kiln never uses, for a smaller library.
//
// libc++'s std::function gives every stored lambda a vtable, seven virtual methods (call, two
// clones, two destroys, target, target_type) and RTTI for target(): with thousands of lambdas
// that was a quarter of an app's library. Here a callable costs one invoker; the manager that
// copies, moves and destroys is shared by every lambda that is trivially copyable and fits in
// place (most of them: captures of pointers and references), and only other callables get one
// of their own. No target() / target_type(), so no RTTI.
//
// Otherwise a drop-in std::function: copyable, empty by default or from nullptr, an empty
// function pointer or an empty function, explicit bool, == nullptr, swap, and calls that
// throw std::bad_function_call when empty.
#include <cstddef>
#include <cstring>
#include <functional>
#include <new>
#include <type_traits>
#include <utility>

namespace kiln {

    template <class> class Function;

    namespace detail {
        template <class> inline constexpr bool kIsFunction = false;
        template <class S> inline constexpr bool kIsFunction<Function<S>> = true;
        template <class S> inline constexpr bool kIsFunction<std::function<S>> = true;

        // std::function's "empty" sources: null function / member pointers and empty functions.
        template <class F>
        bool IsNullCallable(const F& f) {
            if constexpr (std::is_pointer_v<F> || std::is_member_pointer_v<F> || kIsFunction<F>) return !f;
            else return false;
        }

        enum class FunctionOp { Copy, Move, Destroy };
        inline constexpr std::size_t kFunctionInline = 3 * sizeof(void*);

        // Callables stored in place: fit, aligned, and nothrow to move (Function's move is noexcept).
        template <class F>
        inline constexpr bool kFunctionInPlace = sizeof(F) <= kFunctionInline && alignof(F) <= alignof(void*) &&
                                                 std::is_nothrow_move_constructible_v<F>;
        // In place and trivially copyable: copied with memcpy, nothing to destroy, no manager.
        template <class F>
        inline constexpr bool kFunctionTrivial = kFunctionInPlace<F> && std::is_trivially_copyable_v<F> &&
                                                 std::is_trivially_destructible_v<F>;
    }

    template <class R, class... A>
    class Function<R(A...)> {
        using Op = detail::FunctionOp;
        using Invoker = R (*)(void*, A&&...);
        // Copy: construct `to` from `from`; Move: construct `to` from `from`, then destroy `from`;
        // Destroy: `from`.
        using Manager = void (*)(Op, void* from, void* to);

    public:
        using result_type = R;

        Function() noexcept = default;
        Function(std::nullptr_t) noexcept {}
        Function(const Function& o) : m_Invoke(o.m_Invoke), m_Manage(o.m_Manage) {
            if (m_Manage) m_Manage(Op::Copy, const_cast<void*>(static_cast<const void*>(o.m_Buf)), m_Buf);
            else std::memcpy(m_Buf, o.m_Buf, sizeof m_Buf);
        }
        Function(Function&& o) noexcept : m_Invoke(o.m_Invoke), m_Manage(o.m_Manage) {
            if (m_Manage) m_Manage(Op::Move, o.m_Buf, m_Buf);
            else std::memcpy(m_Buf, o.m_Buf, sizeof m_Buf);
            o.m_Invoke = nullptr;
            o.m_Manage = nullptr;
        }
        template <class F, class D = std::decay_t<F>>
            requires(!std::is_same_v<D, Function> && std::is_invocable_r_v<R, D&, A...>)
        Function(F&& f) {
            if (detail::IsNullCallable(f)) return;
            if constexpr (detail::kFunctionInPlace<D>) {
                ::new (static_cast<void*>(m_Buf)) D(std::forward<F>(f));
                if constexpr (!detail::kFunctionTrivial<D>) m_Manage = &ManageInPlace<D>;
                m_Invoke = &InvokeInPlace<D>;
            } else {
                D* p = new D(std::forward<F>(f));
                std::memcpy(m_Buf, &p, sizeof p);
                m_Manage = &ManageHeap<D>;
                m_Invoke = &InvokeHeap<D>;
            }
        }
        ~Function() { Reset(); }

        Function& operator=(const Function& o) {
            if (this != &o) Function(o).swap(*this);
            return *this;
        }
        Function& operator=(Function&& o) noexcept {
            if (this != &o) {
                Reset();
                m_Invoke = o.m_Invoke;
                m_Manage = o.m_Manage;
                if (m_Manage) m_Manage(Op::Move, o.m_Buf, m_Buf);
                else std::memcpy(m_Buf, o.m_Buf, sizeof m_Buf);
                o.m_Invoke = nullptr;
                o.m_Manage = nullptr;
            }
            return *this;
        }
        Function& operator=(std::nullptr_t) noexcept {
            Reset();
            return *this;
        }
        template <class F, class D = std::decay_t<F>>
            requires(!std::is_same_v<D, Function> && std::is_invocable_r_v<R, D&, A...>)
        Function& operator=(F&& f) {
            Function(std::forward<F>(f)).swap(*this);
            return *this;
        }
        void swap(Function& o) noexcept {
            Function t(std::move(o));
            o = std::move(*this);
            *this = std::move(t);
        }
        friend void swap(Function& a, Function& b) noexcept { a.swap(b); }

        explicit operator bool() const noexcept { return m_Invoke != nullptr; }
        friend bool operator==(const Function& f, std::nullptr_t) noexcept { return !f; }

        R operator()(A... args) const {
            if (!m_Invoke) throw std::bad_function_call();
            return m_Invoke(const_cast<unsigned char*>(m_Buf), std::forward<A>(args)...);
        }

    private:
        void Reset() noexcept {
            if (m_Manage) m_Manage(Op::Destroy, m_Buf, nullptr);
            m_Invoke = nullptr;
            m_Manage = nullptr;
        }

        template <class D>
        static R Call(D& f, A&&... args) {
            if constexpr (std::is_void_v<R>) std::invoke(f, std::forward<A>(args)...);
            else return std::invoke(f, std::forward<A>(args)...);
        }
        template <class D>
        static R InvokeInPlace(void* buf, A&&... args) {
            return Call(*std::launder(static_cast<D*>(buf)), std::forward<A>(args)...);
        }
        template <class D>
        static R InvokeHeap(void* buf, A&&... args) {
            D* p;
            std::memcpy(&p, buf, sizeof p);
            return Call(*p, std::forward<A>(args)...);
        }
        template <class D>
        static void ManageInPlace(Op op, void* from, void* to) {
            D* f = std::launder(static_cast<D*>(from));
            switch (op) {
                case Op::Copy: ::new (to) D(*f); break;
                case Op::Move: ::new (to) D(std::move(*f)); f->~D(); break;
                case Op::Destroy: f->~D(); break;
            }
        }
        template <class D>
        static void ManageHeap(Op op, void* from, void* to) {
            D* f;
            std::memcpy(&f, from, sizeof f);
            switch (op) {
                case Op::Copy: { D* p = new D(*f); std::memcpy(to, &p, sizeof p); break; }
                case Op::Move: std::memcpy(to, &f, sizeof f); break;
                case Op::Destroy: delete f; break;
            }
        }

        alignas(void*) unsigned char m_Buf[detail::kFunctionInline] = {};
        Invoker m_Invoke = nullptr;
        Manager m_Manage = nullptr;
    };

    // Function f = &free_function;
    template <class R, class... A>
    Function(R (*)(A...)) -> Function<R(A...)>;

}
