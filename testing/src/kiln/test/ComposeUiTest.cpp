#include "kiln/test/ComposeUiTest.h"

#include "kiln/runtime/Composition.h"
#include "kiln/runtime/Task.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <format>
#include <sstream>

namespace kiln::test {

    namespace {
        std::string Lower(std::string s) {
            for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
            return s;
        }
        bool Equal(const std::string& a, const std::string& b, bool ignoreCase) { return ignoreCase ? Lower(a) == Lower(b) : a == b; }
        bool Contains(const std::string& a, const std::string& b, bool ignoreCase) {
            return ignoreCase ? Lower(a).find(Lower(b)) != std::string::npos : a.find(b) != std::string::npos;
        }
        std::string Quote(const std::vector<std::string>& v) {
            std::string out = "[";
            for (std::size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + v[i];
            return out + "]";
        }
        const char* RoleName(ui::Role r) {
            switch (r) {
                case ui::Role::Button: return "Button";
                case ui::Role::Checkbox: return "Checkbox";
                case ui::Role::Switch: return "Switch";
                case ui::Role::RadioButton: return "RadioButton";
                case ui::Role::Tab: return "Tab";
                case ui::Role::Image: return "Image";
                case ui::Role::DropdownList: return "DropdownList";
                case ui::Role::ValuePicker: return "ValuePicker";
                case ui::Role::Carousel: return "Carousel";
                default: return "None";
            }
        }
        SemanticsMatcher Key(std::string name, kiln::Function<bool(const ui::SemanticsConfiguration&)> has) {
            return { std::move(name), [has = std::move(has)](const NodeRef& n) { return has(n.node->config); } };
        }

        // Every node of the tree (root first, depth first), with its ancestors.
        void Walk(const ui::SemanticsNode& n, std::vector<const ui::SemanticsNode*>& path, const kiln::Function<void(const NodeRef&)>& visit) {
            visit({ &n, path });
            path.push_back(&n);
            for (const auto& c : n.children) Walk(c, path, visit);
            path.pop_back();
        }
        std::vector<NodeRef> All(const ui::SemanticsNode& root, const SemanticsMatcher& m) {
            std::vector<NodeRef> out;
            std::vector<const ui::SemanticsNode*> path;
            Walk(root, path, [&](const NodeRef& r) { if (m.Matches(r)) out.push_back(r); });
            return out;
        }
        void Print(const ui::SemanticsNode& n, int depth, int maxDepth, std::string& out) {
            const std::string pad(std::size_t(depth) * 2, ' ');
            const auto& c = n.config;
            const auto& b = n.boundsInRoot;
            out += pad + (n.id == -1 ? std::string("Node #root") : std::format("Node #{}", n.id)) +
                   std::format(" at (l={}, t={}, r={}, b={})px", b.left, b.top, b.right, b.bottom);
            if (c.testTag) out += ", Tag: '" + *c.testTag + "'";
            out += "\n";
            auto line = [&](const std::string& s) { out += pad + " " + s + "\n"; };
            if (c.role != ui::Role::None) line(std::string("Role = '") + RoleName(c.role) + "'");
            if (!c.text.empty()) line("Text = '" + Quote(c.text) + "'");
            if (c.editableText) line("EditableText = '" + *c.editableText + "'");
            if (!c.contentDescription.empty()) line("ContentDescription = '" + Quote(c.contentDescription) + "'");
            if (c.stateDescription) line("StateDescription = '" + *c.stateDescription + "'");
            if (c.toggleableState) line(std::string("ToggleableState = '") + (*c.toggleableState == 1 ? "On" : *c.toggleableState == 0 ? "Off" : "Indeterminate") + "'");
            if (c.selected) line(std::string("Selected = '") + (*c.selected ? "true" : "false") + "'");
            if (c.focused) line(std::string("Focused = '") + (*c.focused ? "true" : "false") + "'");
            if (c.disabled) line("Disabled = 'kotlin.Unit'");
            if (c.heading) line("Heading = 'kotlin.Unit'");
            std::vector<std::string> actions;
            if (c.onClick) actions.push_back("OnClick");
            if (c.onLongClick) actions.push_back("OnLongClick");
            if (c.scrollBy) actions.push_back("ScrollBy");
            if (c.scrollToIndex) actions.push_back("ScrollToIndex");
            if (c.setText) actions.push_back("SetText");
            if (c.insertTextAtCursor) actions.push_back("InsertTextAtCursor");
            if (c.setSelection) actions.push_back("SetTextSelection");
            if (c.requestFocus) actions.push_back("RequestFocus");
            if (c.onImeAction) actions.push_back("PerformImeAction");
            if (c.setProgress) actions.push_back("SetProgress");
            if (!actions.empty()) line("Actions = " + Quote(actions));
            if (c.mergeDescendants) line("MergeDescendants = 'true'");
            if (depth < maxDepth)
                for (const auto& ch : n.children) Print(ch, depth + 1, maxDepth, out);
        }

        std::string Describe(const std::vector<NodeRef>& nodes) {
            if (nodes.empty()) return "Nodes found: none\n";
            std::string out = std::format("Found {} nodes:\n", nodes.size());
            for (std::size_t i = 0; i < nodes.size(); ++i) {
                out += std::format("{}) ", i + 1);
                Print(*nodes[i].node, 0, 0, out);
            }
            return out;
        }
    }

    // --- matchers ------------------------------------------------------------------------------

