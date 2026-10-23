"""Import locally synthesized combat WAVs and their spatial attenuation asset."""
import json
from pathlib import Path
import traceback
import unreal

ROOT = Path(unreal.Paths.project_dir()).resolve()
DEST = '/Game/Art/Audio'
NAMES = ('SW_FableHit', 'SW_FableSkill', 'SW_FableShield', 'SW_FableAdvance')
report = {'status': 'started', 'assets': []}
try:
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    attenuation_path = DEST + '/ATT_FableCombat'
    attenuation = unreal.load_asset(attenuation_path) if unreal.EditorAssetLibrary.does_asset_exist(attenuation_path) else None
    if attenuation is None:
        attenuation = tools.create_asset('ATT_FableCombat', DEST, unreal.SoundAttenuation,
                                         unreal.SoundAttenuationFactory())
    if not isinstance(attenuation, unreal.SoundAttenuation):
        raise RuntimeError('Could not create combat attenuation asset')
    settings = attenuation.get_editor_property('attenuation')
    settings.set_editor_property('attenuate', True)
    settings.set_editor_property('spatialize', True)
    settings.set_editor_property('attenuation_shape_extents', unreal.Vector(300, 0, 0))
    settings.set_editor_property('falloff_distance', 2700.0)
    attenuation.set_editor_property('attenuation', settings)
    if not unreal.EditorAssetLibrary.save_loaded_asset(attenuation, only_if_is_dirty=False):
        raise RuntimeError('Could not save combat attenuation')
    report['assets'].append(attenuation.get_path_name())

    for name in NAMES:
        source = ROOT / 'ArtSource/Audio' / (name + '.wav')
        if not source.is_file():
            raise RuntimeError('Missing generated WAV: ' + str(source))
        task = unreal.AssetImportTask()
        task.set_editor_property('filename', str(source))
        task.set_editor_property('destination_path', DEST)
        task.set_editor_property('destination_name', name)
        task.set_editor_property('automated', True)
        task.set_editor_property('replace_existing', True)
        task.set_editor_property('save', False)
        factory = unreal.SoundFactory()
        factory.set_editor_property('auto_create_cue', False)
        task.set_editor_property('factory', factory)
        tools.import_asset_tasks([task])
        sound = unreal.load_asset(DEST + '/' + name)
        if not isinstance(sound, unreal.SoundWave):
            raise RuntimeError('WAV import failed: ' + name)
        sound.set_editor_property('attenuation_settings', attenuation)
        sound.set_editor_property('volume', 0.70)
        sound.set_editor_property('looping', False)
        if not unreal.EditorAssetLibrary.save_loaded_asset(sound, only_if_is_dirty=False):
            raise RuntimeError('Could not save combat sound: ' + name)
        report['assets'].append(sound.get_path_name())
    report['status'] = 'ready'
except Exception:
    report['status'] = 'failed'
    report['error'] = traceback.format_exc()
    raise
finally:
    out = ROOT / 'Saved/ArtImport/combat-audio.json'
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    unreal.log('FABLE_COMBAT_AUDIO_IMPORT ' + json.dumps(report, ensure_ascii=False))
