#include "../Source/ArrangementTransport.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
using licklubin::ArrangementModel;
using licklubin::ArrangementTransport;

constexpr double sampleRate = 102.0; // At 102 BPM, one beat spans 60 frames.
constexpr int framesPerBeat = 60;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void firstBeatStartsAtFrameZero()
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
    require (event && event.bar == 0 && event.beat == 0 && event.trackMask == (1u << 2),
             "frame zero must report and trigger the first beat");
    require (! transport.advanceFrame (arrangement), "the second frame must not repeat the beat");
}

void beatTimingContinuesAcrossBlocks()
{
    const ArrangementModel arrangement;
    ArrangementTransport transport;
    constexpr int deviceRate = 48000;
    constexpr int blockSize = 257;
    constexpr int lastFrame = 480000;
    transport.start (deviceRate);

    int beat = 0;
    for (int blockStart = 0; blockStart <= lastFrame; blockStart += blockSize)
    {
        const auto blockEnd = std::min (blockStart + blockSize, lastFrame + 1);
        for (int frame = blockStart; frame < blockEnd; ++frame)
        {
            const auto event = transport.advanceFrame (arrangement);
            if (event)
            {
                const int expectedFrame = (beat * deviceRate * 60 + 101) / 102;
                require (frame == expectedFrame, "beat must land at its absolute device frame");
                require (event.bar == beat / 4 && event.beat == beat % 4,
                         "beat and bar indices must advance through block boundaries");
                require (event.trackMask == 0, "empty bars must report beats without triggers");
                require (transport.playheadBar() == event.bar, "playhead must follow the active bar");
                ++beat;
            }
        }
    }

    require (beat == 18, "block boundaries must not lose or duplicate beats");
}

void activeTracksTriggerOnEveryBeatOfTheirBar()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    arrangement.toggleClip (0, 0);
    arrangement.toggleClip (9, 0);
    arrangement.toggleClip (2, 1);
    transport.start (sampleRate);

    for (int frame = 0; frame < 3 * 4 * framesPerBeat; ++frame)
    {
        const auto event = transport.advanceFrame (arrangement);
        if (frame % framesPerBeat == 0)
        {
            const int bar = frame / (4 * framesPerBeat);
            const unsigned expectedMask[] { (1u << 0) | (1u << 9), 1u << 2, 0u };
            require (event && event.trackMask == expectedMask[bar],
                     "each beat must trigger all and only the active tracks in its bar");
        }
        else
        {
            require (! event, "tracks must trigger only on beats");
        }
    }
}

void stopCancelsEventsAndPlayRestartsAtZero()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    arrangement.toggleClip (1, 0);
    transport.start (sampleRate);
    require (static_cast<bool> (transport.advanceFrame (arrangement)), "play must emit the first beat");

    transport.stop();
    require (! transport.isPlaying() && transport.playheadBar() == -1,
             "stop must clear the active playhead");
    for (int frame = 0; frame < 2 * framesPerBeat; ++frame)
        require (! transport.advanceFrame (arrangement), "stop must cancel pending beats");

    transport.start (sampleRate);
    const auto event = transport.advanceFrame (arrangement);
    require (event && event.bar == 0 && event.beat == 0 && event.trackMask == (1u << 1),
             "play after stop must start immediately at the first frame");
}

void transportStopsAtTheEndOfBarSixteen()
{
    ArrangementModel arrangement;
    ArrangementTransport transport;
    arrangement.toggleClip (9, 15);
    transport.start (sampleRate);

    int beats = 0;
    int lastBarTriggers = 0;
    for (int frame = 0; frame < 16 * 4 * framesPerBeat; ++frame)
    {
        require (transport.isPlaying(), "transport must remain active for all sixteen bars");
        const auto event = transport.advanceFrame (arrangement);
        if (event)
        {
            require (event.bar * 4 + event.beat == beats, "beats must cover each bar in order");
            ++beats;
            if (event.trackMask == (1u << 9))
                ++lastBarTriggers;
        }
    }

    require (beats == 64 && lastBarTriggers == 4, "the final bar must play all four beats");
    require (! transport.isPlaying() && transport.playheadBar() == -1,
             "transport must stop immediately after the final bar");
    require (! transport.advanceFrame (arrangement), "arrangement must not loop past the end");
}
}

int main()
{
    try
    {
        firstBeatStartsAtFrameZero();
        beatTimingContinuesAcrossBlocks();
        activeTracksTriggerOnEveryBeatOfTheirBar();
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