    SemanticsMatcher operator&&(const SemanticsMatcher& a, const SemanticsMatcher& b) {
        return { "(" + a.Description() + ") && (" + b.Description() + ")", [a, b](const NodeRef& n) { return a.Matches(n) && b.Matches(n); } };
    }
    SemanticsMatcher operator||(const SemanticsMatcher& a, const SemanticsMatcher& b) {
        return { "(" + a.Description() + ") || (" + b.Description() + ")", [a, b](const NodeRef& n) { return a.Matches(n) || b.Matches(n); } };
    }
    SemanticsMatcher operator!(const SemanticsMatcher& a) {
        return { "NOT (" + a.Description() + ")", [a](const NodeRef& n) { return !a.Matches(n); } };
    }

    SemanticsMatcher IsEnabled() { return Key("is enabled", [](const auto& c) { return !c.disabled; }); }
    SemanticsMatcher IsNotEnabled() { return Key("is disabled", [](const auto& c) { return c.disabled; }); }
    SemanticsMatcher IsToggleable() { return Key("ToggleableState is defined", [](const auto& c) { return c.toggleableState.has_value(); }); }
    SemanticsMatcher IsOn() { return Key("ToggleableState = 'On'", [](const auto& c) { return c.toggleableState == 1; }); }
    SemanticsMatcher IsOff() { return Key("ToggleableState = 'Off'", [](const auto& c) { return c.toggleableState == 0; }); }
    SemanticsMatcher IsSelectable() { return Key("Selected is defined", [](const auto& c) { return c.selected.has_value(); }); }
    SemanticsMatcher IsSelected() { return Key("Selected = 'true'", [](const auto& c) { return c.selected == true; }); }
    SemanticsMatcher IsNotSelected() { return Key("Selected = 'false'", [](const auto& c) { return c.selected == false; }); }
    SemanticsMatcher IsFocusable() { return Key("Focused is defined", [](const auto& c) { return c.focused.has_value(); }); }
    SemanticsMatcher IsNotFocusable() { return Key("Focused is not defined", [](const auto& c) { return !c.focused.has_value(); }); }
    SemanticsMatcher IsFocused() { return Key("Focused = 'true'", [](const auto& c) { return c.focused == true; }); }
    SemanticsMatcher IsNotFocused() { return Key("Focused = 'false'", [](const auto& c) { return c.focused == false; }); }
    SemanticsMatcher HasClickAction() { return Key("OnClick is defined", [](const auto& c) { return bool(c.onClick); }); }
    SemanticsMatcher HasNoClickAction() { return Key("OnClick is not defined", [](const auto& c) { return !c.onClick; }); }
    SemanticsMatcher HasScrollAction() { return Key("ScrollBy is defined", [](const auto& c) { return bool(c.scrollBy); }); }
    SemanticsMatcher HasNoScrollAction() { return Key("ScrollBy is not defined", [](const auto& c) { return !c.scrollBy; }); }
    SemanticsMatcher HasContentDescription(std::string value, bool substring, bool ignoreCase) {
        return Key(std::format("ContentDescription {} '{}' (ignoreCase: {})", substring ? "contains" : "=", value, ignoreCase),
                   [=](const auto& c) {
                       return std::any_of(c.contentDescription.begin(), c.contentDescription.end(), [&](const std::string& d) {
                           return substring ? Contains(d, value, ignoreCase) : Equal(d, value, ignoreCase);
                       });
                   });
    }
    SemanticsMatcher HasContentDescriptionExactly(std::vector<std::string> values) {
        return Key("ContentDescription = " + Quote(values), [values](const auto& c) {
            auto a = c.contentDescription, b = values;
            std::sort(a.begin(), a.end());
            std::sort(b.begin(), b.end());
            return a == b;
        });
    }
    SemanticsMatcher HasText(std::string text, bool substring, bool ignoreCase) {
        return Key(std::format("Text + InputText + EditableText contains '{}' (ignoreCase: {}){}", text, ignoreCase, substring ? " as substring" : ""),
                   [=](const auto& c) {
                       auto match = [&](const std::string& s) { return substring ? Contains(s, text, ignoreCase) : Equal(s, text, ignoreCase); };
                       return (c.inputText && match(*c.inputText)) || (c.editableText && match(*c.editableText)) ||
                              std::any_of(c.text.begin(), c.text.end(), match);
                   });
    }
    SemanticsMatcher HasTextExactly(std::vector<std::string> values, bool includeEditableText) {
        return Key("Text + EditableText = " + Quote(values), [=](const auto& c) {
            std::vector<std::string> all = c.text;
            if (includeEditableText && c.editableText) all.push_back(*c.editableText);
            auto b = values;
            std::sort(all.begin(), all.end());
            std::sort(b.begin(), b.end());
            return all == b;
        });
    }
    SemanticsMatcher HasStateDescription(std::string value) {
        return Key("StateDescription = '" + value + "'", [value](const auto& c) { return c.stateDescription == value; });
    }
    SemanticsMatcher IsHeading() { return Key("Heading is defined", [](const auto& c) { return c.heading; }); }
    SemanticsMatcher HasTestTag(std::string tag) { return Key("TestTag = '" + tag + "'", [tag](const auto& c) { return c.testTag == tag; }); }
    SemanticsMatcher HasRole(ui::Role role) {
        return Key(std::string("Role = '") + RoleName(role) + "'", [role](const auto& c) { return c.role == role; });
    }
    SemanticsMatcher IsDialog() { return Key("IsDialog is defined", [](const auto& c) { return c.isDialog; }); }
    SemanticsMatcher IsPopup() { return Key("IsPopup is defined", [](const auto& c) { return c.isPopup; }); }
    SemanticsMatcher IsHiddenFromAccessibility() {
        return Key("HideFromAccessibility is defined", [](const auto& c) { return c.hideFromAccessibility || c.invisibleToUser; });
    }
    SemanticsMatcher HasImeAction(int action) {
        return Key(std::format("ImeAction = '{}'", action), [action](const auto& c) { return c.imeAction == action; });
    }
    SemanticsMatcher HasSetTextAction() { return Key("SetText is defined", [](const auto& c) { return bool(c.setText); }); }
    SemanticsMatcher HasInsertTextAtCursorAction() { return Key("InsertTextAtCursor is defined", [](const auto& c) { return bool(c.insertTextAtCursor); }); }
    SemanticsMatcher HasPerformImeAction() { return Key("PerformImeAction is defined", [](const auto& c) { return bool(c.onImeAction); }); }
    SemanticsMatcher HasRequestFocusAction() { return Key("RequestFocus is defined", [](const auto& c) { return bool(c.requestFocus); }); }
    SemanticsMatcher HasScrollToIndexAction() { return Key("ScrollToIndex is defined", [](const auto& c) { return bool(c.scrollToIndex); }); }
    SemanticsMatcher IsEditable() { return Key("EditableText is defined", [](const auto& c) { return c.editableText.has_value() && c.isEditable != false; }); }
    SemanticsMatcher IsRoot() { return { "isRoot", [](const NodeRef& n) { return n.IsRoot(); } }; }
    SemanticsMatcher HasParent(SemanticsMatcher m) {
        return { "hasParentThat(" + m.Description() + ")", [m](const NodeRef& n) {
                    if (n.ancestors.empty()) return false;
                    NodeRef p{ n.ancestors.back(), { n.ancestors.begin(), n.ancestors.end() - 1 } };
                    return m.Matches(p);
                } };
    }
    SemanticsMatcher HasAnyAncestor(SemanticsMatcher m) {
        return { "hasAnyAncestorThat(" + m.Description() + ")", [m](const NodeRef& n) {
                    for (std::size_t i = n.ancestors.size(); i-- > 0;)
                        if (m.Matches({ n.ancestors[i], { n.ancestors.begin(), n.ancestors.begin() + std::ptrdiff_t(i) } })) return true;
                    return false;
                } };
    }
    SemanticsMatcher HasAnyChild(SemanticsMatcher m) {
        return { "hasAnyChildThat(" + m.Description() + ")", [m](const NodeRef& n) {
                    auto path = n.ancestors;
                    path.push_back(n.node);
                    return std::any_of(n.node->children.begin(), n.node->children.end(), [&](const ui::SemanticsNode& c) { return m.Matches({ &c, path }); });
                } };
    }
    SemanticsMatcher HasAnySibling(SemanticsMatcher m) {
        return { "hasAnySiblingThat(" + m.Description() + ")", [m](const NodeRef& n) {
                    const ui::SemanticsNode* parent = n.Parent();
                    if (!parent) return false;
                    return std::any_of(parent->children.begin(), parent->children.end(), [&](const ui::SemanticsNode& c) {
                        return &c != n.node && m.Matches({ &c, n.ancestors });
                    });
                } };
    }
    SemanticsMatcher HasAnyDescendant(SemanticsMatcher m) {
        return { "hasAnyDescendantThat(" + m.Description() + ")", [m](const NodeRef& n) {
                    bool found = false;
                    std::vector<const ui::SemanticsNode*> path = n.ancestors;
                    path.push_back(n.node);
                    for (const auto& c : n.node->children) Walk(c, path, [&](const NodeRef& r) { if (!found && m.Matches(r)) found = true; });
                    return found;
                } };
    }

