// Home-screen widgets: the element tree a widget's content builds, as KilnGlance.java reads it,
// the sizes drawn for each SizeMode, and the action callbacks.
#include "Check.h"

#include "kiln/glance/Glance.h"
#include "kiln/glance/GlanceComponents.h"
#include "kiln/jvm/Codec.h"
#include "kiln/ui/AppDirectories.h"
#include <kiln/kiln.h>

#include <chrono>
#include <thread>

using namespace kiln;
using namespace kiln::glance;

namespace {
    // A task run to its end on the main dispatcher (IO results come back through it).
    template<typename T> rt::Task<> Into(rt::Task<T> task, std::optional<T>* out) { *out = co_await std::move(task); }
    rt::Task<> Into(rt::Task<> task, bool* done) { co_await std::move(task); *done = true; }
    template<typename T> T Await(rt::Task<T> task) {
        std::optional<T> out;
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), Into(std::move(task), &out));
        for (int i = 0; i < 5000 && !job.Done(); ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(out.has_value());
        return std::move(*out);
    }
    void Await(rt::Task<> task) {
        bool done = false;
        rt::Job job = rt::Launch(rt::Dispatcher::Main(), Into(std::move(task), &done));
        for (int i = 0; i < 5000 && !job.Done(); ++i) {
            rt::Dispatcher::Main().RunPending();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        CHECK(done);
    }

    // The encoded tree, read back: kind, modifiers (kind and the parts tests look at), props, children.
    struct Mod {
        int kind = 0;
        int dimension = -1;
        float dp = 0;
        std::string resource;
        int actionKind = -1;
        std::string actionKey;
        std::vector<std::uint8_t> params;
        std::string description;
        bool hasColor = false;
        std::uint32_t day = 0;
        std::string colorResource;
        float padding[4] = {};
    };
    struct Node {
        int kind = 0;
        std::vector<Mod> mods;
        std::vector<int> modifiers;
        std::vector<std::uint8_t> props;
        std::vector<Node> children;
        const Mod* Find(GlanceModifier::Item::Kind k) const {
            for (const Mod& m : mods) if (m.kind == int(k)) return &m;
            return nullptr;
        }
    };
    void ReadColor(jvm::Reader& r, Mod* m = nullptr) {
        bool set = r.Bool();
        std::uint32_t day = std::uint32_t(r.I32());
        r.I32();
        std::string res = r.Str();
        if (m) { m->hasColor = set; m->day = day; m->colorResource = res; }
    }
    Node ReadNode(jvm::Reader& r) {
        Node n;
        n.kind = r.I32();
        int count = r.I32();
        for (int i = 0; i < count; ++i) {
            Mod m;
            m.kind = r.I32();
            n.modifiers.push_back(m.kind);
            using K = GlanceModifier::Item::Kind;
            switch (K(m.kind)) {
                case K::Width: case K::Height: case K::CornerRadius: m.dimension = r.I32(); m.dp = r.F32(); m.resource = r.Str(); break;
                case K::Padding:
                    for (float& p : m.padding) p = r.F32();
                    for (int k = 0; k < 4; ++k) r.Str();
                    r.Bool();
                    break;
                case K::BackgroundColor: ReadColor(r, &m); break;
                case K::Clickable: m.actionKind = r.I32(); r.Str(); m.actionKey = r.Str(); r.Str(); m.params = r.Bytes(); r.Bool(); break;
                case K::Visibility: case K::Enabled: case K::ClipToOutline: r.I32(); break;
                case K::Semantics: r.Bool(); m.description = r.Str(); break;
                case K::AppWidgetBackground: case K::SelectableGroup: break;
                default: CHECK(false); break;
            }
            n.mods.push_back(std::move(m));
        }
        n.props = r.Bytes();
        int c = r.I32();
        for (int i = 0; i < c; ++i) n.children.push_back(ReadNode(r));
        return n;
    }
    // The normalized tree's single root element.
    Node ReadTree(const std::vector<std::uint8_t>& bytes) {
        jvm::Reader r(bytes);
        CHECK(r.I32() == 2);
        Node root = ReadNode(r);
        CHECK(r.Remaining() == 0);
        return root;
    }
    // A Text's or Button's props: text, then its style's colour.
    struct TextProps {
        std::string text;
        bool hasStyle = false;
        Mod color;
        float fontSize = 0;
        int weight = 0, fontStyle = 0, align = 0, decoration = 0;
        std::string family;
        int maxLines = 0;
    };
    TextProps ReadText(const Node& n) {
        jvm::Reader r(n.props);
        TextProps t;
        t.text = r.Str();
        t.hasStyle = r.Bool();
        if (t.hasStyle) {
            ReadColor(r, &t.color);
            t.fontSize = r.F32();
            t.weight = r.I32();
            t.fontStyle = r.I32();
            t.align = r.I32();
            t.decoration = r.I32();
            t.family = r.Str();
        }
        t.maxLines = r.I32();
        r.I32();
        return t;
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
                Button("Add", ActionRunCallback("add"));
            });
            if (w.size.width > 200) Text("wide");
            LazyColumn(GlanceModifier{}, [](LazyListScope& s) {
                s.Items(3, [](int i) { return std::int64_t(100 + i); }, [](int i) { Text("item " + std::to_string(i)); });
            });
        });
    } });
    const Node column = ReadTree(Render("test-notes", { "test-notes", 7, { 120, 120 } }));
    CHECK(drawn == 1 && seen == (ui::Size{ 120, 120 }));
    using K = GlanceModifier::Item::Kind;
    CHECK(column.kind == 3 && column.modifiers == (std::vector<int>{ int(K::BackgroundColor), int(K::Width), int(K::Height), int(K::Padding) }));
    CHECK(column.children.size() == 3);
    const TextProps title = ReadText(column.children[0]);
    CHECK(column.children[0].kind == 4 && title.text == "Notes" && title.weight == 700 && title.maxLines == 1);
    // TextDefaults: black unless the style says otherwise.
    CHECK(title.color.hasColor && title.color.day == 0xFF000000u);
    const Node& row = column.children[1];
    CHECK(row.kind == 2 && row.children.size() == 2 && row.children[1].kind == 6);
    CHECK(row.children[0].modifiers == (std::vector<int>{ int(K::Width), int(K::Height) }));   // DefaultWeight: Expand both ways
    const Node& list = column.children[2];
    CHECK(list.kind == 12 && list.children.size() == 3 && list.children[2].kind == 13);
    // normalizeLazyListItem: each item's content in a Box.
    CHECK(list.children[2].children.size() == 1 && list.children[2].children[0].kind == 1 && list.children[2].children[0].children[0].kind == 4);
    jvm::Reader itemId(list.children[1].props);
    CHECK(itemId.I64() == 101);
    // A wider size draws more.
    CHECK(ReadTree(Render("test-notes", { "test-notes", 7, { 300, 120 } })).children.size() == 4);
    // The column's props: the gravity of Center.
    jvm::Reader g(column.props);
    CHECK(g.I32() == (0x01 | 0x10));
    CHECK(Render("nobody", {}).empty());
}

