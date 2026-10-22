"""Fetch a small, fixed CC0 Poly Haven source set; never starts Unreal.

Powered by Poly Haven: https://polyhaven.com/
The downloaded assets are free and public domain (CC0), not sold by this tool.
Only 2K maps and glTF dependencies are selected, with at most three requests
running at once. Re-running checks existing bytes instead of downloading again.
"""
from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import hashlib
import itertools
import json
from pathlib import Path, PurePosixPath
import time
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "Unreal/ArtSource/PublicAssets"
CACHE = ROOT / "artifacts/realism"
MANIFEST = DEST / "manifest.json"
USER_AGENT = "NeonCoastLocalPrototype/1.0"
MAX_BYTES = 450_000_000
LICENSE = "CC0-1.0"
LICENSE_URL = "https://creativecommons.org/publicdomain/zero/1.0/"
SOURCE_LICENSE = "https://polyhaven.com/license"
API_TERMS = "https://github.com/Poly-Haven/Public-API/blob/master/ToS.md"
SCHEMA_URL = "https://api.polyhaven.com/api-docs/swagger.json"
TEXTURES = (
    "asphalt_02", "concrete_pavement", "coast_sand_02", "white_plaster_02",
    "red_brick_plaster_patch_02", "grass_path_2",
)
MODELS = (
    "modular_urban_apartments_facade", "island_tree_02",
    "painted_wooden_bench", "street_lamp_01",
)


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    temporary.replace(path)


def request(url):
    parsed = urllib.parse.urlsplit(url)
    if parsed.scheme != "https" or parsed.hostname not in {"api.polyhaven.com", "dl.polyhaven.org"}:
        raise ValueError(f"Unexpected source URL: {url}")
    return urllib.request.urlopen(urllib.request.Request(
        url, headers={"User-Agent": USER_AGENT}), timeout=120)


def api_json(name, url, offline):
    path = CACHE / name
    if not path.exists():
        if offline:
            raise RuntimeError(f"Missing metadata cache: {path}")
        with request(url) as response:
            payload = json.load(response)
        write_json(path, payload)
    return json.loads(path.read_text(encoding="utf-8"))


def safe_path(relative):
    p = PurePosixPath(relative)
    if p.is_absolute() or ".." in p.parts or ":" in relative or "\\" in relative:
        raise ValueError(f"Unsafe source path: {relative}")
    target = (DEST / Path(*p.parts)).resolve()
    target.relative_to(DEST.resolve())
    return target


def file_entry(key, relative, role, metadata):
    safe_path(f"{key}/{relative}")
    return {
        "path": f"{key}/{relative}", "role": role,
        "download_url": metadata["url"], "expected_size": metadata["size"],
        "upstream_md5": metadata["md5"].lower(),
    }


def plan(offline):
    catalogues = {
        "texture": api_json("assets_t_textures.json", "https://api.polyhaven.com/assets?t=textures", offline),
        "model": api_json("assets_t_models.json", "https://api.polyhaven.com/assets?t=models", offline),
    }
    schema = api_json("api_swagger.json", SCHEMA_URL, offline)
    units = schema["components"]["schemas"]["texture"]["properties"]["dimensions"]["description"]
    if "millimeters" not in units:
        raise RuntimeError(f"Physical dimensions unit requires review: {units}")
    assets = []
    for key in TEXTURES + MODELS:
        kind = "texture" if key in TEXTURES else "model"
        info = catalogues[kind][key]
        files = api_json(f"files_{key}.json", f"https://api.polyhaven.com/files/{key}", offline)
        asset = {
            "id": key, "name": info["name"], "kind": kind,
            "asset_url": f"https://polyhaven.com/a/{key}",
            "metadata_url": f"https://api.polyhaven.com/info/{key}",
            "catalogue_source_url": "https://api.polyhaven.com/assets?t=" + ("textures" if kind == "texture" else "models"),
            "files_api_url": f"https://api.polyhaven.com/files/{key}",
            "authors": info["authors"], "license": LICENSE,
            "license_url": LICENSE_URL, "source_license_url": SOURCE_LICENSE,
            "resolution": "2k", "source_files_hash": info.get("files_hash"),
            "source_dimensions_mm": info.get("dimensions"), "files": [],
        }
        if kind == "texture":
            # API schema explicitly defines millimeters; Unreal world units are centimeters.
            asset["tile_size_cm"] = [round(x / 10, 4) for x in info["dimensions"]]
            asset["dimensions_schema_url"] = SCHEMA_URL
            for channel, role in (("Diffuse", "base_color"), ("nor_dx", "normal_directx"), ("Rough", "roughness")):
                entry = files[channel]["2k"]["jpg"]
                filename = PurePosixPath(urllib.parse.urlsplit(entry["url"]).path).name
                asset["files"].append(file_entry(key, f"textures/{filename}", role, entry))
        else:
            gltf = files["gltf"]["2k"]["gltf"]
            filename = PurePosixPath(urllib.parse.urlsplit(gltf["url"]).path).name
            asset["entry_point"] = f"{key}/{filename}"
            asset["source_polycount"] = info.get("polycount")
            asset["source_has_lods"] = info.get("lods", False)
            asset["files"].append(file_entry(key, filename, "gltf_scene", gltf))
            for relative, entry in sorted(gltf.get("include", {}).items()):
                asset["files"].append(file_entry(key, relative, "gltf_dependency", entry))
            if key == "island_tree_02":
                # The upstream glTF leaf base color is JPG; the official separate
                # mask is needed when preparing correctly clipped foliage for UE.
                alpha = files["leaves_alpha"]["2k"]["png"]
                filename = PurePosixPath(urllib.parse.urlsplit(alpha["url"]).path).name
                asset["files"].append(file_entry(key, f"textures/{filename}", "foliage_opacity_mask", alpha))
        assets.append(asset)
    total = sum(f["expected_size"] for a in assets for f in a["files"])
    if total > MAX_BYTES:
        raise RuntimeError(f"Planned asset bytes {total} exceed {MAX_BYTES}")
    return assets, total


