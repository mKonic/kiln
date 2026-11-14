#pragma once

// Time stretching and resampling of PCM audio: media3's Sonic (after Bill Cox's libsonic), which
// ExoPlayer's SonicAudioProcessor uses for playback speed and pitch. Speed changes the duration
// without the pitch (pitch periods are skipped or repeated, overlap-added); pitch and a sample
// rate change resample linearly.
//
// The arithmetic follows the Java exactly, 32-bit integer wrap-around in the 16-bit path's pitch
// search included, so its output matches media3's sample for sample.
//
// Not ported: Sonic's static getExpectedFrameCountAfterProcessorApplied /
// getExpectedInputFrameCountForOutputFrameCount (BigDecimal arithmetic for media3's Transformer,
// which sizes edited outputs; playback maps time with SonicAudioProcessor's byte ratios below).

#include <cstdint>
#include <vector>

namespace kiln::media {

    template<typename Sample>   // std::int16_t or float
    class Sonic {
    public:
        Sonic(int inputSampleRateHz, int channelCount, float speed, float pitch, int outputSampleRateHz);

        int PendingInputFrames() const { return m_InputFrameCount; }
        // Interleaved frames in; as many frames out as fit (returned).
        void QueueInput(const Sample* samples, int frameCount);
        int GetOutput(Sample* samples, int maxFrames);
        int OutputFrames() const { return m_OutputFrameCount; }
        // Processes what is left (padded with silence) and trims the output to its expected length.
        void QueueEndOfStream();
        void Flush();

    private:
        void CopyToOutput(int positionFrames, int frameCount);
        int CopyInputToOutput(int positionFrames);
        int FindPitchPeriod(int positionFrames);
        void AdjustRate(float rate, int originalOutputFrameCount);
        void MoveNewSamplesToPitchBuffer(int originalOutputFrameCount);
        void RemovePitchFrames(int frameCount);
        int SkipPitchPeriod(int position, double speed, int period);
        int InsertPitchPeriod(int position, double speed, int period);
        void ChangeSpeed(double speed);
        void RemoveProcessedInputFrames(int positionFrames);
        void ProcessStreamInput();

        // SonicShortImpl / SonicFloatImpl.
        void InterpolateFrame(int positionFrames, std::int64_t oldSampleRate, std::int64_t newSampleRate);
        bool IsPreviousPeriodBetter() const;
        void DownSampleInput(int positionFrames, int skip);
        int FindPitchPeriodInRange(const std::vector<Sample>& samples, int positionFrames, int minPeriod, int maxPeriod);
        void OverlapAdd(int frameCount, int outPosition, int rampDownPosition, int rampUpPosition);
        void Ensure(std::vector<Sample>& buffer, int frameCount, int additionalFrameCount);
        Sample Interpolate(const std::vector<Sample>& in, int inPos, std::int64_t oldSampleRate, std::int64_t newSampleRate) const;

        const int m_InputSampleRateHz, m_ChannelCount;
        const float m_Speed, m_Pitch, m_Rate;
        const int m_MinPeriod, m_MaxPeriod, m_MaxRequiredFrameCount;
        std::vector<Sample> m_DownSampleBuffer, m_InputBuffer, m_OutputBuffer, m_PitchBuffer;
        int m_InputFrameCount = 0, m_OutputFrameCount = 0, m_PitchFrameCount = 0;
        int m_OldRatePosition = 0, m_NewRatePosition = 0;
        int m_RemainingInputToCopyFrameCount = 0;
        int m_PrevPeriod = 0;
        double m_AccumulatedSpeedAdjustmentError = 0;
        // int in the 16-bit path, double in the float one.
        double m_MinDiff = 0, m_MaxDiff = 0, m_PrevMinDiff = 0;
    };

    // SonicAudioProcessor's part in playback: Sonic when speed, pitch or the sample rate differ
    // from the defaults (else the audio passes through), and the mapping between media time and
    // playout time from the bytes processed so far.
    class SonicProcessor {
    public:
        // speed, pitch > 0; outputSampleRateHz 0: the input's.
        void Configure(int sampleRateHz, int channelCount, float speed, float pitch, int outputSampleRateHz = 0);
        bool IsActive() const { return m_Active; }
        void QueueInput(const std::int16_t* samples, int frameCount);
        // Appends what is ready to out.
        void GetOutput(std::vector<std::int16_t>& out);
        void QueueEndOfStream();
        bool IsEnded() const;
        void Flush();   // a seek: Sonic starts again, the byte counts too
        // The time mapping counted from now on (a new audio output mid-stream); what Sonic holds
        // is not yet processed.
        void RestartDurationCounts();
        // getMediaDuration / getPlayoutDuration (microseconds).
        std::int64_t MediaDuration(std::int64_t playoutDurationUs) const;
        std::int64_t PlayoutDuration(std::int64_t mediaDurationUs) const;

    private:
        static constexpr std::int64_t kMinBytesForDurationScaling = 1024;
        std::vector<Sonic<std::int16_t>> m_Sonic;   // 0 or 1
        int m_InputRate = 0, m_OutputRate = 0, m_Channels = 0;
        float m_Speed = 1, m_Pitch = 1;
        bool m_Active = false, m_InputEnded = false;
        std::int64_t m_InputBytes = 0, m_OutputBytes = 0;
    };

    // Util.scaleLargeTimestamp: timestamp * multiplier / divisor without overflow.
    std::int64_t ScaleLargeTimestamp(std::int64_t timestamp, std::int64_t multiplier, std::int64_t divisor);

}
