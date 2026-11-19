import AVFoundation
import Foundation
import Synchronization

// Main/UI is the sole producer. Each track has one audio-thread consumer.
// Voices and command storage have fixed capacity for the lifetime of a render node.
private enum RenderAction { case hit, release, duck }
private struct SampleEvent {
    var action: RenderAction = .hit
    var startRate = 1.0
    var endRate = 1.0
    var strength: Float = 1
}
private struct SampleVoice {
    var position = 0.0
    var rate = 1.0
    var slideFactor = 1.0
    var slideFrames = 0
    var releaseFrames = -1
    var strength: Float = 1
}

private final class WaveRenderer {
    static let rate = 44100.0
    private let buffer: AVAudioPCMBuffer
    private let left: UnsafePointer<Float>
    private let right: UnsafePointer<Float>
    private let length: Int
    private let read = Atomic<Int>(0)
    private let written = Atomic<Int>(0)
    private let queue = UnsafeMutablePointer<SampleEvent>.allocate(capacity: 256)
    private let voices = UnsafeMutablePointer<SampleVoice>.allocate(capacity: 64)
    private let gainBits = Atomic<UInt32>(Float(1).bitPattern)
    private var active = 0
    private var duckFrame = 0
    private var duckAmount: Float = 0
    private var outputGain: Float = 1
    let fileRatio: Double

    init(_ buffer: AVAudioPCMBuffer) {
        self.buffer = buffer
        let channels = buffer.floatChannelData!
        left = UnsafePointer(channels[0])
        right = UnsafePointer(channels[buffer.format.channelCount > 1 ? 1 : 0])
        length = Int(buffer.frameLength)
        fileRatio = buffer.format.sampleRate / Self.rate
        queue.initialize(repeating: SampleEvent(), count: 256)
        voices.initialize(repeating: SampleVoice(), count: 64)
    }

    deinit {
        queue.deinitialize(count: 256)
        queue.deallocate()
        voices.deinitialize(count: 64)
        voices.deallocate()
    }

    func setGain(_ gain: Float) { gainBits.store(gain.bitPattern, ordering: .relaxed) }

    func enqueue(_ command: SampleEvent) {
        let tail = written.load(ordering: .relaxed)
        let next = (tail + 1) & 255
        guard next != read.load(ordering: .acquiring) else { return }
        queue[tail] = command
        written.store(next, ordering: .releasing)
    }

    private func drain() {
        var head = read.load(ordering: .relaxed)
        let tail = written.load(ordering: .acquiring)
        while head != tail {
            let command = queue[head]
            head = (head + 1) & 255
            switch command.action {
            case .duck:
                duckFrame = 5512
                duckAmount = command.strength
            case .release:
                for i in 0..<active where voices[i].releaseFrames < 0 { voices[i].releaseFrames = 441 }
            case .hit:
                // Prefer new notes if rapid chords exhaust the fixed voice pool.
                if active == 64 {
                    var oldest = 0
                    for i in 1..<active where voices[i].position > voices[oldest].position { oldest = i }
                    voices[oldest] = voices[active-1]
                    active -= 1
                }
                let frames = command.endRate == command.startRate ? 0 : 6485 // one 16th at 102 BPM
                let factor = frames > 0 ? pow(command.endRate / command.startRate, 1.0 / Double(frames)) : 1
                voices[active] = SampleVoice(position: 0, rate: command.startRate, slideFactor: factor,
                                             slideFrames: frames, releaseFrames: -1, strength: command.strength)
                active += 1
            }
        }
        read.store(head, ordering: .releasing)
    }

