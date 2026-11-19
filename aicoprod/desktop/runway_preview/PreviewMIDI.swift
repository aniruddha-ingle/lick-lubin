import AudioToolbox
import Foundation

enum PreviewMIDI {
    typealias Pattern = [Int: [Int: [Int]]]

    private struct Manifest: Decodable {
        struct Sample: Decodable { let id: String; let root_note: String? }
        struct Motif: Decodable { let id: String; let relative_path: String }
        let samples: [Sample]
        let motifs: [Motif]
    }

    struct ImportError: LocalizedError {
        let message: String
        var errorDescription: String? { message }
        init(_ message: String) { self.message = message }
    }

    /// New Flow spans four bars of 4/4. Quantize note attacks to the nearest
    /// sixteenth and return one-based bar/step coordinates. The original MIDI
    /// pitches already encode F minor. Normalize against the channel root in
    /// octave 3 (MIDI C3 = 48). Durations are supplied by the sampled voice.
    static func importNewFlow(manifestURL: URL, sampleRoot: URL, trackID: String) throws -> Pattern {
        let manifest = try JSONDecoder().decode(Manifest.self, from: Data(contentsOf: manifestURL))
        let notes = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
        guard let name = manifest.samples.first(where: { $0.id == trackID })?.root_note,
              let pitchClass = notes.firstIndex(of: name) else {
            throw ImportError("Choose an Orchid track with a known root pitch.")
        }
        guard let motif = manifest.motifs.first(where: { $0.id == "new_flow" }) else {
            throw ImportError("The manifest is missing New Flow.")
        }
        let root = sampleRoot.standardizedFileURL.resolvingSymlinksInPath()
        let file = root.appendingPathComponent(motif.relative_path).standardizedFileURL.resolvingSymlinksInPath()
        guard file.pathComponents.starts(with: root.pathComponents),
              file.pathComponents.count > root.pathComponents.count,
              FileManager.default.isReadableFile(atPath: file.path) else {
            throw ImportError("New Flow MIDI is unavailable in the Orchid pack.")
        }
        return try read(file, rootPitch: 48 + pitchClass)
    }

    private static func check(_ status: OSStatus) throws {
        if status != noErr { throw ImportError("Could not read MIDI events (status \(status)).") }
    }

    private static func read(_ url: URL, rootPitch: Int) throws -> Pattern {
        var sequence: MusicSequence?
        try check(NewMusicSequence(&sequence))
        guard let sequence else { throw ImportError("Could not create MIDI sequence.") }
        defer { DisposeMusicSequence(sequence) }
        try check(MusicSequenceFileLoad(sequence, url as CFURL, .midiType, []))
        var count: UInt32 = 0
        try check(MusicSequenceGetTrackCount(sequence, &count))
        var pattern = Pattern()
        for index in 0..<count {
            var track: MusicTrack?
            try check(MusicSequenceGetIndTrack(sequence, index, &track))
            guard let track else { throw ImportError("Could not access MIDI track.") }
            var iterator: MusicEventIterator?
            try check(NewMusicEventIterator(track, &iterator))
            guard let iterator else { throw ImportError("Could not iterate MIDI track.") }
            defer { DisposeMusicEventIterator(iterator) }
            var hasEvent = DarwinBoolean(false)
            try check(MusicEventIteratorHasCurrentEvent(iterator, &hasEvent))
            while hasEvent.boolValue {
                var beat: MusicTimeStamp = 0
                var type: MusicEventType = 0
                var ptr: UnsafeRawPointer?
                var size: UInt32 = 0
                try check(MusicEventIteratorGetEventInfo(iterator, &beat, &type, &ptr, &size))
                if type == kMusicEventType_MIDINoteMessage,
                   let ptr, size >= MemoryLayout<MIDINoteMessage>.size {
                    let note = ptr.loadUnaligned(as: MIDINoteMessage.self)
                    if note.velocity != 0 {
                        guard beat.isFinite, beat >= 0, beat < 16 else {
                            throw ImportError("New Flow contains a note outside the four-bar phrase.")
                        }
                        let tick = min(63, Int((beat * 4).rounded()))
                        let pitch = Int(note.note) - rootPitch
                        guard (-24...24).contains(pitch) else {
                            throw ImportError("A note lies outside the selected sample’s four-octave range.")
                        }
                        let bar = tick / 16 + 1, step = tick % 16 + 1
                        var chord = pattern[bar]?[step] ?? []
                        if !chord.contains(pitch) {
                            chord.append(pitch)
                            pattern[bar, default: [:]][step] = chord.sorted()
                        }
                    }
                }
                try check(MusicEventIteratorNextEvent(iterator))
                try check(MusicEventIteratorHasCurrentEvent(iterator, &hasEvent))
            }
        }
        guard !pattern.isEmpty else { throw ImportError("New Flow has no playable MIDI notes.") }
        return pattern
    }
}
