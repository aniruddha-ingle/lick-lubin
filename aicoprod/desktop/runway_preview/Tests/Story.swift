import Foundation

@main struct DemoRehearsal {
    @MainActor static func main() {
        // Run only with an isolated application-support directory.
        precondition(ProcessInfo.processInfo.environment["ORCHID_REHEARSAL"] == "1")
        let s = PreviewStore()
        func say(_ line:String) {
            s.submitTranscript(line)
            precondition(s.message.hasPrefix("Applied"), "\(line) → \(s.message)")
        }
        say("open a new lick-lubin project")
        say("open the track called Closed in steps view and start looping with metronome")
        say("play Closed on every 1/8th note")
        precondition(s.metro && s.running && s.mode == .steps && s.pattern.count == 8)
        say("loop this in song mode")
        say("edit Clean in steps view")
        say("syncopated 4/4 groove")
        precondition(s.mode == .song && Set(s.pattern.keys) == [1,7,9,12])
        say("edit Heavyweight in loop track mode")
        say("steps 1 and 11")
        say("put G3 on step 1")
        say("go up 2 notes")
        precondition(s.pitches(step:1)==[9])
        say("go back down 2 steps")
        say("on 11 can I get a 808 slide from G3 to G4")
        precondition(s.pitches(step:1)==[7] && s.pattern[11]?.slide==19)
        say("play in song loop mode")
        say("arrange loop1 on the 1st,2nd,3rd, and 4th bar of the runway")
        precondition(s.mode == .song && s.project?.slots == [1,1,1,1])
        say("duplicate loop")
        say("swap Heavyweight for Perfect Donk")
        say("replace loop 1 on bars 2 and 4 with loop 2")
        precondition(s.project?.slots == [1,2,1,2] && s.mode == .song)
        say("put Gmaj on step 0th")
        precondition(s.pitches(step:1)==[7,11,14])
        say("sidechain Clean to Heavyweight")
        precondition(s.track==0 && s.project?.mixes[1].sidechainFrom=="Clean")
        say("loop the written part of the runway")
        precondition(s.mode == .runway && s.rangeStart==1 && s.rangeEnd==64)
        say("loop runway bars 49 through 64")
        precondition(s.rangeStart==49 && s.rangeEnd==64 && s.repeatRunway)
        say("stop")
        let restored=PreviewStore()
        precondition(restored.project?.name==s.project?.name && restored.project?.slots==s.project?.slots)
        print("Passed the spoken producer story, loop independence, mixer routing, and project restore.")
    }
}
