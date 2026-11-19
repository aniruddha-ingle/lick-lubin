#pragma once

#include <array>

namespace licklubin
{
class ArrangementModel
{
public:
    static constexpr int barCount = 16;
    static constexpr int trackCount = 10;

    struct TrackInfo
    {
        const char* id;
        const char* alias;
    };

    struct Section
    {
        const char* name;
        int firstBar;
        int barCount;
    };

    inline static constexpr std::array<TrackInfo, trackCount> tracks {{
        { "perfect_donk", "Perfect Donk" },
        { "heavyweight", "Heavyweight" },
        { "clean_kick", "Clean" },
        { "dancehall_kick", "Dancehall" },
        { "bunker_rimshot", "Bunker" },
        { "roll_hihat", "Roll" },
        { "closed_hihat", "Closed" },
        { "open_hihat", "Open" },
        { "flam_clap", "Flam" },
        { "pop_clap", "Pop" }
    }};

    inline static constexpr std::array<Section, 3> sections {{
        { "INTRO", 0, 4 },
        { "VERSE", 4, 8 },
        { "CHORUS", 12, 4 }
    }};

    bool hasClip (int track, int bar) const noexcept
    {
        return isValidCell (track, bar) && clips[track][bar];
    }

    // A valid cell is toggled and returns true. Invalid coordinates are a no-op.
    bool toggleClip (int track, int bar) noexcept
    {
        if (! isValidCell (track, bar))
            return false;

        clips[track][bar] = ! clips[track][bar];
        return true;
    }

    static bool isValidCell (int track, int bar) noexcept
    {
        return track >= 0 && track < trackCount && bar >= 0 && bar < barCount;
    }

    static const Section* sectionForBar (int bar) noexcept
    {
        if (bar < 0 || bar >= barCount)
            return nullptr;

        for (const auto& section : sections)
            if (bar >= section.firstBar && bar < section.firstBar + section.barCount)
                return &section;

        return nullptr;
    }

private:
    bool clips[trackCount][barCount] {};
};
}