TEST(GlanceButtonsTakeTheThemeAndTheirAdjustments) {
    RegisterWidget("test-buttons", { .content = [](const WidgetContext&) {
        Column({}, {}, [] {
            Button("Themed", ActionRunCallback("x"));
            Button("Custom", ActionRunCallback("x"), Padding(GlanceModifier{}, 4), true, TextStyle{ .fontSize = 12 },
                   ButtonDefaults::buttonColors(0xFF112233u, 0xFF445566u));
            Button("Off", ActionRunCallback("x"), {}, false);
            GlanceTheme(ColorProviders{ .primary = 0xFFAA0000u, .onPrimary = 0xFF00AA00u }, [] { Button("Red", ActionRunCallback("x")); });
        });
    } });
    const Node column = ReadTree(Render("test-buttons", { "test-buttons", 1, { 100, 100 } }));
    using K = GlanceModifier::Item::Kind;
    const Node& themed = column.children[0];
    // ButtonElement then translateEmittableButton: tap, the colours' background, enabled, 16 dp
    // corners, default padding; a button has its own ripple, so no wrapping box.
    CHECK(themed.kind == 6);
    CHECK(themed.modifiers == (std::vector<int>{ int(K::Clickable), int(K::BackgroundColor), int(K::Enabled), int(K::CornerRadius), int(K::Padding) }));
    CHECK(themed.Find(K::BackgroundColor)->colorResource == "kiln_glance_colorPrimary");
    CHECK(ReadText(themed).color.colorResource == "kiln_glance_colorOnPrimary");
    CHECK(themed.Find(K::Padding)->padding[0] == 16 && themed.Find(K::Padding)->padding[1] == 8 && themed.Find(K::CornerRadius)->dp == 16);
    const Node& custom = column.children[1];
    CHECK(custom.Find(K::BackgroundColor)->day == 0xFF112233u && ReadText(custom).color.day == 0xFF445566u && ReadText(custom).fontSize == 12);
    CHECK(custom.Find(K::Padding)->padding[0] == 4);   // its own padding: no default
    // Disabled: no tap.
    CHECK(!column.children[2].Find(K::Clickable));
    CHECK(column.children[3].Find(K::BackgroundColor)->day == 0xFFAA0000u && ReadText(column.children[3]).color.day == 0xFF00AA00u);
    CHECK(DynamicThemeColorProviders().widgetBackground.resource == "kiln_glance_colorWidgetBackground");
    CHECK(GlanceTheme::colors() == DynamicThemeColorProviders());   // outside content: the default
}

