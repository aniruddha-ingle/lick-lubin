import SwiftUI
import AppKit
import Combine

let names = ["Perfect Donk","Heavyweight","Clean","Dancehall","Bunker","Roll","Closed","Open","Flam","Pop"]
let seeds = [[1,7,11],[1,11],[1,7,9,12],[1,4,7,11],[5,13],[13],[1,3,5,7,9,11,13,15],[3,7,11,15],[5,13],[5,13]]
func rootMIDI(_ track:Int) -> Int { [48,48,53,58,48,48,48,48,48,48][track] }
func pitchName(_ midi:Int) -> String {
    let keys = ["C","C♯","D","D♯","E","F","F♯","G","G♯","A","A♯","B"]
    return keys[(midi % 12 + 12) % 12] + String(midi / 12 - 1)
}
struct Hit: Codable, Equatable { var offsets:[Int] = [0]; var slide:Int? }
struct Mix: Codable, Equatable {
    var gainDB = 0.0, rackMix = 1.0, delayMix = 0.0, reverbMix = 0.0
    var rackOn = true, cutItself = true
    var sidechainFrom:String? = nil
    var hall:Bool? = nil
}
struct BeatLoop: Codable, Identifiable {
    var id:Int
    var tag:String
    // Default pattern repeats on every bar. Bar overrides are explicit copies.
    var patterns:[String:[Int:Hit]] = [:]
    init(id:Int) {
        self.id=id
        tag="loop-" + String((0..<5).map { _ in "abcdefghijklmnopqrstuvwxyz".randomElement()! }) + "-\(id)"
    }
    func pattern(_ track:Int,_ bar:Int) -> [Int:Hit] {
        patterns["\(track):\(bar)"] ?? patterns["\(track):all"] ?? [:]
    }
    func hasTrack(_ track:Int,_ bar:Int) -> Bool { !pattern(track,bar).isEmpty }
}
struct Project:Codable {
    var archiveID:UUID? = UUID()
    var name:String
    var loops:[BeatLoop] = [BeatLoop(id:1)]
    var slots:[Int?] = Array(repeating:nil,count:4)
    var mixes = Array(repeating:Mix(),count:names.count)
}
enum Mode:String,CaseIterable { case steps="Track loop",song="Song loop",runway="Runway" }

