#include "Check.h"

#include <kiln/Function.h>

#include <memory>
#include <stdexcept>

namespace {
    // Counts live copies, so copies, moves and destroys are visible.
    struct Tracked {
        int* live;
        std::shared_ptr<int> payload;   // fits in place but is not trivially copyable: the manager path
        explicit Tracked(int* l) : live(l), payload(std::make_shared<int>(1)) { ++*live; }
        Tracked(const Tracked& o) : live(o.live), payload(o.payload) { ++*live; }
        Tracked(Tracked&& o) noexcept : live(o.live), payload(std::move(o.payload)) { ++*live; }
        ~Tracked() { --*live; }
        int operator()(int v) const { return v + *payload; }
    };
    static_assert(kiln::detail::kFunctionInPlace<Tracked> && !kiln::detail::kFunctionTrivial<Tracked>);
    struct Big {
        int* live;
        double pad[8] = {};   // larger than the in-place buffer: stored on the heap
        explicit Big(int* l) : live(l) { ++*live; }
        Big(const Big& o) : live(o.live) { ++*live; }
        ~Big() { --*live; }
        int operator()(int v) const { return v * 2; }
    };
    static_assert(!kiln::detail::kFunctionInPlace<Big>);
    int Twice(int v) { return v * 2; }
}

TEST(FunctionEmptyStatesMatchStdFunction) {
    kiln::Function<int(int)> a;
    CHECK(!a);
    CHECK(a == nullptr);
    kiln::Function<int(int)> b = nullptr;
    CHECK(!b);
    int (*null)(int) = nullptr;
    kiln::Function<int(int)> c = null;
    CHECK(!c);
    std::function<int(int)> emptyStd;
    kiln::Function<int(int)> d = emptyStd;
    CHECK(!d);
    bool threw = false;
    try { a(1); } catch (const std::bad_function_call&) { threw = true; }
    CHECK(threw);
    kiln::Function<int(int)> e = &Twice;
    CHECK(bool(e));
    CHECK_EQ(e(4), 8);
}

TEST(FunctionTrivialLambdasCopyByValue) {
    int calls = 0;
    int* p = &calls;
    kiln::Function<void()> f = [p] { ++*p; };
    kiln::Function<void()> g = f;
    f();
    g();
    CHECK_EQ(calls, 2);
    kiln::Function<void()> h = std::move(f);
    CHECK(!f);
    h();
    CHECK_EQ(calls, 3);
}

TEST(FunctionCopiesMovesAndDestroysItsCallable) {
    int live = 0;
    {
        kiln::Function<int(int)> f = Tracked(&live);
        CHECK_EQ(live, 1);
        CHECK_EQ(f(1), 2);
        kiln::Function<int(int)> g = f;
        CHECK_EQ(live, 2);
        kiln::Function<int(int)> h = std::move(g);
        CHECK_EQ(live, 2);
        CHECK(!g);
        CHECK_EQ(h(2), 3);
        h = nullptr;
        CHECK_EQ(live, 1);
        h = f;
        CHECK_EQ(live, 2);
        f = [](int v) { return v; };
        CHECK_EQ(live, 1);
        CHECK_EQ(f(7), 7);
        swap(f, h);
        CHECK_EQ(f(1), 2);
        CHECK_EQ(h(1), 1);
    }
    CHECK_EQ(live, 0);
    {
        kiln::Function<int(int)> f = Big(&live);
        CHECK_EQ(live, 1);
        kiln::Function<int(int)> g = f;
        CHECK_EQ(live, 2);
        kiln::Function<int(int)> h = std::move(f);
        CHECK_EQ(live, 2);
        CHECK_EQ(h(3) + g(1), 8);
        g = h;
        CHECK_EQ(live, 2);
    }
    CHECK_EQ(live, 0);
}

TEST(FunctionCallsForwardArgumentsAndConvertResults) {
    // Move-only arguments reach the callable; a non-void result converts or is discarded.
    kiln::Function<int(std::unique_ptr<int>)> take = [](std::unique_ptr<int> p) { return *p; };
    CHECK_EQ(take(std::make_unique<int>(5)), 5);
    int seen = 0;
    kiln::Function<void(int&)> ref = [](int& v) { v = 9; return 1; };
    ref(seen);
    CHECK_EQ(seen, 9);
    kiln::Function<double()> widen = [] { return 3; };
    CHECK_EQ(widen(), 3.0);
    // Converts to std::function for APIs outside kiln.
    std::function<double()> s = widen;
    CHECK_EQ(s(), 3.0);
}
