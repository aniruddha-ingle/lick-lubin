#include "../Source/TrackEffects.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace
{
using licklubin::ReverbMode;
using licklubin::TrackEffects;
using licklubin::VoiceRelease;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

bool closeTo (float actual, float expected)
{
    return std::abs (actual - expected) < 0.00001f;
}

void retriggerReleaseStartsAtUnityAndDoesNotRestart()
{
    VoiceRelease previousVoice;
    VoiceRelease newVoice;
    VoiceRelease otherTrack;
    previousVoice.start (4);
    require (closeTo (previousVoice.nextGain(), 1.0f), "release must start at the current full gain");
    require (closeTo (previousVoice.nextGain(), 2.0f / 3.0f), "release must ramp down");
    previousVoice.start (4);
    require (closeTo (previousVoice.nextGain(), 1.0f / 3.0f), "another release must not restart the ramp");
    require (closeTo (previousVoice.nextGain(), 0.0f) && previousVoice.finished(),
             "release must reach zero before the voice is retired");
    require (closeTo (newVoice.nextGain(), 1.0f) && closeTo (otherTrack.nextGain(), 1.0f),
             "unreleased voices must keep full gain");
}

void duckingUsesShortAttackAndRecoversAtTheRequestedRate()
{
    TrackEffects target;
    target.prepare (1000.0);
    target.setSidechainDepth (0.6f);
    target.setSidechainReleaseMs (20.0f);

    TrackEffects unrelated;
    unrelated.prepare (1000.0);
    unrelated.setSidechainDepth (0.6f);

    auto processConstant = [] (TrackEffects& track)
    {
        float left = 1.0f;
        float right = -1.0f;
        track.process (left, right);
        require (closeTo (left, -right), "sidechain must apply the same gain to both channels");
        return left;
    };

    target.triggerSidechain();
    for (int frame = 1; frame <= 5; ++frame)
        require (closeTo (processConstant (target), 1.0f - 0.12f * static_cast<float> (frame)),
                 "sidechain must reach configured depth over a 5 ms attack");
    for (int frame = 1; frame <= 20; ++frame)
        require (closeTo (processConstant (target), 0.4f + 0.03f * static_cast<float> (frame)),
                 "sidechain must recover to unity over configured release");
    require (closeTo (processConstant (target), 1.0f), "sidechain must stay at unity after recovery");
    require (closeTo (processConstant (unrelated), 1.0f), "an untriggered target must remain untouched");

    target.triggerSidechain();
    require (closeTo (processConstant (target), 0.88f), "a recovered target must duck again");
    target.triggerSidechain();
    require (closeTo (processConstant (target), 0.784f), "a repeated kick must duck from current gain");
}

void delayFollowsTempoAndKeepsChannelsSeparate()
{
    TrackEffects track;
    track.prepare (1000.0);

    const auto checkDelay = [&track] (double bpm, int framesToEcho)
    {
        track.setTempo (bpm);
        track.setDelayBeats (0.5);
        for (int frame = 0; frame <= 2 * framesToEcho; ++frame)
        {
            float left = frame == 0 ? 1.0f : 0.0f;
            float right = 0.0f;
            track.process (left, right);
            const auto expected = frame == 0 ? 1.0f : frame == framesToEcho ? 0.30f
                                : frame == 2 * framesToEcho ? 0.105f : 0.0f;
            require (closeTo (left, expected), "first echo and feedback must land on the selected subdivision");
            require (closeTo (right, 0.0f), "delay must preserve stereo separation");
        }

        track.setDelayBeats (0.0);
        float left = 0.25f;
        float right = -0.5f;
        track.process (left, right);
        require (closeTo (left, 0.25f) && closeTo (right, -0.5f), "bypassed delay must pass dry audio");
    };

    checkDelay (120.0, 250);
    checkDelay (60.0, 500);
}

void roomAndHallProduceDistinctDecayingTails()
{
    const auto tailEnergy = [] (ReverbMode mode)
    {
        TrackEffects track;
        track.prepare (44100.0);
        track.setReverb (mode);
        double energy = 0.0;
        for (int frame = 0; frame < 44100; ++frame)
        {
            float left = frame == 500 ? 0.8f : 0.0f;
            float right = left;
            track.process (left, right);
            if (frame > 500)
                energy += static_cast<double> (left * left + right * right);
        }

        track.setReverb (ReverbMode::off);
        float left = 0.25f;
        float right = -0.5f;
        track.process (left, right);
        require (closeTo (left, 0.25f) && closeTo (right, -0.5f), "bypassed reverb must pass dry audio");
        return energy;
    };

    require (tailEnergy (ReverbMode::off) < 0.00001, "bypassed reverb must produce no tail");
    const auto room = tailEnergy (ReverbMode::room);
    const auto hall = tailEnergy (ReverbMode::hall);
    require (std::isfinite (room) && room > 0.00001, "room must produce a finite tail");
    require (std::isfinite (hall) && hall > room, "hall must produce a stronger tail than room");
}
}

int main()
{
    try
    {
        retriggerReleaseStartsAtUnityAndDoesNotRestart();
        duckingUsesShortAttackAndRecoversAtTheRequestedRate();
        delayFollowsTempoAndKeepsChannelsSeparate();
        roomAndHallProduceDistinctDecayingTails();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Track effects scenario failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "Track effects scenarios passed.\n";
    return 0;
}
