"""Update the existing forest's art lighting; preserve gameplay actors and PlayerStarts."""
from pathlib import Path
import unreal,json,traceback
p=Path(unreal.Paths.project_dir()).resolve();r={'status':'started'}
try:
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not level.load_level('/Game/Maps/ForestClearing'): raise RuntimeError('Map load failed')
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    all_actors=actors.get_all_level_actors()
    for actor in all_actors:
        if isinstance(actor,unreal.DirectionalLight):
            c=actor.get_component_by_class(unreal.DirectionalLightComponent)
            if c.get_editor_property('atmosphere_sun_light'):
                c.set_editor_property('intensity',3.3);c.set_editor_property('light_color',unreal.Color(255,239,212,255));c.set_editor_property('light_source_angle',2.2)
                actor.set_actor_rotation(unreal.Rotator(pitch=-43,yaw=-35),False)
            else: c.set_editor_property('intensity',.85)
        elif isinstance(actor,unreal.SkyLight):
            actor.get_component_by_class(unreal.SkyLightComponent).set_editor_property('intensity',1.6)
    volume=next((a for a in all_actors if isinstance(a,unreal.PostProcessVolume) and a.get_actor_label()=='Storybook finish'),None)
    if volume is None:
        volume=actors.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector());volume.set_actor_label('Storybook finish')
    volume.set_editor_property('unbound',True)
    settings=volume.get_editor_property('settings')
    for k,v in {'bloom_intensity':.22,'vignette_intensity':.16,'ambient_occlusion_intensity':.8,'ambient_occlusion_radius':85.0,'color_saturation':unreal.Vector4(.96,.96,.96,1.0)}.items():
        settings.set_editor_property('override_'+k,True);settings.set_editor_property(k,v)
    volume.set_editor_property('settings',settings)
    if not level.save_current_level():raise RuntimeError('Failed to save lighting')
    forest_cls=unreal.load_class(None,'/Script/Fablewood.FableWorld')
    forest=next(a for a in actors.get_all_level_actors() if a.get_class()==forest_cls)
    batches=forest.get_components_by_class(unreal.InstancedStaticMeshComponent)
    if not batches or not all(c.get_name().startswith('Fable_') for c in batches):raise RuntimeError('Stable forest batches did not restore')
    r['environment_batches']=len(batches);r['environment_instances']=sum(c.get_instance_count() for c in batches)
    r['batches']=[{'name':c.get_name(),'instances':c.get_instance_count(),'mesh':c.get_editor_property('static_mesh').get_path_name() if c.get_editor_property('static_mesh') else None} for c in batches]
    r['status']='updated';r['actors']=[a.get_actor_label() for a in actors.get_all_level_actors()]
except Exception:r['status']='failed';r['error']=traceback.format_exc();unreal.log_error(r['error'])
finally:
    (p/'Saved/ArtImport/lighting.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
    unreal.SystemLibrary.quit_editor()
