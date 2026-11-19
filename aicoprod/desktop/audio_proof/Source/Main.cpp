#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <BinaryData.h>
#include "ArrangementModel.h"
#include "ArrangementTransport.h"
#include "CommandDispatcher.h"
#include "CommandServer.h"
#include "SampleBank.h"
#include "TrackEffects.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <memory>
#include <thread>
#include <vector>

namespace
{
using licklubin::ArrangementModel;
using licklubin::ArrangementTransport;
using licklubin::ReverbMode;
using licklubin::SampleBank;
using licklubin::TrackEffects;

constexpr double stopReleaseSeconds = 0.012;
constexpr double retriggerReleaseSeconds = 0.010;
constexpr float voiceGain = 0.25f;
constexpr float sidechainDepth = 0.55f;
constexpr int voicesPerTrack = 8;
constexpr int cleanTrack = 2;
static_assert (std::atomic<bool>::is_always_lock_free && std::atomic<int>::is_always_lock_free,
               "Audio/UI state must use lock-free atomics.");

juce::var findNamedEntry (const juce::var& collection, const juce::String& wantedId)
{
    if (auto* object = collection.getDynamicObject())
        return object->getProperty (juce::Identifier (wantedId));

    if (auto* entries = collection.getArray())
    {
        for (const auto& entry : *entries)
        {
            if (auto* object = entry.getDynamicObject())
            {
                auto id = object->getProperty (juce::Identifier ("id")).toString();
                if (id.isEmpty())
                    id = object->getProperty (juce::Identifier ("sample_id")).toString();

                if (id == wantedId)
                    return entry;
            }
        }
    }

    return {};
}

juce::String readSamplePath (const juce::var& entry)
{
    if (entry.isString())
        return entry.toString();

    if (auto* object = entry.getDynamicObject())
    {
        for (const auto* key : { "path", "relative_path", "relativePath", "file" })
        {
            const auto value = object->getProperty (juce::Identifier (key));
            if (value.isString() && value.toString().isNotEmpty())
                return value.toString();
        }
    }

    return {};
}

juce::var parseEmbeddedManifest (juce::String& error)
{
    if (BinaryData::manifest_json == nullptr || BinaryData::manifest_jsonSize <= 0)
    {
        error = "Manifest error: embedded manifest_json is empty.";
        return {};
    }

    const auto jsonText = juce::String::fromUTF8 (BinaryData::manifest_json,
                                                   BinaryData::manifest_jsonSize);
    const auto manifest = juce::JSON::parse (jsonText);
    if (manifest.isVoid())
    {
        error = "Manifest error: embedded manifest_json is not valid JSON.";
        return {};
    }

    return manifest;
}

struct MotifNote
{
    int startTick = 0;
    int durationTicks = 0;
    int midiNote = 0;
    int velocity = 0;
    int channel = 1;
};

struct Motif
{
    juce::String alias;
    juce::String key;
    double tempoBpm = 0.0;
    int ticksPerQuarter = 0;
    int endTick = 0;
    int trackCount = 0;
    std::vector<MotifNote> notes;
};

bool isSafeRelativePath (const juce::String& path)
{
    const auto normalized = path.replaceCharacter ('\\', '/');
    return normalized.isNotEmpty() && ! normalized.startsWithChar ('/')
        && ! normalized.contains (":") && ! normalized.contains ("../")
        && normalized != "..";
}

juce::File resolveLocalFile (const juce::File& root, const juce::String& path)
{
    return root.getChildFile (path.replaceCharacter ('\\', '/'));
}

bool importMotif (const juce::var& manifest, const juce::File& root,
                  Motif& motif, juce::String& error)
{
    auto* object = manifest.getDynamicObject();
    const auto entry = object != nullptr ? findNamedEntry (
        object->getProperty (juce::Identifier ("motifs")), "new_flow") : juce::var();
    if (entry.isVoid() || entry.getDynamicObject() == nullptr)
    {
        error = "Motif error: New Flow entry is missing.";
        return false;
    }

    auto* metadata = entry.getDynamicObject();
    const auto relativePath = readSamplePath (entry).replaceCharacter ('\\', '/');
    const auto tempo = static_cast<double> (metadata->getProperty (juce::Identifier ("tempo_bpm")));
    const auto key = metadata->getProperty (juce::Identifier ("key")).toString();
    if (! isSafeRelativePath (relativePath) || tempo <= 0.0 || key.isEmpty()
        || metadata->getProperty (juce::Identifier ("metadata_source")).toString() != "filename")
    {
        error = "Motif error: New Flow path or filename metadata is invalid.";
        return false;
    }

    const auto file = resolveLocalFile (root, relativePath);
    if (! file.isAChildOf (root) || ! file.existsAsFile())
    {
        error = "MIDI unavailable: " + file.getFullPathName();
        return false;
    }

    juce::FileInputStream input (file);
    juce::MidiFile midi;
    if (! input.openedOk() || ! midi.readFrom (input) || midi.getTimeFormat() <= 0
        || midi.getNumTracks() < 1)
    {
        error = "MIDI error: New Flow is not a readable PPQ MIDI file.";
        return false;
    }

    Motif imported;
    imported.alias = metadata->getProperty (juce::Identifier ("alias")).toString();
    imported.key = key;
    imported.tempoBpm = tempo;
    imported.ticksPerQuarter = midi.getTimeFormat();
    imported.trackCount = midi.getNumTracks();
    for (int trackIndex = 0; trackIndex < midi.getNumTracks(); ++trackIndex)
    {
        const auto* track = midi.getTrack (trackIndex);
        if (track == nullptr)
            continue;
        imported.endTick = juce::jmax (imported.endTick,
                                        static_cast<int> (std::ceil (track->getEndTime())));
        for (int eventIndex = 0; eventIndex < track->getNumEvents(); ++eventIndex)
        {
            const auto* event = track->getEventPointer (eventIndex);
            if (event == nullptr || ! event->message.isNoteOn())
                continue;
            const auto noteOffIndex = track->getIndexOfMatchingKeyUp (eventIndex);
            const auto noteOffTick = noteOffIndex >= 0
                                   ? track->getEventTime (noteOffIndex) : event->message.getTimeStamp();
            imported.notes.push_back ({ static_cast<int> (std::lround (event->message.getTimeStamp())),
                                        juce::jmax (0, static_cast<int> (std::lround (
                                            noteOffTick - event->message.getTimeStamp()))),
                                        event->message.getNoteNumber(),
                                        static_cast<int> (event->message.getVelocity()),
                                        event->message.getChannel() });
        }
    }
    if (imported.notes.empty() || imported.ticksPerQuarter <= 0 || imported.endTick <= 0)
    {
        error = "MIDI error: New Flow contains no usable note timeline.";
        return false;
    }
    motif = std::move (imported);
    return true;
}

struct Voice
{
    const SampleBank::Sample* sample = nullptr;
    double sourcePosition = 0.0;
    double sourceStep = 0.0;
    licklubin::VoiceRelease releaseRamp;