def hashes(path):
    md5, sha256, size = hashlib.md5(), hashlib.sha256(), 0
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            size += len(block)
            md5.update(block)
            sha256.update(block)
    return size, md5.hexdigest(), sha256.hexdigest()


def fetch(entry, offline):
    path = safe_path(entry["path"])
    values = hashes(path) if path.exists() else None
    cached = bool(values and values[0] == entry["expected_size"] and values[1] == entry["upstream_md5"])
    if not cached:
        if offline:
            raise RuntimeError(f"Missing or mismatched asset: {path}")
        path.parent.mkdir(parents=True, exist_ok=True)
        temporary = path.with_suffix(path.suffix + ".partial")
        for attempt in range(3):
            try:
                with request(entry["download_url"]) as response, temporary.open("wb") as out:
                    received = 0
                    while block := response.read(1024 * 1024):
                        received += len(block)
                        if received > entry["expected_size"]:
                            raise ValueError(f"Download exceeded expected size: {entry['path']}")
                        out.write(block)
                values = hashes(temporary)
                if values[0] != entry["expected_size"] or values[1] != entry["upstream_md5"]:
                    raise ValueError(f"Upstream size/MD5 mismatch for {entry['path']}: {values[:2]}")
                temporary.replace(path)
                break
            except Exception:
                temporary.unlink(missing_ok=True)
                if attempt == 2:
                    raise
                time.sleep(attempt + 1)
    entry.update(size=values[0], md5=values[1], sha256=values[2], md5_verified=True)
    print(f"{'VERIFIED' if cached else 'FETCHED'} {entry['path']} ({values[0]} bytes)", flush=True)
    return entry


def verify_gltf(asset):
    path = safe_path(asset["entry_point"])
    data = json.loads(path.read_text(encoding="utf-8"))
    declared = {f["path"] for f in asset["files"]}
    references = []
    for entry in data.get("buffers", []) + data.get("images", []):
        uri = entry.get("uri", "")
        if not uri or uri.startswith("data:"):
            continue
        relative = f"{asset['id']}/{urllib.parse.unquote(uri)}"
        target = safe_path(relative)
        if relative not in declared or not target.is_file():
            raise RuntimeError(f"Unresolved glTF dependency: {relative}")
        if "byteLength" in entry and target.stat().st_size < entry["byteLength"]:
            raise RuntimeError(f"Truncated glTF buffer: {relative}")
        references.append(relative)
    asset["gltf_dependency_check"] = {"status": "passed", "references_checked": len(references)}