    // --- the test --------------------------------------------------------------------------------

    ComposeUiTest::ComposeUiTest(ComposeUiTestOptions options) : m_Options(options) {
        auto& d = rt::Dispatcher::Main();
        m_WasVirtual = d.VirtualTime();
        d.SetVirtualTime(true);
        m_Owner.SetDensity({ options.density, options.fontScale });
        m_Owner.SetText(options.text ? options.text : &m_DefaultText);
        m_Composition = std::make_unique<rt::Composition>(m_Owner.Root(), d);
    }

    ComposeUiTest::~ComposeUiTest() {
        m_Composition->SetContent([] {});
        Pump();
        m_Composition.reset();
        rt::Dispatcher::Main().RunPending();
        rt::Dispatcher::Main().SetVirtualTime(m_WasVirtual);
    }

    void ComposeUiTest::SetContent(kiln::Function<void()> content) {
        m_Composition->SetContent(std::move(content));
        WaitForIdle();
    }

    bool ComposeUiTest::Pump() {
        auto& d = rt::Dispatcher::Main();
        d.RunPending();
        const bool recomposed = rt::Composition::RecomposeAll();
        m_Owner.MeasureAndLayout(m_Options.windowSize);
        d.RunPending();
        return recomposed;
    }

    void ComposeUiTest::RunFrame() {
        auto& d = rt::Dispatcher::Main();
        d.AdvanceTimeBy(std::chrono::milliseconds(MainTestClock::frameDelayMillis));
        m_Clock.m_Millis += MainTestClock::frameDelayMillis;
        m_FrameNanos += MainTestClock::frameDelayMillis * 1'000'000;
        d.RunPending();
        d.RunFrame(m_FrameNanos);
        Pump();
    }

    void ComposeUiTest::WaitForIdle() {
        // Idle: nothing to recompose, lay out or run, and (auto-advancing) no frame wanted.
        constexpr int kMaxFrames = 60'000 / int(MainTestClock::frameDelayMillis);
        for (int frames = 0;;) {
            for (int i = 0; i < 100 && Pump(); ++i) {}
            if (!m_Clock.autoAdvance || !rt::Dispatcher::Main().HasFrameWaiters()) return;
            if (++frames > kMaxFrames)
                throw AssertionError("WaitForIdle: frames were still wanted after a minute of the clock (an endless animation?)");
            RunFrame();
        }
    }

