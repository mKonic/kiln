// Subtitles in the player: media3's MergingCuesResolverTest, DefaultTrackSelectorTest's text
// track choices, and side-loaded subtitles showing at the position.
#include "Check.h"

#include "kiln/media/Captioning.h"
#include "kiln/media/Player.h"
#include "kiln/media/TrackSelection.h"
#include "kiln/runtime/Task.h"

#include <chrono>
#include <climits>
#include <map>
#include <set>
#include <thread>

using namespace kiln;
using namespace kiln::media;

namespace {
    std::vector<Cue> CuesOf(std::initializer_list<const char*> texts) {
        std::vector<Cue> cues;
        for (const char* t : texts) cues.push_back(Cue{ .text = t });
        return cues;
    }
    std::string Texts(const std::vector<Cue>& cues) {
        std::string s;
        for (const auto& c : cues) s += (s.empty() ? "" : "|") + c.text.Text();
        return s;
    }
    // CuesListTestUtil: the text throughout [start, end), changes at both ends.
    bool TextBetween(const MergingCuesResolver& r, std::int64_t start, std::int64_t end, const std::string& text) {
        return Texts(r.GetCuesAtTimeUs(start)) == text && Texts(r.GetCuesAtTimeUs((start + end) / 2)) == text
               && Texts(r.GetCuesAtTimeUs(end - 1)) == text && r.GetPreviousCueChangeTimeUs(end - 1) == start
               && r.GetNextCueChangeTimeUs(start) == end;
    }
    const auto kFirst = [] { return CuesOf({ "first cue" }); };
    const auto kSecond = [] { return CuesOf({ "second group: cue1", "second group: cue2" }); };
    const auto kThird = [] { return CuesOf({ "third cue" }); };

    MediaItem::SubtitleConfiguration Text(std::string language, int flags = 0, int roles = 0, std::string label = {}) {
        return { .uri = "x.vtt", .mimeType = "text/vtt", .language = std::move(language), .label = std::move(label), .selectionFlags = flags, .roleFlags = roles };
    }
    int Select(const std::vector<MediaItem::SubtitleConfiguration>& tracks, const TrackSelectionParameters& p = {},
               std::optional<std::string> audio = std::nullopt) {
        return SelectTextTrack(tracks, std::vector<bool>(tracks.size(), true), p, std::nullopt, audio);
    }
}

