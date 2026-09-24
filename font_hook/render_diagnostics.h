// font_hook.cpp의 익명 네임스페이스 안에 포함된다.
// 통계는 렌더링 콜백에서만 갱신하며 별도 폴링 스레드를 만들지 않는다.
using DrawUPFn = HRESULT (WINAPI*)(void*, UINT, UINT, const void*, UINT);
using PresentFn = HRESULT (WINAPI*)(void*, const RECT*, const RECT*, HWND, const RGNDATA*);
DrawUPFn g_draw_up = nullptr;
PresentFn g_present = nullptr;
struct RenderStats {
	LONGLONG start = 0, draw_ticks = 0, present_ticks = 0;
	LONGLONG max_draw = 0, max_present = 0, last_frame = 0, max_frame = 0;
	unsigned long long draws = 0, frames = 0, primitives = 0, failures = 0;
} g_render_stats;
LONGLONG g_render_frequency = 0;

// 성능 카운터의 현재 값을 읽는다. 주파수로 나누면 경과 시간을 구할 수 있다.
LONGLONG RenderClock() {
	LARGE_INTEGER value;
	QueryPerformanceCounter(&value);
	return value.QuadPart;
}

// 매 그리기마다 파일을 쓰지 않고 10초 동안 쌓인 통계만 기록한다.
void ReportRenderStats(LONGLONG now) {
	auto& s = g_render_stats;
	if (!s.start) s.start = now;
	const double seconds = double(now - s.start) / g_render_frequency;
	if (seconds < 10.0) return;
	char path[MAX_PATH]{};
	GetModuleFileNameA(g_self, path, MAX_PATH);
	char* slash = std::strrchr(path, '\\');
	if (!slash) return;
	std::strcpy(slash + 1, "mama_sis_render.log");
	FILE* file = nullptr;
	fopen_s(&file, path, "a");
	if (file) {
		SYSTEMTIME time;
		GetLocalTime(&time);
		const double ms = 1000.0 / g_render_frequency;
		std::fprintf(file,
			"%04u-%02u-%02u %02u:%02u:%02u pid=%lu window_s=%.2f fps=%.2f "
			"draws=%llu draws_per_frame=%.2f primitives=%llu "
			"draw_ms=%.2f max_draw_ms=%.2f present_ms=%.2f max_present_ms=%.2f "
			"max_frame_ms=%.2f failures=%llu gdi=%lu handles=%lu\n",
			time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
			GetCurrentProcessId(), seconds, s.frames / seconds,
			s.draws, s.frames ? double(s.draws) / s.frames : 0.0, s.primitives,
			s.draw_ticks * ms, s.max_draw * ms, s.present_ticks * ms,
			s.max_present * ms, s.max_frame * ms, s.failures,
			GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS), []() -> DWORD {
				DWORD count = 0; GetProcessHandleCount(GetCurrentProcess(), &count); return count;
			}());
		std::fclose(file);
	}
	const LONGLONG last = s.last_frame;
	s = {};
	s.start = now;
	s.last_frame = last;
}

// 원래 그리기 함수를 호출하고 소요 시간·오류 수를 누적한다.
HRESULT WINAPI DiagnosticDrawUP(void* device, UINT type, UINT count,
								const void* vertices, UINT stride) {
	const auto begin = RenderClock();
	const HRESULT result = g_draw_up(device, type, count, vertices, stride);
	const auto end = RenderClock();
	auto& s = g_render_stats;
	++s.draws;
	s.primitives += count;
	s.draw_ticks += end - begin;
	if (end - begin > s.max_draw) s.max_draw = end - begin;
	if (FAILED(result)) ++s.failures;
	ReportRenderStats(end);
	return result;
}

// 원래 화면 표시 함수를 호출하고 프레임 간격·소요 시간을 누적한다.
HRESULT WINAPI DiagnosticPresent(void* device, const RECT* source, const RECT* dest,
									HWND window, const RGNDATA* dirty) {
	const auto begin = RenderClock();
	const HRESULT result = g_present(device, source, dest, window, dirty);
	const auto end = RenderClock();
	auto& s = g_render_stats;
	++s.frames;
	s.present_ticks += end - begin;
	if (end - begin > s.max_present) s.max_present = end - begin;
	if (s.last_frame && end - s.last_frame > s.max_frame) s.max_frame = end - s.last_frame;
	s.last_frame = end;
	if (FAILED(result)) ++s.failures;
	ReportRenderStats(end);
	return result;
}

// 검증된 D3D 장치의 함수 주소 표에 진단 함수를 연결한다.
void TryInstallRenderDiagnostics() {
	if (g_draw_up) return;
	auto* base = reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));
	// 검증된 DrawPrimitiveUP 호출이 참조하는 장치 주소인지 확인한다.
	const unsigned char signature[] = {0xA1, 0x2C, 0x09, 0x4C, 0x00, 0x83, 0xC4, 0x20};
	if (std::memcmp(base + 0x4CA96, signature, sizeof(signature))) return;
	auto* device = *reinterpret_cast<void***>(base + 0xC092C);
	if (!device) return;
	void** table = *reinterpret_cast<void***>(device);
	LARGE_INTEGER frequency;
	QueryPerformanceFrequency(&frequency);
	g_render_frequency = frequency.QuadPart;
	DWORD protection = 0;
	if (!VirtualProtect(table + 17, 67 * sizeof(void*), PAGE_READWRITE, &protection)) return;
	g_present = reinterpret_cast<PresentFn>(table[17]);
	g_draw_up = reinterpret_cast<DrawUPFn>(table[83]);
	table[17] = reinterpret_cast<void*>(&DiagnosticPresent);
	table[83] = reinterpret_cast<void*>(&DiagnosticDrawUP);
	DWORD ignored;
	VirtualProtect(table + 17, 67 * sizeof(void*), protection, &ignored);
	Log("Render diagnostics installed: Present + DrawPrimitiveUP; reports every 10 seconds");
}
