import AVFoundation
import Foundation
import Speech

// A single command is deliberately short. Stop accepting input after four seconds.
final class Dictation {
    let engine = AVAudioEngine()
    let request = SFSpeechAudioBufferRecognitionRequest()
    let recognizer = SFSpeechRecognizer(locale: Locale(identifier: "en-US"))
    var task: SFSpeechRecognitionTask?

    func fail(_ message: String) -> Never {
        FileHandle.standardError.write(Data((message + "\n").utf8))
        exit(1)
    }

    func start() {
        SFSpeechRecognizer.requestAuthorization { status in
            guard status == .authorized else { self.fail("Allow Speech Recognition for this terminal in System Settings.") }
            AVCaptureDevice.requestAccess(for: .audio) { allowed in
                guard allowed else { self.fail("Allow Microphone access for this terminal in System Settings.") }
                DispatchQueue.main.async { self.record() }
            }
        }
    }

    func record() {
        guard let recognizer = recognizer, recognizer.isAvailable, recognizer.supportsOnDeviceRecognition else {
            fail("On-device English speech recognition is unavailable on this Mac.")
        }
        request.contextualStrings = Array(CommandLine.arguments.dropFirst())
        request.requiresOnDeviceRecognition = true
        request.shouldReportPartialResults = false
        let input = engine.inputNode
        let format = input.outputFormat(forBus: 0)
        guard format.channelCount > 0, format.sampleRate > 0 else { fail("No microphone input available.") }
        input.installTap(onBus: 0, bufferSize: 1024, format: format) { buffer, _ in
            self.request.append(buffer)
        }
        task = recognizer.recognitionTask(with: request) { result, error in
            if let result = result, result.isFinal {
                print(result.bestTranscription.formattedString)
                exit(0)
            }
            if let error = error { self.fail(error.localizedDescription) }
        }
        do { try engine.start() } catch { fail(error.localizedDescription) }
        FileHandle.standardError.write(Data("Listening for four seconds. Speak now.\n".utf8))
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) {
            self.engine.stop()
            input.removeTap(onBus: 0)
            self.request.endAudio()
        }
        DispatchQueue.main.asyncAfter(deadline: .now() + 14) { self.fail("Speech recognition timed out.") }
    }
}

let dictation = Dictation()
dictation.start()
RunLoop.main.run()
