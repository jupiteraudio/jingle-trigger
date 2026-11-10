# Jingle Trigger (VST3)

16-slot file player for LiveProfessor (or any VST3 host). Each slot holds one audio
file (wav/aiff/flac/ogg/mp3, mono or stereo, any sample rate) and plays it on command.

## Triggering
- **MIDI**: slot 1 = note 36 (C1), slot 2 = 37 ... slot 16 = 51. Base note is adjustable.
  Note-off stops Gate and Loop slots. All-notes-off / all-sound-off stops everything.
- **Parameters** (automatable / usable from cues): `Trigger 1..16` (0 to 1 = start,
  1 to 0 = release for Gate/Loop), `Stop All`, `Gain 1..16`, `Mode 1..16`, `Master Gain`.
- **GUI**: Play/Stop button per slot, Stop All, drag & drop a file onto a row.

## Modes
One-shot (plays to end, note-off ignored), Gate (stops on note-off), Loop (loops until
note-off), Toggle (each trigger starts/stops). Retrigger crossfades (3 ms in / 20 ms out).

## CPU
Files are decoded into RAM on load (no disk streaming). When nothing is playing the audio
callback only does 16 atomic reads and returns. No allocation, no blocking locks in audio.
GUI timer runs only while the plugin window is open.

## Build (Windows)
Install Visual Studio 2022 Build Tools (C++ workload) + CMake, then:

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

Result: `build\JingleTrigger_artefacts\Release\VST3\Jingle Trigger.vst3`
Copy it to `C:\Program Files\Common Files\VST3\` and rescan in LiveProfessor.
(Or push to GitHub: .github/workflows/build.yml builds it and attaches the VST3 as an artifact.)
Static runtime: no VC++ redistributable needed.

Audio file paths are stored absolutely in the project; missing files show in red.