TEST(GlanceRipplesAndBackgroundImagesWrapTheElement) {
    RegisterWidget("test-ripple", { .content = [](const WidgetContext&) {
        Column({}, {}, [] {
            Text("tap", Clickable(Size(GlanceModifier{}, 40), ActionRunCallback("x")));
            Text("own", Clickable(GlanceModifier{}, ActionRunCallback("x"), "my_ripple"));
            Box(CornerRadius(Background(Width(GlanceModifier{}, 80), ImageProvider::Resource("bg"), ContentScale::Crop, ColorFilter::Tint(0xFF00FF00u)), 8),
                Alignment::Center());
            Text("plain", Background(GlanceModifier{}, 0xFF123456u));
        });
    } });
    const Node column = ReadTree(Render("test-ripple", { "test-ripple", 1, { 100, 100 } }));
    using K = GlanceModifier::Item::Kind;
    // transformBackgroundImageAndActionRipple: the tap and size on a box, the text filling it,
    // the ripple image in front.
    const Node& tap = column.children[0];
    CHECK(tap.kind == 1 && tap.modifiers == (std::vector<int>{ int(K::Clickable), int(K::Width), int(K::Height) }));
    CHECK(tap.children.size() == 2 && tap.children[0].kind == 4 && tap.children[1].kind == 5);
    CHECK(tap.children[0].modifiers == (std::vector<int>{ int(K::Width), int(K::Height) }) && tap.children[0].Find(K::Width)->dimension == 1);
    jvm::Reader ripple(tap.children[1].props);
    CHECK(ripple.Str() == "kiln_glance_ripple");
    jvm::Reader own(column.children[1].children[1].props);
    CHECK(own.Str() == "my_ripple");
    // A background image: an image behind the content, the size and corners on the box.
    const Node& bg = column.children[2];
    CHECK(bg.kind == 1 && bg.modifiers == (std::vector<int>{ int(K::Width), int(K::CornerRadius) }));
    CHECK(bg.children.size() == 2 && bg.children[0].kind == 5 && bg.children[1].kind == 1);
    jvm::Reader image(bg.children[0].props);
    CHECK(image.Str() == "bg");
    image.Bytes(); image.Str(); image.Bool(); image.Str();
    CHECK(image.I32() == int(ContentScale::Crop) && image.Bool());
    // A background colour alone: no wrapping.
    CHECK(column.children[3].kind == 4 && column.children[3].modifiers == (std::vector<int>{ int(K::BackgroundColor) }));
}

TEST(GlanceTextStylesAndSemantics) {
    RegisterWidget("test-text", { .content = [](const WidgetContext&) {
        Text("styled", Semantics(GlanceModifier{}, [](SemanticsPropertyReceiver& s) { s.SetContentDescription("a"); s.SetTestTag("t"); }),
             { .color = ColorProvider::Resource("my_color"), .fontWeight = FontWeight::Medium, .fontStyle = FontStyle::Italic,
               .textAlign = TextAlign::End, .textDecoration = TextDecoration::Underline() + TextDecoration::LineThrough(),
               .fontFamily = FontFamily::Monospace() });
    } });
    const Node text = ReadTree(Render("test-text", { "test-text", 1, { 100, 100 } }));
    using K = GlanceModifier::Item::Kind;
    CHECK(text.Find(K::Semantics)->description == "a");
    const TextProps t = ReadText(text);
    CHECK(t.color.colorResource == "my_color" && t.weight == 500 && t.fontStyle == 1 && t.align == int(TextAlign::End) && t.decoration == 3 && t.family == "monospace");
    CHECK((TextDecoration::Underline() + TextDecoration::LineThrough()).Contains(TextDecoration::Underline()));
    CHECK(!TextDecoration::Underline().Contains(TextDecoration::LineThrough()));
    CHECK(TextDecoration::Combine({ TextDecoration::Underline(), TextDecoration::LineThrough() }).mask == 3);
    SemanticsConfiguration c;
    c.SetContentDescription("x");
    CHECK(c.Get(SemanticsProperties::ContentDescription()) == std::vector<std::string>{ "x" } && !c.GetOrNull(SemanticsProperties::TestTag()));
    bool threw = false;
    try { c.Get(SemanticsProperties::TestTag()); } catch (const std::logic_error&) { threw = true; }
    CHECK(threw);
    CHECK(SemanticsProperties::ContentDescription().Merge(std::vector<std::string>{ "p" }, { "c" }) == (std::vector<std::string>{ "p", "c" }));
    CHECK(SemanticsProperties::TestTag().Merge(std::string("p"), "c") == std::string("p"));
    // GlanceModifier: then, any, all, foldIn, foldOut.
    GlanceModifier m = Padding(GlanceModifier{}, 1).Then(Width(GlanceModifier{}, 3));
    CHECK(m.Items().size() == 2 && m.Any([](const GlanceModifier::Item& i) { return i.kind == K::Width; }));
    CHECK(!m.All([](const GlanceModifier::Item& i) { return i.kind == K::Width; }));
    CHECK(m.FoldIn<std::string>("", [](std::string acc, const GlanceModifier::Item& i) { return acc + std::to_string(int(i.kind)); }) == "20");
    CHECK(m.FoldOut<std::string>("", [](const GlanceModifier::Item& i, std::string acc) { return acc + std::to_string(int(i.kind)); }) == "02");
}