TEST(MergingCuesResolverAsMedia3) {
    {
        MergingCuesResolver r;
        CHECK(r.GetPreviousCueChangeTimeUs(999'999'999) == kTimeUnset && r.GetNextCueChangeTimeUs(0) == kTimeEndOfSource && r.GetCuesAtTimeUs(0).empty());
    }
    {   // nonOverlappingCues (added in reverse)
        MergingCuesResolver r;
        r.AddCues(CuesWithTiming(kSecond(), 6'000'000, 1'000'000), 0);
        r.AddCues(CuesWithTiming(kFirst(), 3'000'000, 2'000'000), 0);
        CHECK(r.GetCuesAtTimeUs(2'999'999).empty() && r.GetNextCueChangeTimeUs(0) == 3'000'000);
        CHECK(TextBetween(r, 3'000'000, 5'000'000, "first cue"));
        CHECK(r.GetCuesAtTimeUs(5'500'000).empty() && r.GetNextCueChangeTimeUs(5'000'000) == 6'000'000);
        CHECK(TextBetween(r, 6'000'000, 7'000'000, "second group: cue1|second group: cue2"));
        CHECK(r.GetNextCueChangeTimeUs(7'000'000) == kTimeEndOfSource && r.GetCuesAtTimeUs(7'000'000).empty());
    }
    {   // overlappingCues: the later start later in the list
        MergingCuesResolver r;
        r.AddCues(CuesWithTiming(kFirst(), 1'000'000, 3'000'000), 0);
        r.AddCues(CuesWithTiming(kSecond(), 2'000'000, 4'000'000), 0);
        CHECK(TextBetween(r, 1'000'000, 2'000'000, "first cue"));
        CHECK(TextBetween(r, 2'000'000, 4'000'000, "first cue|second group: cue1|second group: cue2"));
        CHECK(TextBetween(r, 4'000'000, 6'000'000, "second group: cue1|second group: cue2"));
    }
    {   // matching start times: the shorter later
        MergingCuesResolver r;
        r.AddCues(CuesWithTiming(kFirst(), 1'000'000, 4'000'000), 0);
        r.AddCues(CuesWithTiming(kSecond(), 1'000'000, 3'000'000), 0);
        CHECK(TextBetween(r, 1'000'000, 4'000'000, "first cue|second group: cue1|second group: cue2"));
        CHECK(TextBetween(r, 4'000'000, 5'000'000, "first cue"));
    }
    {   // matching end times
        MergingCuesResolver r;
        r.AddCues(CuesWithTiming(kFirst(), 1'000'000, 4'000'000), 0);
        r.AddCues(CuesWithTiming(kSecond(), 2'000'000, 3'000'000), 0);
        CHECK(TextBetween(r, 1'000'000, 2'000'000, "first cue"));
        CHECK(TextBetween(r, 2'000'000, 5'000'000, "first cue|second group: cue1|second group: cue2"));
    }
    {
        MergingCuesResolver r;
        bool threw = false;
        try { r.AddCues(CuesWithTiming(kFirst(), 3'000'000, kTimeUnset), 0); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        // Whether they show at the position.
        CHECK(!r.AddCues(CuesWithTiming(kFirst(), 3'000'000, 2'000'000), 1'000'000));
        CHECK(r.AddCues(CuesWithTiming(kFirst(), 3'000'000, 2'000'000), 3'000'000));
        CHECK(r.AddCues(CuesWithTiming(kFirst(), 3'000'000, 2'000'000), 4'000'000));
        CHECK(!r.AddCues(CuesWithTiming(kFirst(), 3'000'000, 2'000'000), 6'000'000));
        CHECK(!r.AddCues(CuesWithTiming(kFirst(), 3'000'000, 2'000'000), 5'000'000));
    }
    {   // discardCuesBeforeTimeUs, clear
        MergingCuesResolver r;
        r.AddCues(CuesWithTiming(kFirst(), 3'000'000, 2'000'000), 0);
        r.AddCues(CuesWithTiming(kSecond(), 6'000'000, 1'000'000), 0);
        r.AddCues(CuesWithTiming(kThird(), 8'000'000, 4'000'000), 0);
        r.DiscardCuesBeforeTimeUs(6'500'000);
        CHECK(r.GetCuesAtTimeUs(4'999'990).empty() && r.GetPreviousCueChangeTimeUs(4'999'990) == kTimeUnset
              && r.GetNextCueChangeTimeUs(4'999'990) == 6'000'000);
        r.Clear();
        CHECK(r.GetPreviousCueChangeTimeUs(999'999'999) == kTimeUnset && r.GetNextCueChangeTimeUs(0) == kTimeEndOfSource);
    }
}

TEST(TextTrackSelectionAsDefaultTrackSelector) {
    CHECK(NormalizeLanguageCode("eng") == "en" && NormalizeLanguageCode("en_US") == "en-us" && NormalizeLanguageCode("ger") == "de"
          && NormalizeLanguageCode("nb") == "no-nob" && NormalizeLanguageCode("zh-guoyu") == "zh-cmn" && NormalizeLanguageCode("und") == "und");
    // textTrackSelectionFlags
    const auto forcedOnly = Text("eng", kSelectionFlagForced), forcedDefault = Text("eng", kSelectionFlagForced | kSelectionFlagDefault);
    const auto defaultOnly = Text("eng", kSelectionFlagDefault), noFlag = Text("eng");
    CHECK(Select({ forcedOnly, forcedDefault, defaultOnly, noFlag }) == 1);
    CHECK(Select({ forcedOnly, noFlag, defaultOnly }) == 2);
    TrackSelectionParameters ignoreDefault{ .ignoredTextSelectionFlags = kSelectionFlagDefault };
    CHECK(Select({ defaultOnly, noFlag, forcedOnly, forcedDefault }, ignoreDefault) == -1);
    TrackSelectionParameters ignoreBoth{ .ignoredTextSelectionFlags = kSelectionFlagDefault | kSelectionFlagForced };
    CHECK(Select({ forcedOnly, forcedDefault, defaultOnly, noFlag }, ignoreBoth) == -1);
    TrackSelectionParameters eng{ .preferredTextLanguages = { "eng" }, .ignoredTextSelectionFlags = kSelectionFlagDefault | kSelectionFlagForced };
    eng.ignoredTextSelectionFlags = 0;
    CHECK(Select({ forcedOnly, forcedDefault, defaultOnly, noFlag }, eng) == 2);
    eng.ignoredTextSelectionFlags = kSelectionFlagDefault;
    CHECK(Select({ noFlag, forcedOnly, forcedDefault, defaultOnly }, eng) == 0);

    // forcedAndDefaultTextTracksInteractWithSelectedAudioLanguageAsExpected
    const auto forcedEnglish = Text("eng", kSelectionFlagForced), defaultEnglish = Text("eng", kSelectionFlagDefault);
    const auto forcedGerman = Text("deu", kSelectionFlagForced), defaultGerman = Text("deu", kSelectionFlagDefault);
    const auto forcedNoLanguage = Text("und", kSelectionFlagForced);
    CHECK(Select({ forcedNoLanguage }) == 0);
    CHECK(Select({ forcedEnglish, forcedGerman }) == -1);
    CHECK(Select({ forcedGerman, forcedEnglish }, {}, "de") == 0);
    CHECK(Select({ forcedEnglish, forcedGerman }, {}, "de") == 1);
    CHECK(Select({ forcedGerman, defaultGerman, forcedEnglish, defaultEnglish }, {}, "de") == 1);
    CHECK(Select({ forcedGerman, forcedEnglish, defaultEnglish }, {}, "de") == 2);

    // selectUndeterminedTextLanguageAsFallback
    const auto spanish = Text("spa"), german = Text("de"), und = Text("und"), none = Text("");
    CHECK(Select({ spanish, german, und, none }) == -1);
    CHECK(Select({ spanish, german, und, none }, { .selectUndeterminedTextLanguage = true }) == 2);
    TrackSelectionParameters spa{ .preferredTextLanguages = { "spa" } };
    CHECK(Select({ spanish, german, und, none }, spa) == 0);
    CHECK(Select({ german, und, none }, spa) == -1);
    spa.selectUndeterminedTextLanguage = true;
    CHECK(Select({ german, und, none }, spa) == 1);
    CHECK(Select({ german, none }, spa) == 1);
    CHECK(Select({ german }, spa) == -1);

    // Role flags, labels, selectTextByDefault, a disabled type.
    const auto noRoles = Text(""), lessRoles = Text("", 0, kRoleFlagCaption), moreRoles = Text("", 0, kRoleFlagCaption | kRoleFlagCommentary | kRoleFlagDub);
    CHECK(Select({ noRoles, moreRoles, lessRoles }, { .preferredTextRoleFlags = kRoleFlagCaption | kRoleFlagCommentary }) == 1);
    CHECK(Select({ noRoles, moreRoles, lessRoles }, { .preferredTextRoleFlags = kRoleFlagCaption }) == 2);
    CHECK(Select({ Text(""), Text("", 0, 0, "commentary") }, { .preferredTextLabels = { "commentary" } }) == 1);
    CHECK(Select({ Text("", 0, 0, "low_priority"), Text("", 0, 0, "high_priority") }, { .preferredTextLabels = { "high_priority", "low_priority" } }) == 1);
    CHECK(Select({ Text("", 0, kRoleFlagSupplementary, "commentary"), Text("", 0, kRoleFlagCaption) },
                 { .preferredTextRoleFlags = kRoleFlagCaption, .preferredTextLabels = { "commentary" } }) == 1);
    CHECK(Select({ Text("deu", 0, 0, "commentary") }, { .preferredTextLanguages = { "eng" }, .preferredTextLabels = { "commentary" } }) == -1);
    CHECK(Select({ Text("") }) == -1 && Select({ Text("") }, { .selectTextByDefault = true }) == 0);
    CHECK(Select({ Text("fra") }, { .preferredTextLanguages = { "eng" } }) == -1);
    CHECK(Select({ Text("fra") }, { .preferredTextLanguages = { "eng" }, .selectTextByDefault = true }) == 0);
    CHECK(Select({ Text("deu"), Text("eng") }, { .preferredTextLanguages = { "eng" }, .selectTextByDefault = true }) == 1);
    CHECK(Select({ Text("") }, { .selectTextByDefault = true, .disabledTrackTypes = { TrackType::Text } }) == -1);
    // The system's captions: their language, caption roles first.
    const std::vector<MediaItem::SubtitleConfiguration> two{ Text("de"), Text("fr", 0, kRoleFlagCaption) };
    CHECK(SelectTextTrack(two, { true, true }, {}, std::string("fr-FR"), std::nullopt) == 1);
    CHECK(SelectTextTrack(two, { true, false }, {}, std::string("fr-FR"), std::nullopt) == -1);   // can't be read
}

TEST(PlayerShowsSideLoadedSubtitlesAtThePosition) {
    auto& d = rt::Dispatcher::Main();
    const bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    struct Engine : PlayerEngine {
        EngineLoad load;
        bool play = false;
        void Load(EngineLoad l) override { load = l; }
        void SetPlayWhenReady(bool p) override { play = p; }
        void SeekTo(std::int64_t) override {}
        void SetRepeat(bool) override {}
        void SetVolume(float) override {}
        void SetSurface(ui::PlatformSurface) override {}
        void Report(std::int64_t positionMs, bool playing) {
            PlayerState s;
            s.state = PlaybackState::Ready;
            s.durationMs = 60'000;
            s.positionMs = positionMs;
            s.isPlaying = playing;
            onState(s, load.loadId);
        }
    };
    Engine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<Engine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    {
        Player p;
        std::vector<std::string> shown;
        p.AddListener({ .onCues = [&](const CueGroup& g) { shown.push_back(Texts(g.cues)); } });
        MediaItem item = MediaItem::FromUri("asset:///clip.mp4");
        // Two subtitles: the default one shows; a .srt (no mimeType) is told by its extension.
        item.subtitleConfigurations = {
            { .uri = "data:text/vtt,WEBVTT%0A%0A00:01.000 --> 00:03.000%0AHello%0A%0A00:02.000 --> 00:04.000%0Athere%0A", .mimeType = "text/vtt",
              .language = "en", .selectionFlags = kSelectionFlagDefault },
            { .uri = "data:,1%0A00:00:01,000 --> 00:00:02,000%0AHallo%0A", .mimeType = "application/x-subrip", .language = "de" },
        };
        p.SetMediaItem(item);
        p.Prepare();
        p.Play();
        // Read and parsed in the background, then on the main thread.
        auto settle = [&] {
            for (int i = 0; i < 500; ++i) {
                d.RunPending();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        engine->Report(0, true);
        settle();
        CHECK(p.State().currentCues.cues.empty());
        // The next change is a timer while playing: one second on, "Hello".
        d.AdvanceTimeBy(std::chrono::milliseconds(1000));
        d.RunPending();
        CHECK(Texts(p.State().currentCues.cues) == "Hello" && p.State().currentCues.presentationTimeUs == 1'000'000);
        d.AdvanceTimeBy(std::chrono::milliseconds(1000));
        d.RunPending();
        CHECK(Texts(p.State().currentCues.cues) == "Hello|there");
        // Twice as fast: the end of "Hello" (3 s) comes half a second later.
        p.SetPlaybackSpeed(2);
        engine->Report(2000, true);
        d.RunPending();
        d.AdvanceTimeBy(std::chrono::milliseconds(500));
        d.RunPending();
        CHECK(Texts(p.State().currentCues.cues) == "there");
        // Paused: no timer; a seek shows what's there.
        engine->Report(3500, false);
        d.AdvanceTimeBy(std::chrono::milliseconds(5000));
        d.RunPending();
        CHECK(Texts(p.State().currentCues.cues) == "there");
        engine->Report(1500, false);
        d.RunPending();
        CHECK(Texts(p.State().currentCues.cues) == "Hello");
        CHECK((shown == std::vector<std::string>{ "Hello", "Hello|there", "there", "Hello" }));
        // Preferring German picks the SubRip file.
        p.SetTrackSelectionParameters({ .preferredTextLanguages = { "de" } });
        settle();
        CHECK(Texts(p.State().currentCues.cues) == "Hallo");
        // Text disabled: nothing shows.
        p.SetTrackSelectionParameters({ .disabledTrackTypes = { TrackType::Text } });
        d.RunPending();
        CHECK(p.State().currentCues.cues.empty());
        // The system captions' language wins when they're on.
        SetCaptioningHost({ .isEnabled = [] { return true; }, .localeTag = []() -> std::optional<std::string> { return "de-DE"; } });
        p.SetTrackSelectionParameters({});
        settle();
        CHECK(Texts(p.State().currentCues.cues) == "Hallo");
        SetCaptioningHost({});
    }
    d.RunPending();
    SetPlayerEngineFactory({});
    d.SetVirtualTime(wasVirtual);
}

// kiln: the media's own text tracks (an adaptive stream's subtitle renditions) compete with the
// side-loaded ones; the engine loads the chosen one's cues, timed on the period's timeline.
TEST(PlayerShowsTheMediasOwnTextTracks) {
    auto& d = rt::Dispatcher::Main();
    const bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    struct Engine : PlayerEngine {
        EngineLoad load;
        std::string textGroup;
        int textTrack = -2, textCalls = 0;
        void Load(EngineLoad l) override { load = l; }
        void SetPlayWhenReady(bool) override {}
        void SeekTo(std::int64_t) override {}
        void SetRepeat(bool) override {}
        void SetVolume(float) override {}
        void SetSurface(ui::PlatformSurface) override {}
        void SetTextTrack(std::string group, int track) override {
            textGroup = std::move(group);
            textTrack = track;
            ++textCalls;
        }
        void Report(std::int64_t positionMs, std::int64_t windowInPeriodMs) {
            PlayerState s;
            s.state = PlaybackState::Ready;
            s.durationMs = 60'000;
            s.positionMs = positionMs;
            s.positionInFirstPeriodMs = windowInPeriodMs;
            Tracks::Group text;
            text.id = "subtitle:English";
            text.type = TrackType::Text;
            text.formats = { { .language = "en", .sampleMimeType = "text/vtt", .selectionFlags = kSelectionFlagDefault },
                             { .language = "fr", .sampleMimeType = "text/vtt" } };
            text.trackSupported = { true, true };
            text.trackSelected = { false, false };
            s.tracks.groups.push_back(text);
            onState(s, load.loadId);
        }
    };
    Engine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<Engine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    {
        Player p;
        p.SetMediaItem(MediaItem::FromUri("https://example.com/live.m3u8"));
        p.Prepare();
        CHECK_EQ(engine->textTrack, -1);   // nothing to choose from yet
        // A live window 100 s into the period; the default English rendition is chosen.
        engine->Report(5'000, 100'000);
        d.RunPending();
        CHECK_EQ(engine->textGroup, std::string("subtitle:English"));
        CHECK_EQ(engine->textTrack, 0);
        bool selected = false;
        for (const auto& g : p.State().tracks.groups)
            if (g.id == "subtitle:English") selected = g.trackSelected[0] && !g.trackSelected[1];
        CHECK(selected);
        // Its cues, timed on the period's timeline, show at window position + 100 s.
        engine->onCues({ CuesWithTiming(CuesOf({ "Bonjour" }), 104'000'000, 2'000'000) }, true, engine->load.loadId);
        d.RunPending();
        CHECK(Texts(p.State().currentCues.cues) == "Bonjour");
        engine->Report(7'000, 100'000);
        d.RunPending();
        CHECK(p.State().currentCues.cues.empty());
        // A reset (a seek) forgets the cues before.
        engine->Report(5'000, 100'000);
        engine->onCues({}, true, engine->load.loadId);
        d.RunPending();
        CHECK(p.State().currentCues.cues.empty());
        // Preferring French: the other track of the group.
        p.SetTrackSelectionParameters({ .preferredTextLanguages = { "fr" } });
        d.RunPending();
        CHECK_EQ(engine->textTrack, 1);
        // An override picks the track whatever the preferences say.
        p.SetTrackSelectionParameters(TrackSelectionParameters{}.SetOverrideForType({ .type = TrackType::Text, .groupId = "subtitle:English", .trackIndices = { 0 } }));
        d.RunPending();
        CHECK_EQ(engine->textTrack, 0);
        p.SetTrackSelectionParameters({ .disabledTrackTypes = { TrackType::Text } });
        d.RunPending();
        CHECK_EQ(engine->textTrack, -1);
        CHECK(engine->textGroup.empty());
    }
    d.RunPending();
    SetPlayerEngineFactory({});
    d.SetVirtualTime(wasVirtual);
}

TEST(AudioTrackSelectionAsDefaultTrackSelector) {
    // DefaultTrackSelectorTest's AUDIO_FORMAT: AAC, stereo, 44.1 kHz, 128 kb/s; the system in en-US.
    const Format audio{ .sampleMimeType = "audio/mp4a-latm", .bitrate = 128000, .channelCount = 2, .sampleRate = 44100 };
    auto with = [&](auto f) { Format x = audio; f(x); return x; };
    const std::vector<std::string> en{ "en-US" };
    auto select = [&](const std::vector<Format>& tracks, const TrackSelectionParameters& p = {}) {
        return SelectAudioTrack(tracks, std::vector<bool>(tracks.size(), true), p, en);
    };
    const Format fr = with([](Format& f) { f.language = "fra"; }), enA = with([](Format& f) { f.language = "eng"; });
    CHECK(select({ fr, enA }, { .preferredAudioLanguages = { "eng" } }) == 1);
    const Format noRoles = audio, less = with([](Format& f) { f.roleFlags = kRoleFlagCaption; }),
                 more = with([](Format& f) { f.roleFlags = kRoleFlagCaption | kRoleFlagCommentary | kRoleFlagDub; });
    CHECK(select({ noRoles, more, less }, { .preferredAudioRoleFlags = kRoleFlagCaption | kRoleFlagCommentary }) == 1);
    CHECK(select({ noRoles, more, less }, { .preferredAudioRoleFlags = kRoleFlagCaption }) == 2);
    const Format def = with([](Format& f) { f.selectionFlags = kSelectionFlagDefault; });
    CHECK(select({ audio, def, less }) == 1);    // withoutPreferredAudioRoleFlags_selectsDefaultTrack
    CHECK(select({ audio, less }) == 0);         // ... OrDefaultTrack_selectsFirstTrack
    const Format frDefault = with([](Format& f) { f.language = "fra"; f.selectionFlags = kSelectionFlagDefault; });
    CHECK(select({ frDefault, enA }, { .preferredAudioLanguages = { "eng" } }) == 1);
    const Format low = with([](Format& f) { f.language = "en"; f.bitrate = 15000; }), high = with([](Format& f) { f.language = "en"; f.bitrate = 30000; });
    CHECK(select({ low, high }) == 1);           // same language: the higher bitrate
    const Format hi = with([](Format& f) { f.language = "hi"; f.bitrate = 15000; }), te = with([](Format& f) { f.language = "te"; f.bitrate = 30000; });
    CHECK(select({ hi, te }) == 0);              // different languages: the first
    // Preferred mime types.
    const Format aac{ .sampleMimeType = "audio/mp4a-latm" }, opus{ .sampleMimeType = "audio/opus" }, flac{ .sampleMimeType = "audio/flac" };
    CHECK(select({ aac, opus, flac }, { .preferredAudioMimeTypes = { "audio/opus" } }) == 1);
    CHECK(select({ aac, opus, flac }, { .preferredAudioMimeTypes = { "audio/opus", "audio/mp4a-latm" } }) == 1);
    CHECK(select({ aac, opus, flac }, { .preferredAudioMimeTypes = { "audio/3gpp", "audio/flac" } }) == 2);
    CHECK(select({ aac, opus, flac }) == 0);
    // Labels; role flags over labels.
    const Format labeled = with([](Format& f) { f.label = "commentary"; });
    CHECK(select({ audio, labeled }, { .preferredAudioLabels = { "commentary" } }) == 1);
    CHECK(select({ with([](Format& f) { f.label = "low_priority"; }), with([](Format& f) { f.label = "high_priority"; }) },
                 { .preferredAudioLabels = { "high_priority", "low_priority" } }) == 1);
    CHECK(select({ with([](Format& f) { f.roleFlags = kRoleFlagDescribesVideo; f.label = "commentary"; }), less },
                 { .preferredAudioRoleFlags = kRoleFlagCaption, .preferredAudioLabels = { "commentary" } }) == 1);
    // The system's language without a preference; disabled audio; overrides.
    CHECK(SelectAudioTrack({ fr, enA }, { true, true }, {}, en) == 1);
    CHECK(SelectAudioTrack({ fr, enA }, { true, true }, {}, { "fr-FR", "en-US" }) == 0);
    CHECK(select({ audio }, TrackSelectionParameters{}.SetTrackTypeDisabled(TrackType::Audio, true)) == -1);
    TrackSelectionParameters p;
    p.SetOverrideForType({ .type = TrackType::Audio, .groupId = "m1", .trackIndices = { 0 } });
    p.AddOverride({ .type = TrackType::Text, .groupId = "s0", .trackIndices = {} });
    p.SetOverrideForType({ .type = TrackType::Audio, .groupId = "m2", .trackIndices = { 0 } });
    CHECK(p.overrides.size() == 2 && OverrideForType(p, TrackType::Audio)->groupId == "m2" && OverrideForType(p, TrackType::Text)->track == -1
          && !OverrideForType(p, TrackType::Video));
}

TEST(PlayerTracksJoinTheMediasAndTheSubtitles) {
    auto& d = rt::Dispatcher::Main();
    struct Engine : PlayerEngine {
        EngineLoad load;
        TrackSelectionParameters parameters;
        void Load(EngineLoad l) override { load = l; parameters = l.trackSelection; }
        void SetPlayWhenReady(bool) override {}
        void SeekTo(std::int64_t) override {}
        void SetRepeat(bool) override {}
        void SetVolume(float) override {}
        void SetSurface(ui::PlatformSurface) override {}
        void SetTrackSelectionParameters(TrackSelectionParameters p) override { parameters = std::move(p); }
    };
    Engine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<Engine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    {
        Player p;
        MediaItem item = MediaItem::FromUri("asset:///clip.mp4");
        item.subtitleConfigurations = { { .uri = "a.vtt", .language = "de" }, { .uri = "b.vtt", .language = "fr", .selectionFlags = kSelectionFlagForced } };
        p.SetMediaItem(item);
        p.Prepare();
        // The engine's tracks: video and two audio languages, French playing.
        PlayerState s;
        s.state = PlaybackState::Ready;
        auto group = [](std::string id, TrackType type, Format f, bool selected) {
            return Tracks::Group{ .id = std::move(id), .type = type, .formats = { std::move(f) }, .trackSupported = { true }, .trackSelected = { selected } };
        };
        s.tracks.groups = { group("m0", TrackType::Video, {}, true), group("m1", TrackType::Audio, { .language = "en" }, false),
                            group("m2", TrackType::Audio, { .language = "fr" }, true) };
        engine->onState(s, engine->load.loadId);
        d.RunPending();
        const Tracks& t = p.State().tracks;
        CHECK(t.groups.size() == 5 && t.groups[3].id == "s0" && t.groups[3].type == TrackType::Text);
        // The forced French subtitle follows the French audio.
        CHECK(t.groups.size() == 5 && !t.groups[3].trackSelected[0] && t.groups[3].formats[0].language == "de");
        CHECK(p.State().tracks.IsTypeSelected(TrackType::Text) && p.State().tracks.groups.back().trackSelected[0]);
        // An override for the German one; the parameters reach the engine.
        TrackSelectionParameters params;
        params.SetOverrideForType({ .type = TrackType::Text, .groupId = "s0", .trackIndices = { 0 } });
        params.preferredAudioLanguages = { "en" };
        p.SetTrackSelectionParameters(params);
        CHECK(engine->parameters == params);
        CHECK(p.State().tracks.groups[3].trackSelected[0] && !p.State().tracks.groups[4].trackSelected[0]);
        // An empty override: no subtitle.
        params.SetOverrideForType({ .type = TrackType::Text, .groupId = "s0", .trackIndices = {} });
        p.SetTrackSelectionParameters(params);
        CHECK(!p.State().tracks.IsTypeSelected(TrackType::Text));
    }
    d.RunPending();
    SetPlayerEngineFactory({});
}

TEST(PlayerAvailableCommandsAsUtil) {
    SetPlayerEngineFactory({});
    Player p;
    using C = PlayerCommand;
    CHECK(p.IsCommandAvailable(C::PlayPause) && p.IsCommandAvailable(C::SeekToMediaItem) && !p.IsCommandAvailable(C::SeekToPrevious)
          && !p.IsCommandAvailable(C::SeekBack));
    p.SetMediaItems({ MediaItem::FromUri("a.mp4"), MediaItem::FromUri("b.mp4") });
    CHECK(p.IsCommandAvailable(C::SeekToPrevious) && p.IsCommandAvailable(C::SeekToNext) && p.IsCommandAvailable(C::SeekToNextMediaItem)
          && !p.IsCommandAvailable(C::SeekToPreviousMediaItem));
    CHECK(!p.IsCommandAvailable(C::SeekInCurrentMediaItem) && p.BufferedPercentage() == 0 && !p.IsCurrentMediaItemLive());
}

TEST(PlayerWakeLocksDeviceVolumeAndStuckDetection) {
    auto& d = rt::Dispatcher::Main();
    const bool wasVirtual = d.VirtualTime();
    d.SetVirtualTime(true);
    struct Engine : PlayerEngine {
        EngineLoad load;
        void Load(EngineLoad l) override { load = l; }
        void SetPlayWhenReady(bool) override {}
        void SeekTo(std::int64_t) override {}
        void SetRepeat(bool) override {}
        void SetVolume(float) override {}
        void SetSurface(ui::PlatformSurface) override {}
        void Report(PlaybackState st, std::int64_t pos, bool playing) {
            PlayerState s;
            s.state = st;
            s.durationMs = 10'000;
            s.positionMs = pos;
            s.isPlaying = playing;
            onState(s, load.loadId);
        }
    };
    Engine* engine = nullptr;
    SetPlayerEngineFactory([&] {
        auto e = std::make_unique<Engine>();
        engine = e.get();
        return std::unique_ptr<PlayerEngine>(std::move(e));
    });
    std::map<int, std::pair<bool, bool>> locks;
    StreamVolume music{ .volume = 5, .muted = false, .minVolume = 0, .maxVolume = 15 };
    int adjusts = 0, volumeId = 0;
    SetAudioHost({ .streamVolume = [&](int stream) { return stream == kStreamMusic ? music : StreamVolume{}; },
                   .setStreamVolume = [&](int, int v, int) { music.volume = v; },
                   .adjustStreamVolume = [&](int, int dir, int) { ++adjusts; if (dir == kAdjustRaise) ++music.volume; if (dir == kAdjustMute) music.muted = true; },
                   .setVolumeChangedReceiver = [&](int id, bool on) { if (on) volumeId = id; },
                   .setLocks = [&](int id, bool wake, bool wifi) { locks[id] = { wake, wifi }; } });
    {
        Player p;
        p.SetMediaItem(MediaItem::FromUri("asset:///a.mp4"));
        p.Prepare();
        p.Play();
        auto held = [&] { return locks.empty() ? std::pair{ false, false } : locks.begin()->second; };
        // Buffering to play: the wake lock (Local by default), no Wi-Fi lock.
        CHECK(held() == std::pair(true, false));
        p.SetWakeMode(WakeMode::Network);
        CHECK(held() == std::pair(true, true));
        p.Pause();
        CHECK(held() == std::pair(false, false));
        p.SetWakeMode(WakeMode::Local);
        p.Play();
        // Device volume: off until enabled; then the music stream's.
        CHECK(!p.IsCommandAvailable(PlayerCommand::GetDeviceVolume) && p.State().deviceVolume == 0);
        p.SetDeviceVolumeControlEnabled(true);
        CHECK(p.State().deviceVolume == 5 && p.State().deviceInfo.maxVolume == 15 && p.IsCommandAvailable(PlayerCommand::SetDeviceVolumeWithFlags));
        p.SetDeviceVolume(9);
        p.IncreaseDeviceVolume();
        CHECK(p.State().deviceVolume == 10 && adjusts == 1);
        p.SetDeviceVolume(99);   // out of range: ignored
        p.SetDeviceMuted(true);
        CHECK(p.State().deviceVolume == 10 && p.State().deviceMuted);
        music.volume = 3;
        OnStreamVolumeChanged(volumeId);   // a change elsewhere is heard
        CHECK(p.State().deviceVolume == 3);

        // Stuck playing: no progress for 10 s fails with a timeout, and the locks go.
        int errors = 0;
        p.AddListener({ .onPlayerError = [&](const PlaybackException& e) { if (e.errorCode == PlaybackException::Timeout) ++errors; } });
        engine->Report(PlaybackState::Ready, 1000, true);
        d.RunPending();
        d.AdvanceTimeBy(std::chrono::milliseconds(9000));
        engine->Report(PlaybackState::Ready, 1500, true);   // progress: the timeout starts again
        d.RunPending();
        d.AdvanceTimeBy(std::chrono::milliseconds(9000));
        d.RunPending();
        CHECK(errors == 0);
        d.AdvanceTimeBy(std::chrono::milliseconds(1500));
        d.RunPending();
        CHECK(errors == 1 && p.State().state == PlaybackState::Idle && p.State().playerError && held() == std::pair(false, false));
        // Playing at the end without ending: 60 s.
        p.Prepare();
        engine->Report(PlaybackState::Ready, 10'000, true);
        d.RunPending();
        d.AdvanceTimeBy(std::chrono::milliseconds(60'500));
        d.RunPending();
        CHECK(errors == 2);
        // Never, when turned off.
        p.SetStuckDetectionTimeoutsMs(INT_MAX, INT_MAX, INT_MAX, INT_MAX);
        p.Prepare();
        engine->Report(PlaybackState::Ready, 2000, true);
        d.AdvanceTimeBy(std::chrono::milliseconds(120'000));
        d.RunPending();
        CHECK(errors == 2);
    }
    CHECK(locks.begin()->second == std::pair(false, false));
    SetAudioHost({});
    d.RunPending();
    SetPlayerEngineFactory({});
    d.SetVirtualTime(wasVirtual);
}

// Ported from media3 DefaultTrackSelectorTest's video cases (VideoTrackInfo).
namespace {
    Format VideoFormat(int bitrate = 450'000) {
        Format f;
        f.sampleMimeType = "video/avc";
        f.width = 1024;
        f.height = 768;
        f.SetBitrates(bitrate, -1);
        return f;
    }
    VideoSelection SelectVideo(const std::vector<std::vector<Format>>& groups, const TrackSelectionParameters& p = {},
                               std::optional<std::string> audio = std::nullopt, std::vector<std::vector<int>> support = {}) {
        return SelectVideoTracks(groups, support, p, audio, std::nullopt);
    }
    bool Is(const VideoSelection& s, int group, std::set<int> tracks) {
        return s.group == group && std::set<int>(s.tracks.begin(), s.tracks.end()) == tracks && s.tracks.size() == tracks.size();
    }
}

TEST(VideoTrackSelection_multipleTracks_adaptive) {
    CHECK(Is(SelectVideo({ { VideoFormat(), VideoFormat() } }), 0, { 0, 1 }));
    // Without bitrates: a fixed selection of the first.
    CHECK(Is(SelectVideo({ { VideoFormat(-1), VideoFormat(-1) } }), 0, { 0 }));
}

TEST(VideoTrackSelection_mixedMimeTypes) {
    Format h264 = VideoFormat(), h265 = VideoFormat();
    h265.sampleMimeType = "video/hevc";
    CHECK(Is(SelectVideo({ { h264, h265 } }), 0, { 0 }));
    CHECK(Is(SelectVideo({ { h265, h264 } }), 0, { 0 }));
    TrackSelectionParameters p;
    p.allowVideoMixedMimeTypeAdaptiveness = true;
    CHECK(Is(SelectVideo({ { h265, h264 } }, p), 0, { 0, 1 }));
}

TEST(VideoTrackSelection_interactsWithSelectedAudioLanguage) {
    Format main = VideoFormat();
    main.roleFlags = kRoleFlagMain;
    Format mainEnglish = main, mainGerman = main, mainNone = main, alternateGerman = VideoFormat();
    mainEnglish.language = "en";
    mainGerman.language = "de";
    alternateGerman.language = "de";
    alternateGerman.roleFlags = kRoleFlagAlternate;
    CHECK(Is(SelectVideo({ { mainNone } }), 0, { 0 }));
    CHECK(Is(SelectVideo({ { mainGerman }, { mainEnglish } }, {}, "deu"), 0, { 0 }));
    CHECK(Is(SelectVideo({ { alternateGerman }, { mainEnglish } }, {}, "deu"), 1, { 0 }));
}

TEST(VideoTrackSelection_forceBitrates) {
    // forceLowestBitrate: the lowest within the capabilities; forceHighestSupportedBitrate: the
    // highest within them (as the audio cases upstream).
    TrackSelectionParameters p;
    p.forceLowestBitrate = true;
    CHECK(Is(SelectVideo({ { VideoFormat(5000), VideoFormat(15000), VideoFormat(30000) } }, p, std::nullopt, { { 1, 2, 2 } }), 0, { 1 }));
    TrackSelectionParameters q;
    q.forceHighestSupportedBitrate = true;
    CHECK(Is(SelectVideo({ { VideoFormat(5000), VideoFormat(15000), VideoFormat(30000) } }, q, std::nullopt, { { 2, 2, 1 } }), 0, { 1 }));
}

TEST(VideoTrackSelection_preferredMimeTypes) {
    Format av1, vp9, low, high;
    av1.sampleMimeType = "video/av01";
    vp9.sampleMimeType = "video/x-vnd.on2.vp9";
    low.sampleMimeType = high.sampleMimeType = "video/avc";
    low.SetBitrates(400, -1);
    high.SetBitrates(800, -1);
    const std::vector<std::vector<Format>> groups = { { av1 }, { vp9 }, { low, high } };
    TrackSelectionParameters p;
    p.preferredVideoMimeTypes = { "video/x-vnd.on2.vp9" };
    CHECK(Is(SelectVideo(groups, p), 1, { 0 }));
    p.preferredVideoMimeTypes = { "video/x-vnd.on2.vp9", "video/av01" };
    CHECK(Is(SelectVideo(groups, p), 1, { 0 }));
    p.preferredVideoMimeTypes = { "video/divx", "video/avc" };
    CHECK(Is(SelectVideo(groups, p), 2, { 0, 1 }));
    // No preference: the most tracks.
    CHECK(Is(SelectVideo(groups), 2, { 0, 1 }));
}

TEST(VideoTrackSelection_preferredLabelsAndRoles) {
    Format unlabeled = VideoFormat(), labeled = VideoFormat();
    labeled.labels = { { "", "commentary" } };
    TrackSelectionParameters p;
    p.preferredVideoLabels = { "commentary" };
    CHECK(Is(SelectVideo({ { unlabeled }, { labeled } }, p), 1, { 0 }));
    Format lowPriority = VideoFormat(), highPriority = VideoFormat();
    lowPriority.labels = { { "", "low_priority" } };
    highPriority.labels = { { "", "high_priority" } };
    p.preferredVideoLabels = { "high_priority", "low_priority" };
    CHECK(Is(SelectVideo({ { lowPriority }, { highPriority } }, p), 1, { 0 }));
    // Role flags come before labels.
    Format labeledWrongRole = VideoFormat(), unlabeledRightRole = VideoFormat();
    labeledWrongRole.roleFlags = kRoleFlagDub;
    labeledWrongRole.labels = { { "", "commentary" } };
    unlabeledRightRole.roleFlags = kRoleFlagCommentary;
    p.preferredVideoLabels = { "commentary" };
    p.preferredVideoRoleFlags = kRoleFlagCommentary;
    CHECK(Is(SelectVideo({ { labeledWrongRole }, { unlabeledRightRole } }, p), 1, { 0 }));
    // More matching roles win, over an adaptive group without roles.
    Format noRolesLow = VideoFormat(4000), noRolesHigh = VideoFormat(8000), lessRoles = VideoFormat(8000), moreRoles = VideoFormat(8000);
    lessRoles.roleFlags = kRoleFlagCaption;
    moreRoles.roleFlags = kRoleFlagCaption | kRoleFlagCommentary | kRoleFlagDub;
    TrackSelectionParameters r;
    r.preferredVideoRoleFlags = kRoleFlagCaption | kRoleFlagCommentary;
    CHECK(Is(SelectVideo({ { noRolesLow, noRolesHigh }, { moreRoles }, { lessRoles } }, r), 1, { 0 }));
}

TEST(VideoTrackSelection_viewport) {
    Format low, mid, high;
    low.sampleMimeType = mid.sampleMimeType = high.sampleMimeType = "video/avc";
    low.SetBitrates(400, -1);
    low.width = 600;
    low.height = 400;
    mid.SetBitrates(800, -1);
    mid.width = 1200;
    mid.height = 800;
    high.SetBitrates(2000, -1);
    high.width = 2400;
    high.height = 1600;
    const std::vector<std::vector<Format>> groups = { { low, mid, high } };
    CHECK(Is(SelectVideo(groups, TrackSelectionParameters{}.SetViewportSize(450, 650, true)), 0, { 0, 1 }));
    CHECK(Is(SelectVideo(groups, TrackSelectionParameters{}.SetViewportSize(450, 650, false)), 0, { 0 }));
    CHECK(Is(SelectVideo(groups, TrackSelectionParameters{}.SetViewportSize(1201, 801, true)), 0, { 0, 1 }));
    // The physical display's size (450 x 650).
    TrackSelectionParameters display = TrackSelectionParameters{}.SetViewportSizeToPhysicalDisplaySize(true);
    CHECK(Is(SelectVideoTracks(groups, {}, display, std::nullopt, std::pair{ 450, 650 }), 0, { 0, 1 }));
    display.SetViewportSizeToPhysicalDisplaySize(false);
    CHECK(Is(SelectVideoTracks(groups, {}, display, std::nullopt, std::pair{ 450, 650 }), 0, { 0 }));
    // kiln: max size constraints leave the larger ones out (setMaxVideoSizeSd).
    CHECK(Is(SelectVideo(groups, TrackSelectionParameters{}.SetMaxVideoSizeSd()), 0, { 0 }));
    // ... unless nothing is within them: the closest, fixed.
    CHECK(Is(SelectVideo({ { mid, high } }, TrackSelectionParameters{}.SetMaxVideoSizeSd()), 0, { 0 }));
    TrackSelectionParameters strict = TrackSelectionParameters{}.SetMaxVideoSizeSd();
    strict.exceedVideoConstraintsIfNecessary = false;
    CHECK(SelectVideo({ { mid, high } }, strict).group == -1);
}
