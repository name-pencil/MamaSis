"""컴파일된 DLL·런처와 최종 번역만 골라 자동 패치 배포 ZIP을 만든다."""
import json
import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from patch_common import sha256


def main() -> None:
	parser = argparse.ArgumentParser(description='한글 패치 릴리즈 빌드')
	parser.add_argument('--game-dir', type=Path, help='번역 수정 후 기준 해시를 갱신할 원본 게임 폴더')
	args = parser.parse_args()
	version = (ROOT / 'VERSION').read_text(encoding='utf-8').strip()
	if not re.fullmatch(r'\d+\.\d+\.\d+', version):
		raise ValueError('VERSION은 숫자.숫자.숫자 형식이어야 합니다.')
	if os.environ.get('GITHUB_REF_TYPE') == 'tag' and os.environ.get('GITHUB_REF_NAME') != 'v' + version:
		raise ValueError('태그와 VERSION의 버전이 다릅니다.')
	# CI에서 컴파일한 바이너리의 해시는 매번 달라질 수 있으므로 여기서 갱신한다.
	manifest_path = ROOT / 'manifest.json'
	manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
	manifest['version'] = version
	# 번역 수정 시 과거 완료본 해시를 그대로 쓰면 설치가 거부된다.
	translation_changed = any(sha256(ROOT / name) != digest for name, digest in manifest['files'].items()
		if name.startswith('translation/final/'))
	if translation_changed or args.game_dir:
		if not args.game_dir:
			raise ValueError('번역이 변경됐습니다. --game-dir로 패치 전 원본 게임 폴더를 지정해 주세요.')
		from refresh_translation import refresh_translation
		manifest['expected_outputs'] = refresh_translation(args.game_dir.resolve(), manifest)
	manifest.pop('source_workspace', None)
	files = {}
	for folder in ('translation/final', 'tools', 'runtime'):
		for path in sorted((ROOT / folder).rglob('*')):
			if path.is_file() and path.suffix in ('.json', '.exe', '.dll', '.ttf'):
				files[path.relative_to(ROOT).as_posix()] = sha256(path)
	manifest['files'] = files
	manifest_path.write_text(json.dumps(manifest, indent='\t') + '\n', encoding='utf-8')
	# onefile 형식: 데이터와 Python을 EXE 안에 넣고 실행 중에만 임시로 푼다.
	command = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean',
		'--onefile', '--console', '--name', 'MamaSisPatch',
		'--distpath', str(ROOT / 'dist'), '--workpath', str(ROOT / 'build/pyinstaller'),
		'--specpath', str(ROOT / 'build')]
	# 폴더 전체를 넣지 않는다. 검수 이력·중간 산출물은 배포에서 제외한다.
	for name in ('manifest.json', 'INSTALL.txt', 'LICENSE', 'VERSION', *files):
		command.extend(['--add-data', f'{ROOT / name}:{Path(name).parent.as_posix()}'])
	command.append(str(ROOT / 'build_patch.py'))
	subprocess.run(command, cwd=ROOT, check=True)
	release = ROOT / 'build/release-single'
	release.mkdir(parents=True, exist_ok=True)
	shutil.copy2(ROOT / 'dist/MamaSisPatch.exe', release / 'MamaSisPatch.exe')
	shutil.copy2(ROOT / 'INSTALL.txt', release / '한글패치_사용안내.txt')
	shutil.copy2(ROOT / 'LICENSE', release / 'LICENSE.txt')
	archive = shutil.make_archive(str(ROOT / 'dist/MamaSis-KoreanPatch-Setup'), 'zip', release)
	print(f'릴리즈 ZIP 생성 완료: {archive}')


if __name__ == '__main__':
	main()
