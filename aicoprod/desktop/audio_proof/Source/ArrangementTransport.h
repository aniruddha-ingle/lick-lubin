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
    using TrackMask = std::uint16_t;

    static_assert (ArrangementModel::trackCount <= std::numeric_limits<TrackMask>::digits);

    struct BeatEvent
    {
        TrackMask trackMask = 0; // Bit n corresponds to track n.
        int bar = -1;
        int beat = -1;

        explicit operator bool() const noexcept { return bar >= 0; }
    };

    void start (double sampleRate, double tempoBpm = defaultTempoBpm) noexcept
    {
        stop();
        if (! (sampleRate > 0.0 && tempoBpm > 0.0))
            return;

        samplesPerMinute = sampleRate * 60.0;
        bpm = tempoBpm;
        const auto frames = frameAtBeat (totalBeats);

        // At most one beat per frame, and the exclusive end must fit in the frame counter.
        if (! (samplesPerMinute / bpm >= 1.0
               && frames < static_cast<double> (std::numeric_limits<Frame>::max())))
            return;

        endFrame = static_cast<Frame> (frames);
        currentFrame = 0;
        nextBeatFrame = 0;
        nextBeat = 0;
        currentBar = 0;
        running = true;
    }

    void stop() noexcept { running = false; }
    bool isPlaying() const noexcept { return running; }

    // Zero-based bar while playing, or -1 while stopped.
    int playheadBar() const noexcept { return running ? currentBar : -1; }

    // Call once per audio frame before rendering it. Beat and bar indices are zero-based.
    BeatEvent advanceFrame (const ArrangementModel& arrangement) noexcept
    {
        if (! running)
            return {};

        BeatEvent event;
        if (currentFrame == nextBeatFrame)
        {
            currentBar = nextBeat / beatsPerBar;
            event.bar = currentBar;
            event.beat = nextBeat % beatsPerBar;
            for (int track = 0; track < ArrangementModel::trackCount; ++track)
                if (arrangement.hasClip (track, currentBar))
                    event.trackMask |= static_cast<TrackMask> (1u << track);

            nextBeatFrame = static_cast<Frame> (frameAtBeat (++nextBeat));
        }

        if (++currentFrame == endFrame)
            stop();

        return event;
    }

private:
    using Frame = std::uint64_t;
    static constexpr int totalBeats = ArrangementModel::barCount * beatsPerBar;

    double frameAtBeat (int beat) const noexcept
    {
        return std::ceil (samplesPerMinute * beat / bpm);
    }

    double samplesPerMinute = 0.0;
    double bpm = defaultTempoBpm;
    Frame currentFrame = 0;
    Frame nextBeatFrame = 0;
    Frame endFrame = 0;
    int nextBeat = 0;
    int currentBar = 0;
    bool running = false;
};
}
