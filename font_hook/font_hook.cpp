#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace {
HMODULE g_self = nullptr;
LONG g_font_loaded = 0;
LONG g_text_log_count = 0;
LONG g_lead_byte_patched = 0;
using MultiByteToWideCharFn = int (WINAPI*)(UINT, DWORD, LPCCH, int, LPWSTR, int);
MultiByteToWideCharFn g_original_multi_byte_to_wide_char = nullptr;
decltype(&MessageBoxA) g_original_message_box_a = nullptr;
decltype(&CreateWindowExA) g_original_create_window_ex_a = nullptr;
decltype(&GetGlyphOutlineA) g_original_get_glyph_outline_a = nullptr;
void Log(const char* message);
bool TryPatchKoreanLeadByteRange();

#include "render_diagnostics.h"

using CreateTextBoxFn = void* (__cdecl*)(const DWORD*, DWORD);
CreateTextBoxFn g_original_create_text_box = nullptr;

// 세이브에서 복원된 본문 상자의 글자 간격을 메모리 할당 전에 보정한다.
void* __cdecl HookCreateTextBox(const DWORD* source, DWORD context) {
	// 세이브 복원과 시나리오 초기화가 같은 생성자를 사용한다.
	// 그리기 간격만 바꾸면 줄당 글자 수가 그대로여서 새 대사가 잘릴 수 있다.
	DWORD layout[21];
	std::memcpy(layout, source, sizeof(layout));
	if (layout[1] == 1 && layout[2] == 241 && layout[3] == 566 &&
		layout[4] == 778 && layout[5] == 115 && layout[8] == 28 &&
		layout[6] == 30 && layout[7] == 43) {
		layout[6] = 24;
		Log("Text box restore: body pitch 30 -> 24 before row allocation");
	}
	return g_original_create_text_box(layout, context);
}