@MainActor final class PreviewStore:ObservableObject {
    @Published var project:Project? { didSet { save() } }
    @Published var mode:Mode = .steps
    @Published var track=6
    @Published var bar=1
    @Published var loopID=1
    @Published var selectedSteps:Set<Int> = []
    @Published var metro=false
    @Published var running=false
    @Published var rangeStart=1
    @Published var rangeEnd=64
    @Published var repeatRunway=true
    @Published var message=""
    @Published var command=""
    @Published var runningStep=0
    @Published var runningBar=0
    @Published var allBars=true
    @Published var showHome=true
    @Published var runwayPage=1
    @Published var newBuild=false
    @Published var editingPitch=0
    @Published var chord="Single"
    @Published var glide=false
    @Published var slidePitch=12
    private var anchor=0.0
    private var emitted = -1
    private var timer:Timer?
    private let audio=OrchidPreviewAudio()
    private let saveURL:URL
    private let launchStamp:Date?
    private let exeURL=Bundle.main.executableURL
    var currentMix:Mix { project?.mixes[track] ?? Mix() }
    var currentLoop:BeatLoop? { project?.loops.first { $0.id==loopID } }
    var pattern:[Int:Hit] { currentLoop?.pattern(track,bar) ?? [:] }
    var headline:String { project?.name ?? "Lick-Lubin" }
    init() {
        let base=ProcessInfo.processInfo.environment["LICK_LUBIN_PROJECT_DIR"].map{ URL(fileURLWithPath:$0,isDirectory:true) } ?? FileManager.default.urls(for:.applicationSupportDirectory,in:.userDomainMask)[0].appendingPathComponent("Lick-Lubin")
        saveURL=base.appendingPathComponent("RunwayPreview.json")
        launchStamp=try? exeURL?.resourceValues(forKeys:[.contentModificationDateKey]).contentModificationDate
        if let data=try? Data(contentsOf:saveURL), let p=try? JSONDecoder().decode(Project.self,from:data),
           !p.loops.isEmpty,p.mixes.count==10,p.slots.count==4 { project=p;loopID=p.loops[0].id }
        if let state=ProcessInfo.processInfo.environment["ORCHID_RELOAD_VIEW"]?.split(separator:"|").map(String.init),
           state.count==10,let id=Int(state[0]),loopExists(id),let t=Int(state[1]),names.indices.contains(t),
           let b=Int(state[2]),(1...16).contains(b),let m=Mode(rawValue:state[3]),
           let first=Int(state[7]),let last=Int(state[8]),(1...64).contains(first),(first...64).contains(last) {
            loopID=id;track=t;bar=b;mode=m;showHome=state[4]=="1";allBars=state[5]=="1";metro=state[6]=="1"
            rangeStart=first;rangeEnd=last;repeatRunway=state[9]=="1"
        }
        message=audio.prepare(manifestURL: Bundle.main.url(forResource:"manifest",withExtension:"json"))
        syncMix()
        timer=Timer.scheduledTimer(withTimeInterval:0.012,repeats:true){ [weak self] _ in
            MainActor.assumeIsolated { self?.tick() }
        }
        if let timer { RunLoop.main.add(timer,forMode:.common) }
        let updater=Timer.scheduledTimer(withTimeInterval:3,repeats:true){ [weak self] _ in MainActor.assumeIsolated { self?.checkBuild() } }
        RunLoop.main.add(updater,forMode:.common)
    }
    func checkBuild() {
        guard let old=launchStamp,let now=try? exeURL?.resourceValues(forKeys:[.contentModificationDateKey]).contentModificationDate else { return }
        if now>old && !newBuild { newBuild=true }
    }
    func reload() {
        save()
        let config=NSWorkspace.OpenConfiguration(); config.createsNewApplicationInstance=true
        var environment=ProcessInfo.processInfo.environment
        let view=[String(loopID),String(track),String(bar),mode.rawValue,showHome ? "1":"0",allBars ? "1":"0",
                  metro ? "1":"0",String(rangeStart),String(rangeEnd),repeatRunway ? "1":"0"]
        environment["ORCHID_RELOAD_VIEW"]=view.joined(separator:"|")
        config.environment=environment
        NSWorkspace.shared.openApplication(at:Bundle.main.bundleURL,configuration:config) { _,error in
            DispatchQueue.main.async { if error==nil { NSApp.terminate(nil) } }
        }
    }
    func save() {
        guard let p=project else { return }
        do {
            try FileManager.default.createDirectory(at:saveURL.deletingLastPathComponent(),withIntermediateDirectories:true)
            try JSONEncoder().encode(p).write(to:saveURL,options:.atomic)
        } catch { message="Project save failed: " + error.localizedDescription }
    }
    var archiveNames:[String]{
        let files=(try? FileManager.default.contentsOfDirectory(at:saveURL.deletingLastPathComponent(),includingPropertiesForKeys:nil)) ?? []
        return files.filter{$0.pathExtension=="json" && $0.lastPathComponent != saveURL.lastPathComponent}.map{$0.deletingPathExtension().lastPathComponent}.sorted()
    }
    private func archive(_ p:Project) throws {
        let folder=saveURL.deletingLastPathComponent()
        try FileManager.default.createDirectory(at:folder,withIntermediateDirectories:true)
        // Old projects may have no stable ID. Keep any unrelated or unreadable archive.
        let safeName=p.name.replacingOccurrences(of:"/",with:"_")
        var file=folder.appendingPathComponent(safeName+".json")
        if FileManager.default.fileExists(atPath:file.path) {
            let prior=(try? Data(contentsOf:file)).flatMap{try? JSONDecoder().decode(Project.self,from:$0)}
            if prior?.archiveID == nil || prior?.archiveID != p.archiveID {
                file=folder.appendingPathComponent(safeName+"-"+(p.archiveID ?? UUID()).uuidString+".json")
            }
        }
        try JSONEncoder().encode(p).write(to:file,options:.atomic)
    }
    func openArchive(_ name:String) {
        guard archiveNames.contains(name) else{return}
        do {
            let folder=saveURL.deletingLastPathComponent()
            let loaded=try JSONDecoder().decode(Project.self,from:Data(contentsOf:folder.appendingPathComponent(name+".json")))
            guard loaded.slots.count==4,loaded.mixes.count==names.count,let loop=loaded.loops.first else{return}
            if let previous=project {try archive(previous)}
            stop();project=loaded;loopID=loop.id;bar=1;selectedSteps=[];syncMix();showHome=false
        }catch{message=error.localizedDescription}
    }
    func importMotif() {
        guard let asset=Bundle.main.url(forResource:"manifest",withExtension:"json"),var p=project,
              let i=p.loops.firstIndex(where:{$0.id==loopID}) else{return}
        let ids=["perfect_donk","heavyweight","clean_kick","dancehall_kick","bunker_rimshot","roll_hihat","closed_hihat","open_hihat","flam_clap","pop_clap"]
        let root=ProcessInfo.processInfo.environment["ORCHID_SAMPLE_ROOT"].map{ URL(fileURLWithPath:$0,isDirectory:true) }
          ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Music/Lick-Lubin/Orchid Sample Pack")
        do {
            let parsed=try PreviewMIDI.importNewFlow(manifestURL:asset,sampleRoot:root,trackID:ids[track])
            for bar in 1...4 {
                p.loops[i].patterns["\(track):\(bar)"]=(parsed[bar] ?? [:]).mapValues{Hit(offsets:$0,slide:nil)}
            }
            project=p;allBars=false;bar=1;selectedSteps=[];message="New Flow imported to bars 1–4 of "+names[track]+"."
        }catch{message=error.localizedDescription}
    }
    func newProject(demo:Bool=false) {
        stop()
        // Keep the previous project available on disk when starting another.
        if let p=project {
            do { try archive(p) }
            catch { message="Could not archive current project."; return }
        }
        let a=["toilet","waffle","pickle","velvet","disco","pocket"].randomElement()!
        let b=["nut","badger","biscuit","moth","wizard","squid"].randomElement()!
        project=Project(name:"\(a)-\(b)-\(Int.random(in:1...999))")
        track=6;bar=1;loopID=1;mode = .steps;rangeStart=1;rangeEnd=64;selectedSteps=[];showHome=false;metro=false;allBars=true;syncMix()
        if demo {
            project?.arrangeDemoBeat();loopID=4;mode = .runway;rangeStart=49;rangeEnd=64;selectTrack(0);syncMix()
            message="Bars 49–64 are ready. Press Play to rehearse the final section."
        } else { message="Fresh loop. Select Closed and try eighths." }
    }
    func duplicateLoop() {
        guard var p=project,var loop=currentLoop else{return}
        let next=(p.loops.map(\.id).max() ?? 0)+1
        loop.id=next;loop.tag=BeatLoop(id:next).tag
        p.loops.append(loop);project=p;loopID=next;selectedSteps=[];message="Editing " + loop.tag
    }
    func selectTrack(_ index:Int){ guard names.indices.contains(index) else{return};if running && mode == .steps && index != track {audio.stop()};track=index;selectedSteps=[];editingPitch=0;chord="Single";glide=false }
    func pitches(step:Int)->[Int]{ pattern[step]?.offsets ?? [] }
    func loopExists(_ id:Int)->Bool{ project?.loops.contains(where:{$0.id==id}) == true }
    @discardableResult func selectLoop(_ id:Int)->Bool{ guard loopExists(id) else{return false};loopID=id;selectedSteps=[];return true }
    func currentPlacement(slot:Int)->Int?{guard (1...4).contains(slot) else{return nil};return project?.slots[slot-1] ?? nil}
    var writtenEnd:Int{project?.slots.lastIndex(where:{$0 != nil}).map{($0+1)*16} ?? 0}
    func goHome(){stop();showHome=true}
    func setLoopPattern(_ steps:[Int]){let previous=allBars;allBars=true;setSteps(steps);allBars=previous}
    func placeLoop(_ id:Int,slot:Int){
        guard (1...4).contains(slot),var p=project,p.loops.contains(where:{$0.id==id}) else{return}
        p.slots[slot-1]=id; project=p;message="Loop \(id) placed at bars \((slot-1)*16+1)–\(slot*16)."
    }
    func clearSlot(_ slot:Int) { guard var p=project,(1...4).contains(slot) else{return};p.slots[slot-1]=nil;project=p }
    func setMix(_ mix:Mix){guard var p=project else{return};p.mixes[track]=mix;project=p;applyMix(track,mix)}
    private func applyMix(_ i:Int,_ m:Mix){audio.mix(track:names[i],gainDB:m.gainDB,rackOn:m.rackOn,rackMix:m.rackMix,delayMix:m.delayMix,reverbMix:m.reverbMix,cutItself:m.cutItself,sidechainFrom:m.sidechainFrom,hall:m.hall ?? true)}
    private func syncMix(){ for i in names.indices {applyMix(i,project?.mixes[i] ?? Mix())} }
    private func write(_ updated:[Int:Hit]){
        guard var p=project,let i=p.loops.firstIndex(where:{$0.id==loopID}) else{return}
        if allBars {
            for b in 1...16 { p.loops[i].patterns.removeValue(forKey:"\(track):\(b)") }
            p.loops[i].patterns["\(track):all"]=updated
        }else{p.loops[i].patterns["\(track):\(bar)"]=updated}
        project=p
    }
    func setSteps(_ numbers:[Int]){
        guard numbers.allSatisfy({(1...16).contains($0)}) else{return}
        var result:[Int:Hit]=[:]
        for n in numbers {result[n]=pattern[n] ?? Hit()}
        write(result);selectedSteps=Set(numbers)
    }
    func setNote(step:Int,offsets:[Int],slide:Int?){
        guard (1...16).contains(step),offsets.allSatisfy({(-24...24).contains($0)}),slide.map({(-24...24).contains($0)}) ?? true else{return}
        let hit:Hit?=offsets.isEmpty ? nil : Hit(offsets:Array(Set(offsets)).sorted(),slide:slide)
        guard var p=project,let i=p.loops.firstIndex(where:{$0.id==loopID}) else{return}
        if allBars {
            // Keep each bar's existing variations while applying this single-step edit everywhere.
            let prefix="\(track):"
            var keys=Set(p.loops[i].patterns.keys.filter{$0.hasPrefix(prefix)})
            keys.insert(prefix+"all")
            for key in keys {var hits=p.loops[i].patterns[key] ?? [:];hits[step]=hit;p.loops[i].patterns[key]=hits}
        } else {
            var hits=p.loops[i].pattern(track,bar);hits[step]=hit;p.loops[i].patterns["\(track):\(bar)"]=hits
        }
        project=p
    }
    func tap(_ step:Int){
        if pattern[step] != nil {
            setNote(step:step,offsets:[],slide:nil)
            selectedSteps.remove(step)
        } else {
            setNote(step:step,offsets:[0],slide:nil)
            selectHit(step)
        }
    }
    func selectHit(_ step:Int){
        guard let hit=pattern[step] else{return}
        selectedSteps=[step]
        editingPitch=hit.offsets.first ?? 0
        glide=hit.slide != nil
        slidePitch=hit.slide ?? min(24,editingPitch+12)
        chord=hit.offsets.count>1 ? (hit.offsets[1]-editingPitch==3 ? "Minor":"Major") : "Single"
    }
    func applyNotes(){
        let offsets=(chord=="Major" ? [0,4,7] : chord=="Minor" ? [0,3,7] : [0]).map{$0+editingPitch}
        guard offsets.allSatisfy({(-24...24).contains($0)}) else{message="Chord exceeds the two-octave pitch range.";return}
        for n in selectedSteps {setNote(step:n,offsets:offsets,slide:glide ? slidePitch:nil)}
    }
    func loopWritten(){
        guard let slots=project?.slots,let first=slots.firstIndex(where:{$0 != nil}),let last=slots.lastIndex(where:{$0 != nil}) else{message="Place a loop on the runway first.";return}
        rangeStart=first*16+1;rangeEnd=(last+1)*16;repeatRunway=true;start(.runway)
    }
    func start(_ m:Mode){
        guard project != nil else{return}
        showHome=false
        if running && m != .runway {if m != mode { audio.stop() }; mode=m;return}
        audio.stop();mode=m;running=true;emitted = -1;anchor=ProcessInfo.processInfo.systemUptime;tick()
    }
    func stop(){running=false;runningStep=0;runningBar=0;audio.stop()}
    private func tick(){
        guard running,let p=project else{return}
        let pos=Int((ProcessInfo.processInfo.systemUptime-anchor)*102*4/60)
        guard pos>emitted else{return};emitted=pos
        let raw=pos/16,step=pos%16+1
        var playBar=raw%16+1,loop=currentLoop
        if mode == .steps {playBar=bar}
        if mode == .runway {
            let length=max(1,rangeEnd-rangeStart+1)
            if !repeatRunway && raw>=length{stop();return}
            playBar=rangeStart+raw%length
            guard (1...64).contains(playBar) else{stop();return}
            loop=p.slots[(playBar-1)/16].flatMap{ id in p.loops.first{$0.id==id} }
        }
        runningStep=step;runningBar=playBar
        if metro && (step-1)%4==0 {audio.click(accent:step==1)}
        let localBar=(playBar-1)%16+1
        for i in names.indices where mode != .steps || i==track {
            if let hit=loop?.pattern(i,localBar)[step]{audio.hit(track:names[i],notes:hit.offsets,slideTo:hit.slide)}
        }
    }
}

