import Foundation

@main
struct PreviewMIDITests {
    static func main() throws {
        let files = FileManager.default
        let folder = files.temporaryDirectory.appendingPathComponent(UUID().uuidString)
        try files.createDirectory(at: folder, withIntermediateDirectories: true)
        defer { try? files.removeItem(at: folder) }
        let manifest = folder.appendingPathComponent("manifest.json")
        let fixture = #"{"samples":[{"id":"bass","root_note":"C"},{"id":"hat"}],"motifs":[{"id":"new_flow","relative_path":"fixture.mid"}]}"#
        try Data(fixture.utf8).write(to: manifest)
        // PPQ 96. A chord begins at tick 23 (one tick ahead of step 2).
        let events: [UInt8] = [23,0x90,48,100, 0,0x90,55,90, 10,0x80,48,0, 0,0x80,55,0, 0,0xff,0x2f,0]
        var bytes: [UInt8] = Array("MThd".utf8) + [0,0,0,6,0,0,0,1,0,96]
        bytes += Array("MTrk".utf8) + [0,0,0,UInt8(events.count)]
        try Data(bytes + events).write(to: folder.appendingPathComponent("fixture.mid"))
        let pattern = try PreviewMIDI.importNewFlow(manifestURL: manifest, sampleRoot: folder, trackID: "bass")
        precondition(pattern == [1: [2: [0,7]]])
        do {
            _ = try PreviewMIDI.importNewFlow(manifestURL: manifest, sampleRoot: folder, trackID: "hat")
            preconditionFailure("Unpitched track accepted")
        } catch is PreviewMIDI.ImportError {}

        if CommandLine.arguments.count == 3 {
            let path = URL(fileURLWithPath: CommandLine.arguments[1])
            let pack = URL(fileURLWithPath: CommandLine.arguments[2])
            let actual = try PreviewMIDI.importNewFlow(manifestURL: path, sampleRoot: pack, trackID: "heavyweight")
            precondition(Set(actual.keys) == Set(1...4))
            precondition(actual.values.flatMap(\.values).map(\.count).reduce(0,+) == 22)
            print("New Flow: four bars, 22 pitched notes")
        }
        print("MIDI import passed")
    }
}
