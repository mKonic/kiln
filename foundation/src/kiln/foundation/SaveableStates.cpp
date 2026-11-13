// remember*State with their savers (ScrollState.Saver, LazyListState.Saver, LazyGridState.Saver,
// LazyStaggeredGridState.Saver, PagerState's DefaultPagerState.Saver, TextFieldState.Saver).

#include "kiln/foundation/Lazy.h"
#include <kiln/Function.h>
#include "kiln/foundation/Pager.h"
#include "kiln/foundation/Scroll.h"
#include "kiln/foundation/text/TextFieldState.h"
#include "kiln/runtime/Saveable.h"
#include "kiln/text/Savers.h"

#include <algorithm>

namespace kiln::foundation {

    namespace {
        int IntAt(const rt::SavedValue::List& l, std::size_t i) { return i < l.size() ? int(rt::SavedNumber(l[i]).value_or(0)) : 0; }
        double NumberAt(const rt::SavedValue::List& l, std::size_t i) { return i < l.size() ? rt::SavedNumber(l[i]).value_or(0) : 0; }

        // A lazy state as [firstVisibleItemIndex, firstVisibleItemScrollOffset].
        template<typename S>
        S& RememberLazy(int index, int offset, rt::Key key) {
            auto saver = rt::ListSaver<std::shared_ptr<S>>(
                [](const std::shared_ptr<S>& s) {
                    return rt::SavedValue::List{ s->FirstVisibleItemIndex(), s->FirstVisibleItemScrollOffset() };
                },
                [](const rt::SavedValue::List& l) -> std::optional<std::shared_ptr<S>> {
                    if (l.size() < 2) return std::nullopt;
                    return std::make_shared<S>(IntAt(l, 0), IntAt(l, 1));
                });
            return *rt::RememberSaveable<std::shared_ptr<S>>([&] { return std::make_shared<S>(index, offset); }, saver, {}, key);
        }
    }

    ScrollState& RememberScrollState(int initial, rt::Key key) {
        rt::Saver<std::shared_ptr<ScrollState>> saver{
            [](const std::shared_ptr<ScrollState>& s) { return rt::SavedValue(s->Value()); },
            [](const rt::SavedValue& v) -> std::optional<std::shared_ptr<ScrollState>> {
                if (auto n = rt::SavedNumber(v)) return std::make_shared<ScrollState>(int(*n));
                return std::nullopt;
            },
        };
        return *rt::RememberSaveable<std::shared_ptr<ScrollState>>([&] { return std::make_shared<ScrollState>(initial); }, saver, {}, key);
    }

    LazyListState& RememberLazyListState(int i, int o, rt::Key key) { return RememberLazy<LazyListState>(i, o, key); }
    LazyGridState& RememberLazyGridState(int i, int o, rt::Key key) { return RememberLazy<LazyGridState>(i, o, key); }
    LazyStaggeredGridState& RememberLazyStaggeredGridState(int i, int o, rt::Key key) { return RememberLazy<LazyStaggeredGridState>(i, o, key); }

    PagerState& RememberPagerState(int initialPage, float initialFraction, kiln::Function<int()> pageCount, rt::Key key) {
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
            [&] { return std::make_shared<PagerState>(initialPage, initialFraction, pageCount); }, saver, {}, key);
        s->SetPageCountProvider(std::move(pageCount));
        return *s;
    }

    TextFieldState& RememberTextFieldState(std::string_view initialText, std::optional<TextRange> initialSelection, rt::Key key) {
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
                // The styles (TextStyleBuffer.Saver): count, then kind (0 span, 1 paragraph),
                // style, start, end, start / end expands each.
                const auto infos = saved.styles ? saved.styles->Buffer().GetAllStyleInfo() : std::vector<TextStyleBuffer::StyleInfo>{};
                l.push_back(int(infos.size()));
                for (const auto& info : infos) {
                    if (const auto* span = std::get_if<text::SpanStyle>(&info.item)) {
                        l.push_back(0);
                        l.push_back(text::SpanStyleSaver().save(*span));
                    } else if (const auto* para = std::get_if<text::ParagraphStyle>(&info.item)) {
                        l.push_back(1);
                        l.push_back(text::ParagraphStyleSaver().save(*para));
                    } else {
                        l.push_back(-1);
                        l.push_back(rt::SavedValue{});
                    }
                    l.insert(l.end(), { info.start, info.end, info.policy.startExpands ? 1 : 0, info.policy.endExpands ? 1 : 0 });
                }
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
                if (i < l.size()) {
                    TextStyleBuffer styles;
                    const int n = IntAt(l, i++);
                    for (int k = 0; k < n && i + 6 <= l.size(); ++k, i += 6) {
                        const int kind = IntAt(l, i);
                        std::optional<text::Annotation> item;
                        if (kind == 0) { if (auto st = text::SpanStyleSaver().restore(l[i + 1])) item = text::Annotation(*st); }
                        else if (kind == 1) { if (auto st = text::ParagraphStyleSaver().restore(l[i + 1])) item = text::Annotation(*st); }
                        if (item) styles.AddStyle(*item, { IntAt(l, i + 2), IntAt(l, i + 3), IntAt(l, i + 4) != 0, IntAt(l, i + 5) != 0 });
                    }
                    if (!styles.Empty()) saved.styles = std::make_shared<const TextFieldTextStyles>(styles.ToImmutable(), int(saved.text.size()));
                }
                s->Restore(saved);
                return s;
            });
        const std::string initial(initialText);
        return *rt::RememberSaveable<std::shared_ptr<TextFieldState>>([&] { return std::make_shared<TextFieldState>(initial, initialSelection); }, saver, {}, key);
    }

}
