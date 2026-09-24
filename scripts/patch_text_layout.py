"""PJADV 시나리오의 검증된 바이트 패턴을 찾아 글자 간격만 수정한다."""
import argparse
import struct
from pathlib import Path


# 명령어와 위치·크기를 함께 확인하여 다른 창 설정이 잘못 바뀌지 않게 한다.
MAIN_MESSAGE = (
	0x0300000B,
	1,
	0xF1,
	0x236,
	0x30A,
	0x73,
	0x1C,
	0x1E,
	0x2B,
	0xFFFFFF,
	1,
)

LOG_WINDOW = (
	0xF003011B,
	0x7A,
	0x12,
	0x414,
	0xAA,
	4,
	0xAD,
	0x76,
	0x0B,
	0x1C,
	0x1E,
	0x1E,
	0xFFAA0A,
	0xFFAA0A,
	0x10001,
	0x10FFFF,
	0x76,
	0x36,
	0x1C,
	0x1E,
	0x2B,
	0xFFFFFF,
	0xFFFFFF,
	0x10001,
	0x100000,
	0,
)


def packed(values: tuple[int, ...]) -> bytes:
	# 엔진의 32비트 리틀 엔디언 정수 배열로 직렬화한다.
	return struct.pack(f"<{len(values)}I", *values)


def replace_exact(
	data: bytes,
	before: tuple[int, ...],
	after: tuple[int, ...],
	expected: int,
	label: str,
) -> bytes:
	before_bytes = packed(before)
	after_bytes = packed(after)
	before_count = data.count(before_bytes)
	after_count = data.count(after_bytes)
	if before_count + after_count != expected:
		raise RuntimeError(
			f"{label} 패턴 수 불일치: 변경 전={before_count} "
			f"변경 후={after_count} 예상={expected}"
		)
	if before_count:
		data = data.replace(before_bytes, after_bytes)
	print(f"{label}: 수정={before_count} 이미 적용={after_count}")
	return data


def patch_scenario(scenario: Path) -> None:
	"""본문·로그 글자 간격만 줄인다. 게임 화면 해상도는 변경하지 않는다."""
	main_after = list(MAIN_MESSAGE)
	main_after[7] = 0x18  # 본문 글자 배치 간격: 30 → 24
	log_after = list(LOG_WINDOW)
	log_after[19] = 0x18  # 로그 글자 배치 간격: 30 → 24

	data = scenario.read_bytes()
	data = replace_exact(
		data, MAIN_MESSAGE, tuple(main_after), 1, "main-message-width"
	)
	data = replace_exact(
		data, LOG_WINDOW, tuple(log_after), 3, "log-message-width"
	)
	scenario.write_bytes(data)


def main() -> None:
	parser = argparse.ArgumentParser(description="본문·로그의 글자 간격 조정")
	parser.add_argument("--scenario", required=True, type=Path)
	args = parser.parse_args()
	patch_scenario(args.scenario)


if __name__ == "__main__":
	main()
