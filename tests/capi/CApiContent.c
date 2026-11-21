/* Content written against the C ABI, in C: CApiTests.cpp composes and checks it. */
#include <kiln/kiln.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Read by the tests. */
int capi_live_callbacks;      /* callbacks handed to kiln and not dropped yet */
int capi_effect_starts;
int capi_effect_disposals;
int capi_task_frames;
int capi_items_composed;
kiln_state_i64* capi_count;   /* the counter's state */

static void drop_counted(void* user) { (void)user; --capi_live_callbacks; }
static kiln_fn counted(void (*call)(void*), void* user) {
    ++capi_live_callbacks;
    kiln_fn f = { call, user, drop_counted };
    return f;
}

static void increment(void* user) {
    kiln_state_i64* s = (kiln_state_i64*)user;
    kiln_state_i64_set(s, kiln_state_i64_get(s) + 1);
}

static void dispose_extra(void* user) { (void)user; ++capi_effect_disposals; }
static kiln_fn start_extra(void* user) {
    (void)user;
    ++capi_effect_starts;
    return KILN_FN(dispose_extra, NULL);
}

/* A task that waits for three frames, then ends. */
static void on_frame(void* user, kiln_task* task, int64_t time) {
    (void)user; (void)time;
    if (++capi_task_frames < 3) {
        kiln_fn_task_frame next = { on_frame, NULL, NULL };
        kiln_task_next_frame(task, next);
    }
}
static void start_task(void* user, kiln_task* task) {
    (void)user;
    kiln_fn_task_frame next = { on_frame, NULL, NULL };
    kiln_task_next_frame(task, next);
}

static void extra_content(void* user) {
    (void)user;
    kiln_fn_effect effect = { start_extra, NULL, NULL };
    kiln_disposable_effect(KILN_HERE, NULL, 0, effect);
    kiln_box(KILN_HERE, kiln_mod_test_tag(kiln_mod_size(NULL, 10, 10), "extra"), NULL, KILN_FN(NULL, NULL));
}

static void column_content(void* user) {
    kiln_state_i64* count = (kiln_state_i64*)user;
    char label[32];
    const int64_t n = kiln_state_i64_get(count);
    snprintf(label, sizeof label, "Count %lld", (long long)n);
    kiln_basic_text(KILN_HERE, label, kiln_mod_test_tag(NULL, "label"), NULL, NULL);
    kiln_modifier* button = kiln_mod_size(NULL, 40, 40);
    button = kiln_mod_test_tag(button, "button");
    button = kiln_mod_clickable(button, true, counted(increment, count));
    kiln_box(KILN_HERE, button, NULL, KILN_FN(NULL, NULL));
    /* An effect and a box only while the count is odd. */
    if (n % 2 == 1) kiln_group(kiln_key_str("extra"), counted(extra_content, NULL));
}

void capi_counter(void* user) {
    (void)user;
    kiln_state_i64* count = kiln_remember_saveable_i64(KILN_HERE, 0);
    capi_count = count;
    kiln_fn_task task = { start_task, NULL, NULL };
    kiln_launched_effect(KILN_HERE, NULL, 0, task);
    kiln_column_options options;
    kiln_column_options_init(&options);
    options.vertical_arrangement.kind = KILN_ARRANGE_SPACED_BY;
    options.vertical_arrangement.spacing = 8;
    kiln_column(KILN_HERE, kiln_mod_padding_all(NULL, 4), &options, counted(column_content, count));
}

static void item(void* user, int32_t index) {
    (void)user;
    char tag[32];
    snprintf(tag, sizeof tag, "item %d", index);
    ++capi_items_composed;
    kiln_box(KILN_HERE, kiln_mod_test_tag(kiln_mod_fill_max_width(kiln_mod_height(NULL, 20), 1), tag), NULL, KILN_FN(NULL, NULL));
}
static kiln_key item_key(void* user, int32_t index) { (void)user; return kiln_key_int(index); }
static void items(void* user, kiln_lazy_scope* scope) {
    (void)user;
    kiln_fn_int content = { item, NULL, NULL };
    kiln_fn_item_key key = { item_key, NULL, NULL };
    kiln_lazy_items(scope, 1000, content, key);
}

void capi_list(void* user) {
    (void)user;
    kiln_lazy_list_state* state = kiln_remember_lazy_list_state(KILN_HERE, 0, 0);
    kiln_fn_lazy content = { items, NULL, NULL };
    kiln_lazy_column(KILN_HERE, kiln_mod_fill_max_size(NULL, 1), state, NULL, content);
}