    func render(_ count: Int, _ list: UnsafeMutablePointer<AudioBufferList>) {
        let buffers = UnsafeMutableAudioBufferListPointer(list)
        guard buffers.count == 2, let l = buffers[0].mData?.assumingMemoryBound(to: Float.self),
              let r = buffers[1].mData?.assumingMemoryBound(to: Float.self) else { return }
        drain()
        let targetGain = Float(bitPattern: gainBits.load(ordering: .relaxed))
        for frame in 0..<count {
            var a: Float = 0, b: Float = 0
            var i = 0
            while i < active {
                var v = voices[i]
                let p = Int(v.position)
                let q = min(length-1,p+1)
                let fraction = Float(v.position-Double(p))
                let level = v.strength * (v.releaseFrames < 0 ? 1 : Float(v.releaseFrames)/441)
                a += (left[p] + fraction*(left[q]-left[p]))*level
                b += (right[p] + fraction*(right[q]-right[p]))*level
                v.position += v.rate
                if v.slideFrames > 0 { v.rate *= v.slideFactor; v.slideFrames -= 1 }
                if v.releaseFrames > 0 { v.releaseFrames -= 1 }
                if v.position >= Double(length) || v.releaseFrames == 0 {
                    active -= 1
                    voices[i] = voices[active]
                } else {
                    voices[i] = v
                    i += 1
                }
            }
            var duckGain: Float = 1
            if duckFrame > 0 {
                duckGain -= duckAmount * min(Float(5512-duckFrame)/220,Float(duckFrame)/5292)
                duckFrame -= 1
            }
            outputGain += (targetGain-outputGain)*0.01
            l[frame] = a * outputGain * duckGain
            r[frame] = b * outputGain * duckGain
        }
    }
}

private struct PreviewManifest: Decodable {
    struct Sample: Decodable { let id: String; let alias: String; let relative_path: String }
    let samples: [Sample]
}

final class OrchidPreviewAudio {
    private struct Channel {
        let wave: WaveRenderer
        let delay: AVAudioUnitDelay
        let reverb: AVAudioUnitReverb
        var cut = true
        var hall = true
        var rackOn = true
        var rackMix: Double = 1
        var source: String?
    }
    private var engine = AVAudioEngine()
    private let format = AVAudioFormat(standardFormatWithSampleRate: WaveRenderer.rate, channels: 2)!
    private var channels: [String: Channel] = [:]
    private var aliases: [String: String] = [:]
    private var tick: WaveRenderer?
    private var status = ""
    private var routeObserver:NSObjectProtocol?
    var routeFailure:((String)->Void)?
    deinit { if let routeObserver { NotificationCenter.default.removeObserver(routeObserver) } }

    func resumeOutput() throws {
        guard tick != nil else {
            throw NSError(domain:"OrchidAudio",code:1,userInfo:[NSLocalizedDescriptionKey:"Audio graph is unavailable. Check the bundled manifest and relaunch Runway."])
        }
        if !engine.isRunning { try engine.start() }
    }

    @discardableResult
    func prepare(manifestURL: URL? = nil) -> String {
        if engine.isRunning { return status }
        if let routeObserver { NotificationCenter.default.removeObserver(routeObserver); self.routeObserver=nil }
        engine.stop()
        engine = AVAudioEngine()
        aliases.removeAll()
        channels.removeAll()
        tick = nil
        let root = ProcessInfo.processInfo.environment["ORCHID_SAMPLE_ROOT"].map { URL(fileURLWithPath:$0) }
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Music/Lick-Lubin/Orchid Sample Pack")
        do {
            guard let manifest = manifestURL ?? Bundle.main.url(forResource: "manifest", withExtension: "json") else {
                return "Orchid manifest is missing."
            }
            let entries = try JSONDecoder().decode(PreviewManifest.self, from: Data(contentsOf: manifest)).samples
            let base = root.standardizedFileURL.resolvingSymlinksInPath().pathComponents
            for sample in entries {
                let file = root.appendingPathComponent(sample.relative_path).standardizedFileURL.resolvingSymlinksInPath()
                guard file.pathComponents.starts(with:base), file.pathComponents.count > base.count,
                      let wav = try? AVAudioFile(forReading: file, commonFormat: .pcmFormatFloat32, interleaved: false),
                      wav.processingFormat.channelCount <= 2, wav.length > 0,
                      wav.length <= Int64(wav.processingFormat.sampleRate*120),
                      let pcm = AVAudioPCMBuffer(pcmFormat:wav.processingFormat,frameCapacity:AVAudioFrameCount(wav.length)),
                      (try? wav.read(into: pcm)) != nil, pcm.frameLength > 0 else { continue }
                let wave = WaveRenderer(pcm)
                let source = sourceNode(wave)
                let delay = AVAudioUnitDelay()
                delay.delayTime = 60.0/102/2
                delay.feedback = 28
                delay.wetDryMix = 0
                let verb = AVAudioUnitReverb()
                verb.loadFactoryPreset(.mediumHall)
                verb.wetDryMix = 0
                engine.attach(delay)
                engine.attach(verb)
                engine.connect(source,to:delay,format:format)
                engine.connect(delay,to:verb,format:format)
                engine.connect(verb,to:engine.mainMixerNode,format:format)
                aliases[sample.alias.lowercased()] = sample.id
                aliases[sample.id.lowercased()] = sample.id
                channels[sample.id] = Channel(wave:wave,delay:delay,reverb:verb)
            }
            let count = 1764
            let beat = AVAudioPCMBuffer(pcmFormat:format,frameCapacity:AVAudioFrameCount(count))!
            beat.frameLength = AVAudioFrameCount(count)
            for i in 0..<count {
                let t = Double(i)/WaveRenderer.rate
                let sample = Float(0.35*sin(2*Double.pi*1100*t)*exp(-150*t))
                for ch in 0..<2 { beat.floatChannelData![ch][i] = sample }
            }
            let clickWave = WaveRenderer(beat)
            tick = clickWave
            engine.connect(sourceNode(clickWave),to:engine.mainMixerNode,format:format)
            engine.mainMixerNode.outputVolume = 0.65
            engine.prepare()
            try engine.start()
            routeObserver=NotificationCenter.default.addObserver(forName:.AVAudioEngineConfigurationChange,object:engine,queue:.main){ [weak self] _ in
                guard let self else{return}
                do {try self.resumeOutput()} catch { self.routeFailure?("Audio output: "+error.localizedDescription) }
            }
            status = "\(channels.count)/\(entries.count) Orchid samples loaded"
            if channels.count != entries.count { status += " · Check ORCHID_SAMPLE_ROOT" }
        } catch { status = "Audio unavailable: \(error.localizedDescription)" }
        return status
    }