private let mint=Color(red:0.42,green:0.84,blue:0.68)
private let panel=Color(red:0.12,green:0.14,blue:0.17)
struct PreviewScreen:View {
    @ObservedObject var s:PreviewStore
    var body:some View {
        VStack(alignment:.leading,spacing:10) {
            HStack {
                Text(s.showHome ? "Lick-Lubin" : s.headline).font(.system(size:26,weight:.bold,design:.rounded))
                Spacer()
                if s.newBuild {Button("New version available · Reload"){s.reload()}}
                if !s.showHome{Text("102 BPM   •   F minor").foregroundStyle(.secondary);Button("Home"){s.goHome()}}
            }
            if s.showHome {
                Spacer()
                VStack(alignment:.leading,spacing:16){
                    Text("Orchid studio").font(.system(size:42,weight:.bold,design:.rounded))
                    HStack{Button("New project"){s.newProject()}.buttonStyle(.borderedProminent)
                        Button("Demo beat"){s.newProject(demo:true)}
                        if let p=s.project{Button("Resume " + p.name){s.showHome=false}}
                        if !s.archiveNames.isEmpty{Menu("Saved projects"){ForEach(s.archiveNames,id:\.self){name in Button(name){s.openArchive(name)}}}.fixedSize()}
                    }
                }.frame(maxWidth:.infinity,alignment:.leading).padding(30).background(panel,in:RoundedRectangle(cornerRadius:14))
                Spacer()
            }else{
                ScrollView{VStack(alignment:.leading,spacing:12){
                    transport
                    runway
                    loopStrip
                    stepEditor
                    mixer
                }.padding(.trailing,4)}
            }
            HStack{
                TextField("Say or type a music command",text:$s.command).textFieldStyle(.roundedBorder)
                    .onSubmit{let text=s.command;s.command="";s.submitTranscript(text)}
                Button("Send"){let text=s.command;s.command="";s.submitTranscript(text)}.disabled(s.command.isEmpty)
                Button{ s.dictate() }label:{Label("Speak",systemImage:"mic.fill")}
            }
            Text(s.message).font(.caption).foregroundStyle(.secondary).lineLimit(2)
        }.padding(18).background(Color(red:0.075,green:0.087,blue:0.10)).foregroundStyle(.white)
        .tint(mint).preferredColorScheme(.dark).frame(minWidth:900,minHeight:640)
    }
    var transport:some View{
        HStack{
            Button{s.running ? s.stop():s.start(s.mode)}label:{Label(s.running ? "Stop":"Play",systemImage:s.running ? "stop.fill":"play.fill")}.buttonStyle(.borderedProminent)
            Picker("Listen",selection:Binding(get:{s.mode},set:{if s.running{s.start($0)}else{s.mode=$0}})){
                ForEach(Mode.allCases,id:\.self){m in Text(m.rawValue).tag(m)}
            }.pickerStyle(.segmented).frame(maxWidth:400)
            Toggle("Metronome",isOn:$s.metro).toggleStyle(.checkbox)
            Spacer()
            Text(s.running ? "\(s.mode.rawValue)  ·  \(s.runningBar).\(s.runningStep)" : "Stopped").monospacedDigit()
        }.padding(10).background(panel,in:RoundedRectangle(cornerRadius:8))
    }
    var runway:some View{
        VStack(alignment:.leading,spacing:6){
            HStack{
                Text("Runway").font(.title3.bold());Text("64 bars").foregroundStyle(.secondary);Spacer()
                Stepper("From \(s.rangeStart)",value:$s.rangeStart,in:1...s.rangeEnd).fixedSize()
                Stepper("To \(s.rangeEnd)",value:$s.rangeEnd,in:s.rangeStart...64).fixedSize()
                Toggle("Repeat",isOn:$s.repeatRunway).toggleStyle(.checkbox)
                Button("Play range"){s.start(.runway)}
                Button("Loop written"){s.loopWritten()}
            }
            HStack(alignment:.top,spacing:9){
                    VStack(alignment:.trailing,spacing:2){Text("Placement").frame(height:28);Text("Bar").frame(height:18)
                        ForEach(names.indices,id:\.self){t in Button(names[t]){s.selectTrack(t)}.buttonStyle(.plain).foregroundStyle(t==s.track ? mint:.white).frame(height:16)}
                    }.font(.system(size:11,weight:.medium)).frame(width:86)
                    ScrollView(.horizontal){HStack(alignment:.top,spacing:9){
                        ForEach(0..<4,id:\.self){slot in runwaySlot(slot)}
                    }.padding(.bottom,5)}
            }
        }.padding(12).background(panel,in:RoundedRectangle(cornerRadius:8))
    }
    func runwaySlot(_ slot:Int)->some View{
        let loop=s.currentPlacement(slot:slot+1).flatMap{id in s.project?.loops.first{$0.id==id}}
        return VStack(alignment:.leading,spacing:2){
            Menu{
                ForEach(s.project?.loops ?? []){l in Button("Place " + l.tag){s.placeLoop(l.id,slot:slot+1)}}
                Button("Clear placement"){s.clearSlot(slot+1)}
            } label:{Text("\(slot*16+1)–\((slot+1)*16)   " + (loop?.tag ?? "Place loop…"))}.frame(height:28)
            HStack(spacing:5){
                ForEach(0..<4,id:\.self){g in
                    VStack(spacing:2){HStack(spacing:1){ForEach(1...4,id:\.self){b in Text("\(slot*16+g*4+b)").font(.system(size:10,design:.monospaced)).frame(width:20,height:18)}}
                        ForEach(names.indices,id:\.self){t in HStack(spacing:1){ForEach(1...4,id:\.self){b in
                            Rectangle().fill(loop?.hasTrack(t,g*4+b)==true ? mint.opacity(0.78):Color.white.opacity(g%2==0 ? 0.10:0.16))
                                .frame(width:20,height:16)
                                .overlay{ if s.running && s.mode == .runway && s.runningBar==slot*16+g*4+b{Rectangle().strokeBorder(Color.orange,lineWidth:2)} }
                                .onTapGesture{
                                    if let l=loop {s.selectLoop(l.id);s.selectTrack(t);s.bar=g*4+b;s.allBars=false}
                                    else{s.placeLoop(s.loopID,slot:slot+1)}
                                }
                        }}}
                    }
                }
            }
        }
    }
    var loopStrip:some View {
        VStack(alignment:.leading,spacing:7){
            HStack{Text("Loop song").font(.title3.bold());Text(s.currentLoop?.tag ?? "").foregroundStyle(mint);Spacer()
                Button("Solo track"){s.start(.steps)};Button("Play loop song"){s.start(.song)}}
            HStack(spacing:5){ForEach(0..<4,id:\.self){g in HStack(spacing:2){ForEach(1...4,id:\.self){b in
                let n=g*4+b
                Button{ s.bar=n;s.allBars=false;s.selectedSteps=[] }label:{
                    VStack(spacing:2){Text("\(n)").font(.caption.monospacedDigit());HStack(spacing:1){ForEach(names.indices,id:\.self){t in
                        Capsule().fill(s.currentLoop?.hasTrack(t,n)==true ? mint : Color.white.opacity(0.12)).frame(height:4)
                    }}}.padding(4).frame(maxWidth:.infinity).background(s.bar==n ? Color.white.opacity(0.16):Color.clear)
                    .overlay{if s.running && s.mode == .song && s.runningBar==n{RoundedRectangle(cornerRadius:3).strokeBorder(.orange,lineWidth:2)}}
                }.buttonStyle(.plain)
            }}}}
        }.padding(12).background(panel,in:RoundedRectangle(cornerRadius:8))
    }
    var stepEditor:some View{
        VStack(alignment:.leading,spacing:8){
            HStack{
                Text("Steps").font(.title3.bold())
                Picker("Loop",selection:Binding(get:{s.loopID},set:{_ = s.selectLoop($0)})){
                    ForEach(s.project?.loops ?? []){l in Text(l.tag).tag(l.id)}
                }.fixedSize()
                Menu("Loop tools"){Button("Duplicate loop"){s.duplicateLoop()};Button("Import New Flow into "+names[s.track]){s.importMotif()};Button("Swap Heavyweight for Perfect Donk"){s.submitTranscript("swap Heavyweight for Perfect Donk")}}.fixedSize()
                Picker("Track",selection:Binding(get:{s.track},set:{s.selectTrack($0)})){ForEach(names.indices,id:\.self){i in Text(names[i]).tag(i)}}.fixedSize()
                Stepper("Bar \(s.bar)",value:$s.bar,in:1...16).fixedSize()
                Toggle("All bars",isOn:$s.allBars).toggleStyle(.checkbox)
                Spacer()
            }
            HStack(spacing:7){ForEach(0..<4,id:\.self){group in HStack(spacing:2){ForEach(1...4,id:\.self){s0 in stepButton(group*4+s0)}}}}
            HStack{
                Button("Eighths"){s.setSteps(Array(stride(from:1,through:16,by:2)))}
                Button("Kick groove"){s.setSteps([1,7,9,12])}
                Button("Clear steps"){s.setSteps([])}
                Spacer()
                Stepper(pitchName(rootMIDI(s.track)+s.editingPitch),value:$s.editingPitch,in:-24...24).fixedSize()
                Picker("Voicing",selection:$s.chord){ForEach(["Single","Major","Minor"],id:\.self){Text($0)}}.fixedSize()
                Toggle("Slide",isOn:$s.glide).toggleStyle(.checkbox)
                if s.glide{Stepper(pitchName(rootMIDI(s.track)+s.slidePitch),value:$s.slidePitch,in:-24...24).fixedSize()}
                Button("Apply"){s.applyNotes()}.disabled(s.selectedSteps.isEmpty)
                Button("Erase"){for n in s.selectedSteps{s.setNote(step:n,offsets:[],slide:nil)}}.disabled(s.selectedSteps.isEmpty)
            }.controlSize(.small)
        }.padding(12).background(panel,in:RoundedRectangle(cornerRadius:8))
    }
    func stepButton(_ n:Int)->some View{
        let h=s.pattern[n]
        let note=h.map{hit in let key=pitchName(rootMIDI(s.track)+(hit.offsets.first ?? 0));if let slide=hit.slide{return key+"↗"+pitchName(rootMIDI(s.track)+slide)};return key+(hit.offsets.count>1 ? (hit.offsets[1]-hit.offsets[0]==3 ? "m":"maj"):"")} ?? "·"
        return Button{s.tap(n)}label:{VStack(spacing:1){Text("\(n)").fontWeight(.semibold);Text(note).font(.system(size:10,design:.monospaced))}
            .frame(maxWidth:.infinity,minHeight:37)
            .foregroundStyle(h==nil ? Color.white:Color.black)
            .background(h==nil ? Color.white.opacity(0.10):mint,in:RoundedRectangle(cornerRadius:4))
            .overlay{RoundedRectangle(cornerRadius:4).strokeBorder(s.running && s.runningStep==n ? Color.orange : s.selectedSteps.contains(n) ? Color.white:.clear,lineWidth:2)}
        }.buttonStyle(.plain)
        .help("Click to toggle. Right-click a hit to edit its notes.")
        .contextMenu { if h != nil { Button("Edit notes"){s.selectHit(n)} } }
    }
    func mixValue(_ key:WritableKeyPath<Mix,Double>)->Binding<Double>{Binding(get:{s.currentMix[keyPath:key]},set:{var m=s.currentMix;m[keyPath:key]=$0;s.setMix(m)})}
    func mixFlag(_ key:WritableKeyPath<Mix,Bool>)->Binding<Bool>{Binding(get:{s.currentMix[keyPath:key]},set:{var m=s.currentMix;m[keyPath:key]=$0;s.setMix(m)})}
    func fader(_ label:String,_ key:WritableKeyPath<Mix,Double>,_ range:ClosedRange<Double>,_ suffix:String)->some View{
        VStack(alignment:.leading){Text(label+"  "+(suffix=="dB" ? String(format:"%.1f",s.currentMix[keyPath:key]):String(Int(s.currentMix[keyPath:key]*100)))+suffix).font(.caption);Slider(value:mixValue(key),in:range)}
    }
    var mixer:some View{
        VStack(alignment:.leading,spacing:7){
            HStack{Text("Mixer").font(.title3.bold());Text(names[s.track]).foregroundStyle(mint);Spacer()
                Toggle("Rack",isOn:mixFlag(\.rackOn)).toggleStyle(.checkbox)
                Toggle("Cut Itself",isOn:mixFlag(\.cutItself)).toggleStyle(.checkbox)
                Picker("Space",selection:Binding(get:{s.currentMix.hall ?? true},set:{var m=s.currentMix;m.hall=$0;s.setMix(m)})){
                    Text("Room").tag(false);Text("Hall").tag(true)
                }.fixedSize()
                Picker("Ducking",selection:Binding(get:{s.currentMix.sidechainFrom ?? ""},set:{var m=s.currentMix;m.sidechainFrom=$0.isEmpty ? nil:$0;s.setMix(m)})){
                    Text("Off").tag("")
                    ForEach(names.filter{$0 != names[s.track]},id:\.self){t in Text(t+" ducks "+names[s.track]).tag(t)}
                }.fixedSize()
            }
            HStack(spacing:18){fader("Channel",\.gainDB,-48...6,"dB");fader("Rack mix",\.rackMix,0...1,"%");fader("Echo",\.delayMix,0...1,"%");fader("Reverb",\.reverbMix,0...1,"%")}
        }.padding(12).background(panel,in:RoundedRectangle(cornerRadius:8))
    }
}
#if !RUNWAY_TEST
@main struct RunwayPreviewApp:App {
    @StateObject private var store=PreviewStore()
    var body:some Scene{Window("Lick-Lubin · Runway",id:"studio"){PreviewScreen(s:store)}.defaultSize(width:1150,height:820)}
}
#endif
