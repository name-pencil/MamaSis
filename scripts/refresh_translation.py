"""수정한 JSON으로 아카이브를 만들고 재추출한 번역을 확인해 기준 해시를 갱신한다."""
import json
import tempfile
from pathlib import Path

from build_patch import build_archive, run_tool
from patch_common import ROOT, sha256


def refresh_translation(game: Path, manifest: dict) -> dict:
	# 본편 파일은 통합본의 앞부분이므로 두 파일을 서로 다른 번역으로 배포하지 않는다.
	translation = ROOT / 'translation/final'
	base = json.loads((translation / 'messages.base.ko.json').read_text(encoding='utf-8'))
	dlc = json.loads((translation / 'messages.dlc.ko.json').read_text(encoding='utf-8'))
	if len(base) != 23026 or len(dlc) != 24219 or base != dlc[:len(base)]:
		raise ValueError('본편 23026항목은 통합본의 앞 23026항목과 같아야 합니다.')
	for row in dlc:
		for key, value in row.items():
			if key.endswith('_tra') and isinstance(value, str):
				value.encode('cp949')
	for name in ('archive.dat', 'appenddlc.dat'):
		if not (game / name).is_file() or sha256(game / name) != manifest['original_game'][name]:
			raise ValueError(f'기준 갱신에는 본편과 DLC의 패치 전 원본이 필요합니다: {name}')
	results = {}
	with tempfile.TemporaryDirectory(prefix='mama-translation-') as temporary:
		work = Path(temporary)
		payload = work / 'patch'
		payload.mkdir()
		log = work / 'build.log'
		for name, variant, expected in [('archive', 'base', base), ('appenddlc', 'dlc', dlc)]:
			build_archive(name, variant, game, work, payload, log, manifest, verify_reference=False)
			unpacked = work / (name + '-check')
			unpacked.mkdir()
			tools = ROOT / 'tools'
			run_tool([tools / 'PJADV_Pack_V2_Editor.exe', '-mode', 'unpack', '-dat', payload / (name + '.dat'), '-dir', unpacked.as_posix() + '/'], log)
			text = unpacked / 'text.bin'
			run_tool([tools / 'PJADV_TextDataDat_Cryptor.exe', '-bin', unpacked / 'dltextdata.bin', '-save', text], log)
			messages = unpacked / 'messages.json'
			sequences = unpacked / 'sequences.json'
			run_tool([tools / 'PJADV_Text_Editor.exe', '-mode', 'export', '-text', text, '-scen', unpacked / 'dlscenario.dat', '-jmsg', messages, '-jseq', sequences, '-code', '949'], log)
			actual = json.loads(messages.read_text(encoding='utf-8-sig'))
			if len(actual) != len(expected):
				raise ValueError(f'{name}: 재추출 항목 수 불일치')
			for before, after in zip(expected, actual):
				for key, value in before.items():
					if key.endswith('_tra') and after.get(key[:-4] + '_org') != value:
						raise ValueError(f'{name}: 재추출 번역 불일치 ({key})')
			if json.loads(sequences.read_text(encoding='utf-8-sig')) != json.loads((translation / f'sequences.{variant}.json').read_text(encoding='utf-8')):
				raise ValueError(f'{name}: 재추출 시퀀스 불일치')
			results[name + '.dat'] = sha256(payload / (name + '.dat'))
	return results
