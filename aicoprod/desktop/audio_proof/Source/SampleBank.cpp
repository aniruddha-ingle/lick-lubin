#include "SampleBank.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <limits>
#include <memory>
#include <system_error>
#include <utility>

namespace licklubin
{
namespace
{
constexpr double maxSampleSeconds = 120.0;
const juce::String invalidTrackError { "Invalid arrangement track." };

std::filesystem::path canonicalPath (const juce::File& file, std::error_code& error)
{
    return std::filesystem::canonical (std::filesystem::u8path (file.getFullPathName().toRawUTF8()), error);
}

bool isValidTrack (int track) noexcept
{
    return track >= 0 && track < ArrangementModel::trackCount;
}

juce::String loadTrackSample (const juce::Array<juce::var>& entries,
                              const ArrangementModel::TrackInfo& track,
                              const juce::File& root, const std::filesystem::path& resolvedRoot,
                              juce::WavAudioFormat& format,
                              SampleBank::Sample& sample)
{
    const juce::DynamicObject* entry = nullptr;
    for (const auto& value : entries)
    {
        const auto* candidate = value.getDynamicObject();
        if (candidate == nullptr || candidate->getProperty ("id").toString() != track.id)
            continue;

        if (entry != nullptr)
            return "Sample ID '" + juce::String (track.id) + "' is duplicated in the Orchid manifest.";

        entry = candidate;
    }

    if (entry == nullptr)
        return "Sample ID '" + juce::String (track.id) + "' is missing from the Orchid manifest.";

    const auto& alias = entry->getProperty ("alias");
    const auto& path = entry->getProperty ("relative_path");
    if (! alias.isString() || alias.toString() != track.alias || ! path.isString())
        return "The Orchid manifest has an invalid alias or path for '" + juce::String (track.id) + "'.";

    const auto relativePath = path.toString().replaceCharacter ('\\', '/');
    if (relativePath.isEmpty() || relativePath.startsWithChar ('/') || relativePath.containsChar (':')
        || juce::StringArray::fromTokens (relativePath, "/", "").contains (".."))
        return "The sample path must stay inside the Orchid sample folder.";

    const auto candidate = root.getChildFile (relativePath);
    std::error_code error;
    const auto resolvedFile = canonicalPath (candidate, error);
    if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory)
        return "Sample file is missing at " + candidate.getFullPathName();
    if (error)
        return "Could not resolve sample file at " + candidate.getFullPathName();

    const auto [rootPart, filePart] = std::mismatch (resolvedRoot.begin(), resolvedRoot.end(),
                                                    resolvedFile.begin(), resolvedFile.end());
    if (rootPart != resolvedRoot.end() || filePart == resolvedFile.end())
        return "The sample path must stay inside the Orchid sample folder.";

    if (! std::filesystem::is_regular_file (resolvedFile, error))
        return "The sample path must resolve to a regular file at " + candidate.getFullPathName();

    const auto utf8Path = resolvedFile.u8string();
    const juce::File file (juce::String::fromUTF8 (reinterpret_cast<const char*> (utf8Path.c_str())));

    auto stream = file.createInputStream();
    if (stream == nullptr)
        return "Could not open " + file.getFileName() + ".";

    const std::unique_ptr<juce::AudioFormatReader> reader (
        format.createReaderFor (stream.release(), true));
    if (reader == nullptr)
        return "Could not read " + file.getFileName() + " as a WAV file.";

    if (reader->numChannels < 1 || reader->numChannels > 2 || reader->lengthInSamples <= 0
        || reader->lengthInSamples > std::numeric_limits<int>::max()
        || ! std::isfinite (reader->sampleRate) || reader->sampleRate <= 0.0
        || static_cast<double> (reader->lengthInSamples) / reader->sampleRate > maxSampleSeconds)
        return file.getFileName() + " must be a non-empty mono or stereo WAV of at most 120 seconds.";

    const auto bytesPerFrame = reader->numChannels * ((reader->bitsPerSample + 7) / 8);
    if (bytesPerFrame == 0 || reader->lengthInSamples * bytesPerFrame > file.getSize())
        return file.getFileName() + " has incomplete WAV sample data.";

    const auto length = static_cast<int> (reader->lengthInSamples);
    const auto channels = static_cast<int> (reader->numChannels);
    juce::AudioBuffer<float> buffer (channels, length);
    if (! reader->read (&buffer, 0, length, 0, true, channels > 1))
        return "Could not decode the complete sample from " + file.getFileName() + ".";

    for (int channel = 0; channel < channels; ++channel)
    {
        const auto* data = buffer.getReadPointer (channel);
        if (! std::all_of (data, data + length, [] (float value) { return std::isfinite (value); }))
            return file.getFileName() + " contains non-finite audio samples.";
    }

    sample.buffer = std::move (buffer);
    sample.sourceSampleRate = reader->sampleRate;
    return {};
}
}

SampleBank::SampleBank (const juce::var& manifest, const juce::File& sampleRoot)
{
    const auto* object = manifest.getDynamicObject();
    const auto* entries = object != nullptr ? object->getProperty ("samples").getArray() : nullptr;
    if (entries == nullptr)
    {
        for (auto& track : tracks)
            track.error = "The Orchid manifest must contain a samples array.";
        return;
    }

    if (! sampleRoot.isDirectory())
    {
        const auto error = "Orchid sample folder is missing at " + sampleRoot.getFullPathName();
        for (auto& track : tracks)
            track.error = error;
        return;
    }

    std::error_code error;
    const auto resolvedRoot = canonicalPath (sampleRoot, error);
    if (error)
    {
        for (auto& track : tracks)
            track.error = "Could not resolve Orchid sample folder at " + sampleRoot.getFullPathName();
        return;
    }

    juce::WavAudioFormat format;
    for (std::size_t track = 0; track < tracks.size(); ++track)
    {
        auto& slot = tracks[track];
        slot.error = loadTrackSample (*entries, ArrangementModel::tracks[track], sampleRoot, resolvedRoot,
                                      format, slot.sample);
        if (slot.error.isEmpty())
            ++loadedCount;
    }
}

const SampleBank::Sample* SampleBank::sampleForTrack (int track) const noexcept
{
    if (! isValidTrack (track))
        return nullptr;

    const auto& sample = tracks[static_cast<std::size_t> (track)].sample;
    return sample.sourceSampleRate > 0.0 ? &sample : nullptr;
}

const juce::String& SampleBank::errorForTrack (int track) const noexcept
{
    return isValidTrack (track) ? tracks[static_cast<std::size_t> (track)].error : invalidTrackError;
}
}
