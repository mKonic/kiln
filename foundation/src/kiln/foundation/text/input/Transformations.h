#pragma once

// What a text field does to text on the way in and on the way out (InputTransformation,
// OutputTransformation, CodepointTransformation): input transformations see each user edit and
// may change or reject it; output transformations change only what is shown (offsets map across
// them); codepoint transformations replace characters one for one (password masks, single line).

#include "kiln/foundation/text/input/TextFieldBuffer.h"
#include "kiln/ui/Semantics.h"
#include "kiln/ui/TextInput.h"

#include <climits>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kiln::foundation {

    // KeyboardOptions: what the keyboard should offer; unset values take the defaults (or another
    // KeyboardOptions' values, fillUnspecifiedValuesWith).
    struct KeyboardOptions {
        std::optional<ui::Capitalization> capitalization;
        std::optional<bool> autoCorrectEnabled;
        std::optional<ui::KeyboardType> keyboardType;
        std::optional<ui::ImeAction> imeAction;
        std::optional<bool> showKeyboardOnFocus;
        std::vector<std::string> hintLocales;
        KeyboardOptions FillUnspecifiedValuesWith(const KeyboardOptions& o) const;
        // The options with the defaults for what is unset (and single line when asked).
        ui::ImeOptions ToImeOptions(bool singleLine) const;
        bool operator==(const KeyboardOptions&) const = default;
    };

    class InputTransformation {
    public:
        virtual ~InputTransformation() = default;
        // Keyboard options this transformation asks for (merged under the field's own).
        virtual std::optional<foundation::KeyboardOptions> KeyboardOptions() const { return std::nullopt; }
        virtual void ApplySemantics(ui::SemanticsConfiguration&) const {}
        virtual void TransformInput(TextFieldBuffer& buffer) const = 0;
    };
    using InputTransformationPtr = std::shared_ptr<const InputTransformation>;

    // then: `first`, then `next`. byValue: from the current and the proposed text. allCaps:
    // inserted text upper-cased for a locale. maxLengthTrim: drops inserted characters past
    // maxLength; maxLengthReject: rejects the whole edit.
    InputTransformationPtr Then(InputTransformationPtr first, InputTransformationPtr next);
    InputTransformationPtr ByValue(std::function<std::u16string(std::u16string_view current, std::u16string_view proposed)> fn);
    InputTransformationPtr AllCaps(std::string localeTag = {});
    InputTransformationPtr MaxLengthTrim(int maxLength);
    InputTransformationPtr MaxLengthReject(int maxLength);
    InputTransformationPtr InputTransformationOf(std::function<void(TextFieldBuffer&)> fn);

    class OutputTransformation {
    public:
        virtual ~OutputTransformation() = default;
        virtual void TransformOutput(TextFieldBuffer& buffer) const = 0;
    };
    using OutputTransformationPtr = std::shared_ptr<const OutputTransformation>;
    OutputTransformationPtr OutputTransformationOf(std::function<void(TextFieldBuffer&)> fn);
    // SingleLineOutputTransformation: line feeds show as spaces, carriage returns as nothing.
    OutputTransformationPtr SingleLineOutputTransformation(OutputTransformationPtr inner = nullptr);

    class CodepointTransformation {
    public:
        virtual ~CodepointTransformation() = default;
        virtual char32_t Transform(int codepointIndex, char32_t codepoint) const = 0;
    };
    using CodepointTransformationPtr = std::shared_ptr<const CodepointTransformation>;
    CodepointTransformationPtr MaskCodepoints(char32_t character);
    CodepointTransformationPtr SingleLineCodepointTransformation();
    // toVisualText: the text with each code point transformed, recording the edits in `mapping`
    // (nullopt: nothing changed).
    std::optional<std::u16string> ToVisualText(std::u16string_view text, const CodepointTransformation& t, OffsetMappingCalculator& mapping);

    // Upper-casing for AllCaps (String.toUpperCase(Locale) on Android). The default handles the
    // characters with a one-to-one simple upper case mapping.
    // (The host's locale-aware mapping is text::SetCaseMapper.)
    std::u16string ToUpperCase(std::u16string_view text, const std::string& localeTag);

    enum class TextObfuscationMode : std::uint8_t { Visible, RevealLastTyped, Hidden, System };

    // TextFieldLineLimits: one line (scrolls horizontally), or between min and max lines.
    struct TextFieldLineLimits {
        bool singleLine = false;
        int minHeightInLines = 1, maxHeightInLines = INT_MAX;
        static TextFieldLineLimits SingleLine() { return { true, 1, 1 }; }
        static TextFieldLineLimits MultiLine(int minLines = 1, int maxLines = INT_MAX) { return { false, minLines, maxLines }; }
        bool operator==(const TextFieldLineLimits&) const = default;
    };

}