TEST(GlanceActionsCarryParametersAndLambdas) {
    const ActionParameters::Key<std::string> name{ "name" };
    const ActionParameters::Key<std::int32_t> count{ "count" };
    ActionParameters p = ActionParametersOf({ name.to("a"), count.to(3) });
    CHECK(p.Get(name) == std::string("a") && p.GetOrDefault(count, 0) == 3 && p.Contains(count) && !p.IsEmpty());
    CHECK(!p.Get(ActionParameters::Key<bool>{ "name" }));   // another type
    MutableActionParameters mp = ToMutableParameters(p);
    CHECK(mp.Set(count, std::optional<std::int32_t>(4)) == 3 && mp.Get(count) == 4);
    CHECK(mp.Set<std::int32_t>(count, std::nullopt) == 4 && !mp.Contains(count));
    CHECK(mp.Remove(name) == std::string("a") && mp.IsEmpty());
    CHECK(ToParametersKey(datastore::IntKey("n")).name == "n");
    MutableActionParameters all = MutableActionParametersOf({ ActionParameters::Key<bool>{ "b" }.to(true), ActionParameters::Key<std::int64_t>{ "l" }.to(5),
                                                              ActionParameters::Key<float>{ "f" }.to(1.5f), ActionParameters::Key<double>{ "d" }.to(2.5),
                                                              name.to("s") });
    CHECK(ParametersFromBytes(ParametersToBytes(all)) == all);

    int lambdaRuns = 0, keyedRuns = 0;
    RegisterWidget("test-actions", { .content = [&](const WidgetContext&) {
        Column({}, {}, [&] {
            Text("lambda", Clickable(GlanceModifier{}, [&] { ++lambdaRuns; }));
            Button("keyed", [&] { ++keyedRuns; }, {}, true, std::nullopt, std::nullopt, INT_MAX, "k");
            Text("broadcast", Clickable(GlanceModifier{}, ActionSendBroadcast("my.ACTION", "com.x.Receiver")));
            Text("service", Clickable(GlanceModifier{}, ActionStartService("com.x.Service", true)));
            Text("open", Clickable(GlanceModifier{}, ActionStartActivity("app://x", ActionParametersOf({ name.to("v") }))));
            CheckBox(true, ActionRunCallback("toggle"));
        });
    } });
    const Node column = ReadTree(Render("test-actions", { "test-actions", 9, { 100, 100 } }));
    using K = GlanceModifier::Item::Kind;
    // updateLambdaActionKeys: the key, then its place among its siblings (the ripple box's).
    const Mod* lambda = column.children[0].Find(K::Clickable);
    CHECK(lambda->actionKind == int(Action::Kind::Lambda) && lambda->actionKey == "0+0");
    const Mod* keyed = column.children[1].Find(K::Clickable);
    CHECK(keyed->actionKey == "k+1");
    CHECK(column.children[2].Find(K::Clickable)->actionKind == int(Action::Kind::SendBroadcast));
    CHECK(column.children[3].Find(K::Clickable)->actionKind == int(Action::Kind::StartService));
    CHECK(ParametersFromBytes(column.children[4].Find(K::Clickable)->params).Get(name) == std::string("v"));
    // A check box keeps its own ripple: the action stays on it.
    CHECK(column.children[5].kind == 10 && column.children[5].Find(K::Clickable)->actionKey == "toggle");
    CHECK(Await(RunLambdaAction("test-actions", 9, "0+0")) && lambdaRuns == 1);
    CHECK(Await(RunLambdaAction("test-actions", 9, "k+1")) && keyedRuns == 1);
    CHECK(!Await(RunLambdaAction("test-actions", 9, "missing")));
    // An instance this process has not drawn: drawn first.
    CHECK(Await(RunLambdaAction("test-actions", 10, "k+1")) && keyedRuns == 2);
    // Several sizes: each size's lambdas, told apart by the size's index.
    Drawing two = Await(Draw("test-actions", { "test-actions", 11, {}, {} }, { { 100, 100 }, { 200, 100 } }));
    CHECK(two.contents.size() == 2 && ReadTree(two.contents[1]).children[1].Find(K::Clickable)->actionKey == "k+1@1");
    CHECK(Await(RunLambdaAction("test-actions", 11, "k+1@1")) && keyedRuns == 3);
    CHECK(Await(RunLambdaAction("test-actions", 11, "k+1")) && keyedRuns == 4);
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
    MutableActionParameters params;
    params.Set(ToggleableStateKey, std::optional<bool>(true));
    c.parameters = params;
    CHECK(Await(RunActionCallback("test-toggle", c)) && runs == 1 && got.appWidgetId == 3 && got.parameters.Get(ToggleableStateKey) == true);
    CHECK(!Await(RunActionCallback("test-missing", c)));
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
    const Node tree = ReadTree(Render("test-c", { "test-c", 1, { 200, 100 } }));
    using K = GlanceModifier::Item::Kind;
    CHECK(tree.kind == 3 && tree.children.size() == 2 && tree.children[1].kind == 6);
    CHECK(tree.modifiers == (std::vector<int>{ int(K::BackgroundColor), int(K::Width), int(K::Padding) }));
    static std::string paramsSeen;
    kiln_fn_glance_action add{};
    add.call = [](void*, const char*, int32_t, const char* params, int32_t) { paramsSeen = params; };
    kiln_glance_register_action("c-add", add);
    CHECK(Await(RunActionCallback("c-add", { "test-c", 1, ParametersFromBytes(tree.children[1].Find(K::Clickable)->params) })) && paramsSeen == "x");
    static int checkedSeen = -2;
    kiln_fn_glance_action action{};
    action.call = [](void*, const char*, int32_t, const char*, int32_t checked) { checkedSeen = checked; };
    kiln_glance_register_action("c-toggle", action);
    ActionContext c;
    MutableActionParameters off;
    off.Set(ToggleableStateKey, std::optional<bool>(false));
    c.parameters = off;
    CHECK(Await(RunActionCallback("c-toggle", c)) && checkedSeen == 0);
}

