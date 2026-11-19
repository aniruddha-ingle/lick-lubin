#pragma once

#include "ArrangementModel.h"

#include <cmath>
#include <cstdint>
#include <limits>

namespace licklubin
{
// The audio thread owns the transport and supplies an arrangement safe to read.
class ArrangementTransport
{
public:
    static constexpr double defaultTempoBpm = 102.0;
    static constexpr int beatsPerBar = 4;
    static constexpr int stepsPerBeat = ArrangementModel::stepsPerBar / beatsPerBar;
    using TrackMask = std::uint16_t;

    static_assert (ArrangementModel::trackCount <= std::numeric_limits<TrackMask>::digits);

    struct StepEvent
    {
        TrackMask trackMask = 0; // Bit n corresponds to track n.
        int bar = -1;
        int step = -1;

        explicit operator bool() const noexcept { return bar >= 0; }
    };

    void start (double sampleRate, double tempoBpm = defaultTempoBpm) noexcept
    {
        stop();
        if (! (sampleRate > 0.0 && tempoBpm > 0.0))
            return;

        samplesPerMinute = sampleRate * 60.0;
        bpm = tempoBpm;
        const auto frames = frameAtStep (totalSteps);

        // At most one step per frame, and the end must fit in the frame counter.
        if (! (samplesPerMinute / (bpm * stepsPerBeat) >= 1.0
               && frames < static_cast<double> (std::numeric_limits<Frame>::max())))
            return;

        endFrame = static_cast<Frame> (frames);
        currentFrame = 0;
        nextStepFrame = 0;
        nextStep = 0;
        currentBar = 0;
        running = true;
    }

    void stop() noexcept { running = false; }
    bool isPlaying() const noexcept { return running; }

    // Zero-based bar while playing, or -1 while stopped.
    int playheadBar() const noexcept { return running ? currentBar : -1; }

    // Call once per audio frame before rendering it. Step and bar indices are zero-based.
    StepEvent advanceFrame (const ArrangementModel& arrangement) noexcept
    {
        if (! running)
            return {};

        StepEvent event;
        if (currentFrame == nextStepFrame)
        {
            currentBar = nextStep / ArrangementModel::stepsPerBar;
            event.bar = currentBar;
            event.step = nextStep % ArrangementModel::stepsPerBar;
            for (int track = 0; track < ArrangementModel::trackCount; ++track)
                if (arrangement.hasClip (track, currentBar) && arrangement.hasStep (track, event.step))
                    event.trackMask |= static_cast<TrackMask> (1u << track);

            nextStepFrame = static_cast<Frame> (frameAtStep (++nextStep));
        }

        if (++currentFrame == endFrame)
            stop();

        return event;
    }

private:
    using Frame = std::uint64_t;
    static constexpr int totalSteps = ArrangementModel::barCount * ArrangementModel::stepsPerBar;

    double frameAtStep (int step) const noexcept
    {
        return std::ceil (samplesPerMinute * step / (bpm * stepsPerBeat));
    }

    double samplesPerMinute = 0.0;
    double bpm = defaultTempoBpm;
    Frame currentFrame = 0;
    Frame nextStepFrame = 0;
    Frame endFrame = 0;
    int nextStep = 0;
    int currentBar = 0;
    bool running = false;
};
}
