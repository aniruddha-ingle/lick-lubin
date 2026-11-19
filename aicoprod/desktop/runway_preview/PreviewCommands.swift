import Foundation

private struct PhraseError: LocalizedError {
    let detail: String
    init(_ text: String) { detail = text }
    var errorDescription: String? { detail }
}

private func groups(_ pattern: String, _ input: String) -> [String]? {
    let text = input as NSString
    guard let regex = try? NSRegularExpression(pattern: "^(?:" + pattern + ")$"),
          let match = regex.firstMatch(in: input, range: NSRange(location: 0, length: text.length)) else { return nil }
    return (1..<match.numberOfRanges).map {
        let range = match.range(at: $0)
        return range.location == NSNotFound ? "" : text.substring(with: range)
    }
}

private func normalized(_ transcript: String) -> String {
    var s = transcript.lowercased().replacingOccurrences(of: "’", with: "'")
        .trimmingCharacters(in: CharacterSet.whitespacesAndNewlines.union(CharacterSet(charactersIn: ".!?")))
        .replacingOccurrences(of: #"\s+"#, with: " ", options: .regularExpression)
    let words = ["zero","one","two","three","four","five","six","seven","eight","nine","ten","eleven","twelve","thirteen","fourteen","fifteen","sixteen"]
    for (n,word) in words.enumerated() {
        s = s.replacingOccurrences(of: "\\b" + word + "\\b", with: String(n), options: .regularExpression)
    }
    for (i,word) in ["first","second","third","fourth"].enumerated() {
        s = s.replacingOccurrences(of: "\\b" + word + "\\b", with: String(i+1), options: .regularExpression)
    }
    return s.replacingOccurrences(of: #"\b(\d+)(st|nd|rd|th)\b"#, with: "$1", options: .regularExpression)
        .replacingOccurrences(of:"hi hat",with:"hihat").replacingOccurrences(of:"run way",with:"runway")
}

private func positions(_ source:String, zeroBased:Bool, limit:Int = 16) throws -> [Int] {
    let terms = source.replacingOccurrences(of:" and ",with:",").replacingOccurrences(of:" ",with:"").split(separator:",")
    guard !terms.isEmpty else { throw PhraseError("Choose at least one position.") }
    return try terms.map {
        guard let n=Int($0), (0...limit).contains(n) else { throw PhraseError("Unrecognized position.") }
        let value = zeroBased ? n+1 : max(n,1)
        guard n>=0 && (1...limit).contains(value) else { throw PhraseError("Position outside the available range.") }
        return value
    }
}

private func pitch(_ token:String) throws -> Int {
    guard let g=groups("([a-g])([#b]?)([1-5]?)", token),
          let semitone=["c":0,"d":2,"e":4,"f":5,"g":7,"a":9,"b":11][g[0]] else { throw PhraseError("Use a note such as G3.") }
    return 12*((Int(g[2]) ?? 3)+1)+semitone+(g[1]=="#" ? 1 : g[1]=="b" ? -1 : 0)
}

extension PreviewStore {
    func submitTranscript(_ raw:String) {
        do {
            try applyPhrase(raw)
            message = "Applied · " + raw
        } catch { message = error.localizedDescription }
    }

    private func applyPhrase(_ raw:String) throws {
        var s=normalized(raw)
        let zero=s.contains("0-indexed") || s.contains("0 indexed") || s.contains("zero-based") || s.contains("0-based")
        for tag in ["0-indexed","0 indexed","zero-based","0-based"] { s=s.replacingOccurrences(of:tag,with:"") }
        s=s.trimmingCharacters(in:.whitespaces).replacingOccurrences(of:"  ",with:" ")
        let trackIndex: (String) throws -> Int = { alias in
            guard let i=names.firstIndex(where:{$0.lowercased()==alias}) else { throw PhraseError("Unknown track " + alias) };return i
        }
        if ["new project","open a new lick-lubin project","open a new lick lubin project","open a new project"].contains(s) { newProject();return }
        guard project != nil else { throw PhraseError("Open a new project first.") }
        if s=="stop" { stop();return }
        if s=="play" { start(mode);return }
        if s=="metronome on" || s=="metronome off" { metro=s.hasSuffix("on");return }
        if s=="play in song loop mode" || s=="loop this in song mode" || s=="loop song mode" {start(.song);return}
        if s=="loop the written part of the runway" {
            guard writtenEnd>0 else {throw PhraseError("Place a loop on the runway first.")}
            rangeStart=((project?.slots.firstIndex(where:{$0 != nil}) ?? 0)*16)+1;rangeEnd=writtenEnd;repeatRunway=true;start(.runway);return
        }
        if let g=groups("(?:loop|play) (?:runway )?bars? ([0-9]+) (?:to|through) ([0-9]+)",s),
           let a=Int(g[0]),let b=Int(g[1]),(1...64).contains(a),(a...64).contains(b) {
            rangeStart=a;rangeEnd=b;repeatRunway=s.hasPrefix("loop");start(.runway);return
        }
        if let g=groups("(?:play|loop) (?:the )?runway (?:starting )?from bar ([0-9]+)",s),let a=Int(g[0]),(1...64).contains(a){
            rangeStart=a;rangeEnd=64;repeatRunway=s.hasPrefix("loop");start(.runway);return
        }
        if let g=groups("(?:open|edit) (?:the track (?:called )?)?(.+?) in (steps view|loop track mode)( and start looping)?( with metronome)?",s) {
            let i=try trackIndex(g[0]);selectTrack(i)
            if !g[3].isEmpty {metro=true}
            if g[1]=="loop track mode" || !g[2].isEmpty {start(.steps)}
            return
        }
        if let g=groups("play (.+?) on every (?:1/8|8|eighth)(?: note)?",s){
            let i=try trackIndex(g[0]);selectTrack(i);setLoopPattern(Array(stride(from:1,through:15,by:2)));return
        }
        if s=="syncopated 4/4 groove" || s=="syncopated four four groove" || s=="syncopated 4 4 groove" {setLoopPattern([1,7,9,12]);return}
        if let g=groups("(?:on (?:step )?)([0-9]+) (?:can i get (?:a|an) )?(?:808 )?slide from ([a-g][#b]?[1-5]) to ([a-g][#b]?[1-5])",s) {
            let step=try positions(g[0],zeroBased:zero)[0], a=try pitch(g[1])-rootMIDI(track), b=try pitch(g[2])-rootMIDI(track)
            guard abs(a)<=24 && abs(b)<=24 else {throw PhraseError("Slide exceeds this sample's pitch range.")}
            setNote(step:step,offsets:[a],slide:b);selectedSteps=[step];return
        }
        if let g=groups("(?:put |play )?([a-g][#b]?[1-5]?)( major| minor|maj|min)? on step ([0-9]+)",s) {
            let step=try positions(g[2],zeroBased:zero)[0], base=try pitch(g[0])-rootMIDI(track)
            let offsets=g[1].isEmpty ? [base] : [base,base+(g[1].contains("min") ? 3 : 4),base+7]
            guard offsets.allSatisfy({abs($0)<=24}) else {throw PhraseError("Chord exceeds this sample's pitch range.")}
            setNote(step:step,offsets:offsets,slide:nil);selectedSteps=[step];return
        }
        if let g=groups("(?:play )?steps? ([0-9, and]+)",s){let hits=try positions(g[0],zeroBased:zero);setSteps(hits);selectedSteps=Set(hits);return}
        if let g=groups("(?:(?:can we |let's |lets )?go (?:back )?)?(up|down) ([0-9]+) (?:notes|steps|semitones)",s),let n=Int(g[1]),n<=48{
            guard !selectedSteps.isEmpty else {throw PhraseError("Select the notes to transpose.")}
            let delta = g[0]=="up" ? n : -n
            let changes=selectedSteps.map{ step in (step,pitches(step:step).map{$0+delta},pattern[step]?.slide.map{$0+delta}) }
            guard changes.allSatisfy({!$0.1.isEmpty && $0.1.allSatisfy{abs($0)<=24} && abs($0.2 ?? 0)<=24}) else {throw PhraseError("Transposition exceeds the sample range or a selected step is empty.")}
            let selection=selectedSteps;for (step,notes,slide) in changes {setNote(step:step,offsets:notes,slide:slide)};selectedSteps=selection;return
        }
        if let g=groups("(?:swap|replace) (.+) (?:for|with) (.+)",s),names.contains(where:{$0.lowercased()==g[0]}) {
            let a=try trackIndex(g[0]), b=try trackIndex(g[1])
            guard a != b,var p=project,let i=p.loops.firstIndex(where:{$0.id==loopID}) else {throw PhraseError("Choose two different tracks.")}
            let entries=p.loops[i].patterns.filter{$0.key.hasPrefix("\(a):")}
            guard !entries.isEmpty else {throw PhraseError("The source track is empty.")}
            guard !p.loops[i].patterns.keys.contains(where:{$0.hasPrefix("\(b):")}) else {throw PhraseError("The destination track already has a pattern.")}
            for (key,value) in entries {p.loops[i].patterns["\(b):"+key.split(separator:":")[1]]=value;p.loops[i].patterns.removeValue(forKey:key)}
            project=p;selectTrack(b);return
        }
        if ["duplicate loop","create a new loop by duplicating what we have so far","duplicate this loop"].contains(s){duplicateLoop();return}
        if let g=groups("(?:edit runway to )?replace loop ?([0-9]+) on (?:slots|bars) ([0-9, and]+) with loop ?([0-9]+)",s),
           let old=Int(g[0]),let new=Int(g[2]){
            let slots=try positions(g[1],zeroBased:false,limit:4)
            guard loopExists(new) && slots.allSatisfy({currentPlacement(slot:$0)==old}) else {throw PhraseError("Runway placements do not match that request.")}
            for slot in slots{placeLoop(new,slot:slot)};return
        }
        if let g=groups("arrange loop ?([0-9]+) (?:on|in) (?:the )?(?:slots? )?([0-9, and]+)(?: bars? of the runway)?",s),let id=Int(g[0]){
            let slots=try positions(g[1],zeroBased:false,limit:4);guard loopExists(id) else {throw PhraseError("Unknown loop.")}
            for slot in slots{placeLoop(id,slot:slot)};return
        }
        if let g=groups("(?:sidechain|side chain) (.+) to (.+)",s){
            let a=try trackIndex(g[0]),b=try trackIndex(g[1]);guard a != b else {throw PhraseError("Choose two different tracks.")}
            updateMix(b){$0.sidechainFrom=names[a]};return
        }
        if let g=groups("(add echo to|remove echo from|put) (.+?)( in a big room)?",s),g[0] != "put" || !g[2].isEmpty {
            let i=try trackIndex(g[1]);updateMix(i) { if g[0]=="put" {$0.reverbMix=0.5} else {$0.delayMix=g[0]=="add echo to" ? 0.3 : 0} };return
        }
        if let g=groups("(?:set )?(rack|echo|reverb) (?:mix|amount) (?:on|for) (.+) to ([0-9]+)(?: percent|%)",s),
           let amount=Int(g[2]),(0...100).contains(amount){
            let i=try trackIndex(g[1]);updateMix(i){m in let v=Double(amount)/100;switch g[0]{case "rack":m.rackMix=v;case "echo":m.delayMix=v;default:m.reverbMix=v}};return
        }
        if let g=groups("(?:set )?(.+) gain (?:to )?(-?[0-9]+) (?:db|decibels)",s),let db=Double(g[1]),(-60...12).contains(db){
            updateMix(try trackIndex(g[0])){$0.gainDB=db};return
        }
        if let g=groups("(rack|cut itself|echo|reverb) (on|off) for (.+)",s){
            let i=try trackIndex(g[2]);let on=g[1]=="on";updateMix(i){mix in
            switch g[0]{case "rack":mix.rackOn=on;case "cut itself":mix.cutItself=on;case "echo":mix.delayMix=on ? 0.3 : 0;default:mix.reverbMix=on ? 0.25 : 0}
            };return
        }
        throw PhraseError("Try “play Closed on every eighth note” or “loop this in song mode”.")
    }
    private func updateMix(_ i:Int, _ edit:(inout Mix)->Void) {
        let prior=track
        track=i;var m=currentMix;edit(&m);setMix(m);track=prior
    }
    func dictate() {
        guard let helper=Bundle.main.url(forResource:"orchid-dictate",withExtension:nil) else {message="Microphone helper is missing.";return}
        let process=Process(),pipe=Pipe()
        process.executableURL=helper;process.arguments=names;process.standardOutput=pipe
        let errors=Pipe();process.standardError=errors
        process.terminationHandler={ [weak self] task in
            let heard=String(decoding:pipe.fileHandleForReading.readDataToEndOfFile(),as:UTF8.self).trimmingCharacters(in:.whitespacesAndNewlines)
            let reason=String(decoding:errors.fileHandleForReading.readDataToEndOfFile(),as:UTF8.self)
            Task { @MainActor in
                if task.terminationStatus==0 {self?.submitTranscript(heard)}
                else {self?.message=reason.trimmingCharacters(in:.whitespacesAndNewlines)}
            }
        }
        do {try process.run();message="Listening for a command…"} catch {message=error.localizedDescription}
    }
}
