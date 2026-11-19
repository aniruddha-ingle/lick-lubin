#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <BinaryData.h>
#include "ArrangementModel.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

namespace
{
constexpr double tempoBpm = 102.0;
constexpr double stopReleaseSeconds = 0.012;
constexpr float voiceGain = 0.25f;
constexpr int maxVoices = 8;
static_assert (std::atomic<bool>::is_always_lock_free,
               "Audio/UI state flags must use lock-free atomics.");

const juce::String sampleId { "clean_kick" };
const juce::String expectedSamplePath {
    "Drum One Shots/Cymatics - Orchid Kick - Clean (F).wav"
};

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
    bool active = false;
    bool releasing = false;
    double sourcePosition = 0.0;
    float releaseStartGain = 1.0f;
    int releaseSamplesRemaining = 0;
    int releaseSamplesTotal = 0;
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

    std::function<void (int, int, bool)> onToggle;

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
            graphics.setColour (juce::Colour::fromRGB (38, 43, 52));
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
                graphics.setColour (juce::Colour::fromRGB (70, 78, 91));
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
        if (! model.toggleClip (track, bar))
            return;

        repaint();
        if (onToggle != nullptr)
            onToggle (track, bar, model.hasClip (track, bar));
    }

private:
    licklubin::ArrangementModel& model;
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

        sampleLabel.setText ("clean_kick  |  102 BPM  |  quarter-note trigger",
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
        arrangementLabel.setText ("Arrangement state only: grid edits do not affect audio-proof playback.",
                                  juce::dontSendNotification);
        addAndMakeVisible (arrangementLabel);

        arrangementGrid = std::make_unique<ArrangementGridComponent> (arrangementModel);
        arrangementGrid->onToggle = [this] (int track, int bar, bool enabled)
        {
            const auto alias = juce::String (
                licklubin::ArrangementModel::tracks[static_cast<size_t> (track)].alias);
            arrangementLabel.setText ("Arrangement state only: " + alias + " bar "
                + juce::String (bar + 1) + (enabled ? " clip on" : " clip off")
                + "; audio-proof playback unchanged.", juce::dontSendNotification);
        };
        addAndMakeVisible (*arrangementGrid);

        statusLabel.setJustificationType (juce::Justification::centredLeft);
        statusLabel.setMinimumHorizontalScale (0.75f);
        statusLabel.setColour (juce::Label::textColourId, juce::Colours::white);
        addAndMakeVisible (statusLabel);

        playButton.setButtonText ("Play");
        playButton.onClick = [this]
        {
            if (sampleReady.load (std::memory_order_acquire)
                && audioReady.load (std::memory_order_acquire)
                && ! audioError.load (std::memory_order_acquire))
                playRequested.store (true, std::memory_order_release);
        };
        addAndMakeVisible (playButton);

        stopButton.setButtonText ("Stop");
        stopButton.onClick = [this]
        {
            stopRequested.store (true, std::memory_order_release);
        };
        addAndMakeVisible (stopButton);

        setSize (1100, 700);
        startTimerHz (20);
        refreshStatus();

        loadWorker = std::thread ([this]
        {
            loadCleanKickFromManifest();
            deviceInitialisationError = audioDeviceManager.initialise (0, 2, nullptr, true);
            audioDeviceManager.addAudioCallback (this);
            audioSetupFinished.store (true, std::memory_order_release);
            loadFinished.store (true, std::memory_order_release);
        });
    }

    ~MainComponent() override
    {
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
    }

