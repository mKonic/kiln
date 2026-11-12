#include "kiln/foundation/text/input/Transformations.h"
#include "kiln/text/CaseMapping.h"

#include <cwctype>
#include <stdexcept>

namespace kiln::foundation {

    KeyboardOptions KeyboardOptions::FillUnspecifiedValuesWith(const KeyboardOptions& o) const {
        KeyboardOptions r = *this;
        if (!r.capitalization) r.capitalization = o.capitalization;
        if (!r.autoCorrectEnabled) r.autoCorrectEnabled = o.autoCorrectEnabled;
        if (!r.keyboardType) r.keyboardType = o.keyboardType;
        if (!r.imeAction) r.imeAction = o.imeAction;
        if (!r.showKeyboardOnFocus) r.showKeyboardOnFocus = o.showKeyboardOnFocus;
        if (r.hintLocales.empty()) r.hintLocales = o.hintLocales;
        if (!r.platformImeOptions) r.platformImeOptions = o.platformImeOptions;
        return r;
    }

    ui::ImeOptions KeyboardOptions::ToImeOptions(bool singleLine) const {
        ui::ImeOptions out;
        out.capitalization = capitalization.value_or(ui::Capitalization::None);
        out.autoCorrect = autoCorrectEnabled.value_or(true);
        out.keyboardType = keyboardType.value_or(ui::KeyboardType::Text);
        out.imeAction = imeAction.value_or(ui::ImeAction::Default);
        out.singleLine = singleLine;
        out.hintLocales = hintLocales;
        out.platformImeOptions = platformImeOptions;
        return out;
    }

    namespace {
        struct Chain final : InputTransformation {
            InputTransformationPtr first, second;
            std::optional<foundation::KeyboardOptions> KeyboardOptions() const override {
                const auto a = first->KeyboardOptions(), b = second->KeyboardOptions();
                if (b) return a ? b->FillUnspecifiedValuesWith(*a) : *b;
                return a;
            }
            void ApplySemantics(ui::SemanticsConfiguration& c) const override { first->ApplySemantics(c); second->ApplySemantics(c); }
            void TransformInput(TextFieldBuffer& b) const override { first->TransformInput(b); second->TransformInput(b); }
        };
        struct ByValueTransformation final : InputTransformation {
            std::function<std::u16string(std::u16string_view, std::u16string_view)> fn;
            void TransformInput(TextFieldBuffer& b) const override {
                const std::u16string proposed = b.AsCharSequence();
                const std::u16string accepted = fn(b.OriginalText(), proposed);
                // Returning the original rejects the edit; returning the proposal keeps it.
                if (accepted == proposed) return;
                if (accepted == b.OriginalText()) { b.RevertAllChanges(); return; }
                b.SetTextIfChanged(accepted);
            }
        };
        struct AllCapsTransformation final : InputTransformation {
            std::string locale;
            std::optional<foundation::KeyboardOptions> KeyboardOptions() const override {
                foundation::KeyboardOptions o;
                o.capitalization = ui::Capitalization::Characters;
                return o;
            }
            void TransformInput(TextFieldBuffer& b) const override {
                for (int i = 0; i < b.Changes().ChangeCount(); ++i) {
                    const TextRange r = b.Changes().Range(i);
                    if (r.Collapsed()) continue;
                    const std::u16string part = b.AsCharSequence().substr(std::size_t(r.Min()), std::size_t(r.Length()));
                    b.Replace(r.Min(), r.Max(), ToUpperCase(part, locale));
                }
            }
        };
        struct MaxLengthTrimTransformation final : InputTransformation {
            int maxLength;
            void ApplySemantics(ui::SemanticsConfiguration& c) const override { c.maxTextLength = maxLength; }
            void TransformInput(TextFieldBuffer& b) const override {
                if (b.Length() <= maxLength) return;
                int excess = b.Length() - maxLength;
                for (int i = b.Changes().ChangeCount() - 1; i >= 0 && excess > 0; --i) {
                    const TextRange r = b.Changes().Range(i);
                    const int inserted = r.Length();
                    if (inserted > 0) {
                        const int del = std::min(excess, inserted);
                        b.Delete(r.Max() - del, r.Max());
                        excess -= del;
                    }
                }
                if (b.Length() > maxLength) b.Delete(maxLength, b.Length());
            }
        };
        struct MaxLengthRejectTransformation final : InputTransformation {
            int maxLength;
            void ApplySemantics(ui::SemanticsConfiguration& c) const override { c.maxTextLength = maxLength; }
            void TransformInput(TextFieldBuffer& b) const override { if (b.Length() > maxLength) b.RevertAllChanges(); }
        };
        struct LambdaInput final : InputTransformation {
            std::function<void(TextFieldBuffer&)> fn;
            void TransformInput(TextFieldBuffer& b) const override { fn(b); }
        };
        struct LambdaOutput final : OutputTransformation {
            std::function<void(TextFieldBuffer&)> fn;
            void TransformOutput(TextFieldBuffer& b) const override { fn(b); }
        };
        struct Mask final : CodepointTransformation {
            char32_t character;
            char32_t Transform(int, char32_t) const override { return character; }
        };
        struct SingleLineCodepoints final : CodepointTransformation {
            char32_t Transform(int, char32_t cp) const override {
                if (cp == U'\n') return U' ';
                if (cp == U'\r') return 0xFEFF;
                return cp;
            }
        };

    }