TEST(GlanceStateIsPerInstanceAndLoadedBeforeDrawing) {
    const std::string dir = test::MakeTempDir("kiln-glance-");
    ui::SetAppDirectories(dir + "/f", dir + "/c", dir + "/n");
    const auto kCount = datastore::IntKey("count");
    std::vector<std::string> seen;
    int provided = 0;
    RegisterWidget("test-state", { .content = [&](const WidgetContext& w) {
        seen.push_back(std::to_string(w.appWidgetId) + ":" + std::to_string(CurrentState(kCount).value_or(0)));
        Text("x");
    } });
    Await(UpdateAppWidgetState(21, [&](datastore::MutablePreferences& p) { p.Set(kCount, 5); }));
    Drawing d = Await(Draw("test-state", { "test-state", 21, {}, {} }, { { 100, 100 }, { 200, 100 } }));
    CHECK(!d.failed && d.contents.size() == 2 && seen == (std::vector<std::string>{ "21:5", "21:5" }));
    // Another instance has its own state.
    Await(Draw("test-state", { "test-state", 22, {}, {} }, { { 100, 100 } }));
    CHECK(seen.back() == "22:0");
    CHECK(Await(GetAppWidgetState(*PreferencesGlanceStateDefinition(), 21)).Get(kCount) == 5);
    CHECK(std::any_cast<datastore::Preferences>(Await(GetAppWidgetState(std::string("test-state"), 21))).Get(kCount) == 5);
    // Deleted: onDelete, then the state is gone.
    int deletedId = 0;
    RegisterWidget("test-state", { .content = [&](const WidgetContext& w) {
        seen.push_back(std::to_string(w.appWidgetId) + ":" + std::to_string(CurrentState(kCount).value_or(0)));
    }, .onDelete = [&](GlanceId id) -> rt::Task<> { deletedId = id; co_return; } });
    Await(Deleted("test-state", 21));
    CHECK(deletedId == 21);
    Await(Draw("test-state", { "test-state", 21, {}, {} }, { { 100, 100 } }));
    CHECK(seen.back() == "21:0");

    // provideGlance: run before the content, which it provides.
    RegisterWidget("test-provide", { .provideGlance = [&](WidgetContext w) -> rt::Task<kiln::Function<void(const WidgetContext&)>> {
        ++provided;
        std::string label = "for " + std::to_string(w.appWidgetId);
        co_return ProvideContent([label](const WidgetContext&) { Text(label); });
    }, .stateDefinition = nullptr });
    Drawing p = Await(Draw("test-provide", { "test-provide", 3, {}, {} }, { { 100, 100 } }));
    CHECK(provided == 1 && p.contents.size() == 1 && ReadText(ReadTree(p.contents[0])).text == "for 3");

    // A custom state definition: a typed DataStore per instance.
    datastore::Serializer<std::int32_t> serializer{
        .defaultValue = 0,
        .readFrom = [](std::string_view b) { return b.empty() ? 0 : std::int32_t(std::uint8_t(b[0])); },
        .writeTo = [](const std::int32_t& v) { return std::string(1, char(v)); },
    };
    auto definition = std::make_shared<GlanceStateDefinition<std::int32_t>>(
        [serializer](const std::string& key) { return datastore::DataStoreFactory::Create(serializer, datastore::DataStoreFile(key + ".n")); },
        [](const std::string& key) { return datastore::DataStoreFile(key + ".n"); });
    int drawnWith = -1;
    RegisterWidget("test-typed", { .content = [&](const WidgetContext&) { drawnWith = CurrentState<std::int32_t>(); }, .stateDefinition = definition });
    CHECK(Await(UpdateAppWidgetState<std::int32_t>(*definition, 4, [](std::int32_t v) { return v + 7; })) == 7);
    Await(Draw("test-typed", { "test-typed", 4, {}, {} }, { { 10, 10 } }));
    CHECK(drawnWith == 7 && Await(GetAppWidgetState(*definition, 4)) == 7);
    definition->DeleteStore(AppWidgetStateKey(4));
    CHECK(Await(GetAppWidgetState(*definition, 4)) == 0);

    // A content that throws: the error layout, or onCompositionError.
    RegisterWidget("test-error", { .content = [](const WidgetContext&) { throw std::runtime_error("boom"); }, .stateDefinition = nullptr });
    Drawing e = Await(Draw("test-error", { "test-error", 1, {}, {} }, { { 10, 10 } }));
    CHECK(e.failed && e.contents.empty() && e.errorLayout == "kiln_glance_error");
    bool reported = false;
    RegisterWidget("test-error", { .content = [](const WidgetContext&) { throw std::runtime_error("boom"); }, .stateDefinition = nullptr,
                                   .onCompositionError = [&](const WidgetContext&, std::exception_ptr) { reported = true; } });
    e = Await(Draw("test-error", { "test-error", 1, {}, {} }, { { 10, 10 } }));
    CHECK(e.failed && reported && e.errorLayout.empty());
    CHECK(!Await(Draw("nobody", {}, {})).found);

    CHECK(!RedrawsOnResize({ .sizeMode = SizeMode::Single }, 34) && RedrawsOnResize({ .sizeMode = SizeMode::Exact }, 34));
    CHECK(RedrawsOnResize({ .sizeMode = SizeMode::Responsive }, 31) && !RedrawsOnResize({ .sizeMode = SizeMode::Responsive }, 32));
    ui::SetAppDirectories({}, {}, {});
}