def inspect_gltf(asset):
    """Report source accessor bounds, topology counts and node names without UE."""
    source = json.loads(safe_path(asset["entry_point"]).read_text(encoding="utf-8"))
    identity = [[float(i == j) for j in range(4)] for i in range(4)]

    def multiply(a, b):
        return [[sum(a[i][k] * b[k][j] for k in range(4)) for j in range(4)] for i in range(4)]

    def local_matrix(node):
        if "matrix" in node:
            return [[node["matrix"][j * 4 + i] for j in range(4)] for i in range(4)]
        x, y, z, w = node.get("rotation", [0, 0, 0, 1])
        rotation = [
            [1-2*(y*y+z*z), 2*(x*y-z*w), 2*(x*z+y*w), 0],
            [2*(x*y+z*w), 1-2*(x*x+z*z), 2*(y*z-x*w), 0],
            [2*(x*z-y*w), 2*(y*z+x*w), 1-2*(x*x+y*y), 0],
            [0, 0, 0, 1],
        ]
        scale = node.get("scale", [1, 1, 1])
        for row in range(3):
            for col in range(3):
                rotation[row][col] *= scale[col]
            rotation[row][3] = node.get("translation", [0, 0, 0])[row]
        return rotation

    materials = [{"index": i, "name": m.get("name", f"material_{i}"),
                  "alpha_mode": m.get("alphaMode", "OPAQUE"),
                  "double_sided": m.get("doubleSided", False)}
                 for i, m in enumerate(source.get("materials", []))]
    report = {"source": asset["entry_point"], "coordinate_system": "glTF right-handed; X/Y/Z; Y is up; source meters converted to centimeters",
              "bounds_method": "POSITION accessor min/max transformed through scene node hierarchy; no mesh conversion or Unreal import",
              "materials": materials, "nodes": []}

    def visit(index, parent):
        node = source["nodes"][index]
        matrix = multiply(parent, local_matrix(node))
        if "mesh" in node:
            mesh = source["meshes"][node["mesh"]]
            lows, highs, primitives = [], [], []
            for primitive in mesh["primitives"]:
                accessor = source["accessors"][primitive["attributes"]["POSITION"]]
                lows.append(accessor["min"])
                highs.append(accessor["max"])
                count = source["accessors"][primitive["indices"]]["count"] if "indices" in primitive else accessor["count"]
                mode = primitive.get("mode", 4)
                triangles = count // 3 if mode == 4 else max(0, count - 2) if mode in (5, 6) else 0
                primitives.append({"triangles": triangles, "vertices": accessor["count"], "mode": mode,
                                   "material_index": primitive.get("material")})
            low = [min(v[i] for v in lows) for i in range(3)]
            high = [max(v[i] for v in highs) for i in range(3)]
            corners = list(itertools.product(*zip(low, high)))
            transformed = [[sum(matrix[i][j] * (list(v) + [1])[j] for j in range(4)) for i in range(3)] for v in corners]
            scene_low = [min(v[i] for v in transformed) for i in range(3)]
            scene_high = [max(v[i] for v in transformed) for i in range(3)]
            cm = lambda values: [round(value * 100, 4) for value in values]
            report["nodes"].append({
                "node_index": index, "node_name": node.get("name", f"node_{index}"),
                "mesh_index": node["mesh"], "mesh_name": mesh.get("name", f"mesh_{node['mesh']}"),
                "triangles": sum(p["triangles"] for p in primitives),
                "primitives": primitives,
                "material_slots": list(dict.fromkeys(p["material_index"] for p in primitives)),
                "local_bounds_cm": {"min": cm(low), "max": cm(high)},
                "local_size_cm_xyz": cm([high[i] - low[i] for i in range(3)]),
                "scene_bounds_cm": {"min": cm(scene_low), "max": cm(scene_high)},
            })
        for child in node.get("children", []):
            visit(child, matrix)

    for index in source["scenes"][source.get("scene", 0)]["nodes"]:
        visit(index, identity)
    report["node_count"] = len(report["nodes"])
    report["triangle_count_all_scene_nodes"] = sum(n["triangles"] for n in report["nodes"])
    report["scene_layout_bounds_cm"] = {
        "min": [min(n["scene_bounds_cm"]["min"][i] for n in report["nodes"]) for i in range(3)],
        "max": [max(n["scene_bounds_cm"]["max"][i] for n in report["nodes"]) for i in range(3)],
    }
    report_path = f"{asset['id']}/inspection.json"
    write_json(safe_path(report_path), report)
    asset["inspection_path"] = report_path


