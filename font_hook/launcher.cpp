#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cwchar>

// 런처와 같은 폴더에 있는 파일의 절대 경로를 만든다.
static bool SiblingPath(wchar_t* path, const wchar_t* name) {
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
	wchar_t* slash = std::wcsrchr(path, L'\\');
	if (!slash) return false;
	return wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), name) == 0;
}

// 게임을 일시 정지 상태로 시작하고 한글 DLL을 로드한 다음 게임을 재개한다.
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
	wchar_t exe_path[MAX_PATH]{};
	wchar_t dll_path[MAX_PATH]{};
	wchar_t work_dir[MAX_PATH]{};
	if (!SiblingPath(exe_path, L"Mamashisu.exe") ||
		!SiblingPath(dll_path, L"mama_sis_font.dll") ||
		!GetModuleFileNameW(nullptr, work_dir, MAX_PATH)) return 10;
	wchar_t* slash = std::wcsrchr(work_dir, L'\\');
	if (!slash) return 11;
	*slash = L'\0';

	wchar_t pending_path[MAX_PATH]{};
	if (SiblingPath(pending_path, L"mama_sis_font.pending.dll") &&
		GetFileAttributesW(pending_path) != INVALID_FILE_ATTRIBUTES) {
		// 기존 게임이 DLL을 사용 중이면 교체하지 못한다.
		// 실패 시 준비 파일을 남기고 새 게임 실행을 중단한다.
		if (!MoveFileExW(pending_path, dll_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
			MessageBoxW(nullptr, L"게임을 완전히 종료한 뒤 런처를 다시 실행해 주세요. 준비된 패치를 적용하지 못했습니다.",
						L"마마시스 패치 적용", MB_OK | MB_ICONINFORMATION);
			return 12;
		}
	}

	wchar_t command[MAX_PATH + 4]{};
	swprintf_s(command, L"\"%s\"", exe_path);
	STARTUPINFOW startup{sizeof(startup)};
	PROCESS_INFORMATION process{};
	if (!CreateProcessW(exe_path, command, nullptr, nullptr, FALSE, CREATE_SUSPENDED,
						nullptr, work_dir, &startup, &process)) return 20;

	const SIZE_T bytes = (std::wcslen(dll_path) + 1) * sizeof(wchar_t);
	void* remote = VirtualAllocEx(process.hProcess, nullptr, bytes,
									MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote || !WriteProcessMemory(process.hProcess, remote, dll_path, bytes, nullptr)) {
		TerminateProcess(process.hProcess, 21);
		return 21;
	}
	auto load_library = reinterpret_cast<LPTHREAD_START_ROUTINE>(
		GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
	HANDLE loader = CreateRemoteThread(process.hProcess, nullptr, 0, load_library,
										remote, 0, nullptr);
	if (!loader || WaitForSingleObject(loader, 10000) != WAIT_OBJECT_0) {
		TerminateProcess(process.hProcess, 22);
		return 22;
	}
	DWORD module_handle = 0;
	GetExitCodeThread(loader, &module_handle);
	CloseHandle(loader);
	VirtualFreeEx(process.hProcess, remote, 0, MEM_RELEASE);
	if (!module_handle) {
		TerminateProcess(process.hProcess, 23);
		return 23;
	}
	ResumeThread(process.hThread);
	CloseHandle(process.hThread);
	CloseHandle(process.hProcess);
	return 0;
}