TEST(GlanceAsyncCallbacksAndUpdates) {
    int ran = 0;
    RegisterActionCallback("test-async", kiln::Function<rt::Task<>(ActionContext)>([&](ActionContext c) -> rt::Task<> {
        ran = c.appWidgetId;
        co_return;
    }));
    CHECK(Await(RunActionCallback("test-async", { "w", 8, {} })) && ran == 8);
    // The host's update and pin requests.
    std::vector<std::string> calls;
    GlanceHost host;
    host.update = [&](const std::string& w, int id) { calls.push_back(w + "/" + std::to_string(id)); };
    host.appWidgetIds = [](const std::string&) { return std::vector<int>{ 1, 2 }; };
    host.appWidgetSizes = [](int) { return std::vector<ui::Size>{ { 120, 80 } }; };
    host.requestPin = [&](const std::string& w, const std::vector<std::uint8_t>& preview, const std::string& link) {
        calls.push_back("pin " + w + " " + std::to_string(!preview.empty()) + " " + link);
        return true;
    };
    SetGlanceHost(std::move(host));
    const std::string dir = test::MakeTempDir("kiln-glance-");
    ui::SetAppDirectories(dir + "/f", dir + "/c", dir + "/n");
    const auto kOn = datastore::BoolKey("on");
    RegisterWidget("test-update", { .content = [](const WidgetContext&) { Text("x"); } });
    Await(UpdateAppWidgetState(2, [&](datastore::MutablePreferences& p) { p.Set(kOn, true); }));
    Await(UpdateIf<datastore::Preferences>("test-update", [&](const datastore::Preferences& p) { return p.Get(kOn).value_or(false); }));
    CHECK(calls == std::vector<std::string>{ "test-update/2" });
    CHECK(GetGlanceIds("test-update").size() == 2 && GetAppWidgetSizes(1) == (std::vector<ui::Size>{ { 120, 80 } }));
    CHECK(Await(RequestPinGlanceAppWidget("test-update", ui::Size{ 100, 100 }, "app://placed")) && calls.back() == "pin test-update 1 app://placed");
    CHECK(RequestPin("test-update") && calls.back() == "pin test-update 0 ");
    SetGlanceHost({});
    ui::SetAppDirectories({}, {}, {});
}

namespace {
    struct ToggleProps {
        bool checked = false, enabled = false;
        std::string text;
        std::string resource, resource2;
        std::uint32_t checkedDay = 0, uncheckedDay = 0;
    };
    ToggleProps ReadToggle(const Node& n) {
        jvm::Reader r(n.props);
        ToggleProps t;
        t.checked = r.Bool();
        t.enabled = r.Bool();
        t.text = r.Str();
        if (r.Bool()) { ReadColor(r); r.F32(); r.I32(); r.I32(); r.I32(); r.I32(); r.Str(); }
        r.I32();
        t.resource = r.Str();
        Mod c, u;
        ReadColor(r, &c);
        ReadColor(r, &u);
        t.checkedDay = c.day;
        t.uncheckedDay = u.day;
        if (n.kind == 11) { t.resource2 = r.Str(); ReadColor(r); ReadColor(r); }
        return t;
    }
}

