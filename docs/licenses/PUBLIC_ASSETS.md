# Public assets used by Neon Coast

Powered by [Poly Haven](https://polyhaven.com/). These source assets are free and public domain under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).

Poly Haven permits commercial use, modification and redistribution of its assets. Its [asset license](https://polyhaven.com/license) is distinct from the [API access terms](https://github.com/Poly-Haven/Public-API/blob/master/ToS.md). The downloader identifies itself as `NeonCoastLocalPrototype/1.0`, caches metadata and limits concurrent downloads to three. No account, paid asset or protected GTA content is used.

The complete file inventory, original download URLs, upstream MD5 values, verified byte counts and local SHA256 hashes are in [manifest.json](../../Unreal/ArtSource/PublicAssets/manifest.json). Source files are kept unchanged; subsequent UE imports/materials are derived integration work. Acquisition verification does not itself prove successful Unreal import, rendering or cooking.

Acquisition verified at 2026-10-21T16:26:31.126531+00:00; 54 files, 165,503,508 bytes (165.50 MB). All asset files matched upstream MD5 and byte size. All four glTF scene buffer/image references resolve locally.

| Asset / official source | Official author credit | Source format | Physical tile size (cm) | Bytes |
|---|---|---|---:|---:|
| [Asphalt 02](https://polyhaven.com/a/asphalt_02) | Rob Tuytel (All) | 2K diffuse / DirectX normal / roughness JPG | 300 × 300 | 10,358,376 |
| [Concrete Pavement](https://polyhaven.com/a/concrete_pavement) | Charlotte Baglioni (All) | 2K diffuse / DirectX normal / roughness JPG | 180 × 180 | 11,126,610 |
| [Coast Sand 02](https://polyhaven.com/a/coast_sand_02) | Rob Tuytel (All) | 2K diffuse / DirectX normal / roughness JPG | 1500 × 1500 | 9,500,562 |
| [White Plaster 02](https://polyhaven.com/a/white_plaster_02) | Rob Tuytel (All) | 2K diffuse / DirectX normal / roughness JPG | 100 × 100 | 7,611,314 |
| [Red Brick Plaster Patch 02](https://polyhaven.com/a/red_brick_plaster_patch_02) | Rob Tuytel (All) | 2K diffuse / DirectX normal / roughness JPG | 150 × 150 | 5,717,962 |
| [Grass Path 2](https://polyhaven.com/a/grass_path_2) | Rob Tuytel (All) | 2K diffuse / DirectX normal / roughness JPG | 100 × 100 | 6,529,607 |
| [Modular Urban Apartments Facade](https://polyhaven.com/a/modular_urban_apartments_facade) | James Ray Cock (All) | glTF + original 2K texture/buffer dependencies | — | 41,427,124 |
| [Island Tree 02](https://polyhaven.com/a/island_tree_02) | Rob Tuytel (scanning, processing); Rico Cilliers (cleanup, processing) | glTF + original 2K texture/buffer dependencies | — | 59,327,958 |
| [Painted Wooden Bench](https://polyhaven.com/a/painted_wooden_bench) | Kirill Sannikov (All) | glTF + original 2K texture/buffer dependencies | — | 7,994,757 |
| [Street Lamp 01](https://polyhaven.com/a/street_lamp_01) | Josh Dean (All) | glTF + original 2K texture/buffer dependencies | — | 5,909,238 |

## Reproduce and verify

Run `python tools/fetch-realism-assets.py` to fetch or validate the fixed set. `python tools/fetch-realism-assets.py --verify-only` performs offline hash, byte-count and glTF-reference validation using the saved API metadata. The fixed asset plan is capped at 450,000,000 bytes, with textures acquired before model files.

## Import details

Surface textures live in `Unreal/ArtSource/PublicAssets/<asset-id>/textures/`; model entry points are `Unreal/ArtSource/PublicAssets/<asset-id>/<asset-id>_2k.gltf`. Preserve relative `textures/` and `.bin` paths for glTF imports.

The API's [texture dimensions schema](https://api.polyhaven.com/api-docs/swagger.json) explicitly specifies millimeters. The manifest preserves `source_dimensions_mm` and converts texture dimensions to `tile_size_cm` by dividing by 10. Surface base color uses sRGB; DirectX normal and roughness are data textures. Model glTF normal maps are OpenGL-oriented and must follow glTF importer conventions; they are not the separate DirectX surface normal files.

Island Tree 02 is a coastal broadleaf tree, not a palm. It was selected from the three requested island-tree assets because the official metadata identifies a small coastal tree and its source polygon count is lowest of those candidates. Its source still reports 1,762,064 polygons; source LOD availability does not establish that imported UE meshes use LODs. Validate Nanite/LOD, collision, leaf alpha and runtime cost before placing many instances. The other source assets also need UE import/render verification.

This notice covers only the listed Poly Haven assets. Epic vehicle/character content, fonts and original project work retain their separate provenance and terms.
