#pragma once

#include <array>
#include <cstdint>
#include <initializer_list>

namespace licklubin
{
namespace arrangement_detail
{
constexpr std::uint16_t pattern (std::initializer_list<int> steps) noexcept
{
    std::uint16_t mask = 0;
    for (const int step : steps)
        mask |= static_cast<std::uint16_t> (1u << (step - 1));
    return mask;
}
}

class ArrangementModel
{
public:
    static constexpr int barCount = 16;
    static constexpr int trackCount = 10;
    static constexpr int stepsPerBar = 16;

    struct TrackInfo
    {
        const char* id;
        const char* alias;
        std::uint16_t defaultPattern;
    };

    struct Section
    {
        const char* name;
        int firstBar;
        int barCount;
    };

    inline static constexpr std::array<TrackInfo, trackCount> tracks {{
        { "perfect_donk", "Perfect Donk", arrangement_detail::pattern ({ 1, 7, 11 }) },
        { "heavyweight", "Heavyweight", arrangement_detail::pattern ({ 1, 11 }) },
        { "clean_kick", "Clean", arrangement_detail::pattern ({ 1, 7, 9, 12 }) },
        { "dancehall_kick", "Dancehall", arrangement_detail::pattern ({ 1, 4, 7, 11 }) },
        { "bunker_rimshot", "Bunker", arrangement_detail::pattern ({ 5, 13 }) },
        { "roll_hihat", "Roll", arrangement_detail::pattern ({ 13 }) },
        { "closed_hihat", "Closed", arrangement_detail::pattern ({ 1, 3, 5, 7, 9, 11, 13, 15 }) },
        { "open_hihat", "Open", arrangement_detail::pattern ({ 3, 7, 11, 15 }) },
        { "flam_clap", "Flam", arrangement_detail::pattern ({ 5, 13 }) },
        { "pop_clap", "Pop", arrangement_detail::pattern ({ 5, 13 }) }
    }};

    ArrangementModel() noexcept
    {
        for (std::size_t track = 0; track < tracks.size(); ++track)
            patterns[track] = tracks[track].defaultPattern;
    }

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

    // A track has one pattern shared by every bar containing a clip on that track.
    bool hasStep (int track, int step) const noexcept
    {
        return isValidStep (track, step) && (patterns[track] & stepBit (step)) != 0;
    }

    bool setStep (int track, int step, bool enabled) noexcept
    {
        if (! isValidStep (track, step))
            return false;

        const auto bit = stepBit (step);
        if (enabled)
            patterns[track] |= bit;
        else
            patterns[track] &= static_cast<std::uint16_t> (~bit);
        return true;
    }

    static bool isValidStep (int track, int step) noexcept
    {
        return track >= 0 && track < trackCount && step >= 0 && step < stepsPerBar;
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
    static constexpr std::uint16_t stepBit (int step) noexcept
    {
        return static_cast<std::uint16_t> (1u << step);
    }

    bool clips[trackCount][barCount] {};
    std::uint16_t patterns[trackCount] {};
};
}
