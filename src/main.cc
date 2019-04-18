#include <chrono>

#include <windows.h>
#include <shellapi.h>
#include <CommCtrl.h>
#include <functional>
#include <numeric>


#include <boost/filesystem.hpp>
#include <fmt/format.h>

#include "cli-parser.hpp"
#include "update-client.hpp"
#include "logger/log.h"
#include "crash-reporter.hpp"

namespace fs = boost::filesystem;
namespace chrono = std::chrono;

using chrono::high_resolution_clock;
using chrono::duration_cast;

/* Some basic constants that are adjustable at compile time */
const double average_bw_time_span = 250;
const int max_bandwidth_in_average = 8;

struct update_parameters params;
bool update_completed = false;

void ShowError(LPCWSTR lpMsg)
{
	if(params.interactive)
	{
		MessageBoxW(
			NULL,
			lpMsg,
			TEXT("Error"),
			MB_ICONEXCLAMATION | MB_OK
		);
	}
}

void ShowInfo(LPCWSTR lpMsg)
{
	if(params.interactive)
	{
		MessageBoxW(
			NULL,
			lpMsg,
			TEXT("Info"),
			MB_ICONINFORMATION | MB_OK
		);
	}
}

/* Because Windows doesn't provide us a Unicode
 * command line by default and the command line
 * it does provide us is in UTF-16LE. */
struct MultiByteCommandLine {
	MultiByteCommandLine();
	~MultiByteCommandLine();

	MultiByteCommandLine(const MultiByteCommandLine&) = delete;
	MultiByteCommandLine(MultiByteCommandLine&&) = delete;
	MultiByteCommandLine &operator=(const MultiByteCommandLine&) = delete;
	MultiByteCommandLine &operator=(MultiByteCommandLine&&) = delete;

	LPSTR *argv() { return m_argv; };
	int argc() { return m_argc; };

private:
	int m_argc{0};
	LPSTR *m_argv{nullptr};
};

MultiByteCommandLine::MultiByteCommandLine()
{
	LPWSTR lpCommandLine = GetCommandLineW();
	LPWSTR *wargv = CommandLineToArgvW(lpCommandLine, &m_argc);
	m_argv = new LPSTR[m_argc];

	/* The glory of using an unreliably sized byte
	 * type to hold a 2-byte codepoint that practically
	 * no one in the universe wants to actaully use.
	 * If there's one thing I'd like Microsoft to
	 * change, it's their shitty integration with
	 * UTF-16LE (or UCS-2 as they call it).
	 * Anyways... this converts each UTF-16LE string
	 * to UTF-8 (or something it understands). */
	for (int i = 0; i < m_argc; ++i) {
		DWORD size = WideCharToMultiByte(
			CP_UTF8, 0,
			wargv[i], -1,
			NULL, 0,
			NULL, NULL
		);

		m_argv[i] = new CHAR[size];

		size = WideCharToMultiByte(
			CP_UTF8, 0,
			wargv[i], -1,
			m_argv[i], size,
			NULL, NULL
		);
	}

	LocalFree(wargv);
}

MultiByteCommandLine::~MultiByteCommandLine()
{
	for (int i = 0; i < m_argc; ++i) {
		delete [] m_argv[i];
	}

	delete [] m_argv;
}

void LogLastError(LPCWSTR lpFunctionName)
{
	DWORD  dwError = GetLastError();
	DWORD  szMsgBuf = 1024;
	LPTSTR strMsgBuf = new wchar_t[szMsgBuf];

	FormatMessageW(
		FORMAT_MESSAGE_FROM_SYSTEM |
		FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, dwError, 0,
		strMsgBuf, szMsgBuf,
		NULL
	);

	wlog_debug(L"%s: %.*s", lpFunctionName, szMsgBuf, strMsgBuf);

	delete [] strMsgBuf;
}

