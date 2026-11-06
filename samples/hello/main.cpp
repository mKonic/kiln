// M0 sample: a Material-ish card with a wrapped paragraph (Latin, emoji, RTL) laid out by the
// platform text path, and a moving bar so frame pacing is visible. Logs how long layout took.
#include <kiln/kiln.h>

#include <chrono>
#include <cmath>
#include <cstdio>

namespace {

    struct Hello {
        kiln_app* app = nullptr;
        kiln_paragraph* title = nullptr;
        kiln_paragraph* body = nullptr;
        float laidOutWidth = -1;
    };

    const char* kBody =
        "kiln lays this paragraph out with the platform's own line breaker, then draws every "
        "glyph from an atlas on the GPU. Emoji too: \xF0\x9F\x94\xA5\xF0\x9F\x8E\xA8\xF0\x9F\x91\x8D "
        "and right-to-left text: \xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 \xD8\xA8\xD8\xA7\xD9\x84\xD8\xB9\xD8\xA7\xD9\x84\xD9\x85.";

    void OnFrame(void* user, kiln_frame* frame, double t) {
        auto& h = *static_cast<Hello*>(user);
        float w = 0, ht = 0;
        kiln_frame_size(frame, &w, &ht);
        const float dp = kiln_app_density(h.app);
        const float margin = 16 * dp, pad = 16 * dp;
        const float cardW = w - 2 * margin;

        if (cardW != h.laidOutWidth) {
            kiln_paragraph_free(h.title);
            kiln_paragraph_free(h.body);
            const auto t0 = std::chrono::steady_clock::now();
            h.title = kiln_text_layout(h.app, "Hello from kiln", 22, cardW - 2 * pad);
            h.body = kiln_text_layout(h.app, kBody, 16, cardW - 2 * pad);
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - t0).count();
            std::printf("kiln-hello: laid out 2 paragraphs (%d lines) in %lld us\n",
                        kiln_paragraph_line_count(h.body), static_cast<long long>(us));
            std::fflush(stdout);
            h.laidOutWidth = cardW;
        }

        kiln_frame_clear(frame, 0xFFFEF7FF);                  // M3 baseline surface
        const float top = margin * 4;
        const float cardH = pad * 3 + kiln_paragraph_height(h.title) + kiln_paragraph_height(h.body);
        kiln_frame_fill_rect(frame, margin, top, cardW, cardH, 0xFFF3EDF7, 12 * dp);   // container low
        kiln_frame_draw_paragraph(frame, h.title, margin + pad, top + pad, 0xFF1D1B20);
        kiln_frame_draw_paragraph(frame, h.body, margin + pad,
                                  top + 2 * pad + kiln_paragraph_height(h.title), 0xFF49454F);

        const float phase = float(std::fmod(t, 2.0) / 2.0);
        kiln_frame_fill_rect(frame, margin + (cardW - 48 * dp) * phase, top + cardH + margin, 48 * dp,
                             8 * dp, 0xFF6750A4, 4 * dp);
        kiln_app_request_frame(h.app);
    }

    Hello g_Hello;
}

extern "C" void kiln_main(kiln_app* app) {
    g_Hello.app = app;
    kiln_app_callbacks cb{ sizeof(cb), &g_Hello, &OnFrame };
    kiln_app_set_callbacks(app, &cb);
    kiln_app_request_frame(app);
}