private:
    void loadCleanKickFromManifest()
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

        const auto* manifestObject = manifest.getDynamicObject();
        auto* samples = manifestObject != nullptr
                      ? manifestObject->getProperty (juce::Identifier ("samples")).getArray() : nullptr;
        if (samples == nullptr)
        {
            sampleLoadStatus = "Manifest error: 'samples' must be an array.";
            return;
        }

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        int availableSamples = 0;
        int uniqueSamples = 0;
        juce::StringArray unavailableAliases;
        juce::String manifestValidationError;
        juce::StringArray seenIds;
        juce::StringArray seenAliases;
        for (int sampleIndex = 0; sampleIndex < samples->size(); ++sampleIndex)
        {
            const auto& entry = samples->getReference (sampleIndex);
            const auto* sample = entry.getDynamicObject();
            if (sample == nullptr)
            {
                if (manifestValidationError.isEmpty())
                    manifestValidationError = "Manifest error: sample entry "
                                            + juce::String (sampleIndex + 1) + " must be an object.";
                continue;
            }
            const auto id = sample->getProperty (juce::Identifier ("id")).toString();
            const auto alias = sample->getProperty (juce::Identifier ("alias")).toString();
            const auto relativePath = readSamplePath (entry).replaceCharacter ('\\', '/');
            if (id.isEmpty() || alias.isEmpty() || ! isSafeRelativePath (relativePath)
                || seenIds.contains (id) || seenAliases.contains (alias))
            {
                if (manifestValidationError.isEmpty())
                    manifestValidationError = "Manifest error: sample entry "
                        + juce::String (sampleIndex + 1)
                        + " has a missing/unsafe field or duplicate ID/alias.";
                continue;
            }
            seenIds.add (id);
            seenAliases.add (alias);
            ++uniqueSamples;
            const auto candidate = resolveLocalFile (sampleRoot, relativePath);
            if (! candidate.isAChildOf (sampleRoot) || ! candidate.existsAsFile())
                unavailableAliases.add (alias + " (missing)");
            else
            {
                std::unique_ptr<juce::AudioFormatReader> candidateReader (
                    formats.createReaderFor (candidate));
                if (candidateReader == nullptr)
                    unavailableAliases.add (alias + " (unsupported format)");
                else
                    ++availableSamples;
            }
        }
        if (manifestValidationError.isEmpty() && uniqueSamples != 10)
            manifestValidationError = "Manifest error: expected 10 curated Orchid sample entries; found "
                                    + juce::String (uniqueSamples) + ".";
        catalogStatus = manifestValidationError.isNotEmpty() ? manifestValidationError
            : "Orchid catalog: " + juce::String (uniqueSamples) + " entries, "
                + juce::String (availableSamples) + " files available.";
        if (! unavailableAliases.isEmpty())
            catalogStatus += " Unavailable: " + unavailableAliases.joinIntoString (", ") + ".";

        const auto cleanEntry = findNamedEntry (manifestObject->getProperty (
            juce::Identifier ("samples")), sampleId);
        const auto manifestPath = readSamplePath (cleanEntry).replaceCharacter ('\\', '/');
        if (manifestPath.isEmpty())
        {
            sampleLoadStatus = "Manifest error: entry 'clean_kick' is missing or has no relative path.";
            return;
        }

        if (manifestPath != expectedSamplePath)
        {
            sampleLoadStatus = "Manifest error: clean_kick must point to '"
                             + expectedSamplePath + "'.";
            return;
        }

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

        const auto sampleFile = sampleRoot.getChildFile (manifestPath);
        if (! sampleFile.isAChildOf (sampleRoot))
        {
            sampleLoadStatus = "Manifest error: clean_kick path must stay inside ORCHID_SAMPLE_ROOT.";
            return;
        }

        if (! sampleFile.existsAsFile())
        {
            sampleLoadStatus = "WAV unavailable: " + sampleFile.getFullPathName();
            return;
        }

        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (sampleFile));
        if (reader == nullptr)
        {
            sampleLoadStatus = "WAV error: JUCE could not read " + sampleFile.getFileName();
            return;
        }

        if (reader->numChannels < 1 || reader->numChannels > 2
            || reader->lengthInSamples <= 0
            || reader->lengthInSamples > std::numeric_limits<int>::max()
            || reader->sampleRate <= 0.0
            || static_cast<double> (reader->lengthInSamples) / reader->sampleRate > 120.0)
        {
            sampleLoadStatus = "WAV error: clean_kick must be a non-empty mono/stereo WAV under 120 seconds.";
            return;
        }

        sampleLength = static_cast<int> (reader->lengthInSamples);
        sampleRate = reader->sampleRate;
        sampleData.setSize (static_cast<int> (reader->numChannels), sampleLength,
                            false, true, false);

        const bool readOk = reader->read (&sampleData, 0, sampleLength, 0,
                                          true, reader->numChannels > 1);
        if (! readOk)
        {
            sampleData.setSize (0, 0);
            sampleLength = 0;
            sampleLoadStatus = "WAV error: failed to load the complete clean_kick sample.";
            return;
        }

        sampleReady.store (true, std::memory_order_release);
        sampleLoadStatus = "Loaded clean_kick fully into memory from "
                         + sampleFile.getFullPathName();
    }

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const auto rate = device != nullptr ? device->getCurrentSampleRate() : 0.0;
        deviceSampleRate = rate;
        samplesPerBeat = rate > 0.0 ? rate * 60.0 / tempoBpm : 0.0;
        samplesUntilNextBeat = 0.0;
        transportRunning = false;

        for (auto& voice : voices)
            voice = {};

        stopRequested.store (false, std::memory_order_release);
        playRequested.store (false, std::memory_order_release);
        audioError.store (false, std::memory_order_release);
        audioReady.store (rate > 0.0, std::memory_order_release);
        isPlaying.store (false, std::memory_order_release);
    }

    void audioDeviceStopped() override
    {
        transportRunning = false;
        playRequested.store (false, std::memory_order_release);
        stopRequested.store (true, std::memory_order_release);
        isPlaying.store (false, std::memory_order_release);
        audioReady.store (false, std::memory_order_release);
    }

    void audioDeviceError (const juce::String&) override
    {
        audioError.store (true, std::memory_order_release);
        audioReady.store (false, std::memory_order_release);
        isPlaying.store (false, std::memory_order_release);
        playRequested.store (false, std::memory_order_release);
        stopRequested.store (true, std::memory_order_release);
    }

    void audioDeviceIOCallbackWithContext (const float* const*, int,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int channel = 0; channel < numOutputChannels; ++channel)
        {
            if (outputChannelData[channel] != nullptr)
                std::fill_n (outputChannelData[channel], numSamples, 0.0f);
        }

        const bool stopNow = stopRequested.exchange (false, std::memory_order_acq_rel);
        if (stopNow)
        {
            playRequested.store (false, std::memory_order_release);
            transportRunning = false;
            samplesUntilNextBeat = 0.0;
            releaseAllVoices();
            isPlaying.store (false, std::memory_order_release);
        }
        else if (playRequested.exchange (false, std::memory_order_acq_rel)
                 && sampleReady.load (std::memory_order_acquire)
                 && deviceSampleRate > 0.0)
        {
            transportRunning = true;
            samplesUntilNextBeat = 0.0;
            isPlaying.store (true, std::memory_order_release);
        }

        if (! audioReady.load (std::memory_order_acquire)
            || audioError.load (std::memory_order_acquire)
            || ! sampleReady.load (std::memory_order_acquire)
            || deviceSampleRate <= 0.0
            || sampleLength <= 0
            || samplesPerBeat <= 0.0)
            return;

        const auto* const leftData = sampleData.getReadPointer (0);
        const auto* const rightData = sampleData.getNumChannels() > 1
                                      ? sampleData.getReadPointer (1)
                                      : leftData;
        const double sourceStep = sampleRate / deviceSampleRate;
        const int lastSample = sampleLength - 1;
        for (int frame = 0; frame < numSamples; ++frame)
        {
            if (transportRunning && samplesUntilNextBeat <= 0.0)
            {
                startVoice();
                samplesUntilNextBeat += samplesPerBeat;
            }

            float mixedLeft = 0.0f;
            float mixedRight = 0.0f;

            for (auto& voice : voices)
            {
                if (! voice.active)
                    continue;

                const int sourceIndex = static_cast<int> (voice.sourcePosition);
                if (sourceIndex >= sampleLength)
                {
                    voice.active = false;
                    continue;
                }

                const int nextIndex = juce::jmin (sourceIndex + 1, lastSample);
                const float fraction = static_cast<float> (voice.sourcePosition - sourceIndex);
                const float sourceLeft = leftData[sourceIndex]
                                       + (leftData[nextIndex] - leftData[sourceIndex]) * fraction;
                const float sourceRight = rightData[sourceIndex]
                                        + (rightData[nextIndex] - rightData[sourceIndex]) * fraction;

                float gain = voiceGain;
                if (voice.releasing)
                {
                    const int denominator = juce::jmax (1, voice.releaseSamplesTotal - 1);
                    const int numerator = juce::jmax (0, voice.releaseSamplesRemaining - 1);
                    const float releaseProgress = static_cast<float> (numerator)
                                                / static_cast<float> (denominator);
                    gain *= voice.releaseStartGain * releaseProgress;
                }

                mixedLeft += sourceLeft * gain;
                mixedRight += sourceRight * gain;
                voice.sourcePosition += sourceStep;

                if (voice.releasing)
                {
                    --voice.releaseSamplesRemaining;
                    if (voice.releaseSamplesRemaining <= 0)
                        voice.active = false;
                }
                else if (voice.sourcePosition >= sampleLength)
                {
                    voice.active = false;
                }
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

            if (transportRunning)
                samplesUntilNextBeat -= 1.0;
        }
    }

    void startVoice() noexcept
    {
        for (auto& voice : voices)
        {
            if (! voice.active)
            {
                voice.active = true;
                voice.releasing = false;
                voice.sourcePosition = 0.0;
                voice.releaseStartGain = 1.0f;
                voice.releaseSamplesRemaining = 0;
                voice.releaseSamplesTotal = 0;
                return;
            }
        }

        voiceLimitReached.store (true, std::memory_order_release);
    }

    void releaseAllVoices() noexcept
    {
        const int releaseLength = deviceSampleRate > 0.0
                                ? juce::jmax (1, static_cast<int> (
                                      std::round (deviceSampleRate * stopReleaseSeconds)))
                                : 1;

        for (auto& voice : voices)
        {
            if (! voice.active)
                continue;

            if (! voice.releasing)
            {
                voice.releasing = true;
                voice.releaseStartGain = 1.0f;
                voice.releaseSamplesTotal = releaseLength;
                voice.releaseSamplesRemaining = releaseLength;
            }
        }
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
        const bool isCurrentlyPlaying = isPlaying.load (std::memory_order_acquire);
        const bool overflowed = voiceLimitReached.exchange (false, std::memory_order_acq_rel);
        const bool loadingFinished = loadFinished.load (std::memory_order_acquire);
        const bool audioSetupDone = audioSetupFinished.load (std::memory_order_acquire);

        playButton.setEnabled (loadingFinished && sampleIsReady && deviceIsReady && ! deviceFailed);
        stopButton.setEnabled (isCurrentlyPlaying || ! deviceIsReady);

        juce::String status = loadingFinished ? sampleLoadStatus : "Loading Orchid catalog, Clean kick, and New Flow MIDI...";
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
            status = "Playing clean_kick at 102 BPM.";
        else
            status = "Sample ready. Press Play to begin; hits repeat every quarter note.";

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

    licklubin::ArrangementModel arrangementModel;
    std::unique_ptr<ArrangementGridComponent> arrangementGrid;

    juce::AudioBuffer<float> sampleData;
    int sampleLength = 0;
    double sampleRate = 0.0;
    juce::String sampleLoadStatus { "Loading clean_kick..." };
    juce::String catalogStatus { "Orchid catalog is not loaded." };
    juce::String motifStatus { "New Flow motif is not imported." };
    juce::String deviceInitialisationError;
    Motif motif;

    std::array<Voice, maxVoices> voices {};
    double deviceSampleRate = 0.0;
    double samplesPerBeat = 0.0;
    double samplesUntilNextBeat = 0.0;
    bool transportRunning = false;

    std::atomic<bool> playRequested { false };
    std::atomic<bool> stopRequested { false };
    std::atomic<bool> sampleReady { false };
    std::atomic<bool> audioReady { false };
    std::atomic<bool> audioError { false };
    std::atomic<bool> isPlaying { false };
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
