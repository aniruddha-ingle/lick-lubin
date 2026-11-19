#include "../Source/SampleBank.h"

#include <BinaryData.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace
{
using licklubin::ArrangementModel;
using licklubin::SampleBank;

constexpr int fixtureFrames = 32;
constexpr int cleanTrack = 2;
constexpr int missingTrack = 3;
constexpr int corruptTrack = 4;

void require (bool condition, const char* message)
{
    if (! condition)
        throw std::runtime_error (message);
}

struct FixtureRoot
{
    ~FixtureRoot() { folder.deleteRecursively(); }

    const juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("orchid-sample-bank", {}, false);
};

juce::File fileForTrack (const juce::File& root, const juce::Array<juce::var>& entries, int track)
{
    const auto id = ArrangementModel::tracks[static_cast<std::size_t> (track)].id;
    const auto* entry = std::find_if (entries.begin(), entries.end(), [id] (const juce::var& value)
    {
        return value.getProperty ("id", {}).toString() == id;
    });
    require (entry != entries.end(), "expected a curated manifest entry for the track");
    return root.getChildFile (entry->getProperty ("relative_path", {}).toString());
}

void writeWavFixture (const juce::File& file, int channels, float amplitude)
{
    require (file.getParentDirectory().createDirectory().wasOk(), "could not create fixture folder");
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    require (stream != nullptr, "could not open fixture file");

    juce::WavAudioFormat wav;
    const auto options = juce::AudioFormatWriterOptions().withSampleRate (44100.0)
        .withNumChannels (channels).withBitsPerSample (16);
    const auto writer = wav.createWriterFor (stream, options);
    require (writer != nullptr, "could not create WAV fixture writer");

    juce::AudioBuffer<float> buffer (channels, fixtureFrames);
    buffer.clear();
    buffer.setSample (0, 0, amplitude);
    if (channels == 2)
        buffer.setSample (1, 0, -amplitude);
    require (writer->writeFromAudioSampleBuffer (buffer, 0, fixtureFrames), "could not write WAV fixture");
}

void loadsByTrackIdAndIsolatesUnavailableFiles()
{
    const FixtureRoot root;
    const auto manifest = juce::JSON::parse (juce::String::fromUTF8 (
        BinaryData::manifest_json, BinaryData::manifest_jsonSize));
    auto* entries = manifest.getProperty ("samples", {}).getArray();
    require (entries != nullptr && entries->size() == ArrangementModel::trackCount,
             "expected the ten curated entries in the embedded manifest");
    std::reverse (entries->begin(), entries->end());

    for (int track = 0; track < ArrangementModel::trackCount; ++track)
        writeWavFixture (fileForTrack (root.folder, *entries, track), 1 + track % 2,
                         static_cast<float> (track + 1) / 32.0f);

    const SampleBank complete (manifest, root.folder);
    require (complete.readyCount() == ArrangementModel::trackCount, "all valid files should decode");
    for (int track = 0; track < ArrangementModel::trackCount; ++track)
    {
        const auto* sample = complete.sampleForTrack (track);
        require (sample != nullptr && complete.errorForTrack (track).isEmpty(), "track should be ready");
        require (std::abs (sample->sourceSampleRate - 44100.0) < 1.0,
                 "source sample rate should be preserved");
        require (sample->buffer.getNumChannels() == 1 + track % 2
                 && sample->buffer.getNumSamples() == fixtureFrames, "WAV dimensions should be preserved");
        const auto amplitude = static_cast<float> (track + 1) / 32.0f;
        require (std::abs (sample->buffer.getSample (0, 0) - amplitude) < 0.0001f,
                 "manifest order must not change the sample for a track");
        if (sample->buffer.getNumChannels() == 2)
            require (std::abs (sample->buffer.getSample (1, 0) + amplitude) < 0.0001f,
                     "right channel data should be preserved");
    }

    require (fileForTrack (root.folder, *entries, missingTrack).deleteFile(), "could not remove fixture WAV");
    require (fileForTrack (root.folder, *entries, corruptTrack).replaceWithText ("invalid WAV fixture"),
             "could not corrupt fixture WAV");

    const SampleBank partial (manifest, root.folder);
    require (partial.readyCount() == ArrangementModel::trackCount - 2,
             "only valid samples should be marked ready");
    require (partial.sampleForTrack (missingTrack) == nullptr
             && partial.errorForTrack (missingTrack).contains ("missing"), "missing file needs a track error");
    require (partial.sampleForTrack (corruptTrack) == nullptr
             && partial.errorForTrack (corruptTrack).contains ("WAV"), "invalid WAV needs a track error");
    require (partial.sampleForTrack (cleanTrack) != nullptr && partial.errorForTrack (cleanTrack).isEmpty(),
             "the Clean sample should stay usable when other samples fail");
    require (partial.sampleForTrack (-1) == nullptr
             && partial.sampleForTrack (ArrangementModel::trackCount) == nullptr,
             "out-of-range tracks must not return a sample");

    const FixtureRoot external;
    const auto externalFile = fileForTrack (external.folder, *entries, missingTrack);
    writeWavFixture (externalFile, 1, 0.5f);
    const auto link = fileForTrack (root.folder, *entries, missingTrack);
    require (externalFile.createSymbolicLink (link, false), "could not create external symbolic link");
    const SampleBank externalLink (manifest, root.folder);
    require (externalLink.sampleForTrack (missingTrack) == nullptr
             && externalLink.errorForTrack (missingTrack).contains ("inside"),
             "sample symlinks must not escape the root");

    require (link.deleteFile(), "could not remove external symbolic link");
    const auto localFile = fileForTrack (root.folder, *entries, cleanTrack);
    require (localFile.createSymbolicLink (link, false), "could not create internal symbolic link");
    const SampleBank internalLink (manifest, root.folder);
    require (internalLink.sampleForTrack (missingTrack) != nullptr
             && internalLink.errorForTrack (missingTrack).isEmpty(),
             "sample symlinks to files inside the root should still load");
}
}

int main()
{
    try
    {
        loadsByTrackIdAndIsolatesUnavailableFiles();
    }
    catch (const std::exception& error)
    {
        std::cerr << "Sample bank scenario failed: " << error.what() << '\n';
        return 1;
    }

    std::cout << "Sample bank scenario passed.\n";
    return 0;
}