BOOL StartApplication(LPWSTR lpCommandLine, LPCWSTR lpWorkingDirectory)
{
	BOOL bSuccess;
	DWORD dwShellProcId;
	HANDLE hShellProc;
	HANDLE hShellToken;
	HANDLE hNewToken;
	STARTUPINFO xStartupInfo = { 0 };
	PROCESS_INFORMATION xProcInfo = { 0 };
	HWND hwndShell = GetShellWindow();

	if (hwndShell == NULL)
		return FALSE;

	GetWindowThreadProcessId(hwndShell, &dwShellProcId);

	hShellProc = OpenProcess(
		PROCESS_QUERY_INFORMATION,
		FALSE, dwShellProcId
	);

	if (hShellProc == NULL) {
		LogLastError(L"OpenProcess");
		return FALSE;
	}

	bSuccess = OpenProcessToken(hShellProc, TOKEN_DUPLICATE, &hShellToken);

	if (bSuccess == 0) {
		LogLastError(L"OpenProcessToken");
		return FALSE;
	}

	bSuccess = DuplicateTokenEx(
		hShellToken,
		TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY |
		TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT |
		TOKEN_ADJUST_SESSIONID,
		NULL,
		SecurityImpersonation,
		TokenPrimary,
		&hNewToken
	);

	if (bSuccess == 0) {
		LogLastError(L"DuplicateTokenEx");
		return FALSE;
	}

	bSuccess = CreateProcessWithTokenW(
		hNewToken,
		0, NULL,
		lpCommandLine,
		0, NULL,
		lpWorkingDirectory,
		&xStartupInfo,
		&xProcInfo
	);

	if (bSuccess == 0) {
		LogLastError(L"CreateProcessWithTokenW");
		return FALSE;
	}

	return TRUE;
}

static LPWSTR ConvertToUtf16(const char *from, int *from_size)
{
	int to_size = MultiByteToWideChar(
		CP_UTF8, 0,
		from, *from_size,
		NULL, 0
	);

	auto to = new WCHAR[to_size];

	int size = MultiByteToWideChar(
		CP_UTF8, 0,
		from, *from_size,
		to, to_size
	);

	if (size == 0) {
		*from_size = 0;
		LogLastError(L"MultiByteToWideChar");
		return NULL;
	}

	*from_size = to_size;

	return to;
}

BOOL StartApplication(const char *lpCommandLine, const char *lpWorkingDir)
{
	/* Convert UTF-8 command line back to UTF-16 */
	int dwCLSize = -1;
	LPWSTR lpWideCommandLine = ConvertToUtf16(lpCommandLine, &dwCLSize);
	LPWSTR lpWideWorkingDir;
	BOOL bSuccess = false;

	dwCLSize = -1;

	lpWideWorkingDir = ConvertToUtf16(lpWorkingDir, &dwCLSize);

	bSuccess = StartApplication(lpWideCommandLine, lpWideWorkingDir);

	delete lpWideCommandLine;
	delete lpWideWorkingDir;

	return bSuccess;
}

struct bandwidth_chunk {
	high_resolution_clock::time_point time_point;
	size_t chunk_size;
};

/* We cannot use the default WM_CLOSE since there
 * are various things outside of our control that
 * can send this event (such as right click and close).
 * However, we also cannot send a WM_DESTROY notification
 * either (it causes jank and unexpected behavior since
 * it's expected that DestroyWindow be called). So we
 * create a custom message type here and send it to be
 * executed in the main thread instead to properly call
 * DestroyWindow for us. */
#define CUSTOM_CLOSE_MSG (WM_USER + 1)

/* Same as above but this differentiates behavior.
 * It allows us to assume there's an error and to
 * handle that error before closing. */
#define CUSTOM_ERROR_MSG (WM_USER + 2)

#define CLS_PROGRESS_LABEL (1)

static LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK ProgressLabelWndProc(
	HWND hwnd,
	UINT msg,
	WPARAM wParam,
	LPARAM lParam,
	UINT_PTR  uIdSubclass,
	DWORD_PTR dwRefData
);

