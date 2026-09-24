"""배포 데이터 경로와 파일 해시 계산을 공유한다."""
import hashlib
import sys
from pathlib import Path

# 단일 EXE는 실행 중 임시로 푼 폴더, 소스 실행은 프로젝트 폴더에서 데이터를 찾는다.
ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))


def sha256(path: Path) -> str:
	"""큰 파일도 1MB씩 읽어 메모리 사용을 제한한다."""
	digest = hashlib.sha256()
	with path.open('rb') as stream:
		for block in iter(lambda: stream.read(1024 * 1024), b''):
			digest.update(block)
	return digest.hexdigest()
