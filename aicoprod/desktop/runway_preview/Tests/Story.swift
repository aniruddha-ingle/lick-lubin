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
        say("loop runway bars forty-nine through sixty four")
        precondition(s.rangeStart==49 && s.rangeEnd==64 && s.repeatRunway)
        say("stop")
        // Add a note only in bar four, then edit another step across the loop.
        s.bar=4;s.allBars=false;s.setNote(step:6,offsets:[3],slide:nil)
        s.allBars=true;s.setNote(step:9,offsets:[0],slide:nil)
        precondition(s.pitches(step:6)==[3] && s.pitches(step:9)==[0])
        s.bar=3
        precondition(s.pitches(step:6).isEmpty && s.pitches(step:9)==[0])
        say("0-indexed steps 0 and 10")
        precondition(Set(s.pattern.keys)==[1,11])
        let restored=PreviewStore()
        precondition(restored.project?.name==s.project?.name && restored.project?.slots==s.project?.slots)
        s.newProject(demo:true)
        precondition(s.project?.slots == [1,2,3,4] && s.project?.loops.count == 4)
        precondition(s.mode == .runway && s.rangeStart == 49 && s.rangeEnd == 64 && !s.running)
        precondition(s.currentLoop?.pattern(0,4)[11]?.slide == 5)
        say("play runway bars 49 through 50")
        var visited=Set<Int>()
        let deadline=Date().addingTimeInterval(8)
        while s.running && Date()<deadline {
            visited.insert(s.runningBar)
            RunLoop.main.run(until:Date().addingTimeInterval(0.04))
        }
        precondition(!s.running && visited==[49,50])
        s.bar=1;s.allBars=false
        s.tap(6);precondition(s.pattern[6] != nil)
        s.selectHit(6);precondition(s.selectedSteps==[6] && s.pattern[6] != nil)
        s.tap(6);precondition(s.pattern[6]==nil && !s.selectedSteps.contains(6))
        let oldName=s.headline
        s.newProject()
        s.project?.name=oldName
        s.newProject()
        precondition(s.archiveNames.filter{$0.hasPrefix(oldName)}.count >= 2)
        print("Passed the spoken producer story, loop independence, mixer routing, and project restore.")
    }
}
