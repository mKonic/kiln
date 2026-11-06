// remember*State with their savers (ScrollState.Saver, LazyListState.Saver, LazyGridState.Saver,
// LazyStaggeredGridState.Saver, PagerState's DefaultPagerState.Saver, TextFieldState.Saver).

#include "kiln/foundation/Lazy.h"
#include "kiln/foundation/Pager.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/runtime/Saveable.h"

#include <algorithm>

namespace kiln::foundation {

    namespace {
        int IntAt(const rt::SavedValue::List& l, std::size_t i) { return i < l.size() ? int(rt::SavedNumber(l[i]).value_or(0)) : 0; }
        double NumberAt(const rt::SavedValue::List& l, std::size_t i) { return i < l.size() ? rt::SavedNumber(l[i]).value_or(0) : 0; }

        // A lazy state as [firstVisibleItemIndex, firstVisibleItemScrollOffset].
        template<typename S>
        S& RememberLazy(int index, int offset) {
            auto saver = rt::ListSaver<std::shared_ptr<S>>(
                [](const std::shared_ptr<S>& s) {
                    return rt::SavedValue::List{ s->FirstVisibleItemIndex(), s->FirstVisibleItemScrollOffset() };
                },
                [](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<S>> {
                    if (l.size() < 2) return std::nullopt;
                    return std::make_shared<S>(IntAt(l, 0), IntAt(l, 1));
                });
            return *rt::RememberSaveable<std::shared_ptr<S>>([&] { return std::make_shared<S>(index, offset); }, saver);
        }
    }

    ScrollState& RememberScrollState(int initial) {
        rt::Saver<std::shared_ptr<ScrollState>> saver{
            [](const std::shared_ptr<ScrollState>& s) { return rt::SavedValue(s->Value()); },
            [](const rt::SavedValue& v) -> std::optional<std::shared_ptr<ScrollState>> {
                if (auto n = rt::SavedNumber(v)) return std::make_shared<ScrollState>(int(*n));
                return std::nullopt;
            },
        };
        return *rt::RememberSaveable<std::shared_ptr<ScrollState>>([&] { return std::make_shared<ScrollState>(initial); }, saver);
    }

    LazyListState& RememberLazyListState(int i, int o) { return RememberLazy<LazyListState>(i, o); }
    LazyGridState& RememberLazyGridState(int i, int o) { return RememberLazy<LazyGridState>(i, o); }
    LazyStaggeredGridState& RememberLazyStaggeredGridState(int i, int o) { return RememberLazy<LazyStaggeredGridState>(i, o); }

    PagerState& RememberPagerState(int initialPage, float initialFraction, std::function<int()> pageCount) {
        // [currentPage, currentPageOffsetFraction in -0.5..0.5, pageCount]; restored pages take
        // the page count lambda of the composition that restores them.
        auto saver = rt::ListSaver<std::shared_ptr<PagerState>>(
            [](const std::shared_ptr<PagerState>& s) {
                return rt::SavedValue::List{ s->CurrentPage(), double(std::clamp(s->CurrentPageOffsetFraction(), -0.5f, 0.5f)), s->PageCount() };
            },
            [pageCount](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<PagerState>> {
                if (l.size() < 3) return std::nullopt;
                return std::make_shared<PagerState>(IntAt(l, 0), float(NumberAt(l, 1)), pageCount);
            });
        auto& s = rt::RememberSaveable<std::shared_ptr<PagerState>>(
            [&] { return std::make_shared<PagerState>(initialPage, initialFraction, pageCount); }, saver);
        s->SetPageCountProvider(std::move(pageCount));
        return *s;
    }

    TextFieldState& RememberTextFieldState(std::string_view initialText) {
        auto saver = rt::ListSaver<std::shared_ptr<TextFieldState>>(
            [](const std::shared_ptr<TextFieldState>& s) {
                const auto& v = s->Peek();
                return rt::SavedValue::List{ ToUtf8(v.text), v.selection.start, v.selection.end };
            },
            [](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<TextFieldState>> {
                const auto* text = l.empty() ? nullptr : std::get_if<std::string>(&l[0].v);
                if (!text) return std::nullopt;
                auto s = std::make_shared<TextFieldState>(*text);
                s->SetSelection(IntAt(l, 1), IntAt(l, 2));
                return s;
            });
        const std::string initial(initialText);
        return *rt::RememberSaveable<std::shared_ptr<TextFieldState>>([&] { return std::make_shared<TextFieldState>(initial); }, saver);
    }

}