// 확인된 기계어 호출 위치만 수정한다. 실행 파일이 다르면 패치하지 않는다.
bool InstallTextBoxRestoreHook() {
	auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
	// 819200바이트 EXE에서 확인한 호출과 주변 명령을 모두 검사한다.
	const unsigned char expected[] = {
		0x50, 0x57, 0xE8, 0x82, 0xD7, 0xFF, 0xFF,
		0x8B, 0xD8, 0x83, 0xC4, 0x08
	};
	auto* site = base + 0x766A7;
	if (std::memcmp(site, expected, sizeof(expected)) != 0) return false;
	auto* call = site + 2;
	g_original_create_text_box = reinterpret_cast<CreateTextBoxFn>(base + 0x73E30);
	const DWORD displacement = static_cast<DWORD>(
		reinterpret_cast<ULONG_PTR>(&HookCreateTextBox) -
		reinterpret_cast<ULONG_PTR>(call + 5));
	DWORD old_protect = 0;
	if (!VirtualProtect(call, 5, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
	std::memcpy(call + 1, &displacement, sizeof(displacement));
	DWORD ignored = 0;
	VirtualProtect(call, 5, old_protect, &ignored);
	FlushInstructionCache(GetCurrentProcess(), call, 5);
	return true;
}

// 게임의 일본어 시스템 문장을 유니코드 문자열로 바꾼다.
std::wstring DecodeShiftJis(const char* source) {
	if (!source) return {};
	const int length = MultiByteToWideChar(932, 0, source, -1, nullptr, 0);
	if (length <= 0) return {};
	std::wstring result(static_cast<size_t>(length), L'\0');
	MultiByteToWideChar(932, 0, source, -1, result.data(), length);
	if (!result.empty() && result.back() == L'\0') result.pop_back();
	return result;
}

// 문자열 안에서 일치하는 부분을 모두 교체한다.
void ReplaceAll(std::wstring& text, const wchar_t* from, const wchar_t* to) {
	size_t position = 0;
	const size_t from_length = std::wcslen(from);
	const size_t to_length = std::wcslen(to);
	while ((position = text.find(from, position)) != std::wstring::npos) {
		text.replace(position, from_length, to);
		position += to_length;
	}
}

// 알려진 시스템 메시지를 한글로 치환한다. 모르는 문구는 원문을 유지한다.
std::wstring TranslateSystemMessage(const char* source) {
	std::wstring text = DecodeShiftJis(source);
	if (text.empty()) return L"알 수 없는 오류가 발생했습니다.";
	ReplaceAll(text, L"\r\n", L"\n");
	const struct Translation { const wchar_t* source; const wchar_t* target; } translations[] = {
		{L"このソフトウェアをプレイするには最新のソフト電池ランタイムプログラムが\nインストールされている必要があります。", L"이 게임을 실행하려면 최신 소프트덴치 런타임 프로그램이\n설치되어 있어야 합니다."},
		{L"ソフト電池ランタイムプログラム", L"소프트덴치 런타임 프로그램"},
		{L"終了しますか？", L"게임을 종료하시겠습니까?"},
		{L"ご使用のグラフィックデバイスでは、ゲーム画面の表示が出来ません。", L"현재 그래픽 장치에서는 게임 화면을 표시할 수 없습니다."},
		{L"ビデオカード、ドライバ、DirectX のバージョンをご確認下さい。", L"그래픽 카드, 드라이버 및 DirectX 버전을 확인해 주세요."},
		{L"サウンドの初期化に失敗しました。サウンドデバイスの動作を確認して下さい。", L"사운드 초기화에 실패했습니다. 사운드 장치의 작동 상태를 확인해 주세요."},
		{L"何らかのエラーが発生しました。", L"오류가 발생했습니다."},
		{L"サウンドバッファの作成に失敗しました。", L"사운드 버퍼 생성에 실패했습니다."},
		{L"サーフェスの作成に失敗しました。", L"그래픽 표면 생성에 실패했습니다."},
		{L"テクスチャの作成に失敗しました。", L"텍스처 생성에 실패했습니다."},
		{L"ファイルの書き込みに失敗しました。", L"파일 쓰기에 실패했습니다."},
		{L"ディスク容量をチェックして下さい。", L"디스크 여유 공간을 확인해 주세요."},
		{L"ファイルの読み込みに失敗しました。", L"파일 읽기에 실패했습니다."},
		{L"SCRIPT : 未対応のコマンドです。", L"SCRIPT: 지원하지 않는 명령입니다."},
		{L"SCRIPT : 階層が深すぎます。", L"SCRIPT: 계층이 너무 깊습니다."},
		{L"メモリ確保に失敗しました。他のアプリケーションを終了させて下さい。", L"메모리 확보에 실패했습니다. 다른 프로그램을 종료해 주세요."},
		{L"これは体験版用ＥＸＥです。製品版はプレイ出来ません。", L"이 실행 파일은 체험판용입니다. 제품판을 실행할 수 없습니다."},
		{L"セーブデータのバージョンが現在のシナリオバージョンよりも新しいため、正常にロード出来ませんでした。一旦ゲームを終了します。", L"세이브 데이터가 현재 시나리오보다 새 버전이라 불러올 수 없습니다. 게임을 종료합니다."},
		{L"以前のアップデートを実行してから再度ゲームを起動して下さい。", L"이전 업데이트를 적용한 뒤 게임을 다시 실행해 주세요."},
		{L"ウィンドウ作成エラー", L"게임 창 생성 오류"},
		{L"システムファイルの読み込みに失敗しました。", L"시스템 파일을 불러오지 못했습니다."},
		{L"インストールデータが不正です。製品が正しくインストールされた環境で実行して下さい。", L"설치 데이터가 올바르지 않습니다. 제품이 정상적으로 설치된 환경에서 실행해 주세요."},
		{L"設定ファイルの読み込みに失敗しました。", L"설정 파일을 불러오지 못했습니다."},
		{L"別のユーザーが起動中です。ユーザーを切り替えて終了させてから起動して下さい。", L"다른 사용자가 게임을 실행 중입니다. 해당 사용자로 전환해 게임을 종료한 뒤 다시 실행해 주세요."},
		{L"実行できません。", L"실행할 수 없습니다."},
		{L"のダウンロードサイトに接続しますか?", L" 다운로드 사이트에 연결하시겠습니까?"},
		{L"を起動して下さい。", L"을(를) 실행해 주세요."},
	};
	for (const auto& entry : translations) ReplaceAll(text, entry.source, entry.target);
	return text;
}

// 게임의 ANSI 메시지 상자 요청을 한글 유니코드 메시지 상자로 전달한다.
int WINAPI HookMessageBoxA(HWND owner, LPCSTR text, LPCSTR, UINT type) {
	const std::wstring translated = TranslateSystemMessage(text);
	const int utf8_length = WideCharToMultiByte(CP_UTF8, 0, translated.c_str(), -1,
												nullptr, 0, nullptr, nullptr);
	if (utf8_length > 1) {
		std::string line(static_cast<size_t>(utf8_length), '\0');
		WideCharToMultiByte(CP_UTF8, 0, translated.c_str(), -1,
							line.data(), utf8_length, nullptr, nullptr);
		line.resize(static_cast<size_t>(utf8_length - 1));
		line.insert(0, "MessageBoxA Korean: ");
		Log(line.c_str());
	} else {
		Log("MessageBoxA: displayed as Korean Unicode");
	}
	return MessageBoxW(owner, translated.c_str(), L"마마시스", type);
}

// 원래 창 생성 함수를 호출한 뒤 제목만 한글로 바꾼다. 창 크기는 바꾸지 않는다.
HWND WINAPI HookCreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
								DWORD style, int x, int y, int width, int height,
								HWND parent, HMENU menu, HINSTANCE instance, LPVOID parameter) {
	HWND window = g_original_create_window_ex_a(
		ex_style, class_name, window_name, style, x, y, width, height,
		parent, menu, instance, parameter);
	if (window && !parent && window_name) {
		const std::wstring title = DecodeShiftJis(window_name);
		if (title.find(L"ver1.10") != std::wstring::npos) {
			SetWindowTextW(window, L"마마시스 ver1.10");
			Log("CreateWindowExA: main title replaced with Korean Unicode");
		}
	}
	return window;
}

// 훅 설치 상태를 DLL 옆 로그에 기록한다.
void Log(const char* message) {
	char path[MAX_PATH]{};
	if (!GetModuleFileNameA(g_self, path, MAX_PATH)) return;
	char* slash = std::strrchr(path, '\\');
	if (!slash) return;
	std::strcpy(slash + 1, "mama_sis_font.log");
	FILE* file = nullptr;
	fopen_s(&file, path, "a");
	if (!file) return;
	std::fprintf(file, "%s\n", message);
	std::fclose(file);
}

// 폰트를 시스템에 설치하지 않고 현재 게임 프로세스에서만 한 번 등록한다.
void EnsureFontLoaded() {
	if (InterlockedCompareExchange(&g_font_loaded, 1, 0) != 0) return;
	char path[MAX_PATH]{};
	GetModuleFileNameA(g_self, path, MAX_PATH);
	char* slash = std::strrchr(path, '\\');
	if (!slash) return;
	std::strcpy(slash + 1, "fonts\\Paperlogy-6SemiBold.ttf");
	const int added = AddFontResourceExA(path, FR_PRIVATE, nullptr);
	char line[MAX_PATH + 64]{};
	std::snprintf(line, sizeof(line), "Paperlogy private load: %d (%s)", added, path);
	Log(line);
}

// 글꼴 생성 요청의 문자셋과 글꼴 이름을 한글용으로 교체한다.
HFONT WINAPI HookCreateFontIndirectA(const LOGFONTA* source) {
	TryInstallRenderDiagnostics();
	TryPatchKoreanLeadByteRange();
	EnsureFontLoaded();
	LOGFONTA font = *source;
	font.lfCharSet = HANGUL_CHARSET;
	font.lfWeight = 600;
	// 엔진이 글자 표면을 다시 필터링하므로 중복 흐림을 피하도록 선명한 윤곽을 요청한다.
	font.lfQuality = NONANTIALIASED_QUALITY;
	std::strncpy(font.lfFaceName, "Paperlogy 6 SemiBold", LF_FACESIZE - 1);
	font.lfFaceName[LF_FACESIZE - 1] = '\0';
	char line[160]{};
	std::snprintf(line, sizeof(line),
					"CreateFontIndirectA: height=%ld width=%ld weight=%ld quality=%u -> %s",
					font.lfHeight, font.lfWidth, font.lfWeight, font.lfQuality, font.lfFaceName);
	Log(line);
	return CreateFontIndirectA(&font);
}

// 정수부·소수부로 나뉜 고정 소수점 값을 비율에 맞춰 계산한다.
FIXED ScaleFixed(FIXED input, long numerator, long denominator) {
	const long raw = static_cast<long>(input.value) * 65536L +
						static_cast<unsigned short>(input.fract);
	const long scaled = static_cast<long>((static_cast<long long>(raw) * numerator) / denominator);
	FIXED output{};
	output.value = static_cast<short>(scaled >> 16);
	output.fract = static_cast<unsigned short>(scaled & 0xffff);
	return output;
}

// 폭 보정이 필요한 영문자·숫자인지 확인한다.
bool IsAsciiAlphanumeric(UINT character) {
	return (character >= '0' && character <= '9') ||
			(character >= 'A' && character <= 'Z') ||
			(character >= 'a' && character <= 'z');
}

// 영문·숫자 윤곽만 좁혀 글자 겹침을 막는다. 한글 크기는 유지한다.
DWORD WINAPI HookGetGlyphOutlineA(HDC dc, UINT character, UINT format,
									LPGLYPHMETRICS metrics, DWORD buffer_size,
									LPVOID buffer, const MAT2* matrix) {
	if (!g_original_get_glyph_outline_a) return GDI_ERROR;
	if (!matrix || !IsAsciiAlphanumeric(character)) {
		return g_original_get_glyph_outline_a(
			dc, character, format, metrics, buffer_size, buffer, matrix);
	}

	// 영문 대문자와 숫자는 엔진의 반각 칸보다 넓다.
	// 해당 글자만 가로 58%로 줄이고 한글·2바이트 글자는 그대로 둔다.
	MAT2 adjusted = *matrix;
	adjusted.eM11 = ScaleFixed(adjusted.eM11, 58, 100);
	adjusted.eM21 = ScaleFixed(adjusted.eM21, 58, 100);
	return g_original_get_glyph_outline_a(
		dc, character, format, metrics, buffer_size, buffer, &adjusted);
}

// 일본어/기본 문자셋 요청을 CP949 한글 변환으로 연결한다.
int WINAPI HookMultiByteToWideChar(UINT code_page, DWORD flags, LPCCH source,
									int source_length, LPWSTR target, int target_length) {
	const UINT effective = (code_page == CP_ACP || code_page == 932) ? 949 : code_page;
	if (InterlockedIncrement(&g_text_log_count) <= 40) {
		char line[128]{};
		std::snprintf(line, sizeof(line), "MultiByteToWideChar: cp=%u -> %u, bytes=%d",
						code_page, effective, source_length);
		Log(line);
	}
	return g_original_multi_byte_to_wide_char(
		effective, flags, source, source_length, target, target_length);
}

// EXE의 IAT(외부 함수 주소 표)를 찾아 지정 함수의 주소만 훅으로 교체한다.
bool PatchImport(const char* module_name, const char* import_name,
					void* replacement, ULONG_PTR* original = nullptr) {
	auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
	if (!base) return false;
	auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
	auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
	const auto& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!directory.VirtualAddress) return false;
	auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + directory.VirtualAddress);
	for (; descriptor->Name; ++descriptor) {
		const char* module = reinterpret_cast<const char*>(base + descriptor->Name);
		if (_stricmp(module, module_name) != 0) continue;
		if (!descriptor->OriginalFirstThunk) return false;
		auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->OriginalFirstThunk);
		auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + descriptor->FirstThunk);
		for (; names->u1.AddressOfData; ++names, ++slots) {
			if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
			auto* item = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
			if (std::strcmp(reinterpret_cast<const char*>(item->Name), import_name) != 0) continue;
			DWORD old_protect = 0;
			if (!VirtualProtect(&slots->u1.Function, sizeof(void*), PAGE_READWRITE, &old_protect)) return false;
			if (original) *original = slots->u1.Function;
			slots->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);
			DWORD ignored = 0;
			VirtualProtect(&slots->u1.Function, sizeof(void*), old_protect, &ignored);
			FlushInstructionCache(GetCurrentProcess(), &slots->u1.Function, sizeof(void*));
			return true;
		}
	}
	return false;
}

