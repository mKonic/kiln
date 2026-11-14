// media3's Sonic (SonicTest) and SonicAudioProcessor's time mapping.
#include "Check.h"

#include "kiln/media/Sonic.h"

#include <cmath>
#include <limits>

using namespace kiln::media;

namespace {
    template<typename T>
    std::vector<T> Run(Sonic<T>& sonic, const std::vector<T>& in) {
        if (!in.empty()) sonic.QueueInput(in.data(), int(in.size()));
        sonic.QueueEndOfStream();
        std::vector<T> out(std::size_t(sonic.OutputFrames()));
        out.resize(std::size_t(sonic.GetOutput(out.data(), int(out.size()))));
        return out;
    }
    bool Near(const std::vector<float>& a, const std::vector<float>& b) {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i)
            if (std::fabs(a[i] - b[i]) > 1e-6f) return false;
        return true;
    }
    // TestUtil.getPeriodicSamplesBuffer: a square wave between the extremes.
    std::vector<std::int16_t> Periodic(int sampleCount, int period) {
        std::vector<std::int16_t> v;
        bool high = false;
        while (int(v.size()) < sampleCount) {
            for (int i = 0; i < period / 2 && int(v.size()) < sampleCount; ++i)
                v.push_back(high ? std::numeric_limits<std::int16_t>::max() : std::numeric_limits<std::int16_t>::min());
            high = !high;
        }
        return v;
    }
}

TEST(SonicResamplesAsMedia3) {
    Sonic<std::int16_t> up(44100, 1, 1, 1, 88200);
    CHECK((Run(up, { 0, 10, 20, 30, 40, 50 }) == std::vector<std::int16_t>{ 0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 25 }));
    Sonic<float> upF(44100, 1, 1, 1, 88200);
    CHECK(Near(Run(upF, { 0.f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }), { 0.f, 0.05f, 0.1f, 0.15f, 0.2f, 0.25f, 0.30f, 0.35f, 0.40f, 0.45f, 0.50f, 0.25f }));
    Sonic<std::int16_t> down(44100, 1, 1, 1, 22050);
    CHECK((Run(down, { -40, -30, -20, -10, 0, 10, 20, 30, 40, 50 }) == std::vector<std::int16_t>{ -40, -20, 0, 20, 40, 0 }));
    Sonic<float> downF(44100, 1, 1, 1, 22050);
    CHECK(Near(Run(downF, { -0.4f, -0.3f, -0.2f, -0.1f, 0, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f }), { -0.4f, -0.2f, 0, 0.2f, 0.4f, 0 }));
    // One sample does not hang.
    Sonic<std::int16_t> one(44100, 1, 1, 1, 88200);
    CHECK((Run(one, { 10 }) == std::vector<std::int16_t>{ 10, 5 }));
    Sonic<float> oneF(44100, 1, 1, 1, 88200);
    CHECK(Near(Run(oneF, { 1.f }), { 1.f, 0.5f }));
    // A fractional output count is rounded.
    Sonic<std::int16_t> frac(44100, 1, 1, 1, 22050);
    CHECK((Run(frac, { 0, 2, 4, 6, 8 }) == std::vector<std::int16_t>{ 0, 4, 8 }));
    Sonic<float> fracF(44100, 1, 1, 1, 22050);
    CHECK(Near(Run(fracF, { 0, 0.2f, 0.4f, 0.6f, 0.8f }), { 0, 0.4f, 0.8f }));
}

TEST(SonicEndOfStreamNeverLeavesNegativeOutput) {
    // queueEndOfStream_withOutputCountUnderflow_setsNonNegativeOutputSize
    Sonic<std::int16_t> s(48000, 1, 0.95f, 1, 48000);
    const auto in = Periodic(1700, 192);
    s.QueueInput(in.data(), int(in.size()));
    std::vector<std::int16_t> drain(std::size_t(s.OutputFrames()) + 1);
    s.GetOutput(drain.data(), s.OutputFrames());
    CHECK(s.OutputFrames() == 0);
    s.QueueEndOfStream();
    CHECK(s.OutputFrames() == 0);
    // ... and with no input.
    Sonic<std::int16_t> empty(48000, 1, 0.95f, 1, 48000);
    empty.QueueEndOfStream();
    CHECK(empty.OutputFrames() >= 0);
}

TEST(SonicChangesSpeedKeepingThePitch) {
    // Twice as fast: half the frames; half as fast: twice as many (within a pitch period).
    for (float speed : { 2.0f, 0.5f, 1.5f, 3.0f }) {
        Sonic<std::int16_t> s(48000, 2, speed, 1, 48000);
        const auto mono = Periodic(48000, 200);
        std::vector<std::int16_t> stereo;
        for (std::int16_t v : mono) {
            stereo.push_back(v);
            stereo.push_back(v);
        }
        s.QueueInput(stereo.data(), 48000);
        s.QueueEndOfStream();
        const double expected = 48000 / speed;
        CHECK(std::fabs(s.OutputFrames() - expected) < 800);
    }
    // The processor passes audio through at the defaults and maps time by what it processed.
    SonicProcessor p;
    p.Configure(48000, 1, 1, 1);
    CHECK(!p.IsActive() && p.MediaDuration(1'000'000) == 1'000'000);
    p.Configure(48000, 1, 2, 1);
    CHECK(p.IsActive() && p.MediaDuration(1'000'000) == 2'000'000 && p.PlayoutDuration(1'000'000) == 500'000);
    const auto in = Periodic(48000, 200);
    p.QueueInput(in.data(), 48000);
    std::vector<std::int16_t> out;
    p.GetOutput(out);
    const std::int64_t media = p.MediaDuration(500'000);
    CHECK(media > 950'000 && media < 1'050'000);
    p.Flush();
    CHECK(p.MediaDuration(1'000'000) == 2'000'000);
    CHECK(ScaleLargeTimestamp(std::numeric_limits<std::int64_t>::max() / 2, 4, 1) == std::numeric_limits<std::int64_t>::max());
}
