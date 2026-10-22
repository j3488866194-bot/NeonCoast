# Third-party notices

## License scope

The repository's MIT license applies to original project material that the project
can license. It does not relicense Unreal Engine, Epic template content or adapted
Epic example portions, Renderpeople scan characters, Godot, fonts, or other
third-party components. Each retains
its applicable license and ownership. The UE vehicle assets are not original
procedural geometry covered by this project's MIT license.

The compiled Windows game has additional end-user terms in
[RELEASE_LICENSE.txt](docs/RELEASE_LICENSE.txt). Those terms do not restrict the
rights separately granted under MIT, CC0, OFL, or another component's license.
Publishing this project's source does not publish or relicense the proprietary
Unreal Engine source code or the editable Renderpeople character assets.

## Unreal Engine and Epic vehicle content

The UE version is developed with the locally installed Unreal Engine 5.8.4. Unreal
Engine is provided by Epic under its applicable Unreal Engine agreement, not the
MIT license. See the [official Unreal Engine EULA](https://www.unrealengine.com/eula/unreal).

`Unreal/Content/Vehicles/` contains 59 files copied from the installed engine's
`Templates/TemplateResources/Standard/Vehicles/Content`: SportsCar and OffroadCar
meshes, skeletal and physics assets, animation blueprints, control rigs, materials,
textures, and supplied FBX files. All 59 source/destination file pairs were compared
using SHA-256. The runtime uses the SportsCar content; copying the other shared
assets does not mean that a separate off-road vehicle is implemented in gameplay.

Wheel dimensions and baseline driveline settings in `NCCar.cpp` and
`NCVehicleWheels.cpp` were adapted from Epic's installed `TP_VehicleAdv` example.
That source carries: **Copyright Epic Games, Inc. All Rights Reserved.** These
portions retain Epic's applicable terms. Source locations and agreement references
are recorded in [EPIC_VEHICLES_PROVENANCE.md](docs/licenses/EPIC_VEHICLES_PROVENANCE.md).
The notice records provenance; it does not create a separate license grant.

Epic's EULA, rechecked on 2026-10-22, defines content from the engine installation's
Samples and Templates folders as Examples (section 1), and permits distribution
of Examples, including modifications, in source or object form to third parties
(section 5(b)). The 59 vehicle files and 27 Manny/Quinn dependency files were
rechecked against the local installation by SHA-256 and matched. They remain
Epic content, not MIT content. This permission does not extend to unrelated
engine source, Marketplace/Fab content, or Renderpeople models.

Neon Coast uses Unreal® Engine. Unreal® is a trademark or registered trademark of
Epic Games, Inc. in the United States of America and elsewhere.

Unreal® Engine, Copyright 1998 – 2026, Epic Games, Inc. All rights reserved.

Retain the packaged engine's NOTICES.txt and the applicable component licenses
from Engine/Source/ThirdParty/Licenses with distributed binaries. These notices
supplement this project's credits and do not confer rights in Epic trademarks.

The procedural city also uses meshes, materials and fonts supplied with Unreal
Engine, including `/Engine/BasicShapes`. Such engine resources retain their own
terms; generated placement code does not change their ownership.

## Poly Haven public environment models and PBR textures

The realism pass uses four Poly Haven model sets (modular urban apartments facade,
island_tree_02, painted wooden bench, street lamp) and six 2K PBR texture sets.
These are third-party **CC0** assets under [Poly Haven's license](https://polyhaven.com/license).
The source authors, exact official URLs, sizes, hashes, physical texture repeats,
and derived model handling are recorded in
[PUBLIC_ASSETS.md](docs/licenses/PUBLIC_ASSETS.md) and the retained source manifest.
The tree is a coastal broadleaf tree, not a palm. Source originals and derived
GLBs are reproducible local caches; imported game assets preserve the source UVs
and material slots. No claim is made that AI authored these scans or models.

## Renderpeople scan characters and Epic character animations

The realistic character upgrade uses **Eric Rigged 001** and **Carla Rigged 001**,
scanned character models and textures supplied by **Renderpeople GmbH** through
its [official free-model page](https://renderpeople.com/free-3d-people/).
They carry Renderpeople's proprietary free-model license, **not CC0 or MIT**.
Its [terms](https://renderpeople.com/general-terms-and-conditions/), version
13.12.2024, permit game use and restrict raw-model transfer and individual-file
access. This project keeps the FBX, textures and imported uncooked character
`.uasset` files private, and uses them only as integrated cooked game resources.
The source directory under `artifacts/` and generated
`Unreal/Content/RealisticCharacters/` are excluded by `.gitignore`.

The official terms were rechecked on 2026-10-22: section 2.5 applies the same
license provisions to free downloads; section 4.1(b) covers commercial and
private games; section 4.2(c) excludes games from its application-use restriction.
Section 4.3(b) still requires technical handling that does not make individual
model files easily accessible. Public binary releases containing these scans
must therefore use encrypted cooked asset containers and encrypted indexes,
exclude raw and editable assets, and exclude key configuration and metadata.
Cooking alone is not evidence of this protection. Release validation must inspect
the actual containers and confirm the game can load them; it must not claim
encryption makes extraction impossible. These are project release requirements,
not a representation that Renderpeople has separately approved this project.

Idle, walk, forward jog (the runtime run slot) and jump motions for these scans
come from Epic's installed **Manny/Quinn template assets**, retargeted using the
Manny Simple mesh and shared mannequin skeleton. The source is Unreal Engine
5.8.4's `Templates/TemplateResources/High/Characters/Content/Mannequins`, retained
under `/Game/Characters/Mannequins`. These Epic assets retain the applicable
[Unreal Engine EULA](https://www.unrealengine.com/eula/unreal); they are not
Renderpeople motion captures or project-original animations. Their license does
not replace the restrictions on the scan models.

Direct official URLs, archive hashes, source clips and handling details are in
[RENDERPEOPLE.md](docs/licenses/RENDERPEOPLE.md). This notice records provenance,
not successful import, animation quality or final package acceptance.

## Earlier Kenney human characters

The earlier UE human mesh, idle/run/jump animations and three skins originate from Kenney's
[Animated Characters Protagonists 1.1](https://kenney.nl/assets/animated-characters-protagonists),
distributed under CC0 1.0. Original FBX/PNG sources and the unmodified license notice
are preserved in `Unreal/ArtSource/Characters/`. Imported Unreal assets are under
`Unreal/Content/Characters/`: one shared mesh and skeleton, three animations, three
textures and three generated materials. The unused cyborg skin was not copied.

The UE commandlet successfully imported all 11 assets. It reconstructed the FBX
bind pose and reported missing bind-pose bones and smoothing-group warnings;
rendered animation quality must be assessed separately. The original pack has no
separate walk, aim, reload or death clips. Source URLs, archive hash, conversion
measurements and limitations are recorded in
[KENNEY_CHARACTERS.md](docs/licenses/KENNEY_CHARACTERS.md). These are Kenney assets,
not character models authored by Codex or Opus.

## Godot version and fonts

Godot Engine 4.7.2 is distributed under the MIT license. Copyright (c) 2007-present
Juan Linietsky, Ariel Manzur and Godot Engine contributors. Full license and bundled
third-party components: [Godot license](https://godotengine.org/license/) and
[Godot 4.7.2 COPYRIGHT.txt](https://github.com/godotengine/godot/blob/4.7.2-stable/COPYRIGHT.txt).

Barlow Condensed by Jeremy Tribby and Noto Sans SC by the Noto Project are bundled
with the original Godot project under SIL Open Font License 1.1. Their original OFL
notices are included in `assets/fonts`. Sources:
[Barlow Condensed](https://github.com/google/fonts/tree/main/ofl/barlowcondensed)
and [Noto Sans SC](https://github.com/google/fonts/tree/main/ofl/notosanssc).
This does not claim that those font files are currently used by the UE Canvas HUD.

## Original code, audio and model contributions

Original environment-generation code, game systems, interface implementation and
procedural soundtrack compositions are included in this project. UE audio is
deterministically synthesized with NumPy; it contains no third-party recordings,
commercial music or downloaded sound samples. See
[AUDIO_PROVENANCE.md](docs/AUDIO_PROVENANCE.md) for asset paths and reproducibility.

The original Godot map interface was generated by Claude Opus 5.5 high; earlier
full-HUD requests did not produce a usable result. Those are historical Godot
contributions, separate from the current UE work.

For UE, Claude Opus 5.5 high authored the city, HUD and material builder across two
passes. The second pass wrote its five target files and then hit a session limit;
Codex performed API adaptation, gameplay integration, compilation and runtime checks.
See [first receipt](docs/UE_OPUS_RECEIPT.json) and
[second receipt](docs/UE_OPUS_SECOND_PASS_RECEIPT.json). A model contribution is not
an endorsement by the model provider.

In a third completed pass, Opus 5.5 high reviewed the Kenney character preview and
an actual intermediate UE screenshot, then selected the human skins, 178 cm height,
texture filtering and material parameters. See the
[character art direction](docs/UE_CHARACTER_ART_DIRECTION.json) and
[third receipt](docs/UE_CHARACTER_OPUS_RECEIPT.json). Codex implemented the asset
import and runtime integration; the art-direction receipt does not by itself
confirm rendered skeletal animation or final package acceptance.

Opus 5.5 high subsequently authored the mesh-based world markers and collectible
coins, then reviewed an actual rendered frame to improve dusk shadow readability.
Both passes completed successfully; their file hashes and scopes are recorded in
[marker receipt](docs/UE_MARKERS_OPUS_RECEIPT.json) and
[dusk receipt](docs/UE_DUSK_OPUS_RECEIPT.json). Codex performed integration and
runtime verification. Current rendered character, driving and map images are in
`docs/images/ue/`; package acceptance is tracked separately in
[UE_VERIFICATION.md](docs/UE_VERIFICATION.md).

For the current realism pass, Opus 5.5 high supplied the visual direction, PBR and
scan material builders, public-model city placement and lighting, an initial
render review, a visual review after resuming work, and the final thin amber
objective-marker changes. These six actual CLI calls are recorded in
[UE_REALISM_OPUS_RECEIPTS.json](docs/UE_REALISM_OPUS_RECEIPTS.json). The two visual
reviews identify their input stills; their estimated colours, technical hypotheses
and proposed fixes are not evidence of final package acceptance. Codex handled
asset preparation, API fixes, animation retargeting, geometric alignment,
compilation and packaged testing. The earlier driving/map screenshots still show
the previous character pass; the current packaged image is identified explicitly
in [UE_VERIFICATION.md](docs/UE_VERIFICATION.md).

Neon Coast is an independent prototype. It is not GTA VI, is not affiliated with
Rockstar Games, and contains no Rockstar code, story characters, logos, music or
leaked assets. GTA and Rockstar names belong to their respective owners.
