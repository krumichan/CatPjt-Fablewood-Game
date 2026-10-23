import unreal, json, traceback
from pathlib import Path
p=Path(unreal.Paths.project_dir()).resolve()
r={}
try:
    r['version']=unreal.SystemLibrary.get_engine_version()
    r['classes']={n:hasattr(unreal,n) for n in ['FbxImportUI','FbxFactory','MaterialExpressionNoise','MaterialExpressionWorldPosition','MaterialExpressionComponentMask','StaticMeshEditorSubsystem','PostProcessVolume','CineCameraActor']}
    r['noise_doc']=str(unreal.MaterialExpressionNoise.__doc__)
    r['import_doc']=str(unreal.FbxStaticMeshImportData.__doc__)
    r['bound_doc']=str(unreal.StaticMesh.get_bounds.__doc__)
    r['post_doc']=str(unreal.PostProcessSettings.__doc__)
except Exception:
    r['error']=traceback.format_exc()
(p/'Saved'/'ArtApiProbe.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
unreal.SystemLibrary.quit_editor()