    void ComposeUiTest::WaitUntil(const kiln::Function<bool()>& condition, std::int64_t timeoutMillis, const std::string& description) {
        const std::int64_t end = m_Clock.m_Millis + timeoutMillis;
        for (;;) {
            WaitForIdle();
            if (condition()) return;
            if (m_Clock.m_Millis >= end)
                throw AssertionError(std::format("WaitUntil: the condition{} still failed after {} ms", description.empty() ? "" : " (" + description + ")", timeoutMillis));
            RunFrame();
        }
    }

    const ui::SemanticsNode& ComposeUiTest::Snapshot(bool unmerged) {
        // Kept until the test ends: refs taken from a snapshot stay valid while assertions run.
        m_Snapshots.push_back(std::make_unique<ui::SemanticsNode>(m_Owner.BuildSemanticsTree(false, !unmerged, true)));
        if (m_Snapshots.size() > 64) m_Snapshots.erase(m_Snapshots.begin());
        return *m_Snapshots.back();
    }

    std::string ComposeUiTest::PrintTree(bool unmerged) {
        WaitForIdle();
        return PrintToString(Snapshot(unmerged));
    }

    namespace {
        SemanticsNodeInteraction::Selector Matching(SemanticsMatcher m) {
            std::string d = m.Description();
            return { d, [m](const ui::SemanticsNode& root) { return All(root, m); } };
        }
    }

    SemanticsNodeInteraction ComposeUiTest::OnNode(SemanticsMatcher m, bool unmerged) { return { *this, Matching(std::move(m)), unmerged }; }
    SemanticsNodeInteractionCollection ComposeUiTest::OnAllNodes(SemanticsMatcher m, bool unmerged) { return { *this, Matching(std::move(m)), unmerged }; }
    SemanticsNodeInteraction ComposeUiTest::OnNodeWithText(std::string text, bool substring, bool ignoreCase, bool unmerged) {
        return OnNode(HasText(std::move(text), substring, ignoreCase), unmerged);
    }
    SemanticsNodeInteractionCollection ComposeUiTest::OnAllNodesWithText(std::string text, bool substring, bool ignoreCase, bool unmerged) {
        return OnAllNodes(HasText(std::move(text), substring, ignoreCase), unmerged);
    }
    SemanticsNodeInteraction ComposeUiTest::OnNodeWithContentDescription(std::string label, bool substring, bool ignoreCase, bool unmerged) {
        return OnNode(HasContentDescription(std::move(label), substring, ignoreCase), unmerged);
    }
    SemanticsNodeInteractionCollection ComposeUiTest::OnAllNodesWithContentDescription(std::string label, bool substring, bool ignoreCase, bool unmerged) {
        return OnAllNodes(HasContentDescription(std::move(label), substring, ignoreCase), unmerged);
    }
    SemanticsNodeInteraction ComposeUiTest::OnNodeWithTag(std::string tag, bool unmerged) { return OnNode(HasTestTag(std::move(tag)), unmerged); }
    SemanticsNodeInteractionCollection ComposeUiTest::OnAllNodesWithTag(std::string tag, bool unmerged) { return OnAllNodes(HasTestTag(std::move(tag)), unmerged); }
    SemanticsNodeInteraction ComposeUiTest::OnRoot(bool unmerged) { return OnNode(test::IsRoot(), unmerged); }

    // --- the clock ---------------------------------------------------------------------------------

    void MainTestClock::AdvanceTimeByFrame() { m_Test.RunFrame(); }
    void MainTestClock::AdvanceTimeBy(std::int64_t ms, bool ignoreFrameDuration) {
        if (ms <= 0) return;
        if (ignoreFrameDuration) {
            // The exact time: timers due by then run; frames still on frame boundaries.
            auto& d = rt::Dispatcher::Main();
            std::int64_t whole = ms / frameDelayMillis;
            for (std::int64_t i = 0; i < whole; ++i) m_Test.RunFrame();
            std::int64_t rest = ms - whole * frameDelayMillis;
            if (rest) {
                d.AdvanceTimeBy(std::chrono::milliseconds(rest));
                m_Millis += rest;
                m_Test.Pump();
            }
            return;
        }
        for (std::int64_t t = 0; t < ms; t += frameDelayMillis) m_Test.RunFrame();
    }
    void MainTestClock::AdvanceTimeUntil(const kiln::Function<bool()>& condition, std::int64_t timeoutMillis) {
        const std::int64_t end = m_Millis + timeoutMillis;
        while (!condition()) {
            if (m_Millis >= end) throw AssertionError(std::format("AdvanceTimeUntil: the condition still failed after {} ms", timeoutMillis));
            m_Test.RunFrame();
        }
    }

    // --- interactions ----------------------------------------------------------------------------

    std::vector<NodeRef> SemanticsNodeInteraction::Fetch() const {
        m_Test->WaitForIdle();
        return m_Selector->select(m_Test->Snapshot(m_Unmerged));
    }

    NodeRef SemanticsNodeInteraction::FetchOne(const std::string& error) const {
        std::vector<NodeRef> nodes = Fetch();
        if (nodes.size() == 1) return nodes.front();
        std::string msg = (error.empty() ? std::string("Failed to fetch the node.") : error) + "\n";
        if (nodes.empty()) msg += "Reason: Expected exactly '1' node but could not find any node that satisfies: (" + m_Selector->description + ")\n";
        else msg += std::format("Reason: Expected exactly '1' node but found '{}' nodes that satisfy: ({})\n", nodes.size(), m_Selector->description) + Describe(nodes);
        throw AssertionError(msg);
    }

