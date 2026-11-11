// ExposedDropdownMenuSamples.kt
#include "ExposedDropdownMenuSamples.h"
#include "Common.h"

#include <kiln/foundation/text/input/TextFieldBuffer.h>
#include <kiln/material3/ExposedDropdownMenu.h>
#include <kiln/material3/Menu.h>
#include <kiln/material3/TextField.h>
#include <kiln/material3/TextFieldDefaults.h>
#include <kiln/text/AnnotatedString.h>

#include <cctype>
#include <optional>
#include <vector>

namespace catalog::samples {
    namespace MD = m3::MenuDefaults;
    namespace ED = m3::ExposedDropdownMenuDefaults;
    using kiln::text::AnnotatedString;

    namespace {
        const std::vector<std::string> SampleData = {
            "Android", "Base", "Cupcake", "Donut", "Eclair", "Froyo", "Gingerbread", "Honeycomb", "Ice Cream Sandwich",
            "Jelly Bean", "KitKat", "Lollipop", "Marshmallow", "Nougat", "Oreo", "Pie", "Quince Tart", "Red Velvet Cake",
            "Snow Cone", "Tiramisu", "Upside Down Cake", "Vanilla Ice Cream", "W123", "X456", "Y789", "Z000",
        };

        // The options that contain `needle` as a subsequence (ignoring case), with the
        // subsequence underlined.
        std::vector<AnnotatedString> FilteredBy(const std::vector<std::string>& options, std::u16string_view needle) {
            std::vector<AnnotatedString> out;
            for (const std::string& haystack : options) {
                kiln::text::AnnotatedStringBuilder b;
                std::size_t i = 0;
                bool found = true;
                for (char16_t c : needle) {
                    const std::size_t start = i;
                    while (i < haystack.size() && std::tolower((unsigned char)haystack[i]) != (c < 128 ? std::tolower(int(c)) : -1)) i++;
                    if (i >= haystack.size()) { found = false; break; }
                    b.Append(std::string_view(haystack).substr(start, i - start));
                    kiln::text::SpanStyle underline;
                    underline.textDecoration = kiln::text::TextDecoration::Underline;
                    b.WithStyle(underline, [&] { b.Append(std::string_view(haystack).substr(i, 1)); });
                    i++;
                }
                if (!found) continue;
                b.Append(std::string_view(haystack).substr(i));
                out.push_back(b.ToAnnotatedString());
            }
            return out;
        }

        void BodyLarge(const AnnotatedString& text) {
            m3::TextOptions o;
            o.style = m3::MaterialTheme::typography().bodyLarge;
            m3::Text(text, {}, o);
        }

        m3::TextFieldOptions FieldOptions(const m3::ExposedDropdownMenuBoxScope& scope, bool expanded, bool readOnly) {
            m3::TextFieldOptions o;
            o.readOnly = readOnly;
            o.lineLimits = TextFieldLineLimits::SingleLine();
            o.label = [](const m3::TextFieldLabelScope&) { m3::Text("Label"); };
            o.trailingIcon = [scope, expanded, readOnly] {
                if (readOnly) ED::TrailingIcon(expanded);
                // An editable field's trailing icon is a SecondaryEditable anchor, so that
                // accessibility services can open the menu without typing.
                else ED::TrailingIcon(expanded, scope.MenuAnchor({}, m3::ExposedDropdownMenuAnchorType::SecondaryEditable));
            };
            o.colors = ED::textFieldColors();
            return o;
        }

        // The editable samples' menu: up to 280 dp of the filtered options.
        void FilteredMenu(const m3::ExposedDropdownMenuBoxScope& scope, bool expanded, kiln::rt::State<bool>& allowExpanded,
                          std::vector<AnnotatedString> options, std::function<void(const AnnotatedString&)> onPick) {
            m3::ExposedDropdownMenu(scope, expanded, [&allowExpanded] { allowExpanded.Set(false); }, HeightIn({}, Dp::Unspecified(), 280_dp), {},
                                    [options, onPick] {
                                        for (const AnnotatedString& option : options) {
                                            m3::DropdownMenuItemOptions o;
                                            o.contentPadding = ED::ItemContentPadding();
                                            m3::DropdownMenuItem([option] { BodyLarge(option); }, [onPick, option] { onPick(option); }, {}, o);
                                        }
                                    });
        }
    }