/* Refcounted handles and options defaults. */
int capi_handles_ok(void) {
    kiln_shape* s = kiln_shape_rounded(8);
    kiln_shape* again = kiln_shape_retain(s);
    kiln_shape_release(s);
    kiln_shape_release(again);
    kiln_text_style_options o;
    kiln_text_style_options_init(&o);
    if (o.size != sizeof o || !isnan(o.font_size_sp)) return 0;
    o.font_size_sp = 20;
    o.color = KILN_ARGB(0xFF112233);
    kiln_text_style* style = kiln_text_style_new(&o);
    kiln_text_style* merged = kiln_text_style_merge(style, NULL);
    kiln_text_style_release(style);
    kiln_text_style_release(merged);
    uint32_t px[4] = { 0xFFFF0000u, 0xFF00FF00u, 0xFF0000FFu, 0x80FFFFFFu };
    kiln_image_bitmap* b = kiln_image_bitmap_from_pixels(2, 2, px);
    const int ok = b && kiln_image_bitmap_width(b) == 2 && kiln_image_bitmap_height(b) == 2;
    kiln_image_bitmap_release(b);
    return ok && kiln_abi_version() == KILN_ABI_VERSION;
}

/* Styled text with a link, built in C. */
static void on_link(void* user, const char* tag) { (void)user; (void)tag; }
void capi_annotated(void* user) {
    (void)user;
    kiln_text_builder* b = kiln_text_builder_new();
    kiln_text_builder_append(b, "Read the ");
    kiln_text_style_options o;
    kiln_text_style_options_init(&o);
    o.font_weight = 700;
    kiln_text_style* bold = kiln_text_style_new(&o);
    kiln_text_builder_push_style(b, bold);
    kiln_fn_str click = { on_link, NULL, NULL };
    kiln_text_builder_push_link_clickable(b, "terms", click);
    kiln_text_builder_append(b, "terms");
    kiln_text_builder_pop(b);
    kiln_text_builder_pop(b);
    kiln_text_builder_append(b, ".");
    kiln_annotated_string* text = kiln_text_builder_build(b);
    kiln_text_builder_free(b);
    kiln_text_style_release(bold);
    kiln_basic_text_annotated(KILN_HERE, text, kiln_mod_test_tag(NULL, "rich"), NULL, NULL);
    kiln_annotated_string_release(text);
}

/* A player with audio focus and a media session whose next button counts. */
int capi_media_next;
char capi_media_log[128];
static void media_next(void* user) { (void)user; ++capi_media_next; }
static void media_custom(void* user, const char* action) {
    (void)user;
    size_t n = strlen(capi_media_log);
    snprintf(capi_media_log + n, sizeof capi_media_log - n, "custom %s;", action);
}
static void media_rating(void* user, int32_t kind, bool rated, bool liked, int32_t max_stars, float value) {
    (void)user; (void)max_stars; (void)value;
    size_t n = strlen(capi_media_log);
    snprintf(capi_media_log + n, sizeof capi_media_log - n, "rating %d %d %d;", (int)kind, (int)rated, (int)liked);
}
static const char* media_request(void* user, const char* media_id, const char* uri, const char* query) {
    (void)user; (void)uri; (void)query;
    return strcmp(media_id, "known") == 0 ? "asset:///known.mp3" : NULL;
}
void capi_media(void* user) {
    (void)user;
    kiln_player* player = kiln_remember_player(KILN_HERE);
    kiln_player_set_audio_attributes(player, 1, 2, true);
    kiln_player_set_handle_audio_becoming_noisy(player, true);
    kiln_media_session_options options;
    kiln_media_session_options_init(&options);
    options.on_skip_to_next = counted(media_next, NULL);
    options.on_custom_command = (kiln_fn_str){ media_custom, NULL, NULL };
    options.on_set_rating = (kiln_fn_rating){ media_rating, NULL, NULL };
    options.on_media_request = (kiln_fn_media_request){ media_request, NULL, NULL };
    kiln_media_session* session = kiln_remember_media_session(KILN_HERE, player, &options);
    kiln_media_session_set_metadata(session, "Song", "Band", "", "");
    kiln_command_button heart;
    kiln_command_button_init(&heart);
    heart.icon = 0xe87d;   /* ICON_HEART_UNFILLED */
    heart.display_name = "Like";
    heart.custom_action = "like";
    int32_t overflow = 6;
    heart.slots = &overflow;
    heart.slot_count = 1;
    kiln_media_session_set_media_button_preferences(session, &heart, 1);
    const char* key = "k";
    const char* value = "v";
    kiln_media_session_set_extras(session, &key, &value, 1);
}