    ui::SemanticsNode SemanticsNodeInteraction::FetchSemanticsNode(const std::string& error) const { return *FetchOne(error).node; }

    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertExists(const std::string& error) {
        FetchOne(error.empty() ? "Failed: assertExists." : error);
        return *this;
    }
    void SemanticsNodeInteraction::AssertDoesNotExist() {
        std::vector<NodeRef> nodes = Fetch();
        if (!nodes.empty())
            throw AssertionError(std::format("Failed: assertDoesNotExist.\nReason: Did not expect any node but found '{}' nodes that satisfy: ({})\n", nodes.size(), m_Selector->description) + Describe(nodes));
    }

    bool SemanticsNodeInteraction::IsDisplayed() const {
        std::vector<NodeRef> nodes = Fetch();
        if (nodes.empty()) return false;
        if (nodes.size() > 1) throw AssertionError(std::format("Failed to perform isDisplayed check: Expected at most 1 node but found {} nodes that satisfy ({})", nodes.size(), m_Selector->description));
        const ui::SemanticsNode& n = *nodes.front().node;
        if (n.layout && !n.layout->IsPlaced() && n.id != -1) return false;
        for (const auto* a : nodes.front().ancestors)
            if (a->layout && a->id != -1 && !a->layout->IsPlaced()) return false;
        const ui::IntSize w = m_Test->m_Options.windowSize;
        const ui::Rect& b = n.boundsInRoot;   // clipped by its ancestors
        const float l = std::max(b.left, 0.f), t = std::max(b.top, 0.f), r = std::min(b.right, float(w.width)), bt = std::min(b.bottom, float(w.height));
        return r - l > 0 && bt - t > 0;
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertIsDisplayed() {
        if (!IsDisplayed()) {
            std::string msg = "Assert failed: The component is not displayed!\n";
            std::vector<NodeRef> nodes = Fetch();
            if (nodes.empty()) msg += "Reason: no node satisfies (" + m_Selector->description + ")\n";
            else msg += Describe(nodes);
            throw AssertionError(msg);
        }
        return *this;
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertIsNotDisplayed() {
        if (IsDisplayed()) throw AssertionError("Assert failed: The component is displayed!\n" + Describe(Fetch()));
        return *this;
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::Assert(const SemanticsMatcher& matcher, const std::string& prefix) {
        NodeRef n = FetchOne("Failed to assert the following: (" + matcher.Description() + ")");
        if (!matcher.Matches(n)) {
            std::string msg = (prefix.empty() ? "" : prefix + "\n") + "Failed to assert the following: (" + matcher.Description() + ")\nSemantics of the node:\n";
            Print(*n.node, 0, 0, msg);
            msg += "Selector used: (" + m_Selector->description + ")\n";
            throw AssertionError(msg);
        }
        return *this;
    }

    ui::Rect SemanticsNodeInteraction::GetBoundsInRoot() const {
        const ui::Rect b = FetchOne("Failed to retrieve bounds of the node.").node->boundsInRoot;
        const float d = m_Test->Density();
        return { b.left / d, b.top / d, b.right / d, b.bottom / d };
    }
    ui::Rect SemanticsNodeInteraction::GetUnclippedBoundsInRoot() const {
        const ui::Rect b = FetchOne("Failed to retrieve bounds of the node.").node->unclippedBoundsInRoot;
        const float d = m_Test->Density();
        return { b.left / d, b.top / d, b.right / d, b.bottom / d };
    }
    namespace {
        void ExpectDp(float actual, float expected, const char* subject, float tolerance = 0.5f) {
            if (std::abs(actual - expected) > tolerance)
                throw AssertionError(std::format("Actual {} is {}.dp, expected {}.dp (tolerance: {}.dp)", subject, actual, expected, tolerance));
        }
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertWidthIsEqualTo(float dp) { ExpectDp(GetUnclippedBoundsInRoot().Width(), dp, "width"); return *this; }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertHeightIsEqualTo(float dp) { ExpectDp(GetUnclippedBoundsInRoot().Height(), dp, "height"); return *this; }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertWidthIsAtLeast(float dp) {
        const float w = GetUnclippedBoundsInRoot().Width();
        if (w + 0.5f < dp) throw AssertionError(std::format("Actual width is {}.dp, expected at least {}.dp", w, dp));
        return *this;
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertHeightIsAtLeast(float dp) {
        const float h = GetUnclippedBoundsInRoot().Height();
        if (h + 0.5f < dp) throw AssertionError(std::format("Actual height is {}.dp, expected at least {}.dp", h, dp));
        return *this;
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertPositionInRootIsEqualTo(float left, float top) {
        const ui::Rect b = GetUnclippedBoundsInRoot();
        ExpectDp(b.left, left, "left");
        ExpectDp(b.top, top, "top");
        return *this;
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertLeftPositionInRootIsEqualTo(float dp) { ExpectDp(GetUnclippedBoundsInRoot().left, dp, "left"); return *this; }
    SemanticsNodeInteraction& SemanticsNodeInteraction::AssertTopPositionInRootIsEqualTo(float dp) { ExpectDp(GetUnclippedBoundsInRoot().top, dp, "top"); return *this; }

    // --- actions ---------------------------------------------------------------------------------

    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformTouchInput(const kiln::Function<void(TouchInjectionScope&)>& block) {
        const ui::SemanticsNode n = FetchSemanticsNode("Failed to inject touch input.");
        TouchInjectionScope scope(*m_Test, n.boundsInRoot);
        scope.m_Time = m_Test->m_Clock.CurrentTime();
        block(scope);
        m_Test->WaitForIdle();
        return *this;
    }

    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformClick() {
        // performClickImpl: a touch click at the node's centre.
        return PerformTouchInput([](TouchInjectionScope& s) { s.Click(); });
    }

    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformSemanticsAction(const std::string& name, const kiln::Function<bool(const ui::SemanticsConfiguration&)>& invoke) {
        const ui::SemanticsNode n = FetchSemanticsNode("Failed to perform " + name + ".");
        if (!invoke(n.config)) {
            std::string msg = "Failed to perform " + name + ": the node has no such action, or it returned false.\nSemantics of the node:\n";
            Print(n, 0, 0, msg);
            throw AssertionError(msg);
        }
        m_Test->WaitForIdle();
        return *this;
    }

    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformScrollTo() {
        // scrollToNode: the closest scrollable ancestor scrolls by the least that shows the node
        // whole (or fills its viewport with it), until nothing more is needed.
        for (int attempt = 0; attempt < 100; ++attempt) {
            NodeRef ref = FetchOne("Action performScrollTo() failed.");
            const ui::SemanticsNode* scrollable = nullptr;
            for (std::size_t i = ref.ancestors.size(); i-- > 0;)
                if (ref.ancestors[i]->config.scrollBy) { scrollable = ref.ancestors[i]; break; }
            if (!scrollable) throw AssertionError("Action performScrollTo() failed: the node has no scrollable ancestor (ScrollBy)");
            const ui::Rect v = scrollable->boundsInRoot, t = ref.node->unclippedBoundsInRoot;
            auto minimum = [](float start, float end, float vStart, float vEnd) {
                if (start >= vStart && end <= vEnd) return 0.f;              // already whole
                if (end - start > vEnd - vStart) return start - vStart;       // larger: fill
                return start < vStart ? start - vStart : end - vEnd;
            };
            float dx = minimum(t.left, t.right, v.left, v.right), dy = minimum(t.top, t.bottom, v.top, v.bottom);
            if (scrollable->config.horizontalScrollAxisRange && scrollable->config.horizontalScrollAxisRange->reverseScrolling) dx = -dx;
            if (scrollable->config.verticalScrollAxisRange && scrollable->config.verticalScrollAxisRange->reverseScrolling) dy = -dy;
            if (std::abs(dx) < 0.5f && std::abs(dy) < 0.5f) return *this;
            if (!m_Test->m_Clock.autoAdvance)
                throw AssertionError("Action performScrollTo() cannot scroll the node into view because mainClock.autoAdvance is false");
            scrollable->config.scrollBy(dx, dy);
            m_Test->WaitForIdle();
        }
        throw AssertionError("Action performScrollTo() failed: the node did not come into view");
    }

    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformScrollToIndex(int index) {
        return PerformSemanticsAction("performScrollToIndex", [index](const ui::SemanticsConfiguration& c) { return c.scrollToIndex && c.scrollToIndex(index); });
    }

    namespace {
        // getNodeAndFocus: an editable node, focused first.
        void Focus(SemanticsNodeInteraction& n, bool requireEditable) {
            const ui::SemanticsNode node = n.FetchSemanticsNode("Failed to perform text input.");
            if (requireEditable && !node.config.editableText) {
                std::string msg = "Failed to perform text input: the node is not editable.\nSemantics of the node:\n";
                Print(node, 0, 0, msg);
                throw AssertionError(msg);
            }
            if (node.config.focused != true && node.config.requestFocus) n.RequestFocus();
        }
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::RequestFocus() {
        return PerformSemanticsAction("requestFocus", [](const ui::SemanticsConfiguration& c) { return c.requestFocus && c.requestFocus(); });
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformTextInput(const std::string& text) {
        Focus(*this, true);
        return PerformSemanticsAction("performTextInput", [&text](const ui::SemanticsConfiguration& c) { return c.insertTextAtCursor && c.insertTextAtCursor(text); });
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformTextReplacement(const std::string& text) {
        Focus(*this, true);
        return PerformSemanticsAction("performTextReplacement", [&text](const ui::SemanticsConfiguration& c) { return c.setText && c.setText(text); });
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformTextInputSelection(int start, int end) {
        Focus(*this, false);
        return PerformSemanticsAction("performTextInputSelection", [=](const ui::SemanticsConfiguration& c) { return c.setSelection && c.setSelection(start, end, true); });
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformImeAction() {
        Assert(HasPerformImeAction(), "Failed to perform IME action.");
        Focus(*this, false);
        return PerformSemanticsAction("performImeAction", [](const ui::SemanticsConfiguration& c) { return c.onImeAction && c.onImeAction(); });
    }
    SemanticsNodeInteraction& SemanticsNodeInteraction::PerformCustomAccessibilityActionWithLabel(const std::string& label) {
        return PerformSemanticsAction("custom action '" + label + "'", [&label](const ui::SemanticsConfiguration& c) {
            for (const auto& a : c.customActions)
                if (a.label == label) return a.action && a.action();
            return false;
        });
    }

    // --- selectors ---------------------------------------------------------------------------------

    namespace {
        // A selector relative to the one node another selector finds.
        SemanticsNodeInteraction::Selector Relative(std::shared_ptr<SemanticsNodeInteraction::Selector> base, std::string what,
                                                    kiln::Function<std::vector<NodeRef>(const NodeRef&)> step) {
            std::string d = "(" + base->description + ")." + what;
            return { d, [base, step](const ui::SemanticsNode& root) {
                        std::vector<NodeRef> from = base->select(root);
                        if (from.size() != 1) return std::vector<NodeRef>{};
                        return step(from.front());
                    } };
        }
        std::vector<NodeRef> Children(const NodeRef& n) {
            std::vector<NodeRef> out;
            auto path = n.ancestors;
            path.push_back(n.node);
            for (const auto& c : n.node->children) out.push_back({ &c, path });
            return out;
        }
    }
    SemanticsNodeInteractionCollection SemanticsNodeInteraction::OnChildren() const {
        return { *m_Test, Relative(m_Selector, "children", Children), m_Unmerged };
    }
    SemanticsNodeInteraction SemanticsNodeInteraction::OnChild() const {
        return { *m_Test, Relative(m_Selector, "child", Children), m_Unmerged };
    }
    SemanticsNodeInteraction SemanticsNodeInteraction::OnChildAt(int index) const {
        return { *m_Test, Relative(m_Selector, std::format("child[{}]", index), [index](const NodeRef& n) {
                    auto c = Children(n);
                    return index >= 0 && std::size_t(index) < c.size() ? std::vector<NodeRef>{ c[std::size_t(index)] } : std::vector<NodeRef>{};
                }), m_Unmerged };
    }
    SemanticsNodeInteraction SemanticsNodeInteraction::OnParent() const {
        return { *m_Test, Relative(m_Selector, "parent", [](const NodeRef& n) {
                    if (n.ancestors.empty()) return std::vector<NodeRef>{};
                    return std::vector<NodeRef>{ { n.ancestors.back(), { n.ancestors.begin(), n.ancestors.end() - 1 } } };
                }), m_Unmerged };
    }
    SemanticsNodeInteractionCollection SemanticsNodeInteraction::OnSiblings() const {
        return { *m_Test, Relative(m_Selector, "siblings", [](const NodeRef& n) {
                    std::vector<NodeRef> out;
                    if (n.ancestors.empty()) return out;
                    for (const auto& c : n.ancestors.back()->children) if (&c != n.node) out.push_back({ &c, n.ancestors });
                    return out;
                }), m_Unmerged };
    }
    SemanticsNodeInteraction SemanticsNodeInteraction::OnSibling() const {
        return OnSiblings().FilterToOne({ "any", [](const NodeRef&) { return true; } });
    }
    SemanticsNodeInteractionCollection SemanticsNodeInteraction::OnAncestors() const {
        return { *m_Test, Relative(m_Selector, "ancestors", [](const NodeRef& n) {
                    std::vector<NodeRef> out;
                    for (std::size_t i = n.ancestors.size(); i-- > 0;)
                        out.push_back({ n.ancestors[i], { n.ancestors.begin(), n.ancestors.begin() + std::ptrdiff_t(i) } });
                    return out;
                }), m_Unmerged };
    }

    std::string SemanticsNodeInteraction::PrintToString(int maxDepth) const {
        std::vector<NodeRef> nodes = Fetch();
        std::string out;
        for (const NodeRef& n : nodes) Print(*n.node, 0, maxDepth, out);
        return out.empty() ? "Nodes found: none\n" : out;
    }

    // --- collections ---------------------------------------------------------------------------

    std::vector<NodeRef> SemanticsNodeInteractionCollection::Fetch() const {
        m_Test->WaitForIdle();
        return m_Selector->select(m_Test->Snapshot(m_Unmerged));
    }
    std::vector<ui::SemanticsNode> SemanticsNodeInteractionCollection::FetchSemanticsNodes() const {
        std::vector<ui::SemanticsNode> out;
        for (const NodeRef& n : Fetch()) out.push_back(*n.node);
        return out;
    }
    SemanticsNodeInteraction SemanticsNodeInteractionCollection::operator[](int index) const {
        auto base = m_Selector;
        return { *m_Test, { std::format("({})[{}]", base->description, index), [base, index](const ui::SemanticsNode& root) {
                               auto all = base->select(root);
                               return index >= 0 && std::size_t(index) < all.size() ? std::vector<NodeRef>{ all[std::size_t(index)] } : std::vector<NodeRef>{};
                           } }, m_Unmerged };
    }
    SemanticsNodeInteraction SemanticsNodeInteractionCollection::OnLast() const {
        auto base = m_Selector;
        return { *m_Test, { "(" + base->description + ").last", [base](const ui::SemanticsNode& root) {
                               auto all = base->select(root);
                               return all.empty() ? std::vector<NodeRef>{} : std::vector<NodeRef>{ all.back() };
                           } }, m_Unmerged };
    }
    SemanticsNodeInteractionCollection& SemanticsNodeInteractionCollection::AssertCountEquals(int expected) {
        std::vector<NodeRef> nodes = Fetch();
        if (int(nodes.size()) != expected)
            throw AssertionError(std::format("Failed to assert count of nodes.\nReason: Expected '{}' nodes but found '{}' nodes that satisfy: ({})\n", expected, nodes.size(), m_Selector->description) + Describe(nodes));
        return *this;
    }
    SemanticsNodeInteractionCollection& SemanticsNodeInteractionCollection::AssertAny(const SemanticsMatcher& m) {
        std::vector<NodeRef> nodes = Fetch();
        if (nodes.empty() || std::none_of(nodes.begin(), nodes.end(), [&](const NodeRef& n) { return m.Matches(n); }))
            throw AssertionError("Failed to assertAny(" + m.Description() + ")\n" + Describe(nodes));
        return *this;
    }
    SemanticsNodeInteractionCollection& SemanticsNodeInteractionCollection::AssertAll(const SemanticsMatcher& m) {
        std::vector<NodeRef> nodes = Fetch();
        std::vector<NodeRef> violations;
        for (const NodeRef& n : nodes) if (!m.Matches(n)) violations.push_back(n);
        if (!violations.empty()) throw AssertionError("Failed to assertAll(" + m.Description() + ")\nFound nodes that do not match:\n" + Describe(violations));
        return *this;
    }
    SemanticsNodeInteractionCollection SemanticsNodeInteractionCollection::Filter(SemanticsMatcher m) const {
        auto base = m_Selector;
        return { *m_Test, { "(" + base->description + ").filter(" + m.Description() + ")", [base, m](const ui::SemanticsNode& root) {
                               std::vector<NodeRef> out;
                               for (const NodeRef& n : base->select(root)) if (m.Matches(n)) out.push_back(n);
                               return out;
                           } }, m_Unmerged };
    }
    SemanticsNodeInteraction SemanticsNodeInteractionCollection::FilterToOne(SemanticsMatcher m) const {
        auto base = m_Selector;
        return { *m_Test, { "(" + base->description + ").filterToOne(" + m.Description() + ")", [base, m](const ui::SemanticsNode& root) {
                               std::vector<NodeRef> out;
                               for (const NodeRef& n : base->select(root)) if (m.Matches(n)) out.push_back(n);
                               return out;
                           } }, m_Unmerged };
    }

    // --- touch -----------------------------------------------------------------------------------

    void TouchInjectionScope::Send(int type) {
        ui::RawPointerEvent e;
        e.uptimeMillis = m_Time;
        e.type = ui::PointerEventType(type);
        for (const auto& [id, p] : m_Pointers)
            e.pointers.push_back({ id, { m_Bounds.left + p.x, m_Bounds.top + p.y }, true, ui::PointerType::Touch, {} });
        m_Test.m_Owner.ProcessPointer(e);
        m_Test.Pump();
    }
    void TouchInjectionScope::AdvanceEventTime(std::int64_t ms) {
        // The clock follows the events: timers (a long press) fire as time passes.
        while (m_Time + ms > m_Test.m_Clock.CurrentTime()) m_Test.RunFrame();
        m_Time += ms;
    }
    void TouchInjectionScope::Down(int id, ui::Offset p) {
        m_Pointers[id] = p;
        Send(int(ui::PointerEventType::Press));
    }
    void TouchInjectionScope::MoveTo(int id, ui::Offset p) {
        AdvanceEventTime();
        m_Pointers[id] = p;
        Send(int(ui::PointerEventType::Move));
    }
    void TouchInjectionScope::Up(int id) {
        AdvanceEventTime();
        auto it = m_Pointers.find(id);
        if (it == m_Pointers.end()) return;
        // The release event carries the pointer up.
        ui::RawPointerEvent e;
        e.uptimeMillis = m_Time;
        e.type = ui::PointerEventType::Release;
        for (const auto& [pid, p] : m_Pointers)
            e.pointers.push_back({ pid, { m_Bounds.left + p.x, m_Bounds.top + p.y }, pid != id, ui::PointerType::Touch, {} });
        m_Pointers.erase(it);
        m_Test.m_Owner.ProcessPointer(e);
        m_Test.Pump();
    }
    void TouchInjectionScope::Cancel() {
        AdvanceEventTime();
        ui::RawPointerEvent e;
        e.uptimeMillis = m_Time;
        e.cancel = true;
        for (const auto& [pid, p] : m_Pointers) e.pointers.push_back({ pid, { m_Bounds.left + p.x, m_Bounds.top + p.y }, false, ui::PointerType::Touch, {} });
        m_Pointers.clear();
        m_Test.m_Owner.ProcessPointer(e);
        m_Test.Pump();
    }
    void TouchInjectionScope::Click(std::optional<ui::Offset> p) {
        Down(p.value_or(Center()));
        Up();
    }
    void TouchInjectionScope::LongClick(std::optional<ui::Offset> p, std::int64_t duration) {
        if (duration <= 0) duration = m_Test.m_Owner.Config().longPressTimeoutMillis + 100;
        Down(p.value_or(Center()));
        AdvanceEventTime(duration - eventPeriodMillis);
        Up();
    }
    void TouchInjectionScope::DoubleClick(std::optional<ui::Offset> p) {
        Click(p);
        AdvanceEventTime(std::max<std::int64_t>(m_Test.m_Owner.Config().doubleTapMinTimeMillis + 10, 50));
        Click(p);
    }
    void TouchInjectionScope::Swipe(ui::Offset start, ui::Offset end, std::int64_t duration) {
        Down(start);
        const int steps = std::max(1, int(duration / eventPeriodMillis));
        for (int i = 1; i <= steps; ++i) {
            const float f = float(i) / float(steps);
            MoveTo(0, { start.x + (end.x - start.x) * f, start.y + (end.y - start.y) * f });
        }
        Up();
    }
    void TouchInjectionScope::SwipeUp() { Swipe({ Width() / 2, Height() * 0.9f }, { Width() / 2, Height() * 0.1f }); }
    void TouchInjectionScope::SwipeDown() { Swipe({ Width() / 2, Height() * 0.1f }, { Width() / 2, Height() * 0.9f }); }
    void TouchInjectionScope::SwipeLeft() { Swipe({ Width() * 0.9f, Height() / 2 }, { Width() * 0.1f, Height() / 2 }); }
    void TouchInjectionScope::SwipeRight() { Swipe({ Width() * 0.1f, Height() / 2 }, { Width() * 0.9f, Height() / 2 }); }

    std::string PrintToString(const ui::SemanticsNode& node, int maxDepth) {
        std::string out;
        Print(node, 0, maxDepth, out);
        return out;
    }

}
