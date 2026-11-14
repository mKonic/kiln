// Home-screen widgets: the element tree a widget's content builds, as KilnGlance.java reads it,
// the sizes drawn for each SizeMode, and the action callbacks.
#include "Check.h"

#include "kiln/glance/Glance.h"
#include "kiln/jvm/Codec.h"
#include <kiln/kiln.h>

using namespace kiln;
using namespace kiln::glance;

namespace {
    // The encoded tree, read back: kind, modifier kinds, props, children.
    struct Node {
        int kind = 0;
        std::vector<int> modifiers;
        std::vector<std::uint8_t> props;
        std::vector<Node> children;
    };
    Node ReadNode(jvm::Reader& r) {
        Node n;
        n.kind = r.I32();
        int m = r.I32();
        for (int i = 0; i < m; ++i) {
            int k = r.I32();
            n.modifiers.push_back(k);
            using K = GlanceModifier::Item::Kind;
            switch (K(k)) {
                case K::Width: case K::Height: r.I32(); r.F32(); break;
                case K::Padding: case K::CornerRadius: r.F32(); r.F32(); r.F32(); r.F32(); break;
                case K::BackgroundColor: r.Bool(); r.I32(); r.I32(); break;
                case K::BackgroundResource: case K::ContentDescription: r.Str(); break;
                case K::Clickable: r.I32(); r.Str(); r.Str(); r.Str(); break;
                case K::Visibility: case K::Enabled: r.I32(); break;
            }
        }
        n.props = r.Bytes();
        int c = r.I32();
        for (int i = 0; i < c; ++i) n.children.push_back(ReadNode(r));
        return n;
    }
    std::vector<Node> ReadTree(const std::vector<std::uint8_t>& bytes) {
        jvm::Reader r(bytes);
        std::vector<Node> out;
        if (r.I32() != 1) return out;
        int n = r.I32();
        for (int i = 0; i < n; ++i) out.push_back(ReadNode(r));
        CHECK(r.Remaining() == 0);
        return out;
    }
}

TEST(GlanceContentBuildsTheElementTree) {
    int drawn = 0;
    ui::Size seen;
    RegisterWidget("test-notes", { .sizeMode = SizeMode::Exact, .content = [&](const WidgetContext& w) {
        ++drawn;
        seen = w.size;
        Column(Padding(FillMaxSize(Background(GlanceModifier{}, { 0xFFFFFFFF, 0xFF000000 })), 12), Alignment::Center(), [&] {
            Text("Notes", {}, { .fontSize = 18, .fontWeight = FontWeight::Bold }, 1);
            Row(FillMaxWidth(GlanceModifier{}), {}, [] {
                Text("left", DefaultWeight(GlanceModifier{}));
                Button("Add", RunCallback("add", "1"));
            });
            if (w.size.width > 200) Text("wide");
            LazyColumn(GlanceModifier{}, [](LazyListScope& s) {
                s.Items(3, [](int i) { return std::int64_t(100 + i); }, [](int i) { Text("item " + std::to_string(i)); });
            });
        });
    } });
    std::vector<Node> narrow = ReadTree(Render("test-notes", { "test-notes", 7, { 120, 120 } }));
    CHECK(drawn == 1 && seen == (ui::Size{ 120, 120 }));
    CHECK(narrow.size() == 1);
    const Node& column = narrow[0];
    using K = GlanceModifier::Item::Kind;
    CHECK(column.kind == 3 && column.modifiers == (std::vector<int>{ int(K::BackgroundColor), int(K::Width), int(K::Height), int(K::Padding) }));
    CHECK(column.children.size() == 3);
    CHECK(column.children[0].kind == 4);   // Text
    const Node& row = column.children[1];
    CHECK(row.kind == 2 && row.children.size() == 2 && row.children[1].kind == 6);
    CHECK(row.children[0].modifiers == (std::vector<int>{ int(K::Width), int(K::Height) }));   // DefaultWeight: Expand both ways
    const Node& list = column.children[2];
    CHECK(list.kind == 12 && list.children.size() == 3 && list.children[2].kind == 13 && list.children[2].children[0].kind == 4);
    jvm::Reader itemId(list.children[1].props);
    CHECK(itemId.I64() == 101);
    // A wider size draws more.
    CHECK(ReadTree(Render("test-notes", { "test-notes", 7, { 300, 120 } }))[0].children.size() == 4);
    // The column's props: the gravity of Center.
    jvm::Reader g(column.props);
    CHECK(g.I32() == (0x01 | 0x10));
    CHECK(Render("nobody", {}).empty());
}

