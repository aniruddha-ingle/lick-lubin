#include "../Source/ArrangementModel.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

bool sameCells (const licklubin::ArrangementModel& left,
                const licklubin::ArrangementModel& right)
{
    for (int track = 0; track < licklubin::ArrangementModel::trackCount; ++track)
        for (int bar = 0; bar < licklubin::ArrangementModel::barCount; ++bar)
            if (left.hasClip (track, bar) != right.hasClip (track, bar))
                return false;
    return true;
}

void sectionBoundsAreFixedToSixteenBars()
{
    using Model = licklubin::ArrangementModel;
    require (Model::sections[0].firstBar == 0 && Model::sections[0].barCount == 4,
             "INTRO must span bars 1-4");
    require (Model::sections[1].firstBar == 4 && Model::sections[1].barCount == 8,
             "VERSE must span bars 5-12");
    require (Model::sections[2].firstBar == 12 && Model::sections[2].barCount == 4,
             "CHORUS must span bars 13-16");
    require (Model::sectionForBar (0) == &Model::sections[0], "bar 1 must be INTRO");
    require (Model::sectionForBar (3) == &Model::sections[0], "bar 4 must be INTRO");
    require (Model::sectionForBar (4) == &Model::sections[1], "bar 5 must be VERSE");
    require (Model::sectionForBar (11) == &Model::sections[1], "bar 12 must be VERSE");
    require (Model::sectionForBar (12) == &Model::sections[2], "bar 13 must be CHORUS");
    require (Model::sectionForBar (15) == &Model::sections[2], "bar 16 must be CHORUS");
    require (Model::sectionForBar (-1) == nullptr && Model::sectionForBar (16) == nullptr,
             "bars outside the arrangement must not belong to a section");
}

void validCellCanBeTurnedOnAndOff()
{
    licklubin::ArrangementModel model;
    require (model.toggleClip (2, 5), "valid cell edit should be accepted");
    require (model.hasClip (2, 5), "valid cell should turn on");
    require (model.toggleClip (2, 5), "second valid edit should be accepted");
    require (! model.hasClip (2, 5), "second edit should turn the cell off");
}

void invalidEditsAreRejectedWithoutPartialMutation()
{
    licklubin::ArrangementModel model;
    require (model.toggleClip (0, 0), "seed edit should be accepted");
    const auto beforeInvalidEdits = model;

    require (! model.toggleClip (-1, 0), "negative track must be rejected");
    require (sameCells (beforeInvalidEdits, model), "invalid track edit must not mutate cells");
    require (! model.toggleClip (licklubin::ArrangementModel::trackCount, 0),
             "track past final row must be rejected");
    require (sameCells (beforeInvalidEdits, model), "invalid track edit must remain atomic");
    require (! model.toggleClip (0, -1), "negative bar must be rejected");
    require (sameCells (beforeInvalidEdits, model), "invalid bar edit must not mutate cells");
    require (! model.toggleClip (0, licklubin::ArrangementModel::barCount),
             "bar past final column must be rejected");
    require (sameCells (beforeInvalidEdits, model), "out-of-range bar edit must remain atomic");
}

void stepEditsAreSharedAcrossClipsButIsolatedByTrack()
{
    using Model = licklubin::ArrangementModel;
    Model model;
    require (model.toggleClip (2, 0) && model.toggleClip (2, 5), "place two Clean clips");
    require (! model.hasStep (2, 15), "step sixteen starts disabled on Clean");
    require (model.setStep (2, 15, true) && model.hasStep (2, 15),
             "step sixteen can be enabled");
    require (model.hasClip (2, 0) && model.hasClip (2, 5),
             "editing a shared pattern must preserve both clip placements");
    require (! model.hasStep (3, 15), "editing Clean must preserve Dancehall's pattern");

    const auto beforeInvalidEdits = model;
    require (! model.setStep (-1, 15, false) && ! model.setStep (Model::trackCount, 15, true)
             && ! model.setStep (2, -1, true) && ! model.setStep (2, Model::stepsPerBar, true),
             "reject invalid step edits");
    for (int track = 0; track < Model::trackCount; ++track)
        for (int step = 0; step < Model::stepsPerBar; ++step)
            require (model.hasStep (track, step) == beforeInvalidEdits.hasStep (track, step),
                     "invalid step edits must leave the whole pattern intact");

    require (model.setStep (2, 15, false) && ! model.hasStep (2, 15),
             "step sixteen can be disabled again");
    require (model.toggleClip (2, 0) && model.hasStep (2, 0),
             "removing a clip must preserve the track pattern");
}
}

int main()
{
    try
    {
        sectionBoundsAreFixedToSixteenBars();
        validCellCanBeTurnedOnAndOff();
        invalidEditsAreRejectedWithoutPartialMutation();
        stepEditsAreSharedAcrossClipsButIsolatedByTrack();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Arrangement model scenario failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "Arrangement model scenarios passed.\n";
    return 0;
}
