# JUCE Audio Proof — BDD Draft

**Status:** Draft derived from `docs/ORCHID_INTERACTIVE_DEMO.md`. Scenarios cover the first JUCE audio proof, not the later full interactive demo.

## Feature: Load the curated Clean kick

The standalone app loads one sample selected by the manifest and a local sample-root setting. Sample files stay outside the repository.

### Scenario: Load the manifest-selected sample

Given the embedded manifest has a `clean_kick` entry for the curated Clean kick WAV
And `ORCHID_SAMPLE_ROOT` points to a valid Orchid pack folder containing that file
When the application starts
Then the app reports that `clean_kick` is ready
And the user can start playback

### Scenario: Explain a missing sample root

Given `ORCHID_SAMPLE_ROOT` is unset or does not identify a folder
When the application starts
Then sample playback remains unavailable
And the status explains how the sample root is missing or invalid

### Scenario: Explain an invalid or missing manifest sample

Given the manifest is malformed, lacks `clean_kick`, or gives it no usable relative path
When the application starts
Then sample playback remains unavailable
And the status identifies a manifest problem

### Scenario: Reject a sample path outside the configured root

Given the manifest path resolves outside `ORCHID_SAMPLE_ROOT`
When the application tries to load `clean_kick`
Then the app does not use that sample for playback
And the status reports that the manifest path is invalid

### Scenario: Explain an unreadable WAV

Given the manifest-selected WAV is missing, unsupported, corrupt, or has invalid sample properties
When the application loads the sample
Then sample playback remains unavailable
And the status identifies the load failure

## Feature: Play one kick per quarter note

The proof plays the curated kick at a steady 102 BPM, independently of CoreS3 and Python.

### Scenario: Start with an immediate hit

Given `clean_kick` is ready
And an audio output device is ready
When the user presses Play
Then the user hears a kick as playback begins
And the app reports that playback is running

### Scenario: Repeat at 102 BPM

Given playback has started at 102 BPM
When playback continues
Then the user hears evenly spaced quarter-note kicks at 102 BPM

### Scenario: Do not play before readiness

Given the sample or audio output device is unavailable
When the user presses Play
Then playback does not start
And the application communicates the readiness problem

## Feature: Stop cleanly

Stop cancels upcoming hits and releases any currently sounding kick without leaving playback running.

### Scenario: Cancel pending hits on Stop

Given playback is running
When the user presses Stop
Then the app reports that playback has stopped
And the user hears no later quarter-note hits

### Scenario: Release an active hit on Stop

Given a kick is sounding when the user presses Stop
When the sound is released
Then it fades smoothly to silence without an audible click
And no sound remains stuck after playback stops

### Scenario: Play again after Stop

Given playback was stopped
When the user presses Play again
Then the user hears a new kick as playback begins
And subsequent kicks are evenly spaced at 102 BPM

### Scenario: Stop remains safe when already stopped

Given playback is stopped
When Stop is requested
Then the app continues to report that playback is stopped
And the user hears no kick

### Scenario: Resolve rapid Play and Stop actions

Given the user presses Play and Stop one or more times in quick succession
When the app responds to those actions
Then playback state and status follow the documented request-ordering policy

The expected playback state remains undecided: the demo plan does not say which action takes precedence when the user presses Play and Stop quickly.

## Feature: Handle audio-device lifecycle

The app must make audio-device availability and failures clear to the user.

### Scenario: Start without an available output device

Given no usable audio output device is available
When the application starts
Then the user cannot start playback
And status explains the device problem

### Scenario: Stop after device shutdown or error

Given playback is active
When the audio device stops or reports an error
Then the app reports that playback has stopped
And the user hears no further kicks
And status explains the audio-device error

## Verification boundary

See the TDD's Verification evidence section for the distinction between offline checks and target-device audition.
