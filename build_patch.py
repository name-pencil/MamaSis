"""원본 확인 → 번역 삽입 → 게임 폴더에 직접 적용 순서로 자동 패치한다."""
import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

from patch_common import ROOT, sha256
from install_patch import BACKUP_NAME, install_patch, restore_patch
import zipfile
from scripts.patch_text_layout import patch_scenario


def run_tool(command: list, log: Path) -> None:
	"""외부 도구 출력은 로그에 보관하고, 실패하면 다음 단계를 중단한다."""
	result = subprocess.run([str(value) for value in command],
		stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
	with log.open('ab') as stream:
		stream.write(('\n' + repr([str(value) for value in command]) + '\n').encode('utf-8'))
		stream.write(result.stdout)
	if result.returncode:
		raise RuntimeError(f'{Path(command[0]).name} 실행 실패. 상세 기록: {log}')


def build_archive(name: str, variant: str, game: Path, work: Path,
	payload: Path, log: Path, manifest: dict, verify_reference: bool = True) -> None:
	"""아카이브 한 개를 풀고 최종 번역·글자 간격을 반영한 뒤 다시 묶는다."""
	packer = ROOT / 'tools/PJADV_Pack_V2_Editor.exe'
	cryptor = ROOT / 'tools/PJADV_TextDataDat_Cryptor.exe'
	editor = ROOT / 'tools/PJADV_Text_Editor.exe'
	extracted = work / name
	extracted.mkdir()
	# PJADV 도구는 디렉터리 끝에 슬래시가 있는 경로를 요구한다.
	run_tool([packer, '-mode', 'unpack', '-dat', game / f'{name}.dat',
		'-dir', extracted.as_posix() + '/'], log)
	decrypted = work / f'{name}.dec.bin'
	scenario = extracted / 'dlscenario.dat'
	run_tool([cryptor, '-bin', extracted / 'dltextdata.bin', '-save', decrypted], log)
	translation = ROOT / 'translation/final'
	# 한글 문자열은 CP949로 넣는다. 원문의 CP932와 구분해야 한다.
	run_tool([editor, '-mode', 'import', '-text', decrypted, '-scen', scenario,
		'-jmsg', translation / f'messages.{variant}.ko.json',
		'-jseq', translation / f'sequences.{variant}.json', '-code', '949'], log)
	scenario_new = Path(str(scenario) + '.new')
	# 별도 Python 프로세스 대신 함수로 호출하므로 배포 PC에 Python이 필요 없다.
	patch_scenario(scenario_new)
	scenario_new.replace(scenario)
	run_tool([cryptor, '-bin', Path(str(decrypted) + '.new'),
		'-save', extracted / 'dltextdata.bin'], log)
	output = payload / f'{name}.dat'
	run_tool([packer, '-mode', 'repack', '-dir', extracted.as_posix() + '/',
		'-dat', output], log)
	if not output.is_file() or (verify_reference and sha256(output) != manifest['expected_outputs'][output.name]):
		raise RuntimeError(f'{output.name}: 기존 검수 완료 패치와 결과가 다릅니다.')


def default_game_dir() -> Path:
	"""더블클릭 시 현재 작업 위치가 아닌 실행 파일이 놓인 폴더를 쓴다."""
	if getattr(sys, 'frozen', False):
		return Path(sys.executable).resolve().parent
	return Path(__file__).resolve().parent


def main() -> None:
	parser = argparse.ArgumentParser(description='마마시스 한글 자동 패치')
	parser.add_argument('game_dir', nargs='?', type=Path, help='생략하면 실행 파일이 있는 게임 폴더')
	parser.add_argument('--output', type=Path, help='패치 폴더와 ZIP을 추가로 보관할 새 폴더')
	parser.add_argument('--patch-only', action='store_true', help='게임 파일을 바꾸지 않고 패치 파일만 생성')
	parser.add_argument('--no-pause', action='store_true', help='자동화 실행 시 완료 후 입력 대기 생략')
	parser.add_argument('--restore', action='store_true', help='백업에서 패치 전 상태로 복구')
	args = parser.parse_args()
	game = (args.game_dir or default_game_dir()).resolve()
	print(f'마마시스 한글 자동 패치를 시작합니다.\n게임 폴더: {game}', flush=True)
	if not (game / 'Mamashisu.exe').is_file():
		raise ValueError('Mamashisu.exe를 찾지 못했습니다. MamaSisPatch.exe를 게임 폴더에 넣어 주세요.')
	if args.restore:
		restore_patch(game)
		return
	if (game / BACKUP_NAME).exists():
		if args.no_pause or args.patch_only:
			raise ValueError('이미 패치 백업이 있습니다. --restore로 먼저 복구해 주세요.')
		print('패치 백업이 있습니다. 1: 원본 복구 / 0: 취소')
		if input('선택: ').strip() == '1':
			restore_patch(game)
		return
	manifest = json.loads((ROOT / 'manifest.json').read_text(encoding='utf-8'))
	# 누락·변경된 배포 파일이 있으면 원본 처리 전에 중단한다.
	for relative_path, expected in manifest['files'].items():
		path = ROOT / relative_path
		if not path.is_file() or sha256(path) != expected:
			raise ValueError(f'배포 파일이 없거나 변경됐습니다: {relative_path}. ZIP을 다시 풀어 주세요.')
	names = ['archive', 'appenddlc'] if (game / 'appenddlc.dat').exists() else ['archive']
	for name in ['Mamashisu.exe'] + [f'{name}.dat' for name in names]:
		path = game / name
		if not path.is_file() or sha256(path) != manifest['original_game'][name]:
			raise ValueError(f'지원하는 원본 파일과 다릅니다: {name}. 이미 패치된 파일인지 확인해 주세요.')
	output = args.output
	if args.patch_only and output is None:
		output = game / 'MamaSisPatch-output'
	if output is not None:
		output = output.resolve()
		if output.exists():
			raise ValueError('패치 보관 위치는 아직 없는 폴더여야 합니다.')
	# 생성 중에는 원본을 읽기만 한다. 완성된 패치를 백업 후 적용한다.
	with tempfile.TemporaryDirectory(prefix='mama-sis-') as temporary:
		work = Path(temporary)
		# 기존 설치 로그도 복구할 수 있도록 생성 중에는 임시 위치에 쓴다.
		log = work / 'MamaSisPatch.log'
		log.write_text('마마시스 패치 생성 기록\n', encoding='utf-8')
		payload = work / 'KoreanPatch'
		payload.mkdir()
		for index, name in enumerate(names, 1):
			print(f'[{index}/{len(names)}] {name}.dat 한글 패치 생성 중...', flush=True)
			build_archive(name, 'base' if name == 'archive' else 'dlc', game, work, payload, log, manifest)
		shutil.copytree(ROOT / 'runtime', payload, dirs_exist_ok=True)
		shutil.copy2(ROOT / 'INSTALL.txt', payload / 'INSTALL.txt')
		shutil.copy2(log, payload / 'MamaSisPatch.log')
		if output is not None:
			output.mkdir(parents=True)
			shutil.copytree(payload, output / 'KoreanPatch')
			shutil.make_archive(str(output / 'MamaSis-KoreanPatch'), 'zip', work, 'KoreanPatch')
			print(f'패치 보관 완료: {output}')
		if not args.patch_only:
			print('원본을 백업하고 게임 폴더에 패치를 적용합니다.', flush=True)
			install_patch(game, payload)
			print(f'완료! 다음 파일로 게임을 실행하세요:\n{game / "MamaSisKoreanLauncher.exe"}')
			print('복구하려면 MamaSisPatch.exe를 다시 실행하세요. 백업 ZIP은 보관해 주세요.')


if __name__ == '__main__':
	exit_code = 0
	try:
		main()
	except (OSError, ValueError, RuntimeError, subprocess.SubprocessError, zipfile.BadZipFile, KeyError) as error:
		print(f'\n패치 실패: {error}', file=sys.stderr)
		exit_code = 1
	# 더블클릭한 콘솔이 바로 닫혀 오류 안내가 사라지는 것을 막는다.
	if getattr(sys, 'frozen', False) and len(sys.argv) == 1:
		input('\nEnter 키를 누르면 닫힙니다.')
	sys.exit(exit_code)
