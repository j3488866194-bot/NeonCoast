# Renderpeople characters and Epic animation provenance

Recorded on 2026-10-21. The realistic character upgrade uses scanned **Eric Rigged 001** and **Carla Rigged 001** models and textures from **Renderpeople GmbH**. The locomotion clips are retargeted from **Epic Games' Unreal Engine Manny/Quinn template content**. Neither the scans nor those source animations were authored by Codex or Opus.

## Official downloads and verification

The [Renderpeople free-model page](https://renderpeople.com/free-3d-people/) supplies these downloads without registration. Each downloaded ZIP passed a full CRC check before extraction.

| Character | Official FBX download | Archive bytes | SHA-256 |
| --- | --- | ---: | --- |
| Eric Rigged 001 | [rp_eric_rigged_001_FBX.zip](https://renderpeople.com/sample/free/rp_eric_rigged_001_FBX.zip) | 45,390,786 | `55b763ebea36cc8747a57a26cc46252fb0fa195c92ef87a12f0cf42f23264383` |
| Carla Rigged 001 | [rp_carla_rigged_001_FBX.zip](https://renderpeople.com/sample/free/rp_carla_rigged_001_FBX.zip) | 178,888,446 | `34ea79ba21470aea5e6db828cefa5ed14f98f6bb040bf0ff1193473ad5e0bf3e` |

Total archive download: **224,279,232 bytes**. Acquisition times in UTC were 2026-10-21T15:33:46Z and 2026-10-21T15:35:36Z respectively.

Local evidence is retained in `artifacts/realism/renderpeople/`: `eric-source.json`, `carla-source.json`, `free-models.html`, `terms.html` and `LICENSE-NOTES.txt`. The JSON files include the original URL, archive hash, per-entry CRC values and file sizes. The HTML files preserve the official pages read during acquisition.

The selected inputs are `eric/rp_eric_rigged_001_ue4.fbx` and `carla/rp_carla_rigged_001_ue4.fbx`, with each character's `tex/` diffuse, normal, gloss and mask images. Both FBX download packages already include their UE4 skeleton variant. A range-only inspection of the separate official UE4 archive found mannequin/skeleton, physics, mesh and material assets, but no animation sequences; that large archive was not downloaded.

## Renderpeople license and project handling

These are **free licensed assets under Renderpeople's proprietary terms, not CC0, public-domain or MIT assets**. The [official General Terms and Conditions](https://renderpeople.com/general-terms-and-conditions/), version **13.12.2024**, apply the paid-model provisions to free downloads in section 2.5. Section 4.1(b) permits private and commercial video-game use. Sections 4.2(b) and 4.3(b) restrict transfer of model data and access to individual asset files, including modified data.

This project keeps the downloaded FBX/texture sources and imported, uncooked `.uasset` character resources private. It permits their inclusion only as integrated cooked game resources subject to those terms, never as standalone models, texture packs or an editable character-asset library. Conversion or retargeting does not replace the Renderpeople license.

The repository's existing `.gitignore` excludes `artifacts/`, `Unreal/ArtSource/Renderpeople/` and `Unreal/Content/RealisticCharacters/`. The latter is the disk location corresponding to `/Game/RealisticCharacters`. Ignore matching was checked for the source JSON and an Eric asset path. These rules implement the project's source-handling policy; they do not themselves grant distribution rights.

## Epic Manny/Quinn animation dependencies

Source installation: Unreal Engine **5.8.4**, changelist **58902052**.

`D:\Work_software\UnrealEngine\UE_5.8\Templates\TemplateResources\High\Characters\Content\Mannequins`

The import script, `Unreal/Content/Python/build_realistic_characters.py`, preserves the source package root `/Game/Characters/Mannequins` and uses `Meshes/SKM_Manny_Simple` with the shared mannequin skeleton for retargeting. The local `mannequin-source-manifest.json` records **27 copied dependency files, 84,756,481 bytes**, including their installed paths and SHA-256 values.

| Runtime motion | Epic source below `/Game/Characters/Mannequins/` |
| --- | --- |
| Idle | `Anims/Unarmed/MM_Idle` |
| Walk | `Anims/Unarmed/Walk/MF_Unarmed_Walk_Fwd` |
| Run | `Anims/Unarmed/Jog/MF_Unarmed_Jog_Fwd` |
| Jump | `Anims/Unarmed/Jump/MM_Jump` |

The pipeline targets `/Game/RealisticCharacters/Eric/SK_Eric` and `/Game/RealisticCharacters/Carla/SK_Carla`, with each character's retargeted clips in `Animations/A_<Name>_<Motion>`. The run slot uses Epic's forward **jog** clip; it is not an additional Renderpeople running motion. Import and rendered acceptance are recorded separately from this provenance document.

Epic retains ownership of its template meshes, skeletons, animations and dependencies. The [Unreal Engine EULA](https://www.unrealengine.com/eula/unreal), checked on 2026-10-21, defines installation Templates content as Examples in section 1. Sections 2, 4 and 5(b) govern its use/modification, game distribution and Example distribution respectively. The project's MIT license does not relicense these assets, and Epic's permissions do not expand the Renderpeople model license. This record is an attribution and provenance summary, not a separate license grant.
