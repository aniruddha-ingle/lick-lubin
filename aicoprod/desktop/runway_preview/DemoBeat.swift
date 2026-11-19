import Foundation

extension Project {
    // Musical steps and bars use the same one-based numbering as the editor.
    mutating func arrangeDemoBeat() {
        let clean = names.firstIndex(of: "Clean")!, closed = names.firstIndex(of: "Closed")!
        let pop = names.firstIndex(of: "Pop")!, bass = names.firstIndex(of: "Heavyweight")!
        let donk = names.firstIndex(of: "Perfect Donk")!, roll = names.firstIndex(of: "Roll")!
        let rim = names.firstIndex(of: "Bunker")!
        func hits(_ steps:[Int]) -> [Int:Hit] {
            Dictionary(uniqueKeysWithValues:steps.map { ($0,Hit()) })
        }
        loops = (1...4).map { section in
            var loop = BeatLoop(id:section)
            loop.patterns["\(closed):all"] = hits(section == 3 ? [3,7,11,15] : [1,3,5,7,9,11,13,15])
            loop.patterns["\(clean):all"] = hits(section == 1 ? [1,11] : [1,7,9,12])
            loop.patterns["\(pop):all"] = hits(section == 1 ? [13] : [5,13])
            if section > 1 {
                let track = section == 4 ? donk : bass
                // F2 and Ab2 against the Clean kick's F root.
                loop.patterns["\(track):all"] = [1:Hit(offsets:[-7]),11:Hit(offsets:[-4])]
                loop.patterns["\(track):4"] = [1:Hit(offsets:[-7]),11:Hit(offsets:[-7],slide:5)]
                loop.patterns["\(roll):16"] = hits([13])
            }
            if section == 4 {
                for bar in [4,8,12,16] { loop.patterns["\(rim):\(bar)"] = hits([12,15]) }
                loop.patterns["\(closed):16"] = hits([1,3,5,7,9])
            }
            return loop
        }
        slots = loops.map { Optional($0.id) }
        mixes[bass].gainDB = -4; mixes[bass].sidechainFrom = "Clean"
        mixes[donk].gainDB = -4; mixes[donk].sidechainFrom = "Clean"
        mixes[closed].gainDB = -8; mixes[pop].gainDB = -4
        mixes[rim].delayMix = 0.2; mixes[rim].gainDB = -5
        mixes[pop].reverbMix = 0.12; mixes[pop].hall = false
    }
}