    void ExposedDropdownMenuSample() {
        static const std::vector<std::string> options(SampleData.begin(), SampleData.begin() + 5);
        auto& expanded = RememberState(false);
        auto& textFieldState = RememberTextFieldState(options[0]);
        auto& checkedIndex = RememberSaveableState(0);

        m3::ExposedDropdownMenuBox(expanded.Get(), [&expanded](bool e) { expanded.Set(e); }, {}, [&expanded, &textFieldState, &checkedIndex](const m3::ExposedDropdownMenuBoxScope& scope) {
            const bool isExpanded = expanded.Get();
            m3::TextField(textFieldState, scope.MenuAnchor({}, m3::ExposedDropdownMenuAnchorType::PrimaryNotEditable),
                          FieldOptions(scope, isExpanded, true));
            m3::ExposedDropdownMenuOptions menu;
            menu.containerColor = MD::groupStandardContainerColor();
            menu.shape = MD::standaloneGroupShape();
            m3::ExposedDropdownMenu(scope, isExpanded, [&expanded] { expanded.Set(false); }, {}, menu, [&expanded, &textFieldState, &checkedIndex] {
                const int optionCount = int(options.size());
                for (int index = 0; index < optionCount; index++) {
                    m3::MenuItemOptions o;
                    o.selectedLeadingIcon = [] { m3::Icon(Icons::Filled::Check(), std::nullopt, Size({}, MD::LeadingIconSize)); };
                    o.contentPadding = ED::ItemContentPadding();
                    m3::SelectableDropdownMenuItem(
                        index == checkedIndex.Get(),
                        [&, index] {
                            textFieldState.SetTextAndPlaceCursorAtEnd(options[std::size_t(index)]);
                            checkedIndex.Set(index);
                            expanded.Set(false);
                        },
                        [index] { TextStyled(options[std::size_t(index)], m3::MaterialTheme::typography().bodyLarge); }, MD::itemShape(index, optionCount),
                        {}, o);
                }
            });
        });
    }

    void EditableExposedDropdownMenuSample() {
        auto& textFieldState = RememberTextFieldState();
        // The text typed into the field filters the options (by subsequence).
        const std::vector<AnnotatedString> filteredOptions = FilteredBy(SampleData, textFieldState.Text());
        auto& allowExpanded = RememberState(false);
        const bool expanded = allowExpanded.Get() && !filteredOptions.empty();

        m3::ExposedDropdownMenuBox(expanded, [&allowExpanded](bool e) { allowExpanded.Set(e); }, {}, [=, &textFieldState, &allowExpanded](const m3::ExposedDropdownMenuBoxScope& scope) {
            m3::TextField(textFieldState, scope.MenuAnchor(Width({}, m3::TextFieldDefaults::MinWidth), m3::ExposedDropdownMenuAnchorType::PrimaryEditable),
                          FieldOptions(scope, expanded, false));
            FilteredMenu(scope, expanded, allowExpanded, filteredOptions, [&textFieldState, &allowExpanded](const AnnotatedString& option) {
                textFieldState.SetTextAndPlaceCursorAtEnd(option.Text());
                allowExpanded.Set(false);
            });
        });
    }

    void MultiAutocompleteExposedDropdownMenuSample() {
        // The range of the comma-separated token around the cursor.
        auto currentTokenRange = [](const TextFieldState& state) -> std::optional<TextRange> {
            const TextRange selection = state.Selection();
            if (!selection.Collapsed()) return std::nullopt;
            const std::u16string& text = state.Text();
            const int cursor = selection.start;
            int start = cursor;
            while (start > 0 && text[std::size_t(start - 1)] != u',') start--;
            while (start < cursor && text[std::size_t(start)] == u' ') start++;
            int end = cursor;
            while (end < int(text.size()) && text[std::size_t(end)] != u',') end++;
            return TextRange{ start, end };
        };
        auto replaceThenAddComma = [](TextFieldState& state, int start, int end, const std::string& text) {
            state.Edit([&](TextFieldBuffer& b) {
                const int before = b.Length();
                b.Replace(start, end, text);
                const int afterText = end + (b.Length() - before);
                if (afterText == b.Length() || b.CharAt(afterText) != u',') {
                    b.Insert(afterText, u", ");
                    b.PlaceCursorBeforeCharAt(afterText + 2);
                } else {
                    b.PlaceCursorAfterCharAt(afterText);
                }
            });
        };

        auto& textFieldState = RememberTextFieldState();
        const std::optional<TextRange> tokenSelection = currentTokenRange(textFieldState);
        const std::u16string tokenAtCursor =
            tokenSelection ? textFieldState.Text().substr(std::size_t(tokenSelection->start), std::size_t(tokenSelection->end - tokenSelection->start))
                           : std::u16string();
        const bool blank = tokenAtCursor.find_first_not_of(u" \t\n") == std::u16string::npos;
        const std::vector<AnnotatedString> filteredOptions = blank ? std::vector<AnnotatedString>() : FilteredBy(SampleData, tokenAtCursor);
        auto& allowExpanded = RememberState(false);
        const bool expanded = allowExpanded.Get() && !filteredOptions.empty();

        m3::ExposedDropdownMenuBox(expanded, [&allowExpanded](bool e) { allowExpanded.Set(e); }, {}, [=, &textFieldState, &allowExpanded](const m3::ExposedDropdownMenuBoxScope& scope) {
            m3::TextField(textFieldState, scope.MenuAnchor(Width({}, m3::TextFieldDefaults::MinWidth), m3::ExposedDropdownMenuAnchorType::PrimaryEditable),
                          FieldOptions(scope, expanded, false));
            FilteredMenu(scope, expanded, allowExpanded, filteredOptions, [&textFieldState, tokenSelection, replaceThenAddComma](const AnnotatedString& option) {
                if (tokenSelection) replaceThenAddComma(textFieldState, tokenSelection->start, tokenSelection->end, option.Text());
            });
        });
    }
}
