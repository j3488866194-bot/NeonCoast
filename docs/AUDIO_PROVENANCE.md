# Original audio for Neon Coast

All nine sound assets are generated locally by `tools/generate-audio.py` using
NumPy oscillators, shaped noise, envelopes and original note sequences. No audio
recordings, commercial songs, downloaded sample libraries or paid services are
used. The implementation extends the project's earlier procedural Godot audio
approach; both radio arrangements were synthesized afresh for this UE build.

The generator uses seed `721086` and writes PCM signed 16-bit WAVs at 32 kHz to
`artifacts/audio-src`. Its `manifest.json` records SHA-256 hashes, duration,
channels, peak, RMS and byte size. The generated sources total approximately
5.37 MiB. These are intentionally short procedural prototype tracks and effects,
not licensed broadcast music or recordings of real vehicles/weapons.

| Unreal asset | Duration | Channels | Loop | Purpose |
| --- | ---: | ---: | --- | --- |
| `/Game/Audio/SW_Radio_NightDrive` | 16 s | 2 | Yes | Original synthwave station, NIGHT DRIVE 98.3 |
| `/Game/Audio/SW_Radio_Coastline` | 16 s | 2 | Yes | Original softer coastal station, COASTLINE 104.7 |
| `/Game/Audio/SW_EngineLoop` | 2 s | 1 | Yes | Engine timbre, runtime speed changes pitch |
| `/Game/Audio/SW_CoastAmbience` | 16 s | 1 | Yes | Surf, city rumble and synthesized distant bird chirps |
| `/Game/Audio/SW_PoliceSiren` | 4 s | 1 | Yes | One spatialized nearest-patrol loop during pursuit |
| `/Game/Audio/SW_Horn` | 0.55 s | 1 | No | Car horn |
| `/Game/Audio/SW_Gunshot` | 0.30 s | 1 | No | Procedural firearm report |
| `/Game/Audio/SW_Reload` | 0.90 s | 1 | No | Mechanical reload clicks |
| `/Game/Audio/SW_Footstep` | 0.24 s | 1 | No | Ground-contact footstep |

## Build integration

1. Run host Python: `python tools/generate-audio.py` (requires NumPy).
2. Within UE editor Python, execute `build_audio_assets.py` with `runpy.run_path`
   and call its returned `build()` function. It validates source hashes and
   imports/reuses `/Game/Audio` SoundWaves, setting the UE 5.8 `looping` property.
3. Add `/Game/Audio` to `DirectoriesToAlwaysCook`, since runtime loads use asset
   paths. Generated source WAVs are not needed by the packaged game.
4. Call `UNCAudioSubsystem::PlayGunshot` and `PlayReload` from successful character
   actions; bind radio/horn to the subsystem's `CycleRadio` / `SoundHorn` methods.

The subsystem creates each persistent loop once, changes its gain/pitch or pause
state in place, and destroys components on world shutdown. Radio and engine are
audible only while driving. A patrol siren is selected from actual nearby police
vehicles and uses distance attenuation, rather than playing whenever a wanted
number is nonzero. Walking footsteps require movement on the ground. World pause
pauses game audio. Headless smoke tests and no-audio/null-render contexts disable
the subsystem and do not create audio components.

Source synthesis and file measurements can be verified without UE. Editor
import, runtime mixing and listening quality require the normal UE build/play
checks and are not proved by generating the WAVs alone.