struct callbacks_impl :
	public
	  client_callbacks,
	  downloader_callbacks,
	  updater_callbacks,
	  pid_callbacks
{
	int screen_width{0};
	int screen_height{0};
	int width{400};
	int height{180};
	HWND frame{NULL}; /* Toplevel window */
	HWND progress_worker{NULL};
	HWND progress_label{NULL};
	std::atomic_uint files_done{0};
	std::vector<size_t> file_sizes{0};
	size_t num_files{0};
	int num_workers{0};
	high_resolution_clock::time_point start_time;
	size_t total_consumed{0};
	size_t total_consumed_last_tick{0};
	std::list<double>  last_bandwidths;
	std::atomic<double> last_calculated_bandwidth{0.0};
	LPWSTR error_buf{nullptr};
	bool should_start{false};
	LPCWSTR label_format{L"Downloading {} of {} - {:.2f} MB/s"};

	callbacks_impl(const callbacks_impl&) = delete;
	callbacks_impl(const callbacks_impl&&) = delete;
	callbacks_impl &operator=(const callbacks_impl&) = delete;
	callbacks_impl &operator=(callbacks_impl&&) = delete;

	explicit callbacks_impl(HINSTANCE hInstance, int nCmdShow);
	~callbacks_impl();

	void initialize() final;
	void success() final;
	void error(const char* error) final;

	void downloader_start(int num_threads, size_t num_files_) final;

	void download_file(
	  int thread_index,
	  std::string &relative_path,
	  size_t size
	);

	static void bandwidth_tick(
	  HWND hwnd,
	  UINT uMsg,
	  UINT_PTR idEvent,
	  DWORD dwTime
	);

	void download_progress(
	  int thread_index,
	  size_t consumed,
	  size_t accum
	) final;

	void download_worker_finished(int thread_index) final { }
	void downloader_complete() final;

	void pid_start() final { }
	void pid_waiting_for(uint64_t pid) final { }
	void pid_wait_finished(uint64_t pid) final { }
	void pid_wait_complete() final { }

	void updater_start() final;
	void update_file(std::string &filename) final { }
	void update_finished(std::string &filename) final { }
	void updater_complete() final { }
};

callbacks_impl::callbacks_impl(HINSTANCE hInstance, int nCmdShow)
{
	BOOL success = false;
	WNDCLASSEX wc;
	RECT rcParent;

	HICON app_icon = LoadIcon(
		GetModuleHandle(NULL), TEXT("AppIcon")
	);

	wc.cbSize        = sizeof(WNDCLASSEX);
	wc.style         = CS_NOCLOSE;
	wc.lpfnWndProc   = FrameWndProc;
	wc.cbClsExtra    = 0;
	wc.cbWndExtra    = 0;
	wc.hInstance     = hInstance;
	wc.hIcon         = app_icon;
	wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(23, 36, 45));;
	wc.lpszMenuName  = NULL;
	wc.lpszClassName = TEXT("UpdaterFrame");
	wc.hIconSm       = app_icon;

	if(!RegisterClassEx(&wc)) {
		ShowError(L"Window registration failed!");
		LogLastError(L"RegisterClassEx");

		throw std::runtime_error("window registration failed");
	}

	/* We only care about the main display */
	screen_width = GetSystemMetrics(SM_CXSCREEN);
	screen_height = GetSystemMetrics(SM_CYSCREEN);

	/* FIXME: This feels a little dirty */
	auto do_fail = [this] (LPCWSTR user_msg, LPCWSTR context_msg) {
		if (this->frame) DestroyWindow(this->frame);
		ShowError(user_msg);
		LogLastError(context_msg);
		throw std::runtime_error("");
	};

	frame = CreateWindowEx(
		WS_EX_CLIENTEDGE,
		TEXT("UpdaterFrame"),
		TEXT("Streamlabs OBS Updater"),
		WS_OVERLAPPED | WS_MINIMIZEBOX | WS_SYSMENU,
		(screen_width - width) / 2,
		(screen_height - height) / 2,
		width, height,
		NULL, NULL,
		hInstance, NULL
	);

	SetWindowLongPtr(frame, GWLP_USERDATA, (LONG_PTR)this);

	if (!frame) {
		do_fail(L"Failed to create window!", L"CreateWindowEx");
	}

	GetClientRect(frame, &rcParent);

	int x_pos = 10;
	int y_size = 40;
	int x_size = (rcParent.right - rcParent.left) - (x_pos * 2);
	int y_pos = ((rcParent.bottom - rcParent.top) / 2) - (y_size / 2);

	progress_worker = CreateWindow(
		PROGRESS_CLASS,
		TEXT("ProgressWorker"),
		WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
		x_pos, y_pos,
		x_size, y_size,
		frame, NULL,
		NULL, NULL
	);

	if (!progress_worker) {
		do_fail(L"Failed to create progress worker!", L"CreateWindow");
	}

	progress_label = CreateWindow(
		WC_STATIC,
		TEXT("Looking for new files..."),
		WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
		x_pos, 0,
		x_size, (y_pos),
		frame, NULL,
		NULL, NULL
	);

	if (!progress_label) {
		do_fail(L"Failed to create progress label!", L"CreateWindow");
	}

	success = SetWindowSubclass(
		progress_label,
		ProgressLabelWndProc,
		CLS_PROGRESS_LABEL,
		(DWORD_PTR)this
	);

	if (!success) {
		do_fail(
			L"Failed to subclass progress label!",
			L"SetWindowSubclass"
		);
	}

	SendMessage(progress_worker, PBM_SETBARCOLOR, 0, RGB(49, 195, 162));
	SendMessage(progress_worker, PBM_SETRANGE32, 0, INT_MAX);
}