    void release (int frames) noexcept
    {
        if (sample != nullptr)
            releaseRamp.start (frames);
    }
};

struct TrackSettings
{
    bool cutItself = true;
    bool echo = false;
    ReverbMode reverb = ReverbMode::off;
    int sidechainSource = -1;

    int packed() const noexcept
    {
        return static_cast<int> (cutItself) | (static_cast<int> (echo) << 1)
               | (static_cast<int> (reverb) << 2) | ((sidechainSource + 1) << 4);
    }

    static TrackSettings unpack (int value) noexcept
    {
        return { (value & 1) != 0, (value & 2) != 0,
                 static_cast<ReverbMode> ((value >> 2) & 3), (value >> 4) - 1 };
    }
};
}

class ArrangementGridComponent final : public juce::Component
{
public:
    explicit ArrangementGridComponent (licklubin::ArrangementModel& state)
        : model (state)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    std::function<void (int, int)> onCellClicked;

    void setPlayheadBar (int bar)
    {
        if (playheadBar != bar)
        {
            playheadBar = bar;
            repaint();
        }
    }

    void paint (juce::Graphics& graphics) override
    {
        constexpr int labelWidth = 145;
        constexpr int sectionHeight = 24;
        constexpr int barHeaderHeight = 24;
        constexpr int trackHeight = 25;

        graphics.fillAll (juce::Colour::fromRGB (29, 33, 40));
        auto bounds = getLocalBounds();
        const auto cellWidth = static_cast<float> (juce::jmax (1, bounds.getWidth() - labelWidth))
                             / licklubin::ArrangementModel::barCount;
        auto header = bounds.removeFromTop (sectionHeight);
        graphics.setColour (juce::Colours::lightgrey);
        for (const auto& section : licklubin::ArrangementModel::sections)
        {
            const auto x = labelWidth + static_cast<int> (std::lround (section.firstBar * cellWidth));
            const auto width = static_cast<int> (std::lround (section.barCount * cellWidth));
            graphics.drawText (section.name, x, header.getY(), width, header.getHeight(),
                               juce::Justification::centred, false);
        }

        auto barHeader = bounds.removeFromTop (barHeaderHeight);
        graphics.setColour (juce::Colour::fromRGB (75, 84, 98));
        graphics.drawLine (static_cast<float> (labelWidth), static_cast<float> (barHeader.getY()),
                           static_cast<float> (labelWidth), static_cast<float> (getHeight()), 1.0f);
        for (int bar = 0; bar < licklubin::ArrangementModel::barCount; ++bar)
        {
            const auto x = labelWidth + static_cast<int> (std::lround (bar * cellWidth));
            const auto width = static_cast<int> (std::ceil (cellWidth));
            graphics.setColour (bar == playheadBar ? juce::Colour::fromRGB (51, 101, 113)
                                                   : juce::Colour::fromRGB (38, 43, 52));
            graphics.fillRect (x, barHeader.getY(), width, barHeader.getHeight());
            graphics.setColour (juce::Colours::lightgrey);
            graphics.drawText (juce::String (bar + 1), x, barHeader.getY(), width,
                               barHeader.getHeight(), juce::Justification::centred, false);
        }

        for (int track = 0; track < licklubin::ArrangementModel::trackCount; ++track)
        {
            const auto row = bounds.removeFromTop (trackHeight);
            graphics.setColour (juce::Colours::lightgrey);
            graphics.drawText (licklubin::ArrangementModel::tracks[static_cast<size_t> (track)].alias,
                               0, row.getY(), labelWidth - 8, row.getHeight(),
                               juce::Justification::centredRight, false);
            for (int bar = 0; bar < licklubin::ArrangementModel::barCount; ++bar)
            {
                const auto x = labelWidth + static_cast<int> (std::lround (bar * cellWidth));
                const auto width = static_cast<int> (std::ceil (cellWidth));
                const auto cell = juce::Rectangle<int> (x + 2, row.getY() + 2,
                                                         juce::jmax (1, width - 4), row.getHeight() - 4);
                graphics.setColour (model.hasClip (track, bar)
                    ? juce::Colour::fromRGB (104, 178, 141)
                    : juce::Colour::fromRGB (49, 56, 67));
                graphics.fillRoundedRectangle (cell.toFloat(), 3.0f);
                graphics.setColour (bar == playheadBar ? juce::Colour::fromRGB (119, 201, 215)
                                                       : juce::Colour::fromRGB (70, 78, 91));
                graphics.drawRect (cell, 1);
            }
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        constexpr int labelWidth = 145;
        constexpr int headerHeight = 48;
        constexpr int trackHeight = 25;
        if (event.x < labelWidth || event.y < headerHeight)
            return;

        const auto cellWidth = static_cast<float> (juce::jmax (1, getWidth() - labelWidth))
                             / licklubin::ArrangementModel::barCount;
        const auto track = (event.y - headerHeight) / trackHeight;
        const auto bar = static_cast<int> ((event.x - labelWidth) / cellWidth);
        if (onCellClicked != nullptr && ArrangementModel::isValidCell (track, bar))
            onCellClicked (track, bar);
    }

private:
    licklubin::ArrangementModel& model;
    int playheadBar = -1;
};

class MainComponent final : public juce::Component,
                            private juce::AudioIODeviceCallback,
                            private juce::Timer
{
public:
    MainComponent()
    {
        titleLabel.setText ("Orchid Audio Proof", juce::dontSendNotification);
        titleLabel.setFont (juce::Font (juce::FontOptions (24.0f, juce::Font::bold)));
        titleLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (titleLabel);

        sampleLabel.setText ("102 BPM  |  F minor  |  16 bars",
                             juce::dontSendNotification);
        sampleLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (sampleLabel);

        catalogLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        catalogLabel.setMinimumHorizontalScale (0.5f);
        addAndMakeVisible (catalogLabel);

        motifLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        addAndMakeVisible (motifLabel);

        arrangementLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
        arrangementLabel.setMinimumHorizontalScale (0.75f);
        arrangementLabel.setText ("Click a cell to play its sample on each beat of that bar.",
                                  juce::dontSendNotification);
        addAndMakeVisible (arrangementLabel);

        arrangementModel.toggleClip (cleanTrack, 0);
        arrangementGrid = std::make_unique<ArrangementGridComponent> (arrangementModel);
        arrangementGrid->onCellClicked = [this] (int track, int bar)
        {
            setArrangementCell (track, bar, ! arrangementModel.hasClip (track, bar), true);
        };
        addAndMakeVisible (*arrangementGrid);
        initialiseTrackControls();

        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setMinimumHorizontalScale (0.75f);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (statusLabel);

        playButton.setButtonText ("Play");
        playButton.onClick = [this] { requestPlay(); };
        addAndMakeVisible (playButton);

        stopButton.setButtonText ("Stop");
        stopButton.onClick = [this] { requestStop(); };
        addAndMakeVisible (stopButton);

        commandServer = std::make_unique<CommandServer> ([this] (const juce::var& request)
        {
            auto reply = commandDispatcher.dispatch (request, [this] (const juce::var& command)
            {
                return applyCommand (command);
            });
            if (request["op"].toString() == "status")
            {
                const auto bar = currentPlayhead.load (std::memory_order_acquire);
                auto* status = reply.getDynamicObject();
                status->setProperty ("playing", bar >= 0);
                status->setProperty ("playhead_bar", bar + 1);
                status->setProperty ("samples_ready",
                    loadFinished.load (std::memory_order_acquire) && sampleBank != nullptr
                        ? sampleBank->readyCount() : 0);
            }
            return reply;
        });
        if (! commandServer->start())
        {
            commandServer.reset();
            commandServerUnavailable = true;
        }

        setSize (1100, 700);
        startTimerHz (20);
        refreshStatus();

        loadWorker = std::thread ([this]
        {
            loadOrchidAssets();
            deviceInitialisationError = audioDeviceManager.initialise (0, 2, nullptr, true);
            audioDeviceManager.addAudioCallback (this);
            audioSetupFinished.store (true, std::memory_order_release);
            loadFinished.store (true, std::memory_order_release);
        });
    }

    ~MainComponent() override
    {
        commandServer.reset();
        stopTimer();
        if (loadWorker.joinable())
            loadWorker.join();
        audioDeviceManager.removeAudioCallback (this);
        audioDeviceManager.closeAudioDevice();
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.fillAll (juce::Colour::fromRGB (24, 27, 33));

        auto area = getLocalBounds().toFloat().reduced (16.0f);
        graphics.setColour (juce::Colour::fromRGB (48, 54, 64));
        graphics.drawRoundedRectangle (area, 10.0f, 1.0f);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (28, 22);
        titleLabel.setBounds (area.removeFromTop (38));
        area.removeFromTop (4);
        sampleLabel.setBounds (area.removeFromTop (28));
        catalogLabel.setBounds (area.removeFromTop (25));
        motifLabel.setBounds (area.removeFromTop (25));
        area.removeFromTop (8);
        statusLabel.setBounds (area.removeFromTop (90));
        area.removeFromTop (8);

        auto buttons = area.removeFromTop (44);
        playButton.setBounds (buttons.removeFromLeft (130));
        buttons.removeFromLeft (12);
        stopButton.setBounds (buttons.removeFromLeft (130));
        area.removeFromTop (10);
        arrangementLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (6);
        arrangementGrid->setBounds (area.removeFromTop (298));
        area.removeFromTop (12);
        auto controls = area.removeFromTop (29);
        trackEffectsLabel.setBounds (controls.removeFromLeft (105));
        trackSelector.setBounds (controls.removeFromLeft (145));
        controls.removeFromLeft (10);
        cutButton.setBounds (controls.removeFromLeft (104));
        echoButton.setBounds (controls.removeFromLeft (84));
        reverbSelector.setBounds (controls.removeFromLeft (130));
        controls.removeFromLeft (12);
        sidechainSelector.setBounds (controls.removeFromLeft (220));
    }

private:
    void initialiseTrackControls()
    {
        trackEffectsLabel.setText ("Track effects", juce::dontSendNotification);
        cutButton.setButtonText ("Cut itself");
        echoButton.setButtonText ("Echo");
        reverbSelector.addItem ("Reverb off", 1);
        reverbSelector.addItem ("Room reverb", 2);
        reverbSelector.addItem ("Hall reverb", 3);
        sidechainSelector.addItem ("Sidechain off", 1);
        for (int track = 0; track < ArrangementModel::trackCount; ++track)
        {
            const auto index = static_cast<size_t> (track);
            const auto alias = juce::String (ArrangementModel::tracks[index].alias);
            trackSelector.addItem (alias, track + 1);
            sidechainSelector.addItem ("Duck on " + alias, track + 2);
            pendingSettings[index].store (trackSettings[index].packed(), std::memory_order_relaxed);
        }

        trackSelector.setSelectedId (cleanTrack + 1, juce::dontSendNotification);
        trackSelector.onChange = [this] { refreshTrackControls(); };
        cutButton.onClick = [this]
        {
            const auto track = selectedTrack();
            auto settings = trackSettings[track];
            settings.cutItself = cutButton.getToggleState();
            setTrackSettings (track, settings, true);
        };
        echoButton.onClick = [this]
        {
            const auto track = selectedTrack();
            auto settings = trackSettings[track];
            settings.echo = echoButton.getToggleState();
            setTrackSettings (track, settings, true);
        };
        reverbSelector.onChange = [this]
        {
            const auto track = selectedTrack();
            auto settings = trackSettings[track];
            settings.reverb = static_cast<ReverbMode> (reverbSelector.getSelectedId() - 1);
            setTrackSettings (track, settings, true);
        };
        sidechainSelector.onChange = [this]
        {
            const auto track = selectedTrack();
            auto settings = trackSettings[track];
            settings.sidechainSource = sidechainSelector.getSelectedId() - 2;
            setTrackSettings (track, settings, true);
        };
        addAndMakeVisible (trackEffectsLabel);
        addAndMakeVisible (trackSelector);
        addAndMakeVisible (cutButton);
        addAndMakeVisible (echoButton);
        addAndMakeVisible (reverbSelector);
        addAndMakeVisible (sidechainSelector);
        refreshTrackControls();
    }

    size_t selectedTrack() const noexcept
    {
        return static_cast<size_t> (trackSelector.getSelectedId() - 1);
    }

    void refreshTrackControls()
    {
        const auto selected = selectedTrack();
        const auto settings = trackSettings[selected];
        cutButton.setToggleState (settings.cutItself, juce::dontSendNotification);
        echoButton.setToggleState (settings.echo, juce::dontSendNotification);
        reverbSelector.setSelectedId (static_cast<int> (settings.reverb) + 1, juce::dontSendNotification);
        for (int track = 0; track < ArrangementModel::trackCount; ++track)
            sidechainSelector.setItemEnabled (track + 2, static_cast<size_t> (track) != selected);
        sidechainSelector.setSelectedId (settings.sidechainSource + 2, juce::dontSendNotification);
    }

    void setTrackSettings (size_t track, TrackSettings settings, bool local)
    {
        const auto packed = settings.packed();
        if (trackSettings[track].packed() == packed)
            return;

        trackSettings[track] = settings;
        pendingSettings[track].store (packed, std::memory_order_release);
        if (local)
            commandDispatcher.localEdit();
        if (track == selectedTrack())
            refreshTrackControls();
    }

    bool playbackActive() const noexcept
    {
        return playRequested.load (std::memory_order_acquire)
               || currentPlayhead.load (std::memory_order_acquire) >= 0;
    }

    juce::String requestPlay()
    {
        if (playbackActive())
            return {};

        if (! loadFinished.load (std::memory_order_acquire)
            || ! sampleReady.load (std::memory_order_acquire)
            || ! audioReady.load (std::memory_order_acquire)
            || audioError.load (std::memory_order_acquire))
            return "Playback needs a loaded Orchid sample and an active audio device.";

        pendingArrangement = arrangementModel;
        playRequested.store (true, std::memory_order_release);
        refreshStatus();
        return {};
    }

    void requestStop() noexcept
    {
        if (playbackActive())
            stopRequested.store (true, std::memory_order_release);
    }

    juce::String setArrangementCell (int track, int bar, bool enabled, bool local)
    {
        if (! ArrangementModel::isValidCell (track, bar))
            return "Invalid arrangement cell.";
        if (playbackActive())
            return "Stop playback to edit clips.";
        if (arrangementModel.hasClip (track, bar) == enabled)
            return {};

        arrangementModel.toggleClip (track, bar);
        if (local)
            commandDispatcher.localEdit();

        const auto alias = juce::String (ArrangementModel::tracks[static_cast<size_t> (track)].alias);
        auto result = alias + " - Bar " + juce::String (bar + 1)
                    + (enabled ? " - Clip on" : " - Clip off");
        if (loadFinished.load (std::memory_order_acquire) && sampleBank != nullptr
            && sampleBank->sampleForTrack (track) == nullptr)
            result += " - " + sampleBank->errorForTrack (track);

        wasPlaying = false;
        arrangementLabel.setText (result, juce::dontSendNotification);
        arrangementGrid->repaint();
        return {};
    }

    static int findTrack (const juce::String& id)
    {
        for (int track = 0; track < ArrangementModel::trackCount; ++track)
            if (id == ArrangementModel::tracks[static_cast<size_t> (track)].id)
                return track;
        return -1;
    }

    juce::String applyCommand (const juce::var& command)
    {
        const auto op = command["op"].toString();
        if (op == "play")
            return requestPlay();
        if (op == "stop")
        {
            requestStop();
            return {};
        }

        const bool enabled = static_cast<bool> (command["enabled"]);
        if (op == "set_sidechain")
        {
            const auto source = findTrack (command["source_id"].toString());
            const auto target = findTrack (command["target_id"].toString());
            if (source < 0 || target < 0 || source == target)
                return "Invalid sidechain route.";

            const auto index = static_cast<size_t> (target);
            auto settings = trackSettings[index];
            if (enabled || settings.sidechainSource == source)
                settings.sidechainSource = enabled ? source : -1;
            setTrackSettings (index, settings, false);
            return {};
        }

        const auto track = findTrack (command["track_id"].toString());
        if (track < 0)
            return "Unknown arrangement track.";
        if (op == "set_cell")
            return setArrangementCell (track, static_cast<int> (command["bar"]) - 1, enabled, false);

        const auto index = static_cast<size_t> (track);
        auto settings = trackSettings[index];
        if (op == "set_cut_itself")
            settings.cutItself = enabled;
        else if (op == "set_echo")
            settings.echo = enabled;
        else if (op == "set_reverb")
        {
            const auto mode = command["mode"].toString();
            settings.reverb = mode == "room" ? ReverbMode::room
                            : mode == "hall" ? ReverbMode::hall : ReverbMode::off;
        }
        else
            return "Unknown command.";

        setTrackSettings (index, settings, false);
        return {};
    }

    void loadOrchidAssets()
    {
        auto rootText = juce::SystemStats::getEnvironmentVariable (
            "ORCHID_SAMPLE_ROOT", {}).trim();
        if (rootText.isEmpty())
        {
            const auto fallbackRoot = juce::File::getSpecialLocation (
                juce::File::userHomeDirectory).getChildFile ("Music/Lick-Lubin/Orchid Sample Pack");
            if (! fallbackRoot.isDirectory())
            {
                sampleLoadStatus = "Sample unavailable: set ORCHID_SAMPLE_ROOT or place the pack at "
                                 + fallbackRoot.getFullPathName() + ".";
                return;
            }
            rootText = fallbackRoot.getFullPathName();
        }

        const juce::File sampleRoot (rootText);
        if (! sampleRoot.isDirectory())
        {
            sampleLoadStatus = "Sample unavailable: ORCHID_SAMPLE_ROOT is not a folder: "
                             + sampleRoot.getFullPathName();
            return;
        }

        juce::String manifestError;
        const auto manifest = parseEmbeddedManifest (manifestError);
        if (manifest.isVoid())
        {
            sampleLoadStatus = manifestError;
            return;
        }

        sampleBank = std::make_unique<SampleBank> (manifest, sampleRoot);
        juce::StringArray unavailableAliases;
        for (int track = 0; track < ArrangementModel::trackCount; ++track)
            if (sampleBank->sampleForTrack (track) == nullptr)
                unavailableAliases.add (ArrangementModel::tracks[static_cast<size_t> (track)].alias);

        const auto loadedCount = sampleBank->readyCount();
        catalogStatus = "Orchid samples: " + juce::String (loadedCount) + " of "
                      + juce::String (ArrangementModel::trackCount) + " loaded.";
        if (! unavailableAliases.isEmpty())
            catalogStatus += " Unavailable: " + unavailableAliases.joinIntoString (", ") + ".";

        if (loadedCount == 0)
            sampleLoadStatus = "No Orchid samples were loaded. " + sampleBank->errorForTrack (cleanTrack);
        sampleReady.store (loadedCount > 0, std::memory_order_release);

        Motif importedMotif;
        juce::String motifError;
        if (importMotif (manifest, sampleRoot, importedMotif, motifError))
        {
            motif = std::move (importedMotif);
            const auto beatCount = static_cast<double> (motif.endTick) / motif.ticksPerQuarter;
            motifStatus = motif.alias + ": " + juce::String (static_cast<int> (motif.notes.size()))
                        + " notes · " + juce::String (beatCount, 0) + " beats · "
                        + juce::String (beatCount / 4.0, 0) + " bars · "
                        + juce::String (motif.tempoBpm, 0) + " BPM · " + motif.key
                        + " (imported as editable motif; not arranged).";
        }
        else
            motifStatus = motifError;
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const auto rate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
        deviceSampleRate = rate;
        transport.stop();
        voices = {};
        appliedSettings.fill (-1);
        for (auto& effect : trackEffects)
        {
            effect.prepare (rate);
            effect.setTempo (ArrangementTransport::defaultTempoBpm);
        }
        currentPlayhead.store (-1, std::memory_order_release);
        stopRequested.store (false, std::memory_order_release);
        playRequested.store (false, std::memory_order_release);
        audioError.store (false, std::memory_order_release);
        audioReady.store (rate > 0.0, std::memory_order_release);
    }

    void audioDeviceStopped() override
    {
        audioReady.store (false, std::memory_order_release);
        transport.stop();
        currentPlayhead.store (-1, std::memory_order_release);
        playRequested.store (false, std::memory_order_release);
        stopRequested.store (true, std::memory_order_release);
    }

    void audioDeviceError (const juce::String&) override
    {
        audioError.store (true, std::memory_order_release);
        audioReady.store (false, std::memory_order_release);
        transport.stop();
        currentPlayhead.store (-1, std::memory_order_release);
        playRequested.store (false, std::memory_order_release);
        stopRequested.store (true, std::memory_order_release);
    }

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        const juce::ScopedNoDenormals noDenormals;
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] != nullptr)
                std::fill_n (outputChannelData[channel], numSamples, 0.0f);
        }

        const bool canRender = audioReady.load (std::memory_order_acquire)
                            && ! audioError.load (std::memory_order_acquire)
                            && sampleReady.load (std::memory_order_acquire)
                            && deviceSampleRate > 0.0;
        const bool playPending = playRequested.load (std::memory_order_acquire);
        if (stopRequested.exchange (false, std::memory_order_acq_rel) || ! canRender)
        {
            transport.stop();
            releaseAllVoices();
        }
        else if (playPending)
        {
            playbackArrangement = pendingArrangement;
            transport.start (deviceSampleRate);
        }

        if (! canRender)
        {
            currentPlayhead.store (-1, std::memory_order_release);
            if (playPending)
                playRequested.store (false, std::memory_order_release);
            return;
        }

        applyPendingSettings();
        for (int frame = 0; frame < numSamples; ++frame)
        {
            if (transport.isPlaying())
            {
                const auto event = transport.advanceFrame (playbackArrangement);
                if (event.trackMask != 0)
                    triggerTracks (event.trackMask);
                if (! transport.isPlaying())
                    releaseAllVoices();
            }

            float mixedLeft = 0.0f;
            float mixedRight = 0.0f;
            for (size_t track = 0; track < voices.size(); ++track)
            {
                float trackLeft = 0.0f;
                float trackRight = 0.0f;
                for (auto& voice : voices[track])
                {
                    if (voice.sample == nullptr)
                        continue;

                    const auto& buffer = voice.sample->buffer;
                    const auto sampleLength = buffer.getNumSamples();
                    const auto sourceIndex = static_cast<int> (voice.sourcePosition);
                    const auto nextIndex = juce::jmin (sourceIndex + 1, sampleLength - 1);
                    const auto fraction = static_cast<float> (voice.sourcePosition - sourceIndex);
                    const auto* leftData = buffer.getReadPointer (0);
                    const auto* rightData = buffer.getNumChannels() > 1 ? buffer.getReadPointer (1) : leftData;
                    const auto left = leftData[sourceIndex]
                                    + (leftData[nextIndex] - leftData[sourceIndex]) * fraction;
                    const auto right = rightData[sourceIndex]
                                     + (rightData[nextIndex] - rightData[sourceIndex]) * fraction;

                    const auto gain = voiceGain * voice.releaseRamp.nextGain();
                    trackLeft += left * gain;
                    trackRight += right * gain;
                    voice.sourcePosition += voice.sourceStep;
                    if (voice.sourcePosition >= sampleLength || voice.releaseRamp.finished())
                        voice.sample = nullptr;
                }
                trackEffects[track].process (trackLeft, trackRight);
                mixedLeft += trackLeft;
                mixedRight += trackRight;
            }

            if (numOutputChannels == 1 && outputChannelData[0] != nullptr)
                outputChannelData[0][frame] = juce::jlimit (-1.0f, 1.0f,
                                                           (mixedLeft + mixedRight) * 0.5f);
            else if (numOutputChannels > 1)
            {
                if (outputChannelData[0] != nullptr)
                    outputChannelData[0][frame] = juce::jlimit (-1.0f, 1.0f, mixedLeft);
                if (outputChannelData[1] != nullptr)
                    outputChannelData[1][frame] = juce::jlimit (-1.0f, 1.0f, mixedRight);
            }
        }

        currentPlayhead.store (transport.playheadBar(), std::memory_order_release);
        if (playPending)
            playRequested.store (false, std::memory_order_release);
    }

    void applyPendingSettings() noexcept
    {
        for (size_t track = 0; track < pendingSettings.size(); ++track)
        {
            const auto value = pendingSettings[track].load (std::memory_order_acquire);
            if (appliedSettings[track] == value)
                continue;

            appliedSettings[track] = value;
            const auto settings = TrackSettings::unpack (value);
            audioTrackSettings[track] = settings;
            auto& effect = trackEffects[track];
            effect.setDelayBeats (settings.echo ? 0.5 : 0.0);
            effect.setReverb (settings.reverb);
            effect.setSidechainDepth (settings.sidechainSource >= 0 ? sidechainDepth : 0.0f);
        }
    }

    void triggerTracks (ArrangementTransport::TrackMask scheduled) noexcept
    {
        ArrangementTransport::TrackMask started = 0;
        for (int track = 0; track < ArrangementModel::trackCount; ++track)
            if ((scheduled & (1u << track)) != 0 && startTrackVoice (track))
                started |= static_cast<ArrangementTransport::TrackMask> (1u << track);

        for (size_t target = 0; target < audioTrackSettings.size(); ++target)
        {
            const auto source = audioTrackSettings[target].sidechainSource;
            if (source >= 0 && (started & (1u << source)) != 0)
                trackEffects[target].triggerSidechain();
        }
    }

    bool startTrackVoice (int track) noexcept
    {
        const auto* sample = sampleBank->sampleForTrack (track);
        if (sample == nullptr)
            return false;

        const auto index = static_cast<size_t> (track);
        auto& trackVoices = voices[index];
        auto available = std::find_if (trackVoices.begin(), trackVoices.end(),
                                      [] (const Voice& voice) { return voice.sample == nullptr; });
        if (audioTrackSettings[index].cutItself)
        {
            const auto frames = releaseFrames (retriggerReleaseSeconds);
            for (auto& voice : trackVoices)
                voice.release (frames);
        }
        if (available == trackVoices.end())
        {
            voiceLimitReached.store (true, std::memory_order_release);
            return false;
        }

        *available = { sample, 0.0, sample->sourceSampleRate / deviceSampleRate, {} };
        return true;
    }

    int releaseFrames (double seconds) const noexcept
    {
        return juce::jmax (1, static_cast<int> (std::round (deviceSampleRate * seconds)));
    }

    void releaseAllVoices() noexcept
    {
        const auto frames = releaseFrames (stopReleaseSeconds);
        for (auto& trackVoices : voices)
            for (auto& voice : trackVoices)
                voice.release (frames);
    }

    void timerCallback() override
    {
        refreshStatus();
    }

    void refreshStatus()
    {
        const bool sampleIsReady = sampleReady.load (std::memory_order_acquire);
        const bool deviceIsReady = audioReady.load (std::memory_order_acquire);
        const bool deviceFailed = audioError.load (std::memory_order_acquire);
        const bool playPending = playRequested.load (std::memory_order_acquire);
        const int bar = currentPlayhead.load (std::memory_order_acquire);
        const bool isCurrentlyPlaying = bar >= 0;
        const bool overflowed = voiceLimitReached.exchange (false, std::memory_order_acq_rel);
        const bool loadingFinished = loadFinished.load (std::memory_order_acquire);
        const bool audioSetupDone = audioSetupFinished.load (std::memory_order_acquire);

        playButton.setEnabled (loadingFinished && sampleIsReady && deviceIsReady && ! deviceFailed
                               && ! isCurrentlyPlaying && ! playPending);
        stopButton.setEnabled (isCurrentlyPlaying || playPending);
        arrangementGrid->setPlayheadBar (bar);
        if (isCurrentlyPlaying || playPending)
            arrangementLabel.setText ("Stop playback to edit clips.", juce::dontSendNotification);
        else if (wasPlaying)
            arrangementLabel.setText ("Click a cell to play its sample on each beat of that bar.",
                                      juce::dontSendNotification);
        wasPlaying = isCurrentlyPlaying || playPending;

        juce::String status = loadingFinished ? sampleLoadStatus : "Loading Orchid samples and New Flow MIDI...";
        if (! loadingFinished)
        {
            catalogLabel.setText ("Loading curated sample files...", juce::dontSendNotification);
            motifLabel.setText ("Loading New Flow motif...", juce::dontSendNotification);
            statusLabel.setText (status, juce::dontSendNotification);
            return;
        }
        if (! sampleIsReady)
            status = sampleLoadStatus;
        else if (isCurrentlyPlaying)
            status = "Playing bar " + juce::String (bar + 1) + " of " + juce::String (ArrangementModel::barCount) + ".";
        else
            status = "Press Play to hear the arrangement.";

        if (deviceFailed)
            status += " Audio device error; check the selected output device.";
        else if (! audioSetupDone || ! deviceIsReady)
        {
            if (audioSetupDone && deviceInitialisationError.isNotEmpty())
                status += " Audio device unavailable: " + deviceInitialisationError;
            else
                status += " Audio device unavailable; connect an output device and relaunch.";
        }
        else if (auto* device = audioDeviceManager.getCurrentAudioDevice())
        {
            status += " Audio: " + device->getName() + ".";
        }

        if (overflowed)
            status += " Voice limit reached; one beat was skipped.";
        if (commandServerUnavailable)
            status += " Local voice control is unavailable.";

        statusLabel.setText (status, juce::dontSendNotification);
        catalogLabel.setText (catalogStatus, juce::dontSendNotification);
        motifLabel.setText (motifStatus, juce::dontSendNotification);
    }

    juce::AudioDeviceManager audioDeviceManager;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::Label titleLabel;
    juce::Label sampleLabel;
    juce::Label catalogLabel;
    juce::Label motifLabel;
    juce::Label statusLabel;
    juce::Label arrangementLabel;
    juce::Label trackEffectsLabel;
    juce::ComboBox trackSelector;
    juce::ToggleButton cutButton;
    juce::ToggleButton echoButton;
    juce::ComboBox reverbSelector;
    juce::ComboBox sidechainSelector;

    ArrangementModel arrangementModel;
    ArrangementModel pendingArrangement; // Published by the UI using playRequested.
    ArrangementModel playbackArrangement;
    ArrangementTransport transport;
    bool wasPlaying = false;
    CommandDispatcher commandDispatcher;
    std::unique_ptr<CommandServer> commandServer;
    bool commandServerUnavailable = false;
    std::unique_ptr<ArrangementGridComponent> arrangementGrid;

    std::unique_ptr<const SampleBank> sampleBank;
    juce::String sampleLoadStatus;
    juce::String catalogStatus { "Orchid catalog is not loaded." };
    juce::String motifStatus { "New Flow motif is not imported." };
    juce::String deviceInitialisationError;
    Motif motif;

    std::array<std::array<Voice, voicesPerTrack>, ArrangementModel::trackCount> voices {};
    std::array<TrackEffects, ArrangementModel::trackCount> trackEffects;
    std::array<TrackSettings, ArrangementModel::trackCount> trackSettings;
    std::array<std::atomic<int>, ArrangementModel::trackCount> pendingSettings;
    std::array<int, ArrangementModel::trackCount> appliedSettings {};
    std::array<TrackSettings, ArrangementModel::trackCount> audioTrackSettings;
    double deviceSampleRate = 0.0;

    std::atomic<bool> playRequested { false };
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> sampleReady { false };
    std::atomic<bool> audioReady { false };
    std::atomic<bool> audioError { false };
    std::atomic<int> currentPlayhead { -1 };
    std::atomic<bool> voiceLimitReached { false };
    std::atomic<bool> audioSetupFinished { false };
    std::atomic<bool> loadFinished { false };
    std::thread loadWorker;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow()
        : DocumentWindow ("Orchid Audio Proof",
                          juce::Colour::fromRGB (24, 27, 33),
                          juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (true, true);
        setContentOwned (new MainComponent(), true);
        centreWithSize (1100, 700);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainWindow)
};

class OrchidAudioProofApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return "Orchid Audio Proof"; }
    const juce::String getApplicationVersion() override { return "1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (OrchidAudioProofApplication)
