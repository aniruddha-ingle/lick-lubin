# Runway Studio

The Mac preview opens a separate project window and uses the ten curated Orchid samples. Projects autosave in `~/Library/Application Support/Lick-Lubin`. Start another project to archive the current one. Reopen an archive from Home. The running JUCE proof can stay open.

From this directory on macOS 15 or later, with Swift 6 and Xcode Command Line Tools:

```sh
./build.sh "$HOME/Applications/Lick-Lubin Runway.app"
open "$HOME/Applications/Lick-Lubin Runway.app"
```

The default pack location is `~/Music/Lick-Lubin/Orchid Sample Pack`. Set `ORCHID_SAMPLE_ROOT` for a different pack. The local microphone requires on-device English speech recognition and microphone permission. You can also type each phrase and press Return.

## Rehearse a beat

1. From Home, say **open a new Lick-Lubin project**.
2. **Open Closed in steps view and start looping with metronome**. Then **play Closed on every eighth note**.
3. **Loop this in song mode**, **edit Clean in steps view**, **syncopated 4/4 groove**. Both tracks now play.
4. **Edit Heavyweight in loop track mode**, **steps 1 and 11**, **put G3 on step 1**, **on 11 can I get a 808 slide from G3 to G4**. Selected notes can move up or down by semitones. **Put Gmaj on step 1** writes a chord. Use **Import New Flow** on a pitched track to import the four-bar motif.
5. **Play in song loop mode**. **Arrange loop 1 in slots 1, 2, 3 and 4**. Each slot spans 16 musical bars. The loop keeps playing while you arrange.
6. **Duplicate loop**, **swap Heavyweight for Perfect Donk**, then **replace loop 1 on bars 2 and 4 with loop 2**. In this placement command, 2 and 4 refer to whole loop slots. Their starting musical bars are 17 and 49.
7. **Loop the written part of the runway**, **loop runway bars 49 through 64**, or **play runway from bar 49**. Manual scrolling preserves the editing position.
8. Try **sidechain Clean to Heavyweight**, **add echo to Bunker**, **set rack mix for Pop to 50 percent**, or **set Heavyweight gain to -6 dB**. Mixer settings apply across playback modes.

The current workspace uses a UI timer for the preview sequencer and an AVAudioEngine render node for sample playback. Validate timing under load and audition hardware before a stage performance. Chords and slides use the samples as pitched instruments. MIDI import quantizes note starts to sixteenths and triggers one-shots. No plugin host is required.

## Verification

`Tests/run.sh` runs a producer-story rehearsal in temporary project storage. The fixture test in `Tests/PreviewMIDITests.swift` covers note and chord import. Local speech transcription and physical listening are separate from scripted checks.