callbacks_impl::~callbacks_impl()
{
}

void callbacks_impl::initialize()
{
	ShowWindow(frame, SW_SHOWNORMAL);
	UpdateWindow(frame);
}

void callbacks_impl::success()
{
	should_start = true;
	PostMessage(frame, CUSTOM_CLOSE_MSG, NULL, NULL);
}

void callbacks_impl::error(const char* error)
{
	int error_sz = -1;

	this->error_buf = ConvertToUtf16(error, &error_sz);

	PostMessage(frame, CUSTOM_ERROR_MSG, NULL, NULL);
}

void callbacks_impl::downloader_start(int num_threads, size_t num_files_)
{
	file_sizes.resize(num_threads, 0);
	this->num_files = num_files_;
	start_time = high_resolution_clock::now();

	SetTimer(frame, 1, average_bw_time_span, &bandwidth_tick);
}

void callbacks_impl::download_file( int thread_index, std::string &relative_path, size_t size) 
{
	/* Our specific UI doesn't care when we start, we only
	 * care when we're finished. A more technical UI could show
	 * what each thread is doing if they so wanted. */
	file_sizes[thread_index] = size;
}

void callbacks_impl::bandwidth_tick(
  HWND hwnd,
  UINT uMsg,
  UINT_PTR idEvent,
  DWORD dwTime
) {
	LONG_PTR data = GetWindowLongPtr(hwnd, GWLP_USERDATA);
	auto ctx = reinterpret_cast<callbacks_impl *>(data);

	/* Compare current total to last previous total,
	 * then divide by timeout time */
	double bandwidth = (double)(ctx->total_consumed - ctx->total_consumed_last_tick);
	ctx->total_consumed_last_tick = ctx->total_consumed;
	
	
	ctx->last_bandwidths.push_back(bandwidth);
	while (ctx->last_bandwidths.size() > max_bandwidth_in_average)
	{
		ctx->last_bandwidths.pop_front();
	}
	
	double average_bandwidth = std::accumulate(ctx->last_bandwidths.begin(), ctx->last_bandwidths.end(), 0.0);
	//std::for_each(ctx->last_bandwidths.begin(), ctx->last_bandwidths.end(), [&average_bandwidth](double &n) { average_bandwidth+=n; });
	if (ctx->last_bandwidths.size() > 0)
	{
		average_bandwidth /= ctx->last_bandwidths.size();
	}

	/* Average over a set period of time */
	average_bandwidth /= average_bw_time_span / 1000;
	
	/* Convert from bytes to megabytes */
	/* Note that it's important to have only one place where
	 * we atomically assign to last_calculated_bandwidth */
	
	ctx->last_calculated_bandwidth = average_bandwidth * 0.000001;

	std::wstring label(fmt::format(
		ctx->label_format,
		ctx->files_done,
		ctx->num_files,
		ctx->last_calculated_bandwidth
	));

	SetWindowTextW(ctx->progress_label, label.c_str());
	SetTimer(hwnd, idEvent, average_bw_time_span, &bandwidth_tick);
}