def write_notice(manifest):
    lines = [
        "# Public assets used by Neon Coast", "",
        "Powered by [Poly Haven](https://polyhaven.com/). These source assets are free and public domain under [CC0 1.0](https://creativecommons.org/publicdomain/zero/1.0/).",
        "",
        "Poly Haven permits commercial use, modification and redistribution of its assets. Its [asset license](https://polyhaven.com/license) is distinct from the [API access terms](https://github.com/Poly-Haven/Public-API/blob/master/ToS.md). The downloader identifies itself as `NeonCoastLocalPrototype/1.0`, caches metadata and limits concurrent downloads to three. No account, paid asset or protected GTA content is used.",
        "",
        "The complete file inventory, original download URLs, upstream MD5 values, verified byte counts and local SHA256 hashes are in [manifest.json](../../Unreal/ArtSource/PublicAssets/manifest.json). Source files are kept unchanged; subsequent UE imports/materials are derived integration work. Acquisition verification does not itself prove successful Unreal import, rendering or cooking.",
        "",
        f"Acquisition verified at {manifest['completed_at']}; {manifest['file_count']} files, {manifest['total_bytes']:,} bytes ({manifest['total_bytes'] / 1_000_000:.2f} MB). All asset files matched upstream MD5 and byte size. All four glTF scene buffer/image references resolve locally.",
        "",
        "| Asset / official source | Official author credit | Source format | Physical tile size (cm) | Bytes |",
        "|---|---|---|---:|---:|",
    ]
    for asset in manifest["assets"]:
        authors = "; ".join(f"{name} ({role})" for name, role in asset["authors"].items())
        size = " × ".join(f"{value:g}" for value in asset.get("tile_size_cm", [])) or "—"
        format_name = "2K diffuse / DirectX normal / roughness JPG" if asset["kind"] == "texture" else "glTF + original 2K texture/buffer dependencies"
        lines.append(f"| [{asset['name']}]({asset['asset_url']}) | {authors} | {format_name} | {size} | {sum(f['size'] for f in asset['files']):,} |")
    lines += [
        "", "## Reproduce and verify", "",
        "Run `python tools/fetch-realism-assets.py` to fetch or validate the fixed set. `python tools/fetch-realism-assets.py --verify-only` performs offline hash, byte-count and glTF-reference validation using the saved API metadata. The fixed asset plan is capped at 450,000,000 bytes, with textures acquired before model files.",
        "", "## Import details", "",
        "Surface textures live in `Unreal/ArtSource/PublicAssets/<asset-id>/textures/`; model entry points are `Unreal/ArtSource/PublicAssets/<asset-id>/<asset-id>_2k.gltf`. Preserve relative `textures/` and `.bin` paths for glTF imports.",
        "",
        "The API's [texture dimensions schema](https://api.polyhaven.com/api-docs/swagger.json) explicitly specifies millimeters. The manifest preserves `source_dimensions_mm` and converts texture dimensions to `tile_size_cm` by dividing by 10. Surface base color uses sRGB; DirectX normal and roughness are data textures. Model glTF normal maps are OpenGL-oriented and must follow glTF importer conventions; they are not the separate DirectX surface normal files.",
        "",
        "Island Tree 02 is a coastal broadleaf tree, not a palm. It was selected from the three requested island-tree assets because the official metadata identifies a small coastal tree and its source polygon count is lowest of those candidates. Its source still reports 1,762,064 polygons; source LOD availability does not establish that imported UE meshes use LODs. Validate Nanite/LOD, collision, leaf alpha and runtime cost before placing many instances. The other source assets also need UE import/render verification.",
        "",
        "This notice covers only the listed Poly Haven assets. Epic vehicle/character content, fonts and original project work retain their separate provenance and terms.",
    ]
    path = ROOT / "docs/licenses/PUBLIC_ASSETS.md"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true", help="Validate local files and metadata without any network access")
    args = parser.parse_args()
    state = {"schema_version": 1, "status": "in_progress", "started_at": utc_now(),
             "provider": "Poly Haven", "powered_by_url": "https://polyhaven.com/",
             "user_agent": USER_AGENT, "license": LICENSE, "license_url": LICENSE_URL,
             "api_terms_url": API_TERMS, "max_concurrency": 3, "max_bytes": MAX_BYTES}
    write_json(MANIFEST, state)
    try:
        assets, total = plan(args.verify_only)
        state.update(assets=assets, planned_bytes=total)
        write_json(MANIFEST, state)
        print(f"PLAN {len(assets)} assets, {total:,} bytes. Textures first; at most 3 concurrent requests.", flush=True)
        for kind in ("texture", "model"):
            entries = [f for a in assets if a["kind"] == kind for f in a["files"]]
            with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
                list(pool.map(lambda entry: fetch(entry, args.verify_only), entries))
            write_json(MANIFEST, state)
            print(f"READY {kind}: {len(entries)} verified files", flush=True)
        for asset in assets:
            if asset["kind"] == "model":
                verify_gltf(asset)
                inspect_gltf(asset)
        state.update(status="passed", completed_at=utc_now(), total_bytes=total,
                     file_count=sum(len(a["files"]) for a in assets))
        write_json(MANIFEST, state)
        write_notice(state)
        print(f"PASS {state['file_count']} files / {total:,} bytes / 4 glTF dependency graphs", flush=True)
    except Exception as error:
        state.update(status="failed", failed_at=utc_now(), message=str(error))
        write_json(MANIFEST, state)
        raise


if __name__ == "__main__":
    main()
