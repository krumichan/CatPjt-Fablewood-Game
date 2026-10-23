import unreal,json,traceback
from pathlib import Path
p=Path(unreal.Paths.project_dir()).resolve();r={'status':'started','materials':[]}
try:
    for name in ['M_FablePaint','M_FableGround','M_FableWater']:
        m=unreal.load_asset('/Game/Art/Materials/'+name)
        if m is None:raise RuntimeError('Missing '+name)
        usage=unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES
        unreal.MaterialEditingLibrary.set_material_usage(m,usage)
        unreal.MaterialEditingLibrary.recompile_material(m)
        if not unreal.MaterialEditingLibrary.has_material_usage(m,usage):raise RuntimeError('Missing instance usage '+name)
        unreal.EditorAssetLibrary.save_loaded_asset(m)
        r['materials'].append({'name':name,'instanced_static_meshes':True})
    r['status']='compiled'
except Exception:r['status']='failed';r['error']=traceback.format_exc();unreal.log_error(r['error'])
finally:
    (p/'Saved/ArtImport/materials.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
