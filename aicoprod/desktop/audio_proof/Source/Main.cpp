#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <BinaryData.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <memory>

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

juce::var findNamedSample (const juce::var& collection)
{
    if (auto* object = collection.getDynamicObject())
        return object->getProperty (juce::Identifier (sampleId));

    if (auto* entries = collection.getArray())
    {
        for (const auto& entry : *entries)
        {
            if (auto* object = entry.getDynamicObject())
            {
                auto id = object->getProperty (juce::Identifier ("id")).toString();
                if (id.isEmpty())
                    id = object->getProperty (juce::Identifier ("sample_id")).toString();

                if (id == sampleId)
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

juce::String getSamplePathFromManifest (juce::String& error)
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

    juce::var entry;

    if (auto* object = manifest.getDynamicObject())
    {
        entry = findNamedSample (object->getProperty (juce::Identifier ("samples")));
        if (! entry.isVoid())
        {
            const auto path = readSamplePath (entry);
            if (path.isNotEmpty())
                return path;
        }

        entry = object->getProperty (juce::Identifier (sampleId));
    }
    else
    {
        entry = findNamedSample (manifest);
    }

    if (entry.isVoid())
    {
        error = "Manifest error: entry 'clean_kick' is missing.";
        return {};
    }

    const auto path = readSamplePath (entry);
    if (path.isEmpty())
        error = "Manifest error: entry 'clean_kick' has no relative path.";

    return path;
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

        loadCleanKickFromManifest();

        deviceInitialisationError = audioDeviceManager.initialise (0, 2, nullptr, true);
        audioDeviceManager.addAudioCallback (this);

        setSize (520, 270);
        startTimerHz (20);
        refreshStatus();
    }

    ~MainComponent() override
    {
        stopTimer();
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
        area.removeFromTop (12);
        statusLabel.setBounds (area.removeFromTop (62));
        area.removeFromTop (12);

        auto buttons = area.removeFromTop (44);
        playButton.setBounds (buttons.removeFromLeft (130));
        buttons.removeFromLeft (12);
        stopButton.setBounds (buttons.removeFromLeft (130));
    }

private:
    void loadCleanKickFromManifest()
    {
        const auto rootText = juce::SystemStats::getEnvironmentVariable (
            "ORCHID_SAMPLE_ROOT", {}).trim();
        if (rootText.isEmpty())
        {
            sampleLoadStatus = "Sample unavailable: set ORCHID_SAMPLE_ROOT to the Orchid pack folder.";
            return;
        }

        const juce::File sampleRoot (rootText);
        if (! sampleRoot.isDirectory())
        {
            sampleLoadStatus = "Sample unavailable: ORCHID_SAMPLE_ROOT is not a folder: "
                             + sampleRoot.getFullPathName();
            return;
        }

        juce::String manifestError;
        const auto manifestPath = getSamplePathFromManifest (manifestError).replaceCharacter ('\\', '/');
        if (manifestPath.isEmpty())
        {
            sampleLoadStatus = manifestError;
            return;
        }

        if (manifestPath != expectedSamplePath)
        {
            sampleLoadStatus = "Manifest error: clean_kick must point to '"
                             + expectedSamplePath + "'.";
            return;
        }

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

        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
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

        playButton.setEnabled (sampleIsReady && deviceIsReady && ! deviceFailed);
        stopButton.setEnabled (isCurrentlyPlaying || ! deviceIsReady);

        juce::String status = sampleLoadStatus;
        if (! sampleIsReady)
            status = sampleLoadStatus;
        else if (isCurrentlyPlaying)
            status = "Playing clean_kick at 102 BPM.";
        else
            status = "Sample ready. Press Play to begin; hits repeat every quarter note.";

        if (deviceFailed)
            status += " Audio device error; check the selected output device.";
        else if (! deviceIsReady)
        {
            if (deviceInitialisationError.isNotEmpty())
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
    }

    juce::AudioDeviceManager audioDeviceManager;
    juce::TextButton playButton;
    juce::TextButton stopButton;
    juce::Label titleLabel;
    juce::Label sampleLabel;
    juce::Label statusLabel;

    juce::AudioBuffer<float> sampleData;
    int sampleLength = 0;
    double sampleRate = 0.0;
    juce::String sampleLoadStatus { "Loading clean_kick..." };
    juce::String deviceInitialisationError;

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
        centreWithSize (520, 270);
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
