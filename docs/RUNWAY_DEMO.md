# Lick-Lubin demo delivery plan

Keep the current JUCE beat window usable during development. Ship the first clickable project workspace as a separate native Mac preview. Reviewers can use both windows without losing a beat. The preview reads the same curated Orchid manifest.

## Interaction contract

- New project opens a blank loop with a memorable generated project name. Loops have stable numbers and a five-letter name segment. Duplicate makes an independent variation.
- A loop is 16 bars. Each track in each bar can hold 16 steps. A step can hold multiple pitches or a bass note with a glide destination. The sample’s root is referenced to octave 3. Permit 24 semitones either side of that root. Musical coordinates start at 1. Translate explicitly zero-based coordinates once at the input boundary.
- Steps auditions the selected track. Loop Song plays all tracks. Switching the audition scope retains phase and the selected track. The metronome is independent. Pattern edits become audible at the next matching hit.
- Runway is 64 **musical bars**, grouped visually in fours. Four complete 16-bar loops fit. “Arrange loop 1 on the first, second, third and fourth” means the four loop **slots**, beginning at musical bars 1, 17, 33 and 49. Label both units on screen. An explicit musical bar is always a musical bar.
- Editing a placement preserves the current audition mode. Runway playback supports written extent, a bar range, and start through end, with repeat selectable. Manual scrolling remains in place during editing.
- Mixer settings belong to the project track and persist across modes. Prioritize output gain, rack bypass, overall wet mix, echo and reverb amounts, with source/target names for ducking.
- The final 16 bars are an ordinary arrangeable section available for the live rap.

## Delivery dependencies

| Clickable node | Foundation that can proceed now | Integration following UI feedback |
| --- | --- | --- |
| Home and loop picker | Project IDs, loop numbering, durable project data | Resume and switch projects |
| Steps and Loop Song | Step clock, note/chord/slide events, solo mask, click | Edit-during-play and microphone rehearsal |
| Runway | 64-bar bounds, independent loop copies, placement references | Range playback and transport transitions |
| Track mixer | Sample decode, note pitch, gain and effects | Compare wet/dry levels and tune playable defaults |

Build the UI and independent model/audio foundations in separate worktrees. Deliver a running preview before expanding into additional instruments. Record real hardware listening separately from offline and app checks. Treat each node as reviewable on its own. An update prompt should appear only when a replacement build actually exists and should save the open project before reloading.