void callbacks_impl::download_progress( int thread_index, size_t consumed, size_t accum) 
{
	total_consumed += consumed;
	/* We don't currently show per-file progress but we could
	 * progress the bar based on files_done + remainder of
	 * all in-progress files done. */

	if (accum != file_sizes[thread_index]) 
	{
		return;
	}

	++files_done;

	double percent = (double)files_done / (double)num_files;

	std::wstring label(fmt::format( label_format, files_done, num_files, last_calculated_bandwidth ));

	int pos = lround(percent * INT_MAX);
	PostMessage(progress_worker, PBM_SETPOS, pos, 0);
	SetWindowTextW(progress_label, label.c_str());
}

void callbacks_impl::downloader_complete()
{
	KillTimer(frame, 1);
}

void callbacks_impl::updater_start()
{
	SetWindowTextW(progress_label, L"Copying files...");
}

LRESULT CALLBACK ProgressLabelWndProc(
  HWND hwnd,
  UINT msg,
  WPARAM wParam,
  LPARAM lParam,
  UINT_PTR  uIdSubclass,
  DWORD_PTR dwRefData
) {
	switch(msg) {
	case WM_SETTEXT: {
		RECT rect;
		HWND parent = GetParent(hwnd);

		GetWindowRect(hwnd, &rect);
		MapWindowPoints(HWND_DESKTOP, parent, (LPPOINT)&rect, 2);

		RedrawWindow(
			parent,
			&rect,
			NULL,
			RDW_ERASE | RDW_INVALIDATE
		);

		break;
	}
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch(msg) {
	case WM_CLOSE:
		/* Prevent closing in a normal manner. */
		return 0;
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	case CUSTOM_CLOSE_MSG:
		DestroyWindow(hwnd);
		break;
	case CUSTOM_ERROR_MSG: {
		LONG_PTR user_data = GetWindowLongPtr(hwnd, GWLP_USERDATA);
		auto ctx = reinterpret_cast<callbacks_impl *>(user_data);

		ShowError(ctx->error_buf);
		delete [] ctx->error_buf;
		ctx->error_buf = nullptr;

		DestroyWindow(hwnd);

		break;
	}
	case WM_CTLCOLORSTATIC:
		SetTextColor((HDC)wParam, RGB(255, 255, 255));
		SetBkMode((HDC)wParam, TRANSPARENT);
		return (LRESULT)GetStockObject(HOLLOW_BRUSH);
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}
 
extern "C"
int wWinMain(
  HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPWSTR    lpCmdLineUnused,
  int       nCmdShow
) {
	
	setup_crash_reporting();

	callbacks_impl cb_impl(hInstance, nCmdShow);

	MultiByteCommandLine command_line;

	update_completed = su_parse_command_line(
		command_line.argc(),
		command_line.argv(),
		&params
	);

	if (!update_completed) {
		ShowError(L"Failed to parse cli arguments!");
		return 0;
	}

	auto client_deleter = [] (struct update_client *client) {
		destroy_update_client(client);
	};
	

	std::unique_ptr<struct update_client, decltype(client_deleter)>
		client(create_update_client(&params), client_deleter);

	update_client_set_client_events(client.get(), &cb_impl);
	update_client_set_downloader_events(client.get(), &cb_impl);
	update_client_set_updater_events(client.get(), &cb_impl);
	update_client_set_pid_events(client.get(), &cb_impl);

	update_client_start(client.get());

	MSG msg;

	while(GetMessage(&msg, NULL, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	update_client_flush(client.get());

	/* Don't attempt start if application failed to update */
	if (!cb_impl.should_start) {
		return 1;
	}

#define THERE_OR_NOT(x) \
	((x).empty() ? NULL : (x).c_str())

	update_completed = StartApplication(
		THERE_OR_NOT(params.exec),
		THERE_OR_NOT(params.exec_cwd)
	);

	if (!update_completed) {
		ShowInfo(
			L"The application has finished updating.\n"
			"Please manually start Streamlabs OBS."
		);
	}
#undef THERE_OR_NOT

	return 0;
}