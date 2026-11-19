#pragma once

#include "ArrangementModel.h"

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <array>

namespace licklubin
{
class SampleBank final
{
public:
    struct Sample
    {
        juce::AudioBuffer<float> buffer;
        double sourceSampleRate = 0.0;
    };

    // Construct on a worker and publish the finished bank before audio can read it.
    SampleBank (const juce::var& manifest, const juce::File& sampleRoot);

    SampleBank (const SampleBank&) = delete;
    SampleBank& operator= (const SampleBank&) = delete;

    // Callback-safe. A returned sample stays valid for the lifetime of the bank.
    const Sample* sampleForTrack (int track) const noexcept;
    int readyCount() const noexcept { return loadedCount; }

    // For status displays. An empty string means the sample is ready.
    const juce::String& errorForTrack (int track) const noexcept;

private:
    struct TrackSample
    {
        Sample sample;
        juce::String error;
    };

    std::array<TrackSample, ArrangementModel::trackCount> tracks;
    int loadedCount = 0;
};
}