TEST(GlanceSizesFollowTheSizeMode) {
    const std::vector<ui::Size> launcher = { { 100, 200 }, { 300, 120 }, { 100, 200 } };
    const ui::Size minimum{ 80, 80 };
    CHECK(SizesToDraw({ .sizeMode = SizeMode::Single }, launcher, minimum) == std::vector<ui::Size>{ minimum });
    CHECK(SizesToDraw({ .sizeMode = SizeMode::Exact }, launcher, minimum) == (std::vector<ui::Size>{ { 100, 200 }, { 300, 120 } }));
    CHECK(SizesToDraw({ .sizeMode = SizeMode::Exact }, {}, minimum) == std::vector<ui::Size>{ minimum });
    CHECK(SizesToDraw({ .sizeMode = SizeMode::Responsive, .responsiveSizes = { { 50, 50 }, { 200, 100 } } }, launcher, minimum)
          == (std::vector<ui::Size>{ { 50, 50 }, { 200, 100 } }));
    CHECK(Gravity(Alignment::TopStart()) == (0x00800003 | 0x30) && Gravity(Alignment::BottomEnd()) == (0x00800005 | 0x50));
}

TEST(GlanceActionCallbacksRunByKey) {
    ActionContext got;
    int runs = 0;
    RegisterActionCallback("test-toggle", [&](const ActionContext& c) { got = c; ++runs; });
    ActionContext c;
    c.widget = "w";
    c.appWidgetId = 3;
    c.params = "p";
    c.checked = true;
    CHECK(RunActionCallback("test-toggle", c) && runs == 1 && got.appWidgetId == 3 && got.params == "p" && got.checked == true);
    CHECK(!RunActionCallback("test-missing", c));
    // Without a platform host: nothing placed, nothing to pin.
    CHECK(AppWidgetIds("w").empty() && !RequestPin("w"));
}

TEST(GlanceFromTheCApi) {
    kiln_fn_glance_content content{};
    content.call = [](void*, const char* widget, int32_t id, float w, float) {
        kiln_glance_modifier m{};
        m.width_kind = 1;
        m.padding_start = m.padding_end = 8;
        m.background = 0xFFFFFFFF;
        kiln_fn inner{};
        inner.call = [](void*) {
            kiln_glance_text("from C", nullptr, 14, 0, 700, 1);
            kiln_glance_action add{ 2, nullptr, "c-add", "x" };
            kiln_glance_button("Add", add, nullptr);
        };
        kiln_glance_column(&m, KILN_ALIGN_CENTER, inner);
        (void)widget; (void)id; (void)w;
    };
    kiln_glance_register_widget("test-c", 1, content);
    const GlanceAppWidget* w = FindWidget("test-c");
    CHECK(w && w->sizeMode == SizeMode::Exact);
    std::vector<Node> tree = ReadTree(Render("test-c", { "test-c", 1, { 200, 100 } }));
    using K = GlanceModifier::Item::Kind;
    CHECK(tree.size() == 1 && tree[0].kind == 3 && tree[0].children.size() == 2 && tree[0].children[1].kind == 6);
    CHECK(tree[0].modifiers == (std::vector<int>{ int(K::BackgroundColor), int(K::Width), int(K::Padding) }));
    static int checkedSeen = -2;
    kiln_fn_glance_action action{};
    action.call = [](void*, const char*, int32_t, const char*, int32_t checked) { checkedSeen = checked; };
    kiln_glance_register_action("c-toggle", action);
    ActionContext c;
    c.checked = false;
    CHECK(RunActionCallback("c-toggle", c) && checkedSeen == 0);
}
