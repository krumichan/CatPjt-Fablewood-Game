"""Run inside UnrealEditor after the native Fablewood module has compiled.
An existing map is preserved; no user level is overwritten.
"""
import json
import os
import re
from pathlib import Path
import unreal

project = Path(unreal.Paths.project_dir()).resolve()
run_id = os.environ.get("FABLEWOOD_MAP_RUN_ID", "")
if not re.fullmatch(r"[a-f0-9]{32}", run_id):
    raise RuntimeError("Run this generator through Setup-Project.ps1")
report_path = project / "Saved" / "MapGeneration" / (run_id + ".json")
report_path.parent.mkdir(parents=True, exist_ok=True)
level_path = "/Game/Maps/ForestClearing"
level_file = project / "Content" / "Maps" / "ForestClearing.umap"
report = {"status": "not_started", "run_id": run_id, "map": level_path, "actors": []}
try:
    base_material = unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial')
    if base_material is None:
        raise RuntimeError('Engine BasicShapeMaterial is unavailable')
    report['base_material_vector_parameters'] = [str(name) for name in unreal.MaterialEditingLibrary.get_vector_parameter_names(base_material)]
    level = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if level_file.exists():
        if not level.load_level(level_path):
            raise RuntimeError("Unable to load the existing map; it was preserved")
        existing = actors.get_all_level_actors()
        forest_class = unreal.load_class(None, "/Script/Fablewood.FableWorld")
        worlds = [a for a in existing if forest_class is not None and a.get_class() == forest_class]
        starts = [a for a in existing if isinstance(a, unreal.PlayerStart)]
        if len(worlds) != 1 or len(starts) < 2:
            raise RuntimeError("Existing map must have one FableWorld and two PlayerStarts; it was preserved for repair")
        batches = worlds[0].get_components_by_class(unreal.InstancedStaticMeshComponent)
        if not batches or not all(c.get_name().startswith('Fable_') for c in batches):
            raise RuntimeError('The reloaded editor map did not restore its stable forest components')
        report['reloaded_environment_batches'] = len(batches)
        report['reloaded_environment_instances'] = sum(c.get_instance_count() for c in batches)
        report['environment_component_names'] = [c.get_name() for c in batches]
        report["actors"] = [a.get_actor_label() for a in existing]
        report["status"] = "preserved_verified"
    else:
        # Work in an unsaved map. Failed actor/lighting setup leaves no incomplete final asset.
        generated_world = unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
        if generated_world is None:
            raise RuntimeError("Unable to create the temporary blank world")
        def spawn(cls, label, position=(0, 0, 0), rotation=None):
            actor = actors.spawn_actor_from_class(cls, unreal.Vector(*position), rotation or unreal.Rotator())
            if actor is None:
                raise RuntimeError("Unable to spawn " + label)
            actor.set_actor_label(label)
            report["actors"].append(label)
            return actor
        forest_class = unreal.load_class(None, "/Script/Fablewood.FableWorld")
        if forest_class is None:
            raise RuntimeError("Native FableWorld class is missing; build FablewoodEditor first")
        spawn(forest_class, "Fablewood - Woodland Clearing")
        spawn(unreal.PlayerStart, "Adventurer Start A", (0,-900,160), unreal.Rotator(yaw=90))
        spawn(unreal.PlayerStart, "Adventurer Start B", (260,-900,160), unreal.Rotator(yaw=90))
        sun = spawn(unreal.DirectionalLight, "Warm afternoon sun", (0,0,1800), unreal.Rotator(pitch=-38,yaw=-30))
        sun_component = sun.get_component_by_class(unreal.DirectionalLightComponent)
        sun_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sun_component.set_editor_property('atmosphere_sun_light', True)
        sun_component.set_editor_property("intensity", 3.0)
        sun_component.set_editor_property("light_color", unreal.Color(r=255,g=242,b=218,a=255))
        sun_component.set_editor_property('light_source_angle', 3.0)
        fill = spawn(unreal.DirectionalLight, 'Soft woodland fill', (0,0,1600), unreal.Rotator(pitch=-50,yaw=135))
        fill_component = fill.get_component_by_class(unreal.DirectionalLightComponent)
        fill_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        fill_component.set_editor_property('atmosphere_sun_light', False)
        fill_component.set_editor_property('intensity', 1.5)
        fill_component.set_editor_property('light_color', unreal.Color(r=208,g=230,b=255,a=255))
        fill_component.set_editor_property('cast_shadows', False)
        sky = spawn(unreal.SkyLight, "Soft forest sky", (0,0,1600))
        sky_component = sky.get_component_by_class(unreal.SkyLightComponent)
        sky_component.set_mobility(unreal.ComponentMobility.MOVABLE)
        sky_component.set_editor_property("intensity", 2.2)
        sky_component.set_editor_property("real_time_capture", True)
        spawn(unreal.SkyAtmosphere, "Storybook sky")
        fog = spawn(unreal.ExponentialHeightFog, "Soft distant haze")
        fog_component = fog.get_component_by_class(unreal.ExponentialHeightFogComponent)
        fog_component.set_editor_property("fog_density", 0.0035)
        fog_component.set_editor_property("fog_height_falloff", 0.2)
        if level_file.exists():
            raise RuntimeError("ForestClearing appeared while generating; existing file preserved")
        if not unreal.EditorLoadingAndSavingUtils.save_map(generated_world, level_path):
            raise RuntimeError("Unable to save ForestClearing level")
        if not level_file.exists():
            raise RuntimeError("Editor reported success but the map file is missing")
        report["status"] = "created"
        unreal.log("FABLEWOOD_MAP_CREATED " + str(level_file))
except Exception as exc:
    report["status"] = "failed"
    report["error"] = str(exc)
    unreal.log_error("FABLEWOOD_MAP_FAILED " + str(exc))
finally:
    report_path.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.SystemLibrary.quit_editor()
