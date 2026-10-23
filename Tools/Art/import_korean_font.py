"""Import the bundled OFL NanumGothic as a runtime font, safe to repeat."""
import json, traceback
from pathlib import Path
import unreal
P = Path(unreal.Paths.project_dir()).resolve()
report = {"status": "started", "font": "/Game/UI/Fonts/F_Korean"}
try:
    source = P / "ArtSource/Fonts/NanumGothic.ttf"
    if not source.is_file():
        raise RuntimeError("Bundled NanumGothic.ttf source is missing")
    font = unreal.load_asset(report["font"])
    if font is None:
        generated = "/Game/UI/Fonts/F_KoreanFace_Font"
        if not unreal.EditorAssetLibrary.does_asset_exist(generated):
            factory = unreal.FontFileImportFactory()
            factory.set_editor_property("batch_create_font_asset", unreal.BatchCreateFontAsset.YES)
            task = unreal.AssetImportTask()
            task.set_editor_property("filename", str(source))
            task.set_editor_property("destination_path", "/Game/UI/Fonts")
            task.set_editor_property("destination_name", "F_KoreanFace")
            task.set_editor_property("automated", True)
            task.set_editor_property("save", True)
            task.set_editor_property("factory", factory)
            unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        if not unreal.EditorAssetLibrary.does_asset_exist(generated):
            raise RuntimeError("Font import did not create the runtime Font asset")
        if not unreal.EditorAssetLibrary.rename_asset(generated, report["font"]):
            raise RuntimeError("Could not name the imported runtime Font")
        font = unreal.load_asset(report["font"])
    if not isinstance(font, unreal.Font):
        raise RuntimeError("Expected a runtime UFont asset")
    font.set_editor_property("legacy_font_size", 20)
    font.set_editor_property("legacy_font_name", "Default")
    if not unreal.EditorAssetLibrary.save_loaded_asset(font, only_if_is_dirty=False):
        raise RuntimeError("Could not save Korean Font asset")
    unreal.EditorAssetLibrary.save_directory("/Game/UI/Fonts", only_if_is_dirty=False, recursive=True)
    report["status"] = "ready"
    report["source_bytes"] = source.stat().st_size
except Exception:
    report["status"] = "failed"
    report["error"] = traceback.format_exc()
    raise
finally:
    out = P / "Saved/ArtImport/korean-font.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