TEST(GlanceTogglesTakeTheirColoursAndRadioButtonsClick) {
    RegisterWidget("test-toggles", { .content = [](const WidgetContext&) {
        Column(SelectableGroup({}), {}, [] {
            CheckBox(true, ActionRunCallback("c"), {}, "check");
            Switch(false, ActionRunCallback("s"));
            RadioButton(true, ActionRunCallback("r"), {}, true, "one");
            RadioButton(false, ActionRunCallback("r"), {}, false, "off");
            GlanceTheme(ColorProviders{ .primary = 0xFF0000AAu, .onPrimary = 0xFF0000BBu, .onSurface = 0xFF0000CCu, .surfaceVariant = 0xFF0000FFu,
                                        .onSurfaceVariant = 0xFF0000DDu, .outline = 0xFF0000EEu },
                        [] {
                            CheckBox(false, ActionRunCallback("c"));
                            Switch(true, ActionRunCallback("s"));
                            RadioButton(false, ActionRunCallback("r"));
                        });
            CheckBox(false, ActionRunCallback("c"), {}, {}, std::nullopt, CheckboxDefaults::colors(0xFF111111u, 0xFF222222u));
        });
    } });
    const Node column = ReadTree(Render("test-toggles", { "test-toggles", 1, { 100, 100 } }));
    using K = GlanceModifier::Item::Kind;
    CHECK(column.Find(K::SelectableGroup));
    // The dynamic theme: Glance's colour state lists.
    CHECK(ReadToggle(column.children[0]).resource == "kiln_glance_default_check_box" && ReadToggle(column.children[0]).checked);
    CHECK(ReadToggle(column.children[1]).resource == "kiln_glance_default_switch_thumb" && ReadToggle(column.children[1]).resource2 == "kiln_glance_default_switch_track");
    const Node& radio = column.children[2];
    CHECK(radio.kind == 14 && ReadToggle(radio).resource == "kiln_glance_default_radio_button" && ReadToggle(radio).text == "one");
    // Radio buttons have their own ripple: no wrapping box; a disabled one has no tap.
    CHECK(radio.Find(K::Clickable) && !column.children[3].Find(K::Clickable) && !ReadToggle(column.children[3]).enabled);
    // Another theme: its roles.
    CHECK(ReadToggle(column.children[4]).checkedDay == 0xFF0000AAu && ReadToggle(column.children[4]).uncheckedDay == 0xFF0000CCu);
    CHECK(ReadToggle(column.children[5]).checkedDay == 0xFF0000BBu && ReadToggle(column.children[5]).uncheckedDay == 0xFF0000EEu);
    CHECK(ReadToggle(column.children[6]).uncheckedDay == 0xFF0000DDu);
    CHECK(ReadToggle(column.children[7]).checkedDay == 0xFF111111u && ReadToggle(column.children[7]).resource.empty());
    bool threw = false;
    try { CheckboxDefaults::colors(ColorProvider::Resource("x"), 0xFF000000u); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(GlanceLazyListsAndGrids) {
    RegisterWidget("test-lazy", { .content = [](const WidgetContext&) {
        Column({}, {}, [] {
            LazyColumn({}, HorizontalAlignment::End, [](LazyListScope& s) {
                s.Item([] { Text("a"); });
                s.Item([] { Text("b"); });
            });
            LazyVerticalGrid(GridCells::Fixed(3), {}, [](LazyVerticalGridScope& s) {
                s.Items(std::vector<std::string>{ "x", "y" }, [](const std::string& v) { return std::int64_t(v[0]); }, [](const std::string& v) { Text(v); });
            });
            LazyVerticalGrid(GridCells::Adaptive(80), {}, [](LazyVerticalGridScope& s) { s.Item(5, [] { Text("z"); }); });
            LinearProgressIndicator();
        });
    } });
    const Node column = ReadTree(Render("test-lazy", { "test-lazy", 1, { 100, 100 } }));
    using K = GlanceModifier::Item::Kind;
    const Node& list = column.children[0];
    // Implicit ids from the reserved range, not stable.
    jvm::Reader id0(list.children[0].props), id1(list.children[1].props);
    CHECK(id0.I64() == ReservedItemIdRangeEnd && id1.I64() == ReservedItemIdRangeEnd - 1);
    jvm::Reader lp(list.props);
    lp.I32(); lp.F32(); lp.Bool(); lp.Str(); lp.Str(); lp.Str(); lp.Str();
    CHECK(!lp.Bool());
    // The item's box takes the list's horizontal alignment.
    jvm::Reader boxGravity(list.children[0].children[0].props);
    CHECK(boxGravity.I32() == (0x00800005 | 0x10));
    const Node& grid = column.children[1];
    CHECK(grid.kind == 15 && grid.children.size() == 2);
    jvm::Reader gp(grid.props);
    CHECK(gp.I32() == 3);
    gp.F32(); gp.Bool(); gp.Str(); gp.Str(); gp.Str(); gp.Str();
    CHECK(gp.Bool());   // its own ids: stable
    // A grid item fills its cell's width.
    CHECK(grid.children[0].children[0].Find(K::Width)->dimension == 1);
    jvm::Reader ap(column.children[2].props);
    CHECK(ap.I32() == 0 && ap.F32() == 80);
    jvm::Reader progress(column.children[3].props);
    progress.F32();
    CHECK(progress.Bool());   // indeterminate
    bool threw = false;
    RegisterWidget("test-lazy-bad", { .content = [](const WidgetContext&) {
        LazyColumn({}, [](LazyListScope& s) { s.Item(ReservedItemIdRangeEnd, [] {}); });
    }, .stateDefinition = nullptr });
    try { Render("test-lazy-bad", { "test-lazy-bad", 1, { 10, 10 } }); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
    threw = false;
    try { LazyVerticalGrid(GridCells::Fixed(6), {}, [](LazyVerticalGridScope&) {}); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);
}

TEST(GlanceComponentsAndChecks) {
    RegisterWidget("test-components", { .content = [](const WidgetContext&) {
        Scaffold({}, [] { TitleBar(ImageProvider::Resource("icon"), "Title"); }, [] {
            Column({}, {}, [] {
                FilledButton("Go", ActionRunCallback("go"));
                OutlineButton("Out", 0xFF00FF00u, ActionRunCallback("go"), {}, true, ImageProvider::Resource("ic"));
                SquareIconButton(ImageProvider::Resource("ic"), std::string("square"), ActionRunCallback("go"));
                CircleIconButton(ImageProvider::Resource("ic"), std::nullopt, ActionRunCallback("go"), {}, true, std::nullopt);
                AndroidRemoteViews("custom_layout", "container", {}, [] { Text("inside"); });
            });
        });
    } });
    const Node scaffold = ReadTree(Render("test-components", { "test-components", 1, { 200, 200 } }));
    using K = GlanceModifier::Item::Kind;
    CHECK(scaffold.kind == 3 && scaffold.Find(K::AppWidgetBackground) && scaffold.Find(K::BackgroundColor)->colorResource == "kiln_glance_colorWidgetBackground");
    CHECK(scaffold.Find(K::CornerRadius)->resource == "android:dimen/system_app_widget_background_radius");
    const Node& title = scaffold.children[0];
    CHECK(title.kind == 2 && title.children.size() == 2 && ReadText(title.children[1]).text == "Title" && ReadText(title.children[1]).weight == 500);
    const Node& column = scaffold.children[1].children[0];
    // FilledButton: a clickable box (the M3 shape as a tinted background image behind the text).
    const Node& filled = column.children[0];
    CHECK(filled.kind == 1 && filled.Find(K::Clickable) && filled.Find(K::CornerRadius)->resource == "kiln_glance_component_button_corners");
    CHECK(filled.children[0].kind == 5);
    jvm::Reader shape(filled.children[0].props);
    CHECK(shape.Str() == "kiln_glance_component_btn_filled");
    // The content: the size fix, then the text in onPrimary.
    const Node& inner = filled.children[1];
    CHECK(inner.children.size() == 2 && ReadText(inner.children[1]).color.colorResource == "kiln_glance_colorOnPrimary");
    // With an icon: a row of the tinted icon, a spacer and the text.
    CHECK(column.children[1].children[1].children[0].kind == 2 && column.children[1].children[1].children[0].children.size() == 3);
    // Icon buttons: the size and tap on the ripple box; the description on the button's box.
    const Node& square = column.children[2];
    CHECK(square.Find(K::Width) && square.Find(K::Width)->dp == 60 && square.Find(K::Clickable) && square.children.size() == 3);
    CHECK(square.children[1].Find(K::Semantics) && square.children[1].Find(K::Semantics)->description == "square");
    const Node& circle = column.children[3];
    // No background: the button's box and the ripple.
    CHECK(circle.Find(K::Width) && circle.Find(K::Width)->dp == 48 && circle.children.size() == 2 && circle.children[0].children[0].kind == 5);
    const Node& custom = column.children[4];
    jvm::Reader cp(custom.props);
    CHECK(custom.kind == 16 && cp.Str() == "custom_layout" && cp.Str() == "container" && custom.children.size() == 1);

    // ApplyModifiers' checks, as composition errors.
    RegisterWidget("test-checks", { .content = [](const WidgetContext&) { Text("x", SelectableGroup({})); }, .stateDefinition = nullptr });
    CHECK(Await(Draw("test-checks", { "test-checks", 1, {}, {} }, { { 10, 10 } })).failed);
    RegisterWidget("test-checks", { .content = [](const WidgetContext&) {
        Column(AppWidgetBackground({}), {}, [] { Box(AppWidgetBackground({}), {}); });
    }, .stateDefinition = nullptr });
    CHECK(Await(Draw("test-checks", { "test-checks", 1, {}, {} }, { { 10, 10 } })).failed);
    // Children need a container to go in.
    RegisterWidget("test-checks", { .content = [](const WidgetContext&) { AndroidRemoteViews("l", "", {}, [] { Text("x"); }); }, .stateDefinition = nullptr });
    CHECK(Await(Draw("test-checks", { "test-checks", 1, {}, {} }, { { 10, 10 } })).failed);
}

TEST(GlancePreviewsLeaveTapsOut) {
    RegisterWidget("test-preview", { .content = [](const WidgetContext&) { Text("live"); },
                                     .providePreview = [](int category) -> rt::Task<kiln::Function<void(const WidgetContext&)>> {
                                         co_return ProvideContent([category](const WidgetContext&) {
                                             Text("preview " + std::to_string(category), Clickable(GlanceModifier{}, ActionRunCallback("x")));
                                         });
                                     },
                                     .previewSizeMode = SizeMode::Responsive, .previewSizes = { { 100, 50 }, { 200, 100 } } });
    Drawing d = Await(ComposeForPreview("test-preview", WIDGET_CATEGORY_HOME_SCREEN));
    CHECK(d.contents.size() == 2);
    const Node t = ReadTree(d.contents[0]);
    CHECK(t.kind == 4 && ReadText(t).text == "preview 1" && !t.Find(GlanceModifier::Item::Kind::Clickable));
    std::vector<std::string> calls;
    GlanceHost host;
    host.setWidgetPreview = [&](const std::string& w, int category, const std::vector<ui::Size>& sizes, const std::vector<std::vector<std::uint8_t>>& contents) {
        calls.push_back(w + " " + std::to_string(category) + " " + std::to_string(sizes.size()) + " " + std::to_string(contents.size()));
        return category != WIDGET_CATEGORY_KEYGUARD;
    };
    SetGlanceHost(std::move(host));
    CHECK(Await(SetWidgetPreviews("test-preview", { WIDGET_CATEGORY_HOME_SCREEN })) == SET_WIDGET_PREVIEWS_RESULT_SUCCESS);
    CHECK(Await(SetWidgetPreviews("test-preview", { WIDGET_CATEGORY_KEYGUARD, WIDGET_CATEGORY_SEARCHBOX })) == SET_WIDGET_PREVIEWS_RESULT_RATE_LIMITED);
    CHECK(calls == (std::vector<std::string>{ "test-preview 1 2 2", "test-preview 2 2 2" }));
    SetGlanceHost({});
}