    InputTransformationPtr Then(InputTransformationPtr first, InputTransformationPtr next) {
        if (!first) return next;
        if (!next) return first;
        auto c = std::make_shared<Chain>();
        c->first = std::move(first);
        c->second = std::move(next);
        return c;
    }
    InputTransformationPtr ByValue(std::function<std::u16string(std::u16string_view, std::u16string_view)> fn) {
        auto t = std::make_shared<ByValueTransformation>();
        t->fn = std::move(fn);
        return t;
    }
    InputTransformationPtr AllCaps(std::string localeTag) {
        auto t = std::make_shared<AllCapsTransformation>();
        t->locale = std::move(localeTag);
        return t;
    }
    InputTransformationPtr MaxLengthTrim(int maxLength) {
        if (maxLength < 0) throw std::invalid_argument("maxLength must be at least zero");
        auto t = std::make_shared<MaxLengthTrimTransformation>();
        t->maxLength = maxLength;
        return t;
    }
    InputTransformationPtr MaxLengthReject(int maxLength) {
        if (maxLength < 0) throw std::invalid_argument("maxLength must be at least zero");
        auto t = std::make_shared<MaxLengthRejectTransformation>();
        t->maxLength = maxLength;
        return t;
    }
    InputTransformationPtr InputTransformationOf(std::function<void(TextFieldBuffer&)> fn) {
        auto t = std::make_shared<LambdaInput>();
        t->fn = std::move(fn);
        return t;
    }
    OutputTransformationPtr OutputTransformationOf(std::function<void(TextFieldBuffer&)> fn) {
        auto t = std::make_shared<LambdaOutput>();
        t->fn = std::move(fn);
        return t;
    }
    OutputTransformationPtr SingleLineOutputTransformation(OutputTransformationPtr inner) {
        return OutputTransformationOf([inner](TextFieldBuffer& b) {
            if (inner) inner->TransformOutput(b);
            for (int i = 0; i < b.Length(); ++i) {
                const char16_t c = b.CharAt(i);
                if (c == u'\n') b.Replace(i, i + 1, u" ");
                if (c == u'\r') b.Replace(i, i + 1, u"﻿");
            }
        });
    }
    CodepointTransformationPtr MaskCodepoints(char32_t character) {
        auto t = std::make_shared<Mask>();
        t->character = character;
        return t;
    }
    CodepointTransformationPtr SingleLineCodepointTransformation() { return std::make_shared<SingleLineCodepoints>(); }

    std::optional<std::u16string> ToVisualText(std::u16string_view text, const CodepointTransformation& t, OffsetMappingCalculator& mapping) {
        std::u16string out;
        bool changed = false;
        int codePointIndex = 0;
        for (std::size_t i = 0; i < text.size();) {
            char32_t cp = text[i];
            int count = 1;
            if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < text.size() && text[i + 1] >= 0xDC00 && text[i + 1] < 0xE000) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
                count = 2;
            }
            const char32_t next = t.Transform(codePointIndex, cp);
            if (next != cp) {
                changed = true;
                mapping.RecordEditOperation(int(out.size()), int(out.size()) + count, next >= 0x10000 ? 2 : 1);
            }
            if (next >= 0x10000) {
                const char32_t v = next - 0x10000;
                out.push_back(char16_t(0xD800 + (v >> 10)));
                out.push_back(char16_t(0xDC00 + (v & 0x3FF)));
            } else {
                out.push_back(char16_t(next));
            }
            i += std::size_t(count);
            ++codePointIndex;
        }
        if (!changed) return std::nullopt;
        return out;
    }

    std::u16string ToUpperCase(std::u16string_view text, const std::string& localeTag) { return text::MapCase(text, localeTag, text::CaseOp::Upper); }

}
