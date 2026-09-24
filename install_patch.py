"""게임 폴더에 직접 패치하고, 교체 전 파일을 ZIP에 보관하여 복구한다."""
import json
import os
import shutil
import tempfile
import zipfile
from pathlib import Path

from patch_common import sha256

BACKUP_NAME = 'MamaSisPatch-backup.zip'
GENERATED_LOGS = {'MamaSisPatch.log', 'mama_sis_font.log', 'mama_sis_render.log'}
# 게임·세이브 전체가 아니라 설치 프로그램이 수정하는 파일만 취급한다.
ALLOWED_FILES = {
	'archive.dat', 'appenddlc.dat', 'MamaSisKoreanLauncher.exe',
	'mama_sis_font.dll', 'fonts/Paperlogy-6SemiBold.ttf',
} | GENERATED_LOGS


def target_path(game: Path, name: str) -> Path:
	"""백업 안의 경로도 허용 목록과 대조하여 게임 폴더 밖을 수정하지 않는다."""
	if name not in ALLOWED_FILES:
		raise ValueError(f'알 수 없는 패치 대상입니다: {name}')
	path = game / name
	if path.is_symlink() or not path.resolve().is_relative_to(game.resolve()):
		raise ValueError(f'패치 대상이 게임 폴더 밖으로 연결되어 있습니다: {name}')
	return path


def replace_file(source: Path, destination: Path) -> None:
	"""새 파일을 같은 폴더에 준비한 뒤 교체한다. 기존 하드링크 내용도 바꾸지 않는다."""
	destination.parent.mkdir(parents=True, exist_ok=True)
	handle, temporary = tempfile.mkstemp(prefix='.mama-patch-', dir=destination.parent)
	os.close(handle)
	temporary = Path(temporary)
	try:
		shutil.copy2(source, temporary)
		os.replace(temporary, destination)
	finally:
		temporary.unlink(missing_ok=True)


def restore_patch(game: Path) -> None:
	"""모든 백업을 먼저 검증한 뒤 복원한다. 중단되면 ZIP을 남겨 재시도할 수 있다."""
	backup = game / BACKUP_NAME
	if not backup.is_file():
		raise ValueError('복구할 백업이 없습니다. 패치했던 게임 폴더인지 확인해 주세요.')
	with tempfile.TemporaryDirectory(prefix='mama-restore-') as temporary:
		stage = Path(temporary)
		with zipfile.ZipFile(backup) as archive:
			records = json.loads(archive.read('backup.json'))
			# 구버전 백업에는 로그·폴더 기록이 없어 전용 로그와 빈 fonts만 정리한다.
			cleanup = json.loads(archive.read('cleanup.json')) if 'cleanup.json' in archive.namelist() else {'remove_fonts': True}
			if not isinstance(records, dict) or not records:
				raise ValueError('백업 목록이 올바르지 않습니다.')
			for name, digest in records.items():
				target_path(game, name)
				if digest is None:
					continue
				path = stage / name
				path.parent.mkdir(parents=True, exist_ok=True)
				with archive.open('original/' + name) as source, path.open('wb') as output:
					shutil.copyfileobj(source, output)
				if sha256(path) != digest:
					raise ValueError(f'백업 파일이 손상되었습니다: {name}')
		# 백업 검증이 끝나기 전에는 게임 파일에 손대지 않는다.
		for name, digest in records.items():
			destination = target_path(game, name)
			if digest is None:
				destination.unlink(missing_ok=True)
			else:
				replace_file(stage / name, destination)
		for name in GENERATED_LOGS - records.keys():
			target_path(game, name).unlink(missing_ok=True)
		fonts = game / 'fonts'
		if cleanup.get('remove_fonts') and fonts.is_dir() and not fonts.is_symlink() and not any(fonts.iterdir()):
			# 비어 있을 때만 제거한다. 사용자가 추가한 다른 글꼴은 보존한다.
			fonts.rmdir()
	# 복원이 끝난 경우에만 설치 상태인 백업 ZIP을 제거한다.
	backup.unlink()
	print('복구 완료: 패치 전 파일로 되돌렸습니다. 세이브는 유지됩니다.')


def install_patch(game: Path, payload: Path) -> None:
	"""교체 대상의 원본을 먼저 백업하고 패치를 적용한다. 실패하면 자동 복구한다."""
	backup = game / BACKUP_NAME
	if backup.exists():
		raise ValueError('기존 패치 백업이 있습니다. 먼저 복구한 뒤 다시 패치해 주세요.')
	names = sorted(p.relative_to(payload).as_posix() for p in payload.rglob('*')
		if p.is_file() and p.relative_to(payload).as_posix() in ALLOWED_FILES)
	records = {}
	with tempfile.TemporaryDirectory(prefix='.mama-backup-', dir=game) as temporary:
		pending = Path(temporary) / BACKUP_NAME
		with zipfile.ZipFile(pending, 'w', zipfile.ZIP_DEFLATED) as archive:
			for name in sorted(set(names) | GENERATED_LOGS):
				source = target_path(game, name)
				records[name] = sha256(source) if source.exists() else None
				if source.exists():
					archive.write(source, 'original/' + name)
			archive.writestr('backup.json', json.dumps(records, indent='\t'))
			archive.writestr('cleanup.json', json.dumps({'remove_fonts': not (game / 'fonts').exists()}))
		# 원본 백업을 완성한 뒤에만 파일 교체를 시작한다.
		pending.rename(backup)
	try:
		for name in names:
			replace_file(payload / name, target_path(game, name))
	except OSError as install_error:
		try:
			restore_patch(game)
		except (OSError, ValueError, zipfile.BadZipFile) as restore_error:
			raise RuntimeError(f'설치 중단: {install_error}. 자동 복구도 끝나지 못했습니다: {restore_error}. 게임을 종료하고 다시 실행해 복구를 선택하세요. 백업 ZIP은 보존했습니다.') from install_error
		raise RuntimeError(f'설치가 실패하여 원본으로 복구했습니다: {install_error}') from install_error
