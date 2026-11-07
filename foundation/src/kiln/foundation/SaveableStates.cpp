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
        // TextFieldState.Saver: text, selection and the undo history (staging, undo and redo
        // operations, each index, pre/post text, selections and time).
        auto saveOp = [](rt::SavedValue::List& l, const TextUndoOperation& op) {
            l.insert(l.end(), { op.index, ToUtf8(op.preText), ToUtf8(op.postText), op.preSelection.start, op.preSelection.end,
                                op.postSelection.start, op.postSelection.end, op.timeInMillis, op.canMerge ? 1 : 0 });
        };
        auto saver = rt::ListSaver<std::shared_ptr<TextFieldState>>(
            [saveOp](const std::shared_ptr<TextFieldState>& s) {
                const auto saved = s->Save();
                rt::SavedValue::List l{ ToUtf8(saved.text), saved.selection.start, saved.selection.end,
                                        saved.undo.staging ? 1 : 0, int(saved.undo.undo.size()), int(saved.undo.redo.size()) };
                if (saved.undo.staging) saveOp(l, *saved.undo.staging);
                for (const auto& op : saved.undo.undo) saveOp(l, op);
                for (const auto& op : saved.undo.redo) saveOp(l, op);
                return l;
            },
            [](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<TextFieldState>> {
                const auto* text = l.empty() ? nullptr : std::get_if<std::string>(&l[0].v);
                if (!text) return std::nullopt;
                auto s = std::make_shared<TextFieldState>(*text);
                TextFieldState::Saved saved{ ToUtf16(*text), { IntAt(l, 1), IntAt(l, 2) }, {} };
                std::size_t i = 6;
                auto readOp = [&]() -> std::optional<TextUndoOperation> {
                    if (i + 9 > l.size()) return std::nullopt;
                    auto str = [&](std::size_t k) { const auto* v = std::get_if<std::string>(&l[k].v); return v ? ToUtf16(*v) : std::u16string(); };
                    TextUndoOperation op{ IntAt(l, i), str(i + 1), str(i + 2), { IntAt(l, i + 3), IntAt(l, i + 4) },
                                          { IntAt(l, i + 5), IntAt(l, i + 6) }, std::int64_t(NumberAt(l, i + 7)), IntAt(l, i + 8) != 0 };
                    i += 9;
                    return op;
                };
                if (l.size() >= 6) {
                    if (IntAt(l, 3)) saved.undo.staging = readOp();
                    for (int k = 0, n = IntAt(l, 4); k < n; ++k) if (auto op = readOp()) saved.undo.undo.push_back(*op);
                    for (int k = 0, n = IntAt(l, 5); k < n; ++k) if (auto op = readOp()) saved.undo.redo.push_back(*op);
                }
                s->Restore(saved);
                return s;
            });
        const std::string initial(initialText);
        return *rt::RememberSaveable<std::shared_ptr<TextFieldState>>([&] { return std::make_shared<TextFieldState>(initial); }, saver);
    }

}
