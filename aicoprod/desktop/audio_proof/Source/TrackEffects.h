#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstddef>
#include <vector>

namespace licklubin
{
// One ramp per voice. Repeated releases leave an in-progress ramp alone.
class VoiceRelease
{
public:
    void start (int frames) noexcept;
    float nextGain() noexcept;
    bool finished() const noexcept { return total > 0 && remaining == 0; }

private:
    int total = 0;
    int remaining = 0;
};

enum class ReverbMode { off, room, hall };

// Owned by the audio thread after prepare(). Settings and processing keep storage fixed.
class TrackEffects final
{
public:
    static constexpr double minimumTempo = 40.0;
    static constexpr double maximumDelayBeats = 2.0;

    // Call outside the real-time render callback; this allocates the delay and reverb buffers.
    void prepare (double newSampleRate);
    void reset() noexcept;

    void setTempo (double bpm) noexcept;
    // Zero bypasses the delay. Nonzero values are supported from a sixteenth note to two beats.
    void setDelayBeats (double beats) noexcept;
    void setReverb (ReverbMode mode) noexcept;
    void setSidechainDepth (float depth) noexcept;
    void setSidechainReleaseMs (float milliseconds) noexcept;
    void triggerSidechain() noexcept;

    // Run once per frame, including silent input, to render tails and advance ducking.
    void process (float& left, float& right) noexcept;

private:
    struct StereoFrame
    {
        float left = 0.0f;
        float right = 0.0f;
    };

    void updateDelayLength() noexcept;
    float nextSidechainGain() noexcept;

    double sampleRate = 0.0;
    double tempo = 102.0;
    double delayBeats = 0.0;
    std::vector<StereoFrame> delay;
    std::size_t writeIndex = 0;
    std::size_t validFrames = 0;
    std::size_t delayLength = 0;

    juce::Reverb reverb;
    ReverbMode reverbMode = ReverbMode::off;

    float duckDepth = 0.0f;
    float releaseMs = 120.0f;
    float duckGain = 1.0f;
    float duckTarget = 1.0f;
    int attackRemaining = 0;
    int releaseRemaining = 0;
};
}