// 엔진의 2바이트 문자 판별 범위를 CP949 한글 선행 바이트까지 넓힌다.
bool TryPatchKoreanLeadByteRange() {
	if (InterlockedCompareExchange(&g_lead_byte_patched, 0, 0) != 0) return true;
	// 실행 중 압축이 풀리는 EXE도 있어 고정 주소 대신 실행 가능 메모리에서 패턴을 찾는다.
	const unsigned char signature[] = {
		0x3C, 0x81, 0x72, 0x04, 0x3C, 0x9F, 0x76, 0xF2,
		0x3C, 0xE0, 0x73, 0xEE, 0x33, 0xC0, 0xC3
	};
	SYSTEM_INFO system_info{};
	GetSystemInfo(&system_info);
	auto* cursor = static_cast<unsigned char*>(system_info.lpMinimumApplicationAddress);
	auto* limit = static_cast<unsigned char*>(system_info.lpMaximumApplicationAddress);
	unsigned char* instruction = nullptr;
	while (cursor < limit) {
		MEMORY_BASIC_INFORMATION memory{};
		if (!VirtualQuery(cursor, &memory, sizeof(memory)) || memory.RegionSize == 0) break;
		const DWORD protection = memory.Protect & 0xFF;
		const bool executable = protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ ||
								protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY;
		if (memory.State == MEM_COMMIT && executable && !(memory.Protect & PAGE_GUARD) &&
			memory.RegionSize >= sizeof(signature)) {
			auto* begin = static_cast<unsigned char*>(memory.BaseAddress);
			const size_t size = memory.RegionSize;
			for (size_t offset = 0; offset + sizeof(signature) <= size; ++offset) {
				if (std::memcmp(begin + offset, signature, sizeof(signature)) == 0) {
					instruction = begin + offset + 8; // 비교 명령 3C E0 73 EE 의 시작 위치
					break;
				}
			}
		}
		if (instruction) break;
		cursor = static_cast<unsigned char*>(memory.BaseAddress) + memory.RegionSize;
	}
	if (!instruction) return false;
	DWORD old_protect = 0;
	if (!VirtualProtect(instruction, 4, PAGE_EXECUTE_READWRITE, &old_protect)) return false;
	instruction[1] = 0xA0;
	DWORD ignored = 0;
	VirtualProtect(instruction, 4, old_protect, &ignored);
	FlushInstructionCache(GetCurrentProcess(), instruction, 4);
	InterlockedExchange(&g_lead_byte_patched, 1);
	char line[96]{};
	std::snprintf(line, sizeof(line), "CP949 lead-byte parser patched at %p", instruction);
	Log(line);
	return true;
}
} // 파일 내부에서만 사용하는 함수·변수의 범위 끝

