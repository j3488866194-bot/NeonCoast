"""Import the project's original WAV synthesis into /Game/Audio, idempotently.

Run tools/generate-audio.py using host Python first. This script runs only inside
UE 5.8's editor Python. build() is the bootstrap entrypoint and returns asset paths.
"""

from pathlib import Path
import hashlib
import json

import unreal

ASSET_DIRECTORY = "/Game/Audio"
SOURCE_TAG = "NeonCoast.SourceSHA256"


def build():
    root = Path(__file__).resolve().parents[3]
    source = root / "artifacts" / "audio-src"
    manifest_file = source / "manifest.json"
    if not manifest_file.is_file():
        raise RuntimeError("Run tools/generate-audio.py before preparing audio: " + str(manifest_file))
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    if manifest.get("schema") != 1 or len(manifest.get("assets", [])) != 9:
        raise RuntimeError("Unexpected original-audio manifest schema or asset count")
    library = unreal.EditorAssetLibrary
    library.make_directory(ASSET_DIRECTORY)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    paths = []
    for entry in manifest["assets"]:
        wave_path = source / entry["file"]
        if wave_path.parent != source or not wave_path.is_file():
            raise RuntimeError("Missing original audio source: " + str(wave_path))
        digest = hashlib.sha256(wave_path.read_bytes()).hexdigest()
        if digest != entry["sha256"]:
            raise RuntimeError("Audio manifest/source mismatch: " + wave_path.name)
        asset_path = ASSET_DIRECTORY + "/" + entry["name"]
        asset = library.load_asset(asset_path) if library.does_asset_exist(asset_path) else None
        if asset is not None and not isinstance(asset, unreal.SoundWave):
            raise RuntimeError("Existing audio destination is not a SoundWave: " + asset_path)
        if asset is None or library.get_metadata_tag(asset, SOURCE_TAG) != digest:
            factory = unreal.SoundFactory()
            factory.set_editor_property("auto_create_cue", False)
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str(wave_path))
            task.set_editor_property("destination_path", ASSET_DIRECTORY)
            task.set_editor_property("destination_name", entry["name"])
            task.set_editor_property("automated", True)
            task.set_editor_property("replace_existing", True)
            task.set_editor_property("replace_existing_settings", True)
            task.set_editor_property("save", False)
            task.set_editor_property("factory", factory)
            asset_tools.import_asset_tasks([task])
            asset = library.load_asset(asset_path)
            if not isinstance(asset, unreal.SoundWave):
                raise RuntimeError("SoundWave import failed: " + asset_path)
        # USoundWave::bLooping and CompressionQuality are editable properties in
        # the installed UE 5.8 SoundWave.h; no SoundCue is required for looping.
        asset.set_editor_property("looping", bool(entry["looping"]))
        asset.set_editor_property("compression_quality", 75)
        library.set_metadata_tag(asset, SOURCE_TAG, digest)
        library.set_metadata_tag(asset, "NeonCoast.Provenance", manifest["provenance"])
        if not library.save_asset(asset_path, only_if_is_dirty=False):
            raise RuntimeError("Audio asset save failed: " + asset_path)
        if bool(asset.get_editor_property("looping")) != bool(entry["looping"]):
            raise RuntimeError("Audio loop setting did not persist: " + asset_path)
        paths.append(asset_path)
    unreal.log("NEON_COAST_AUDIO_READY assets=" + str(len(paths)))
    return paths


if __name__ == "__main__":
    build()