/* A camera with still capture: zoom, a picture into a file; the outcomes are logged. */
char capi_camera_log[256];
static void camera_log(const char* s) {
    size_t n = strlen(capi_camera_log);
    snprintf(capi_camera_log + n, sizeof capi_camera_log - n, "%s;", s);
}
static void camera_zoomed(void* user, const char* error) { (void)user; camera_log(error ? error : "zoomed"); }
static void camera_picture(void* user, int32_t error, const char* text) {
    (void)user;
    char line[160];
    snprintf(line, sizeof line, "picture %d %s", (int)error, text);
    camera_log(line);
}
void capi_camera(void* user) {
    (void)user;
    kiln_camera* camera = kiln_remember_camera(KILN_HERE, "0", true, 1, 2);
    kiln_camera_state s;
    kiln_camera_get_state(camera, &s);
    if (s.type == 2 && s.zoom_ratio == 1.0f && !capi_camera_log[0]) {
        kiln_camera_set_zoom_ratio(camera, 20, (kiln_fn_camera_done){ camera_zoomed, NULL, NULL });
        kiln_camera_take_picture(camera, "/tmp/kiln-capi-camera.jpg", 0, (kiln_fn_picture){ camera_picture, NULL, NULL });
    }
}

/* Speech: a remembered engine and recognizer; the listeners' events are logged. */
char capi_speech_log[256];
static void speech_log(const char* s) {
    size_t n = 0;
    while (capi_speech_log[n]) ++n;
    snprintf(capi_speech_log + n, sizeof capi_speech_log - n, "%s;", s);
}
static void on_tts(void* user, kiln_tts_event e, const char* id, int32_t a, int32_t b) {
    (void)user; (void)a; (void)b;
    char line[64];
    snprintf(line, sizeof line, "tts %d %s", (int)e, id ? id : "-");
    speech_log(line);
}
static void on_recognition(void* user, kiln_recognition_event e, const char* text, float value) {
    (void)user;
    char line[64];
    snprintf(line, sizeof line, "rec %d %s %d", (int)e, text, (int)value);
    speech_log(line);
}
static void drop_counted_user(void* user) { (void)user; --capi_live_callbacks; }
kiln_tts* capi_tts;
kiln_speech_recognizer* capi_recognizer;
void capi_speech(void* user) {
    (void)user;
    capi_tts = kiln_remember_tts(KILN_HERE, NULL);
    ++capi_live_callbacks;
    kiln_fn_tts_event t = { on_tts, NULL, drop_counted_user };
    kiln_tts_set_listener(capi_tts, t);
    capi_recognizer = kiln_remember_speech_recognizer(KILN_HERE, false);
    ++capi_live_callbacks;
    kiln_fn_recognition r = { on_recognition, NULL, drop_counted_user };
    kiln_speech_recognizer_set_listener(capi_recognizer, r);
}

/* A playlist with a subtitle: navigation, repeat, speed and the tracks, read back. */
kiln_player_state capi_playlist_state;
kiln_track capi_playlist_track;
size_t capi_playlist_tracks;
void capi_playlist(void* user) {
    (void)user;
    kiln_player* player = kiln_remember_player(KILN_HERE);
    static int set = 0;
    if (!set) {
        set = 1;
        kiln_subtitle sub = { "data:text/vtt,WEBVTT", "text/vtt", "de", "Deutsch", 1, 0 };
        kiln_media_item items[2];
        kiln_media_item_init(&items[0]);
        kiln_media_item_init(&items[1]);
        items[0].uri = "asset:///a.mp4";
        items[0].media_id = "a";
        items[0].title = "First";
        items[0].subtitles = &sub;
        items[0].subtitle_count = 1;
        items[1].uri = "asset:///b.mp4";
        items[1].media_id = "b";
        items[1].clip_start_ms = 1000;
        items[1].live_target_offset_ms = 5000;
        items[1].live_max_playback_speed = 1.05f;
        kiln_player_set_media_items(player, items, 2, true);
        kiln_player_set_max_video_size(player, 1280, 720);
        kiln_player_set_max_video_bitrate(player, -1);
        kiln_player_set_viewport_size(player, -1, -1, true);
        kiln_player_set_repeat_mode(player, KILN_REPEAT_ALL);
        kiln_player_set_playback_parameters(player, 1.5f, 1.0f);
        const char* langs[] = { "de" };
        kiln_player_set_preferred_text_languages(player, langs, 1);
        kiln_player_seek_to_next(player);
        kiln_player_seek_to_next(player);   /* wraps to the first */
        kiln_player_seek_to_default_position(player);
    }
    kiln_player_get_state(player, &capi_playlist_state);
    capi_playlist_tracks = kiln_player_track_count(player);
    kiln_player_get_track(player, 0, &capi_playlist_track);
}
