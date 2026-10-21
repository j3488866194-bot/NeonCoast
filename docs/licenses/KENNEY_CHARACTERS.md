# Kenney Animated Characters Protagonists

The human character mesh, skeletal animations and three character textures in
`Unreal/ArtSource/Characters` are from **Animated Characters Protagonists 1.1**,
created and distributed by **Kenney**.

- Official pack: https://kenney.nl/assets/animated-characters-protagonists
- Official archive: https://kenney.nl/media/pages/assets/animated-characters-protagonists/608191acc4-1774773108/kenney_animated-characters-protagonists.zip
- License: Creative Commons Zero 1.0, https://creativecommons.org/publicdomain/zero/1.0/
- The original archive notice is preserved verbatim in `Unreal/ArtSource/Characters/License.txt`.
- Archive SHA-256: `ec3787de70fa2200256848d74201b10f6b6c3126594e9857bf989753312c2b84`.

Included sources are `Model/characterMedium.fbx`, `Animations/idle.fbx`,
`Animations/run.fbx`, `Animations/jump.fbx`, and the `criminalMaleA`, `skaterMaleA`
and `skaterFemaleA` PNG skins. The cyborg skin is not included. CC0 permits use,
modification and redistribution; credit is appreciated by the creator but not
required. This notice records provenance and does not claim authorship by Codex
or Opus.

`docs/UE_CHARACTER_ART_DIRECTION.json` contains the visual choices made by the
requested Opus 5.5 model. The import script creates the Unreal assets, three
opaque textured materials and an import report. The archive supplies idle, run
and jump motions; it does not contain a separate walking, aiming, reloading or
death animation. Each animation FBX also contains a one-frame targeting pose,
which the importer excludes when selecting the named game motion.

## Unreal import verification — 2026-10-21

The UE 5.8 Python commandlet completed the import and emitted
`NEON_COAST_CHARACTERS_READY`. The log is
`artifacts/ue-characters-import.log`; measured asset metadata is recorded in
`Unreal/Saved/CharacterImportReport.json` and `artifacts/ue-character-assets.json`.
All 11 generated `.uasset` files exist: one skeletal mesh, one shared skeleton,
three animation sequences, three textures and three materials. The skeleton has
59 bones. The selected motion durations are Idle 1.066667 seconds, Run 0.666667
seconds and Jump 0.5 seconds; the targeting poses were excluded.

The imported bounds have a height of 376.468611 cm. A uniform scale of
0.472814984 (approximately 0.4728) gives the Opus-directed 178 cm character
height. Runtime feet alignment and facing direction must be checked in the
integrated game; import success alone does not verify rendered animation quality.

The source FBX produced warnings about an invalid original bind pose, missing
bind-pose bones and absent smoothing groups. Unreal reported that bind-pose
reconstruction succeeded and saved the assets. These warnings did not prevent
import, but visible deformation, facing and shading still require in-game review.
The converted Unreal assets retain the source CC0 provenance above.
