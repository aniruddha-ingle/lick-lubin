#include "TrackEffects.h"

#include <algorithm>
#include <cmath>

namespace licklubin
{
namespace
{
constexpr double minimumDelayBeats = 0.25;
constexpr double secondsPerMinute = 60.0;
constexpr double attackSeconds = 0.005;
constexpr float delayWet = 0.30f;
constexpr float delayFeedback = 0.35f;
}

void VoiceRelease::start (int frames) noexcept
{
    if (total == 0)
        remaining = total = std::max (1, frames);
}

float VoiceRelease::nextGain() noexcept
{
    if (total == 0)
        return 1.0f;

    if (remaining == 0)
        return 0.0f;

    return static_cast<float> (--remaining) / static_cast<float> (std::max (1, total - 1));
}

void TrackEffects::prepare (double newSampleRate)
{
    sampleRate = std::isfinite (newSampleRate) && newSampleRate > 0.0 ? newSampleRate : 0.0;
    const auto maxDelay = std::ceil (sampleRate * secondsPerMinute * maximumDelayBeats / minimumTempo);
    delay.resize (static_cast<std::size_t> (maxDelay) + 1);
    if (sampleRate > 0.0)
        reverb.setSampleRate (sampleRate);
    reset();
    updateDelayLength();
}

void TrackEffects::reset() noexcept
{
    writeIndex = 0;
    validFrames = 0;
    reverb.reset();
    duckGain = duckTarget = 1.0f;
    attackRemaining = releaseRemaining = 0;
}

void TrackEffects::setTempo (double bpm) noexcept
{
    if (std::isfinite (bpm) && bpm > 0.0)
    {
        tempo = std::max (minimumTempo, bpm);
        updateDelayLength();
    }
}

void TrackEffects::setDelayBeats (double beats) noexcept
{
    delayBeats = std::isfinite (beats) && beats > 0.0
        ? std::clamp (beats, minimumDelayBeats, maximumDelayBeats) : 0.0;
    if (delayBeats <= 0.0)
        validFrames = 0;
    updateDelayLength();
}

void TrackEffects::updateDelayLength() noexcept
{
    delayLength = sampleRate > 0.0 && delayBeats > 0.0
        ? static_cast<std::size_t> (std::max (1.0, std::round (sampleRate * secondsPerMinute * delayBeats / tempo)))
        : 0;
}

void TrackEffects::setReverb (ReverbMode mode) noexcept
{
    if (mode == reverbMode)
        return;

    reverbMode = mode;
    if (mode == ReverbMode::off)
    {
        reverb.reset();
        return;
    }

    const bool hall = mode == ReverbMode::hall;
    juce::Reverb::Parameters preset;
    preset.roomSize = hall ? 0.82f : 0.40f;
    preset.damping = hall ? 0.38f : 0.60f;
    preset.wetLevel = hall ? 0.22f : 0.15f;
    preset.dryLevel = 0.5f; // JUCE scales this by two internally.
    preset.width = hall ? 1.0f : 0.82f;
    reverb.setParameters (preset);
}

void TrackEffects::setSidechainDepth (float depth) noexcept
{
    duckDepth = std::isfinite (depth) ? std::clamp (depth, 0.0f, 1.0f) : 0.0f;
}

void TrackEffects::setSidechainReleaseMs (float milliseconds) noexcept
{
    if (std::isfinite (milliseconds))
        releaseMs = std::clamp (milliseconds, 10.0f, 1000.0f);
}

void TrackEffects::triggerSidechain() noexcept
{
    if (sampleRate <= 0.0 || duckDepth <= 0.0f)
        return;

    duckTarget = 1.0f - duckDepth;
    attackRemaining = std::max (1, static_cast<int> (std::round (sampleRate * attackSeconds)));
    releaseRemaining = std::max (1, static_cast<int> (std::round (sampleRate * releaseMs / 1000.0)));
}

float TrackEffects::nextSidechainGain() noexcept
{
    if (attackRemaining > 0)
        duckGain += (duckTarget - duckGain) / static_cast<float> (attackRemaining--);
    else if (releaseRemaining > 0)
        duckGain += (1.0f - duckGain) / static_cast<float> (releaseRemaining--);

    return duckGain;
}

void TrackEffects::process (float& left, float& right) noexcept
{
    if (sampleRate <= 0.0)
        return;

    if (delayLength > 0)
    {
        StereoFrame echo;
        if (validFrames >= delayLength)
        {
            const auto readIndex = writeIndex >= delayLength ? writeIndex - delayLength
                                 : writeIndex + delay.size() - delayLength;
            echo = delay[readIndex];
        }

        delay[writeIndex] = { left + echo.left * delayFeedback, right + echo.right * delayFeedback };
        if (++writeIndex == delay.size())
            writeIndex = 0;
        if (validFrames < delay.size())
            ++validFrames;

        left += echo.left * delayWet;
        right += echo.right * delayWet;
    }

    if (reverbMode != ReverbMode::off)
        reverb.processStereo (&left, &right, 1);

    const auto gain = nextSidechainGain();
    left *= gain;
    right *= gain;
}
}