    private func sourceNode(_ wave: WaveRenderer) -> AVAudioSourceNode {
        let node = AVAudioSourceNode(format:format) { _, _, count, list in
            wave.render(Int(count), list)
            return noErr
        }
        engine.attach(node)
        return node
    }

    func hit(track: String, notes: [Int] = [0], slideTo: Int? = nil) {
        guard let id = aliases[track.lowercased()], let channel = channels[id], !notes.isEmpty else { return }
        if channel.rackOn && channel.cut { channel.wave.enqueue(SampleEvent(action:.release)) }
        for note in notes.prefix(12) {
            let rate = channel.wave.fileRatio * pow(2,Double(max(-24,min(24,note)))/12)
            let end = channel.wave.fileRatio * pow(2,Double(max(-24,min(24,slideTo ?? note)))/12)
            channel.wave.enqueue(SampleEvent(action:.hit,startRate:rate,endRate:end,strength:0.35))
        }
        for target in channels.values where target.rackOn && target.source == id {
            target.wave.enqueue(SampleEvent(action:.duck,strength:Float(target.rackMix)*0.55))
        }
    }
    func click(accent: Bool) {
        let rate = accent ? 1.45 : 1
        tick?.enqueue(SampleEvent(action:.hit,startRate:rate,endRate:rate,strength:accent ? 0.85 : 0.58))
    }
    func stop() {
        for channel in channels.values { channel.wave.enqueue(SampleEvent(action:.release)) }
        tick?.enqueue(SampleEvent(action:.release))
    }
    func mix(track: String, gainDB: Double, rackOn: Bool, rackMix: Double, delayMix: Double,
             reverbMix: Double, cutItself: Bool, sidechainFrom: String?, hall: Bool = true) {
        guard let id = aliases[track.lowercased()], var channel = channels[id] else { return }
        channel.wave.setGain(Float(pow(10,max(-72,min(12,gainDB))/20)))
        if channel.hall != hall { channel.reverb.loadFactoryPreset(hall ? .mediumHall : .mediumRoom);channel.hall=hall }
        channel.cut = cutItself
        channel.rackOn = rackOn
        channel.rackMix = max(0,min(1,rackMix))
        channel.source = sidechainFrom.flatMap { aliases[$0.lowercased()] }.flatMap { $0 == id ? nil : $0 }
        let wet = Float(rackOn ? channel.rackMix : 0)*100
        channel.delay.wetDryMix = wet * Float(max(0,min(1,delayMix)))
        channel.reverb.wetDryMix = wet * Float(max(0,min(1,reverbMix)))
        channels[id] = channel
    }
}
