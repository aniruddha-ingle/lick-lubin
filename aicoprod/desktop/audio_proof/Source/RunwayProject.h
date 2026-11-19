#pragma once

#include "ArrangementModel.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <map>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace licklubin
{
// Musician-facing positions are one-based. Track indices use ArrangementModel::tracks.
// Edit this project on the control thread and publish immutable data for rendering.
struct RunwayRange { int first, last; }; // inclusive
struct RunwayPosition { int loopId, loopBar; };

struct RunwayStep
{
    // Semitones from the unpitched sample's normal playback rate (root = 0).
    std::vector<int> pitches;
    // Monophonic slide begins at pitches.front() and ends at this pitch.
    std::optional<int> slideTarget;
};

class RunwayProject
{
public:
    static constexpr int loopBars = 16, runwayBars = 64, groupBars = 4;
    static constexpr int sectionCount = runwayBars / loopBars;
    static constexpr int steps = ArrangementModel::stepsPerBar;
    static constexpr int tracks = ArrangementModel::trackCount;
    static_assert (runwayBars % loopBars == 0 && loopBars % groupBars == 0);

    explicit RunwayProject (unsigned seed = std::random_device{}()) : random (seed) {}

    static bool validRange (RunwayRange r) noexcept
    {
        return r.first >= 1 && r.first <= r.last && r.last <= runwayBars;
    }

    static int groupForBar (int bar) noexcept
    {
        return validRange ({bar, bar}) ? (bar - 1) / groupBars + 1 : 0;
    }

    int createLoop() { return append (Loop{}); }

    int duplicateLoop (int source)
    {
        auto it = loops.find (source);
        return it == loops.end() ? 0 : append (it->second);
    }

    std::string loopName (int id) const
    {
        auto it = loops.find (id);
        return it == loops.end() ? std::string{} : it->second.name;
    }

    bool setStep (int id, int bar, int track, int step,
                  std::vector<int> pitches, std::optional<int> slideTarget = {})
    {
        auto it = loops.find (id);
        if (it == loops.end() || ! validCell (bar, track, step)) return false;
        std::sort (pitches.begin(), pitches.end());
        if (! std::all_of (pitches.begin(), pitches.end(), validPitch)
            || std::adjacent_find (pitches.begin(), pitches.end()) != pitches.end()
            || (slideTarget && (pitches.size() != 1 || ! validPitch (*slideTarget)))) return false;
        const int index = key (bar, track, step);
        if (pitches.empty()) it->second.events.erase (index);
        else it->second.events[index] = { std::move (pitches), slideTarget };
        return true;
    }

    const RunwayStep* event (int id, int bar, int track, int step) const noexcept
    {
        auto it = loops.find (id);
        if (it == loops.end() || ! validCell (bar, track, step)) return nullptr;
        auto ev = it->second.events.find (key (bar, track, step));
        return ev == it->second.events.end() ? nullptr : &ev->second;
    }

    bool transpose (int id, int bar, int track, int step, int semitones)
    {
        const auto* ev = event (id, bar, track, step);
        if (!ev || semitones < -48 || semitones > 48) return false;
        auto shifted = *ev;
        for (auto& n : shifted.pitches) n += semitones;
        if (shifted.slideTarget) *shifted.slideTarget += semitones;
        return setStep (id, bar, track, step, std::move (shifted.pitches), shifted.slideTarget);
    }

    bool placeSections (int loop, std::initializer_list<int> sections)
    {
        return assign (0, loop, sections);
    }

    bool replaceSections (int previous, int loop, std::initializer_list<int> sections)
    {
        return loops.count (previous) != 0 && assign (previous, loop, sections);
    }

    // Explicit runway bars must cover entire loop slots (1–16, 17–32, ...).
    bool placeRange (int loop, RunwayRange range)
    {
        if (!validRange (range) || (range.first - 1) % loopBars || range.last % loopBars || !loops.count (loop))
            return false;
        const int begin = (range.first - 1) / loopBars, end = range.last / loopBars;
        if (!std::all_of (slots.begin() + begin, slots.begin() + end,
                          [loop](int id) { return id == 0 || id == loop; })) return false;
        std::fill (slots.begin() + begin, slots.begin() + end, loop);
        return true;
    }

    std::optional<RunwayPosition> resolve (int bar) const noexcept
    {
        if (!validRange ({bar, bar})) return {};
        const int id = slots[(bar - 1) / loopBars];
        if (id == 0) return {};
        return RunwayPosition { id, (bar - 1) % loopBars + 1 };
    }

    std::optional<RunwayRange> writtenRange() const noexcept
    {
        int first = 0, last = 0;
        for (int i = 0; i < sectionCount; ++i)
            if (slots[i] != 0)
            {
                if (first == 0) first = i * loopBars + 1;
                last = (i + 1) * loopBars;
            }
        if (first == 0) return {};
        return RunwayRange { first, last };
    }

private:
    struct Loop { std::string name; std::map<int, RunwayStep> events; };
    std::map<int, Loop> loops;
    std::array<int, sectionCount> slots {};
    std::mt19937 random;

    int append (Loop loop)
    {
        const int id = static_cast<int> (loops.size()) + 1;
        std::string tag (5, 'a');
        std::uniform_int_distribution<int> letter (0, 25);
        for (auto& c : tag) c = static_cast<char> ('a' + letter (random));
        loop.name = "loop-" + tag + "-" + std::to_string (id);
        loops.emplace (id, std::move (loop));
        return id;
    }

    static bool validPitch (int n) noexcept { return n >= -24 && n <= 24; }
    static bool validCell (int bar, int track, int step) noexcept
    {
        return bar >= 1 && bar <= loopBars && track >= 0 && track < tracks && step >= 1 && step <= steps;
    }
    static int key (int bar, int track, int step) noexcept { return ((bar - 1) * tracks + track) * steps + step - 1; }

    bool assign (int previous, int loop, std::initializer_list<int> sections)
    {
        if (!loops.count (loop) || sections.size() == 0) return false;
        for (int section : sections)
        {
            if (section < 1 || section > sectionCount) return false;
            const int current = slots[section - 1];
            if (current != previous && !(previous == 0 && current == loop)) return false;
        }
        for (int section : sections) slots[section - 1] = loop;
        return true;
    }
};
}
