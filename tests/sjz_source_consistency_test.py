"""Migration integration checks, not a substitute for Xcode or device tests."""
from pathlib import Path
import plistlib
import re

root=Path(__file__).resolve().parents[1]
active=[root/'lara',root/'scripts',root/'.github',root/'lara.xcodeproj']
for folder in active:
    for path in folder.rglob('*'):
        if path.suffix not in {'.h','.m','.mm','.cpp','.swift','.sh','.yml','.pbxproj'}: continue
        if 'third_party' in path.parts or 'lib' in path.parts: continue
        text=path.read_text(encoding='utf-8-sig')
        assert not re.search(r'\b(?:WZ[A-Z_][A-Za-z_]*|wz(?:esp|mem|hud|aim|ax)_?\w*|smoba|UnityFramework|YuanbaoCollector|KoiProjection)\b|王者',text),path

manager=(root/'lara/classes/laramgr.swift').read_text(encoding='utf-8')
hud=(root/'lara/kexploit/SJZHUDBridge.mm').read_text(encoding='utf-8')
assert 'process: String = "DeltaForceClient"' in manager
assert 'sjzesp_supported_game_base()' in manager
assert '@"com.tencent.tmgp.dfm"' in hud
assert 'ExpectedUUID' not in manager and 'requireSJZAuthorization' not in manager
assert 'sjzesp_collect(' in manager and 'sjzesp_aim(' in manager
assert 'sjzhud_update_sjz_snapshot_with_source_times(' in manager
assert manager.index('sjzesp_collect(')<manager.index('let snapshot=Array(items.prefix')<manager.index('sjzesp_aim(')
assert 'sjzesp_cancel_epoch(' in manager
assert 'sjzhud_copy_sjz_config(' in manager
assert 'SJZLauncherViewController' in (root/'lara/lara.swift').read_text(encoding='utf-8')
assert 'sjzax_touch' not in hud and 'game.gtimg.cn' not in hud
assert 'SJZHUDRenderBackendCoreAnimation' in hud
assert 'SJZHUDRenderBackendMetal' in (root/'lara/kexploit/SJZHUDBridge.h').read_text(encoding='utf-8')
assert 'GetGlyphRangesChineseFull' in hud
assert (root/'lara/SJZChinese.otf').stat().st_size>100000
assert not (root/'lara/kexploit/wzesp.mm').exists()
assert not (root/'scripts/build_ipa_wz.sh').exists()
with (root/'lara/Info.plist').open('rb') as stream:
    info=plistlib.load(stream)
assert info['CFBundleDisplayName']=='三角洲悬浮'
project=(root/'lara.xcodeproj/project.pbxproj').read_text(encoding='utf-8')
assert 'com.lujun33333.sjz.overlay' in project and 'SJZ Overlay' in project
print('PASS: active source migration, target UUID binding, collector/HUD wiring, both renderers, Chinese font, product config')
