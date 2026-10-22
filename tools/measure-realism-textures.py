"""Measure source albedo in linear RGB for the Opus PBR builder (no image edits)."""
import hashlib
import json
from pathlib import Path
from PIL import Image


def main():
    project = Path(__file__).resolve().parents[1] / "Unreal"
    source = project / "ArtSource/PublicAssets"
    result = {}
    sets = {"asphalt_02", "concrete_pavement", "coast_sand_02", "white_plaster_02",
            "red_brick_plaster_patch_02", "grass_path_2"}
    for texture in sorted(source.glob("*/textures/*_diff_2k.jpg")):
        name = texture.parent.parent.name
        if name not in sets:
            continue
        with Image.open(texture) as opened:
            image = opened.convert("RGB")
            hist = image.histogram()
            pixels = image.width * image.height
            linear = [v / 255 / 12.92 if v / 255 <= .04045 else
                      ((v / 255 + .055) / 1.055) ** 2.4 for v in range(256)]
            mean = [sum(hist[channel * 256 + i] * linear[i] for i in range(256)) / pixels
                    for channel in range(3)]
            result[name] = {"linear_mean": mean, "size": list(image.size),
                            "source": str(texture.relative_to(project)),
                            "source_sha256": hashlib.sha256(texture.read_bytes()).hexdigest(),
                            "method": "All pixels; sRGB inverse transfer then arithmetic mean per RGB channel"}
    if len(result) != 6:
        raise RuntimeError(f"Expected six PBR diffuse images, found {len(result)}")
    output = project / "Saved/RealismTextureMeans.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(output), "linear_means": {k: v["linear_mean"] for k, v in result.items()}}))


if __name__ == "__main__":
    main()
