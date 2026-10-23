"""Import original art. OBJ sources use Unreal centimetres; staging compensates FBX Y handedness."""
import json, math, traceback, hashlib, os, uuid
from pathlib import Path
import unreal
P=Path(unreal.Paths.project_dir()).resolve()
A=unreal.AssetToolsHelpers.get_asset_tools()
E=unreal.MaterialEditingLibrary
R={'status':'started','run_id':os.environ.get('FABLE_ART_IMPORT_RUN_ID',uuid.uuid4().hex),'meshes':[]}
def node(mat,cls,**props):
    n=E.create_material_expression(mat,cls)
    for k,v in props.items(): n.set_editor_property(k,v)
    return n
def wire(a,b,pin='A',out=''):
    if not E.connect_material_expressions(a,out,b,pin): raise RuntimeError('Material connection failed: '+pin)
def prop(n,mat,p,out=''):
    if not E.connect_material_property(n,out,p): raise RuntimeError('Material output connection failed')
def material(name,kind):
    m=unreal.load_asset('/Game/Art/Materials/'+name)
    # Constructor-loaded materials are rooted by UE; reuse authored materials on reruns.
    if m is not None:
        E.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
        unreal.EditorAssetLibrary.save_loaded_asset(m)
        return m
    m=A.create_asset(name,'/Game/Art/Materials',unreal.Material,unreal.MaterialFactoryNew())
    m.set_editor_property('two_sided',True)
    E.set_material_usage(m,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    color=node(m,unreal.MaterialExpressionVectorParameter,parameter_name='Color',default_value=unreal.LinearColor(1,1,1,1))
    rough=node(m,unreal.MaterialExpressionScalarParameter,parameter_name='Roughness',default_value=.86 if kind!='water' else .22)
    prop(rough,m,unreal.MaterialProperty.MP_ROUGHNESS)
    spec=node(m,unreal.MaterialExpressionConstant,r=.25)
    prop(spec,m,unreal.MaterialProperty.MP_SPECULAR)
    pos=node(m,unreal.MaterialExpressionWorldPosition)
    if kind=='ground':
        mask=node(m,unreal.MaterialExpressionComponentMask,r=True,g=True,b=False,a=False);wire(pos,mask,'')
        scale=node(m,unreal.MaterialExpressionMultiply,const_b=.004);wire(mask,scale)
        tex=node(m,unreal.MaterialExpressionTextureSample,texture=unreal.load_asset('/Game/Art/Textures/T_StorybookMeadow'))
        wire(scale,tex,'UVs')
        mult=node(m,unreal.MaterialExpressionMultiply);wire(tex,mult,out='RGB');wire(color,mult,'B')
        prop(mult,m,unreal.MaterialProperty.MP_BASE_COLOR)
    else:
        noise=node(m,unreal.MaterialExpressionNoise,scale=.027 if kind=='paint' else .006,quality=1,levels=2,output_min=.78,output_max=1.12)
        # Noise defaults to absolute world position when no input is connected.
        mult=node(m,unreal.MaterialExpressionMultiply);wire(color,mult);wire(noise,mult,'B')
        prop(mult,m,unreal.MaterialProperty.MP_BASE_COLOR)
        if kind=='water':
            # Small moving ripples are a normal perturbation, leaving the collision surface unchanged.
            uv=node(m,unreal.MaterialExpressionTextureCoordinate)
            pan=node(m,unreal.MaterialExpressionPanner,speed_x=.04,speed_y=.018);wire(uv,pan,'Coordinate')
            tex=node(m,unreal.MaterialExpressionTextureSample,texture=unreal.load_asset('/Engine/EngineMaterials/DefaultNormal'))
            if tex.get_editor_property('texture'):
                tex.set_editor_property('sampler_type',unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
                wire(pan,tex,'UVs');prop(tex,m,unreal.MaterialProperty.MP_NORMAL,'RGB')
    E.recompile_material(m)
    unreal.EditorAssetLibrary.save_loaded_asset(m)
    return m
try:
    # Legacy FBX OBJ importer exposes exact coordinate preservation options.
    unreal.SystemLibrary.execute_console_command(None,'Interchange.FeatureFlags.Import.OBJ 0')
    tex_task=unreal.AssetImportTask();tex_task.filename=str(P/'ArtSource/Textures/T_StorybookMeadow.png');tex_task.destination_path='/Game/Art/Textures';tex_task.destination_name='T_StorybookMeadow';tex_task.automated=True;tex_task.replace_existing=True;tex_task.save=True
    if unreal.load_asset('/Game/Art/Textures/T_StorybookMeadow') is None: A.import_asset_tasks([tex_task])
    if unreal.load_asset('/Game/Art/Textures/T_StorybookMeadow') is None: raise RuntimeError('Meadow texture did not import')
    paint=material('M_FablePaint','paint');material('M_FableGround','ground');material('M_FableWater','water')
    sources=[s for s in sorted((P/'ArtSource/Meshes').glob(os.environ.get('FABLE_ART_MESH_FILTER','*.obj'))) if s.stem!='SM_ImportProbe']
    stage=P/'Saved/ArtImport/Obj';stage.mkdir(parents=True,exist_ok=True)
    for src in sources:
        pts=[];lines=[]
        for line in src.read_text(encoding='utf-8').splitlines():
            fields=line.split()
            if fields and fields[0] in ('v','vn'):
                xyz=list(map(float,fields[1:4]))
                if fields[0]=='v':pts.append(xyz[:])
                xyz[1]=-xyz[1]
                lines.append(fields[0]+' '+' '.join(format(x,'.7f') for x in xyz))
            elif fields and fields[0]=='f': lines.append('f '+' '.join(reversed(fields[1:])))
            else: lines.append(line)
        staged=stage/src.name;staged.write_text('\n'.join(lines)+'\n',encoding='utf-8')
        opts=unreal.FbxImportUI();opts.import_mesh=True;opts.import_as_skeletal=False;opts.import_materials=False;opts.import_textures=False;opts.import_animations=False;opts.automated_import_should_detect_type=False;opts.mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH
        d=opts.static_mesh_import_data
        d.set_editor_property('convert_scene',False);d.set_editor_property('convert_scene_unit',False);d.set_editor_property('force_front_x_axis',False)
        d.set_editor_property('import_uniform_scale',1.0);d.set_editor_property('combine_meshes',True);d.set_editor_property('auto_generate_collision',False);d.set_editor_property('generate_lightmap_u_vs',False)
        d.set_editor_property('normal_import_method',unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS if any(l.startswith('vn ') for l in lines) else unreal.FBXNormalImportMethod.FBXNIM_COMPUTE_NORMALS)
        task=unreal.AssetImportTask();task.filename=str(staged);task.destination_path='/Game/Art/Meshes';task.destination_name=src.stem;task.automated=True;task.replace_existing=True;task.save=True;task.options=opts;task.factory=unreal.FbxFactory()
        A.import_asset_tasks([task])
        asset=unreal.load_asset('/Game/Art/Meshes/'+src.stem)
        if not isinstance(asset,unreal.StaticMesh): raise RuntimeError('Missing imported mesh '+src.stem)
        asset.set_material(0,paint)
        body=asset.get_editor_property('body_setup')
        if body:body.set_editor_property('collision_trace_flag',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        bounds=asset.get_bounds();origin=bounds.origin;extent=bounds.box_extent
        expected_min=[min(p[i] for p in pts) for i in range(3)];expected_max=[max(p[i] for p in pts) for i in range(3)]
        actual_min=[origin.x-extent.x,origin.y-extent.y,origin.z-extent.z];actual_max=[origin.x+extent.x,origin.y+extent.y,origin.z+extent.z]
        err=max(abs(a-b) for a,b in zip(expected_min+expected_max,actual_min+actual_max))
        if err>.1:raise RuntimeError(f'Coordinate/scale verification failed {src.stem}: error {err}cm, expected {expected_min,expected_max}, actual {actual_min,actual_max}')
        unreal.EditorAssetLibrary.save_loaded_asset(asset)
        R['meshes'].append({'name':src.stem,'vertices':len(pts),'bounds_min':actual_min,'bounds_max':actual_max,'axis_error_cm':err,'source_sha256':hashlib.sha256(src.read_bytes()).hexdigest()})
    R['status']='imported';R['mesh_count']=len(R['meshes'])
except Exception:
    R['status']='failed';R['error']=traceback.format_exc();unreal.log_error(R['error'])
finally:
    (P/'Saved/ArtImport').mkdir(exist_ok=True)
    (P/'Saved/ArtImport/result.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
    (P/'Saved/ArtImport'/(R['run_id']+'.json')).write_text(json.dumps(R,indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
