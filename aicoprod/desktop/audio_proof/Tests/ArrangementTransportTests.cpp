#include "../Source/ArrangementTransport.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
using licklubin::ArrangementModel;
using licklubin::ArrangementTransport;

constexpr double sampleRate = 102.0; // At 102 BPM, one sixteenth note spans 15 frames.
constexpr int framesPerStep = 15;
constexpr int stepsPerBar = ArrangementModel::stepsPerBar;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void firstStepStartsAtFrameZero()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    arrangement.toggleClip (2, 0);

    require (! transport.isPlaying() && transport.playheadBar() == -1,
             "a new transport must be stopped");
    require (! transport.advanceFrame (arrangement), "a stopped transport must have no events");

    transport.start (sampleRate);
    require (transport.isPlaying() && transport.playheadBar() == 0,
             "play must start in the first bar");
    const auto event = transport.advanceFrame (arrangement);
    require (event && event.bar == 0 && event.step == 0 && event.trackMask == (1u << 2),
             "frame zero must report and trigger the first step");
    require (! transport.advanceFrame (arrangement), "the second frame must not repeat the step");
}

void offbeatTimingContinuesAcrossBlocks()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    constexpr int deviceRate = 48000;
    constexpr int blockSize = 257;
    constexpr int lastFrame = 480000;
    constexpr int framesPerMinute = deviceRate * 60;
    constexpr int quarterNotesPerMinute = 102;
    constexpr int stepsPerMinute = quarterNotesPerMinute * 4;

    // Repeat the same two offbeat hits in bars two and three.
    for (int step = 0; step < stepsPerBar; ++step)
        arrangement.setStep (2, step, step == 1 || step == 15);
    arrangement.toggleClip (2, 1);
    arrangement.toggleClip (2, 2);
    transport.start (deviceRate);

    int absoluteStep = 0;
    int triggers = 0;
    for (int blockStart = 0; blockStart <= lastFrame; blockStart += blockSize)
    {
        const auto blockEnd = std::min (blockStart + blockSize, lastFrame + 1);
        for (int frame = blockStart; frame < blockEnd; ++frame)
        {
            const auto event = transport.advanceFrame (arrangement);
            const int expectedFrame = (absoluteStep * framesPerMinute + stepsPerMinute - 1)
                                      / stepsPerMinute;
            require (static_cast<bool> (event) == (frame == expectedFrame),
                     "each sixteenth note must occur at its absolute device frame");
            if (! event)
                continue;

            const int bar = absoluteStep / stepsPerBar;
            const int step = absoluteStep % stepsPerBar;
            require (event.bar == bar && event.step == step,
                     "step and bar indices must advance across block boundaries");
            const bool shouldTrigger = (bar == 1 || bar == 2) && (step == 1 || step == 15);
            require (event.trackMask == (shouldTrigger ? 1u << 2 : 0u),
                     "only programmed offbeats in active bars must trigger");
            require (transport.playheadBar() == bar, "playhead must follow the active bar");
            if (shouldTrigger)
                ++triggers;
            ++absoluteStep;
        }
    }

    require (absoluteStep == 69 && triggers == 4, "block boundaries must not lose or duplicate steps");
}

void defaultPatternsTriggerOnlyInPlacedClips()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    arrangement.toggleClip (2, 0); // Clean syncopation
    arrangement.toggleClip (9, 0); // Pop clap backbeat
    arrangement.toggleClip (6, 0); // Closed eighth notes
    arrangement.toggleClip (5, 1); // Recorded roll
    transport.start (sampleRate);

    constexpr unsigned clean = 1u << 2;
    constexpr unsigned roll = 1u << 5;
    constexpr unsigned closed = 1u << 6;
    constexpr unsigned pop = 1u << 9;
    constexpr unsigned firstBar[] {
        clean | closed, 0u, closed, 0u, closed | pop, 0u, clean | closed, 0u,
        clean | closed, 0u, closed, clean, closed | pop, 0u, closed, 0u
    };

    for (int frame = 0; frame < 3 * stepsPerBar * framesPerStep; ++frame)
    {
        const auto event = transport.advanceFrame (arrangement);
        if (frame % framesPerStep != 0)
        {
            require (! event, "tracks must trigger only on sixteenth-note steps");
            continue;
        }

        const int bar = frame / (stepsPerBar * framesPerStep);
        const int step = frame / framesPerStep % stepsPerBar;
        const unsigned expected = bar == 0 ? firstBar[step] : (bar == 1 && step == 12 ? roll : 0u);
        require (event && event.trackMask == expected,
                 "the kit groove and recorded roll must trigger only in placed clips");
    }
}

void stopCancelsEventsAndPlayRestartsAtZero()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    arrangement.toggleClip (1, 0);
    transport.start (sampleRate);
    require (static_cast<bool> (transport.advanceFrame (arrangement)), "play must emit the first step");

    transport.stop();
    require (! transport.isPlaying() && transport.playheadBar() == -1,
             "stop must clear the active playhead");
    for (int frame = 0; frame < 2 * framesPerStep; ++frame)
        require (! transport.advanceFrame (arrangement), "stop must cancel pending steps");

    transport.start (sampleRate);
    const auto event = transport.advanceFrame (arrangement);
    require (event && event.bar == 0 && event.step == 0 && event.trackMask == (1u << 1),
             "play after stop must start immediately at the first frame");
}

void transportStopsAtTheEndOfBarSixteen()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    arrangement.toggleClip (9, 15);
    transport.start (sampleRate);

    int steps = 0;
    int lastBarTriggers = 0;
    for (int frame = 0; frame < 16 * stepsPerBar * framesPerStep; ++frame)
    {
        require (transport.isPlaying(), "transport must remain active for all sixteen bars");
        const auto event = transport.advanceFrame (arrangement);
        if (event)
        {
            require (event.bar * stepsPerBar + event.step == steps, "steps must cover each bar in order");
            ++steps;
            if (event.trackMask == (1u << 9))
                ++lastBarTriggers;
        }
    }

    require (steps == 256 && lastBarTriggers == 2, "the final bar must play both backbeats");
    require (! transport.isPlaying() && transport.playheadBar() == -1,
             "transport must stop immediately after the final bar");
    require (! transport.advanceFrame (arrangement), "arrangement must not loop past the end");
}
}

int main()
{
    try
    {
        firstStepStartsAtFrameZero();
        offbeatTimingContinuesAcrossBlocks();
        defaultPatternsTriggerOnlyInPlacedClips();
        stopCancelsEventsAndPlayRestartsAtZero();
        transportStopsAtTheEndOfBarSixteen();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Arrangement transport scenario failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "Arrangement transport scenarios passed.\n";
    return 0;
}
