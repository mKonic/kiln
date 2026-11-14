#include "kiln/media/Sonic.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace kiln::media {

    namespace {
        constexpr int kMinimumPitch = 65, kMaximumPitch = 400, kAmdfFrequency = 4000;
        constexpr float kMinimumSpeedupRate = 1.00001f, kMinimumSlowdownRate = 0.99999f;

        // Java's int multiply (wraps at 32 bits).
        std::int32_t MulWrap(std::int32_t a, std::int32_t b) { return std::int32_t(std::uint32_t(a) * std::uint32_t(b)); }
    }

    template<typename Sample>
    Sonic<Sample>::Sonic(int inputSampleRateHz, int channelCount, float speed, float pitch, int outputSampleRateHz)
        : m_InputSampleRateHz(inputSampleRateHz), m_ChannelCount(channelCount), m_Speed(speed), m_Pitch(pitch),
          m_Rate(float(inputSampleRateHz) / float(outputSampleRateHz)), m_MinPeriod(inputSampleRateHz / kMaximumPitch),
          m_MaxPeriod(inputSampleRateHz / kMinimumPitch), m_MaxRequiredFrameCount(2 * m_MaxPeriod) {
        if (channelCount <= 0 || inputSampleRateHz <= 0 || outputSampleRateHz <= 0) throw std::invalid_argument("Sonic: bad format");
        m_DownSampleBuffer.assign(std::size_t(m_MaxRequiredFrameCount), Sample{});
        m_InputBuffer.assign(std::size_t(m_MaxRequiredFrameCount * channelCount), Sample{});
        m_OutputBuffer.assign(std::size_t(m_MaxRequiredFrameCount * channelCount), Sample{});
        m_PitchBuffer.assign(std::size_t(m_MaxRequiredFrameCount * channelCount), Sample{});
    }

    template<typename Sample>
    void Sonic<Sample>::QueueInput(const Sample* samples, int frames) {
        Ensure(m_InputBuffer, m_InputFrameCount, frames);
        std::copy(samples, samples + std::size_t(frames) * std::size_t(m_ChannelCount),
                  m_InputBuffer.begin() + std::ptrdiff_t(m_InputFrameCount) * m_ChannelCount);
        m_InputFrameCount += frames;
        ProcessStreamInput();
    }

    template<typename Sample>
    int Sonic<Sample>::GetOutput(Sample* samples, int maxFrames) {
        if (m_OutputFrameCount < 0) throw std::logic_error("Sonic: negative output");
        const int frames = std::min(maxFrames, m_OutputFrameCount);
        std::copy(m_OutputBuffer.begin(), m_OutputBuffer.begin() + std::ptrdiff_t(frames) * m_ChannelCount, samples);
        m_OutputFrameCount -= frames;
        std::copy(m_OutputBuffer.begin() + std::ptrdiff_t(frames) * m_ChannelCount,
                  m_OutputBuffer.begin() + std::ptrdiff_t(frames + m_OutputFrameCount) * m_ChannelCount, m_OutputBuffer.begin());
        return frames;
    }

    template<typename Sample>
    void Sonic<Sample>::QueueEndOfStream() {
        const int remainingFrameCount = m_InputFrameCount;
        const double s = double(m_Speed) / double(m_Pitch);
        const double r = double(m_Rate) * double(m_Pitch);
        const int adjustedRemainingFrames = remainingFrameCount - m_RemainingInputToCopyFrameCount;
        const int expectedOutputFrames =
            m_OutputFrameCount + int((adjustedRemainingFrames / s + m_RemainingInputToCopyFrameCount + m_AccumulatedSpeedAdjustmentError +
                                      m_PitchFrameCount) / r + 0.5);
        m_AccumulatedSpeedAdjustmentError = 0;
        // Add enough silence to flush both input and pitch buffers.
        Ensure(m_InputBuffer, m_InputFrameCount, remainingFrameCount + 2 * m_MaxRequiredFrameCount);
        std::fill(m_InputBuffer.begin() + std::ptrdiff_t(remainingFrameCount) * m_ChannelCount,
                  m_InputBuffer.begin() + std::ptrdiff_t(remainingFrameCount + 2 * m_MaxRequiredFrameCount) * m_ChannelCount, Sample{});
        m_InputFrameCount += 2 * m_MaxRequiredFrameCount;
        ProcessStreamInput();
        // Throw away any extra frames we generated due to the silence we added.
        if (m_OutputFrameCount > expectedOutputFrames) m_OutputFrameCount = std::max(expectedOutputFrames, 0);
        m_InputFrameCount = 0;
        m_RemainingInputToCopyFrameCount = 0;
        m_PitchFrameCount = 0;
    }

    template<typename Sample>
    void Sonic<Sample>::Flush() {
        m_InputFrameCount = m_OutputFrameCount = m_PitchFrameCount = 0;
        m_OldRatePosition = m_NewRatePosition = 0;
        m_RemainingInputToCopyFrameCount = 0;
        m_PrevPeriod = 0;
        m_AccumulatedSpeedAdjustmentError = 0;
        m_PrevMinDiff = m_MinDiff = m_MaxDiff = 0;
    }

    template<typename Sample>
    void Sonic<Sample>::CopyToOutput(int positionFrames, int frameCount) {
        Ensure(m_OutputBuffer, m_OutputFrameCount, frameCount);
        std::copy(m_InputBuffer.begin() + std::ptrdiff_t(positionFrames) * m_ChannelCount,
                  m_InputBuffer.begin() + std::ptrdiff_t(positionFrames + frameCount) * m_ChannelCount,
                  m_OutputBuffer.begin() + std::ptrdiff_t(m_OutputFrameCount) * m_ChannelCount);
        m_OutputFrameCount += frameCount;
    }

    template<typename Sample>
    int Sonic<Sample>::CopyInputToOutput(int positionFrames) {
        const int frameCount = std::min(m_MaxRequiredFrameCount, m_RemainingInputToCopyFrameCount);
        CopyToOutput(positionFrames, frameCount);
        m_RemainingInputToCopyFrameCount -= frameCount;
        return frameCount;
    }

    template<typename Sample>
    int Sonic<Sample>::FindPitchPeriod(int positionFrames) {
        // Find the pitch period. This is a critical step, and we may have to try multiple ways to
        // get a good answer. This version uses AMDF; to improve speed, we down sample by an
        // integer factor get in the 11 kHz range, and then do it again with a narrower frequency
        // range without down sampling.
        int period, retPeriod;
        const int skip = m_InputSampleRateHz > kAmdfFrequency ? m_InputSampleRateHz / kAmdfFrequency : 1;
        if (m_ChannelCount == 1 && skip == 1) {
            period = FindPitchPeriodInRange(m_InputBuffer, positionFrames, m_MinPeriod, m_MaxPeriod);
        } else {
            DownSampleInput(positionFrames, skip);
            period = FindPitchPeriodInRange(m_DownSampleBuffer, 0, m_MinPeriod / skip, m_MaxPeriod / skip);
            if (skip != 1) {
                period *= skip;
                int minP = period - (skip * 4), maxP = period + (skip * 4);
                if (minP < m_MinPeriod) minP = m_MinPeriod;
                if (maxP > m_MaxPeriod) maxP = m_MaxPeriod;
                if (m_ChannelCount == 1) {
                    period = FindPitchPeriodInRange(m_InputBuffer, positionFrames, minP, maxP);
                } else {
                    DownSampleInput(positionFrames, 1);
                    period = FindPitchPeriodInRange(m_DownSampleBuffer, 0, minP, maxP);
                }
            }
        }
        retPeriod = IsPreviousPeriodBetter() ? m_PrevPeriod : period;
        m_PrevMinDiff = m_MinDiff;   // updatePreviousMinDiff
        m_PrevPeriod = period;
        return retPeriod;
    }

    template<typename Sample>
    void Sonic<Sample>::AdjustRate(float rate, int originalOutputFrameCount) {
        if (m_OutputFrameCount == originalOutputFrameCount) return;
        std::int64_t newSampleRate = std::int64_t(float(m_InputSampleRateHz) / rate);
        std::int64_t oldSampleRate = m_InputSampleRateHz;
        // Set these values to help with the integer math.
        while (newSampleRate != 0 && oldSampleRate != 0 && newSampleRate % 2 == 0 && oldSampleRate % 2 == 0) {
            newSampleRate /= 2;
            oldSampleRate /= 2;
        }
        MoveNewSamplesToPitchBuffer(originalOutputFrameCount);
        // Leave at least one pitch sample in the buffer.
        for (int position = 0; position < m_PitchFrameCount - 1; ++position) {
            while ((m_OldRatePosition + 1) * newSampleRate > m_NewRatePosition * oldSampleRate) {
                Ensure(m_OutputBuffer, m_OutputFrameCount, 1);
                InterpolateFrame(position, oldSampleRate, newSampleRate);
                ++m_NewRatePosition;
                ++m_OutputFrameCount;
            }
            ++m_OldRatePosition;
            if (m_OldRatePosition == oldSampleRate) {
                m_OldRatePosition = 0;
                if (m_NewRatePosition != newSampleRate) throw std::logic_error("Sonic: rate position");
                m_NewRatePosition = 0;
            }
        }
        RemovePitchFrames(m_PitchFrameCount - 1);
    }

    template<typename Sample>
    void Sonic<Sample>::MoveNewSamplesToPitchBuffer(int originalOutputFrameCount) {
        const int frameCount = m_OutputFrameCount - originalOutputFrameCount;
        Ensure(m_PitchBuffer, m_PitchFrameCount, frameCount);
        std::copy(m_OutputBuffer.begin() + std::ptrdiff_t(originalOutputFrameCount) * m_ChannelCount,
                  m_OutputBuffer.begin() + std::ptrdiff_t(originalOutputFrameCount + frameCount) * m_ChannelCount,
                  m_PitchBuffer.begin() + std::ptrdiff_t(m_PitchFrameCount) * m_ChannelCount);
        m_OutputFrameCount = originalOutputFrameCount;
        m_PitchFrameCount += frameCount;
    }

    template<typename Sample>
    void Sonic<Sample>::RemovePitchFrames(int frameCount) {
        if (frameCount == 0) return;
        std::copy(m_PitchBuffer.begin() + std::ptrdiff_t(frameCount) * m_ChannelCount,
                  m_PitchBuffer.begin() + std::ptrdiff_t(m_PitchFrameCount) * m_ChannelCount, m_PitchBuffer.begin());
        m_PitchFrameCount -= frameCount;
    }

    template<typename Sample>
    int Sonic<Sample>::SkipPitchPeriod(int position, double speed, int period) {
        // Skip over a pitch period. Return the number of output frames.
        int newFrameCount;
        if (speed >= 2.0f) {
            const double expectedFrameCount = period / (speed - 1.0) + m_AccumulatedSpeedAdjustmentError;
            newFrameCount = int(std::floor(expectedFrameCount + 0.5));   // Math.round
            m_AccumulatedSpeedAdjustmentError = expectedFrameCount - newFrameCount;
        } else {
            newFrameCount = period;
            const double expectedInputToCopy = period * (2.0f - speed) / (speed - 1.0f) + m_AccumulatedSpeedAdjustmentError;
            m_RemainingInputToCopyFrameCount = int(std::floor(expectedInputToCopy + 0.5));
            m_AccumulatedSpeedAdjustmentError = expectedInputToCopy - m_RemainingInputToCopyFrameCount;
        }
        Ensure(m_OutputBuffer, m_OutputFrameCount, newFrameCount);
        OverlapAdd(newFrameCount, m_OutputFrameCount, position, position + period);
        m_OutputFrameCount += newFrameCount;
        return newFrameCount;
    }

    template<typename Sample>
    int Sonic<Sample>::InsertPitchPeriod(int position, double speed, int period) {
        // Insert a pitch period, and determine how much input to copy directly.
        int newFrameCount;
        if (speed < 0.5f) {
            const double expectedFrameCount = period * speed / (1.0f - speed) + m_AccumulatedSpeedAdjustmentError;
            newFrameCount = int(std::floor(expectedFrameCount + 0.5));
            m_AccumulatedSpeedAdjustmentError = expectedFrameCount - newFrameCount;
        } else {
            newFrameCount = period;
            const double expectedInputToCopy = period * (2.0f * speed - 1.0f) / (1.0f - speed) + m_AccumulatedSpeedAdjustmentError;
            m_RemainingInputToCopyFrameCount = int(std::floor(expectedInputToCopy + 0.5));
            m_AccumulatedSpeedAdjustmentError = expectedInputToCopy - m_RemainingInputToCopyFrameCount;
        }
        Ensure(m_OutputBuffer, m_OutputFrameCount, period + newFrameCount);
        std::copy(m_InputBuffer.begin() + std::ptrdiff_t(position) * m_ChannelCount,
                  m_InputBuffer.begin() + std::ptrdiff_t(position + period) * m_ChannelCount,
                  m_OutputBuffer.begin() + std::ptrdiff_t(m_OutputFrameCount) * m_ChannelCount);
        OverlapAdd(newFrameCount, m_OutputFrameCount + period, position + period, position);
        m_OutputFrameCount += period + newFrameCount;
        return newFrameCount;
    }

    template<typename Sample>
    void Sonic<Sample>::ChangeSpeed(double speed) {
        if (m_InputFrameCount < m_MaxRequiredFrameCount) return;
        const int frameCount = m_InputFrameCount;
        int positionFrames = 0;
        do {
            if (m_RemainingInputToCopyFrameCount > 0) {
                positionFrames += CopyInputToOutput(positionFrames);
            } else {
                const int period = FindPitchPeriod(positionFrames);
                if (speed > 1.0) positionFrames += period + SkipPitchPeriod(positionFrames, speed, period);
                else positionFrames += InsertPitchPeriod(positionFrames, speed, period);
            }
        } while (positionFrames + m_MaxRequiredFrameCount <= frameCount);
        RemoveProcessedInputFrames(positionFrames);
    }

    template<typename Sample>
    void Sonic<Sample>::RemoveProcessedInputFrames(int positionFrames) {
        const int remainingFrames = m_InputFrameCount - positionFrames;
        std::copy(m_InputBuffer.begin() + std::ptrdiff_t(positionFrames) * m_ChannelCount,
                  m_InputBuffer.begin() + std::ptrdiff_t(positionFrames + remainingFrames) * m_ChannelCount, m_InputBuffer.begin());
        m_InputFrameCount = remainingFrames;
    }

    template<typename Sample>
    void Sonic<Sample>::ProcessStreamInput() {
        // Resample as many pitch periods as we have buffered on the input.
        const int originalOutputFrameCount = m_OutputFrameCount;
        const double s = double(m_Speed) / double(m_Pitch);
        const float r = m_Rate * m_Pitch;
        if (s > kMinimumSpeedupRate || s < kMinimumSlowdownRate) {
            ChangeSpeed(s);
        } else {
            CopyToOutput(0, m_InputFrameCount);
            m_InputFrameCount = 0;
        }
        if (r != 1.0f) AdjustRate(r, originalOutputFrameCount);
    }

    // --- SonicShortImpl / SonicFloatImpl -------------------------------------------------------

    template<typename Sample>
    void Sonic<Sample>::InterpolateFrame(int positionFrames, std::int64_t oldSampleRate, std::int64_t newSampleRate) {
        for (int i = 0; i < m_ChannelCount; ++i)
            m_OutputBuffer[std::size_t(m_OutputFrameCount * m_ChannelCount + i)] =
                Interpolate(m_PitchBuffer, positionFrames * m_ChannelCount + i, oldSampleRate, newSampleRate);
    }

    template<typename Sample>
    bool Sonic<Sample>::IsPreviousPeriodBetter() const {
        if (m_MinDiff == 0 || m_PrevPeriod == 0) return false;
        if (m_MaxDiff > m_MinDiff * 3) return false;   // the range is wide: don't trust the previous period
        if (m_MinDiff * 2 <= m_PrevMinDiff * 3) return false;   // the minimum didn't grow much
        return true;
    }

    template<typename Sample>
    void Sonic<Sample>::DownSampleInput(int positionFrames, int skip) {
        const int frameCount = m_MaxRequiredFrameCount / skip;
        const int samplesPerValue = m_ChannelCount * skip;
        positionFrames *= m_ChannelCount;
        for (int i = 0; i < frameCount; ++i) {
            if constexpr (std::is_same_v<Sample, float>) {
                double value = 0;
                for (int j = 0; j < samplesPerValue; ++j) value += m_InputBuffer[std::size_t(positionFrames + i * samplesPerValue + j)];
                value /= samplesPerValue;
                m_DownSampleBuffer[std::size_t(i)] = float(value);
            } else {
                std::int32_t value = 0;
                for (int j = 0; j < samplesPerValue; ++j) value += m_InputBuffer[std::size_t(positionFrames + i * samplesPerValue + j)];
                value /= samplesPerValue;
                m_DownSampleBuffer[std::size_t(i)] = Sample(value);
            }
        }
    }

    template<typename Sample>
    int Sonic<Sample>::FindPitchPeriodInRange(const std::vector<Sample>& samples, int positionFrames, int minPeriod, int maxPeriod) {
        int bestPeriod = 0, worstPeriod = 255;
        positionFrames *= m_ChannelCount;
        if constexpr (std::is_same_v<Sample, float>) {
            double minDiff = 1, maxDiff = 0;
            for (int period = minPeriod; period <= maxPeriod; ++period) {
                double diff = 0;
                for (int i = 0; i < period; ++i)
                    diff += std::fabs(samples[std::size_t(positionFrames + i)] - samples[std::size_t(positionFrames + period + i)]);
                // Note that the highest number of samples we add into diff will be less than 256,
                // since we skip samples. Thus, diff is a 24 bit number, and we can safely multiply
                // by numSamples without overflow.
                if (diff * bestPeriod < minDiff * period) {
                    minDiff = diff;
                    bestPeriod = period;
                }
                if (diff * worstPeriod > maxDiff * period) {
                    maxDiff = diff;
                    worstPeriod = period;
                }
            }
            m_MinDiff = minDiff / bestPeriod;
            m_MaxDiff = maxDiff / worstPeriod;
        } else {
            std::int32_t minDiff = 1, maxDiff = 0;
            for (int period = minPeriod; period <= maxPeriod; ++period) {
                std::int32_t diff = 0;
                for (int i = 0; i < period; ++i)
                    diff += std::abs(std::int32_t(samples[std::size_t(positionFrames + i)]) - std::int32_t(samples[std::size_t(positionFrames + period + i)]));
                if (MulWrap(diff, bestPeriod) < MulWrap(minDiff, period)) {
                    minDiff = diff;
                    bestPeriod = period;
                }
                if (MulWrap(diff, worstPeriod) > MulWrap(maxDiff, period)) {
                    maxDiff = diff;
                    worstPeriod = period;
                }
            }
            m_MinDiff = double(minDiff / bestPeriod);   // Java int division
            m_MaxDiff = double(maxDiff / worstPeriod);
        }
        return bestPeriod;
    }

    template<typename Sample>
    void Sonic<Sample>::OverlapAdd(int frameCount, int outPosition, int rampDownPosition, int rampUpPosition) {
        for (int i = 0; i < m_ChannelCount; ++i) {
            int o = outPosition * m_ChannelCount + i, u = rampUpPosition * m_ChannelCount + i, d = rampDownPosition * m_ChannelCount + i;
            for (int t = 0; t < frameCount; ++t) {
                if constexpr (std::is_same_v<Sample, float>) {
                    m_OutputBuffer[std::size_t(o)] = (m_InputBuffer[std::size_t(d)] * float(frameCount - t) + m_InputBuffer[std::size_t(u)] * float(t)) / float(frameCount);
                } else {
                    m_OutputBuffer[std::size_t(o)] =
                        Sample((std::int32_t(m_InputBuffer[std::size_t(d)]) * (frameCount - t) + std::int32_t(m_InputBuffer[std::size_t(u)]) * t) / frameCount);
                }
                o += m_ChannelCount;
                d += m_ChannelCount;
                u += m_ChannelCount;
            }
        }
    }

    template<typename Sample>
    void Sonic<Sample>::Ensure(std::vector<Sample>& buffer, int frameCount, int additionalFrameCount) {
        const int capacityFrames = int(buffer.size()) / m_ChannelCount;
        if (frameCount + additionalFrameCount <= capacityFrames) return;
        const int newCapacityFrames = 3 * capacityFrames / 2 + additionalFrameCount;
        buffer.resize(std::size_t(newCapacityFrames) * std::size_t(m_ChannelCount), Sample{});
    }

    template<typename Sample>
    Sample Sonic<Sample>::Interpolate(const std::vector<Sample>& in, int inPos, std::int64_t oldSampleRate, std::int64_t newSampleRate) const {
        const Sample left = in[std::size_t(inPos)];
        const Sample right = in[std::size_t(inPos + m_ChannelCount)];
        const std::int64_t position = std::int64_t(m_NewRatePosition) * oldSampleRate;
        const std::int64_t leftPosition = std::int64_t(m_OldRatePosition) * newSampleRate;
        const std::int64_t rightPosition = std::int64_t(m_OldRatePosition + 1) * newSampleRate;
        const std::int64_t ratio = rightPosition - position;
        const std::int64_t width = rightPosition - leftPosition;
        if constexpr (std::is_same_v<Sample, float>) return (float(ratio) * left + float(width - ratio) * right) / float(width);
        else return Sample((ratio * left + (width - ratio) * right) / width);
    }

    template class Sonic<std::int16_t>;
    template class Sonic<float>;

    // --- SonicAudioProcessor -------------------------------------------------------------------

    std::int64_t ScaleLargeTimestamp(std::int64_t timestamp, std::int64_t multiplier, std::int64_t divisor) {
        if (timestamp == 0 || multiplier == 0) return 0;
        const __int128 v = __int128(timestamp) * multiplier / divisor;   // RoundingMode.DOWN
        if (v > std::numeric_limits<std::int64_t>::max()) return std::numeric_limits<std::int64_t>::max();
        if (v < std::numeric_limits<std::int64_t>::min()) return std::numeric_limits<std::int64_t>::min();
        return std::int64_t(v);
    }

    void SonicProcessor::Configure(int sampleRateHz, int channelCount, float speed, float pitch, int outputSampleRateHz) {
        if (!(speed > 0) || !(pitch > 0)) throw std::invalid_argument("speed and pitch must be more than 0");
        constexpr float kCloseThreshold = 0.0001f;
        m_InputRate = sampleRateHz;
        m_OutputRate = outputSampleRateHz > 0 ? outputSampleRateHz : sampleRateHz;
        m_Channels = channelCount;
        m_Speed = speed;
        m_Pitch = pitch;
        // isActive: not with the default parameters (areParametersSetToDefaultValues).
        m_Active = !(std::fabs(speed - 1.0f) < kCloseThreshold && std::fabs(pitch - 1.0f) < kCloseThreshold && m_OutputRate == m_InputRate);
        m_Sonic.clear();
        if (m_Active) m_Sonic.emplace_back(m_InputRate, m_Channels, speed, pitch, m_OutputRate);
        m_InputBytes = m_OutputBytes = 0;
        m_InputEnded = false;
    }

    void SonicProcessor::QueueInput(const std::int16_t* samples, int frameCount) {
        if (frameCount <= 0 || m_Sonic.empty()) return;
        m_InputBytes += std::int64_t(frameCount) * m_Channels * 2;
        m_Sonic.front().QueueInput(samples, frameCount);
    }

    void SonicProcessor::GetOutput(std::vector<std::int16_t>& out) {
        if (m_Sonic.empty()) return;
        Sonic<std::int16_t>& s = m_Sonic.front();
        const int frames = s.OutputFrames();
        if (frames <= 0) return;
        const std::size_t at = out.size();
        out.resize(at + std::size_t(frames) * std::size_t(m_Channels));
        s.GetOutput(out.data() + at, frames);
        m_OutputBytes += std::int64_t(frames) * m_Channels * 2;
    }

    void SonicProcessor::QueueEndOfStream() {
        if (!m_Sonic.empty()) m_Sonic.front().QueueEndOfStream();
        m_InputEnded = true;
    }

    bool SonicProcessor::IsEnded() const { return m_InputEnded && (m_Sonic.empty() || m_Sonic.front().OutputFrames() == 0); }

    void SonicProcessor::Flush() {
        if (!m_Sonic.empty()) m_Sonic.front().Flush();
        m_InputBytes = m_OutputBytes = 0;
        m_InputEnded = false;
    }

    void SonicProcessor::RestartDurationCounts() {
        m_InputBytes = m_Sonic.empty() ? 0 : std::int64_t(m_Sonic.front().PendingInputFrames()) * m_Channels * 2;
        m_OutputBytes = 0;
    }

    std::int64_t SonicProcessor::MediaDuration(std::int64_t playoutUs) const {
        if (m_OutputBytes >= kMinBytesForDurationScaling && !m_Sonic.empty()) {
            const std::int64_t processedInputBytes = m_InputBytes - std::int64_t(m_Sonic.front().PendingInputFrames()) * m_Channels * 2;
            return m_OutputRate == m_InputRate ? ScaleLargeTimestamp(playoutUs, processedInputBytes, m_OutputBytes)
                                               : ScaleLargeTimestamp(playoutUs, processedInputBytes * m_OutputRate, m_OutputBytes * m_InputRate);
        }
        return std::int64_t(double(m_Speed) * double(playoutUs));
    }

    std::int64_t SonicProcessor::PlayoutDuration(std::int64_t mediaUs) const {
        if (m_OutputBytes >= kMinBytesForDurationScaling && !m_Sonic.empty()) {
            const std::int64_t processedInputBytes = m_InputBytes - std::int64_t(m_Sonic.front().PendingInputFrames()) * m_Channels * 2;
            return m_OutputRate == m_InputRate ? ScaleLargeTimestamp(mediaUs, m_OutputBytes, processedInputBytes)
                                               : ScaleLargeTimestamp(mediaUs, m_OutputBytes * m_InputRate, processedInputBytes * m_OutputRate);
        }
        return std::int64_t(double(mediaUs) / double(m_Speed));
    }

}
