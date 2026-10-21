"""Editor-only, repeatable material and startup-map generation. No runtime Python."""
from pathlib import Path
import runpy
import unreal


def main():
    source = Path(__file__).with_name("build_visual_assets.py")
    if not source.is_file():
        raise RuntimeError(f"Missing Opus visual asset builder: {source}")
    namespace = runpy.run_path(str(source), run_name="neon_coast_visual_builder")
    namespace.get("build_all", namespace["build"])()
    audio_source = Path(__file__).with_name("build_audio_assets.py")
    runpy.run_path(str(audio_source), run_name="neon_coast_audio_builder")["build"]()
    character_source = Path(__file__).with_name("build_character_assets.py")
    runpy.run_path(str(character_source), run_name="neon_coast_character_builder")["build"]()
    level_path = "/Game/Maps/NeonCoast"
    editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if unreal.EditorAssetLibrary.does_asset_exist(level_path):
        if not editor.load_level(level_path):
            raise RuntimeError("Cannot open existing startup map")
    else:
        unreal.EditorAssetLibrary.make_directory("/Game/Maps")
        if not editor.new_level(level_path):
            raise RuntimeError("Cannot create startup map")
    if not editor.save_current_level():
        raise RuntimeError("Cannot save startup map")
    if not unreal.EditorAssetLibrary.does_asset_exist("/Game/Materials/M_Coast"):
        raise RuntimeError("Visual material was not generated")
    unreal.log("NEON_COAST_ASSETS_READY")


if __name__ == "__main__":
    main()