// DLL이 게임에 로드되면 필요한 훅을 설치한다. 원래 함수 주소는 별도로 보관한다.
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = module;
		DisableThreadLibraryCalls(module);
		Log(InstallTextBoxRestoreHook() ? "Text box restore hook installed" :
											"Text box restore hook unavailable: signature mismatch or protection failure");
		const bool font = PatchImport("GDI32.dll", "CreateFontIndirectA",
										reinterpret_cast<void*>(&HookCreateFontIndirectA));
		Log("hook checkpoint: font complete");
		ULONG_PTR original_glyph_outline = 0;
		const bool glyph_outline = PatchImport("GDI32.dll", "GetGlyphOutlineA",
												reinterpret_cast<void*>(&HookGetGlyphOutlineA),
												&original_glyph_outline);
		g_original_get_glyph_outline_a =
			reinterpret_cast<decltype(&GetGlyphOutlineA)>(original_glyph_outline);
		Log("hook checkpoint: glyph outline complete");
		ULONG_PTR original = 0;
		const bool text = PatchImport("KERNEL32.dll", "MultiByteToWideChar",
										reinterpret_cast<void*>(&HookMultiByteToWideChar), &original);
		g_original_multi_byte_to_wide_char = reinterpret_cast<MultiByteToWideCharFn>(original);
		Log("hook checkpoint: text complete");
		ULONG_PTR original_message_box = 0;
		const bool message_box = PatchImport("USER32.dll", "MessageBoxA",
												reinterpret_cast<void*>(&HookMessageBoxA),
												&original_message_box);
		g_original_message_box_a = reinterpret_cast<decltype(&MessageBoxA)>(original_message_box);
		Log("hook checkpoint: message box complete");
		ULONG_PTR original_create_window = 0;
		const bool create_window = PatchImport("USER32.dll", "CreateWindowExA",
												reinterpret_cast<void*>(&HookCreateWindowExA),
												&original_create_window);
		g_original_create_window_ex_a = reinterpret_cast<decltype(&CreateWindowExA)>(original_create_window);
		Log("hook checkpoint: create window complete");
		char line[160]{};
		std::snprintf(line, sizeof(line),
						"hooks: font=%d glyph-outline=%d text=%d message-box=%d create-window=%d cp949-lead-byte=deferred",
						font ? 1 : 0, glyph_outline ? 1 : 0, text ? 1 : 0, message_box ? 1 : 0,
						create_window ? 1 : 0);
		Log(line);
	}
	return TRUE;
}
