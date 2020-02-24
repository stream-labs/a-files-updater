#include <chrono>

#include <windows.h>
#include <CommCtrl.h>
#include <functional>
#include <numeric>


#include <fmt/format.h>

#include "cli-parser.hpp"
#include "update-client.hpp"
#include "logger/log.h" 
#include "crash-reporter.hpp"
#include "utils.hpp"
#include <atomic>

namespace fs = std::filesystem;
namespace chrono = std::chrono;

using chrono::high_resolution_clock;
using chrono::duration_cast;

struct update_parameters params;

/* Some basic constants that are adjustable at compile time */
const double average_bw_time_span = 250;
const int max_bandwidth_in_average = 8;

const int ui_padding = 10;
const int ui_basic_height = 40;

bool update_completed = false;

void ShowError(LPCWSTR lpMsg)
{
	if (params.interactive)
	{
		MessageBoxW(NULL, lpMsg, TEXT("Error"), MB_ICONEXCLAMATION | MB_OK);
	}
}

void ShowInfo(LPCWSTR lpMsg)
{
	if (params.interactive)
	{
		MessageBoxW(NULL, lpMsg, TEXT("Info"), MB_ICONINFORMATION | MB_OK);
	}
}

struct bandwidth_chunk {
	high_resolution_clock::time_point time_point;
	size_t chunk_size;
};


static LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

static LRESULT CALLBACK ProgressLabelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR  uIdSubclass, DWORD_PTR dwRefData);
static LRESULT CALLBACK BlockersListWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR  uIdSubclass, DWORD_PTR dwRefData);

struct callbacks_impl :
	public
	client_callbacks,
	downloader_callbacks,
	updater_callbacks,
	pid_callbacks,
	blocker_callbacks
{
	int screen_width{ 0 };
	int screen_height{ 0 };
	int width{ 500 };
	int height{ ui_basic_height*4+ui_padding*2 };

	HWND frame{ NULL }; /* Toplevel window */
	HWND progress_worker{ NULL };
	HWND progress_label{ NULL };
	HWND blockers_list{ NULL };
	HWND kill_button{ NULL };
	HWND cancel_button{ NULL };

	std::atomic_uint files_done{ 0 };
	std::vector<size_t> file_sizes{ 0 };
	size_t num_files{ 0 };
	int num_workers{ 0 };
	high_resolution_clock::time_point start_time;
	size_t total_consumed{ 0 };
	size_t total_consumed_last_tick{ 0 };
	std::list<double>  last_bandwidths;
	std::atomic<double> last_calculated_bandwidth{ 0.0 };
	LPWSTR error_buf{ nullptr };
	bool should_start{ false };
	bool should_cancel{ false };
	bool should_kill_blockers{ false };
	LPCWSTR label_format{ L"Downloading {} of {} - {:.2f} MB/s" };

	callbacks_impl(const callbacks_impl&) = delete;
	callbacks_impl(const callbacks_impl&&) = delete;
	callbacks_impl &operator=(const callbacks_impl&) = delete;
	callbacks_impl &operator=(callbacks_impl&&) = delete;

	explicit callbacks_impl(HINSTANCE hInstance, int nCmdShow);
	~callbacks_impl();

	void initialize() final;
	void success() final;
	void error(const char* error, const char * error_type) final;

	void downloader_preparing() final;
	void downloader_start(int num_threads, size_t num_files_) final;
	void download_file(int thread_index, std::string &relative_path, size_t size);
	void download_progress(int thread_index, size_t consumed, size_t accum) final;
	void download_worker_finished(int thread_index) final { }
	void downloader_complete() final;
	static void bandwidth_tick(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime);

	void pid_start() final { }
	void pid_waiting_for(uint64_t pid) final { }
	void pid_wait_finished(uint64_t pid) final { }
	void pid_wait_complete() final { }

	void blocker_start() final;
	int blocker_waiting_for(const std::wstring &processes_list, bool list_changed) final;
	void blocker_wait_complete() final;

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

	HICON app_icon = LoadIcon(GetModuleHandle(NULL), TEXT("AppIcon"));

	wc.cbSize = sizeof(WNDCLASSEX);
	wc.style = CS_NOCLOSE;
	wc.lpfnWndProc = FrameWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInstance;
	wc.hIcon = app_icon;
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = CreateSolidBrush(RGB(23, 36, 45));;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = TEXT("UpdaterFrame");
	wc.hIconSm = app_icon;

	if (!RegisterClassEx(&wc))
	{
		ShowError(L"Window registration failed!");
		LogLastError(L"RegisterClassEx");

		throw std::runtime_error("window registration failed");
	}

	/* We only care about the main display */
	screen_width = GetSystemMetrics(SM_CXSCREEN);
	screen_height = GetSystemMetrics(SM_CYSCREEN);

	/* FIXME: This feels a little dirty */
	auto do_fail = [this](LPCWSTR user_msg, LPCWSTR context_msg) {
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

	if (!frame)
	{
		do_fail(L"Failed to create window!", L"CreateWindowEx");
	}

	GetClientRect(frame, &rcParent);

	int x_pos = ui_padding;
	int y_size = ui_basic_height;
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

	if (!progress_worker)
	{
		do_fail(L"Failed to create progress worker!", L"CreateWindow");
	}
	
	progress_label = CreateWindow(
		WC_STATIC,
		TEXT("Looking for new files..."),
		WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
		x_pos, ui_padding,
		x_size, ui_basic_height,
		frame, NULL,
		NULL, NULL
	);

	if (!progress_label)
	{
		do_fail(L"Failed to create progress label!", L"CreateWindow");
	}

	success = SetWindowSubclass(progress_label, ProgressLabelWndProc, CLS_PROGRESS_LABEL, (DWORD_PTR)this);

	if (!success)
	{
		do_fail(L"Failed to subclass progress label!", L"SetWindowSubclass");
	}

	blockers_list = CreateWindow(
		WC_EDIT, 
		L"Blockers list",
		WS_CHILD | WS_VSCROLL | ES_LEFT | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_BORDER  | ES_READONLY ,
		x_pos, y_pos, x_size, ui_basic_height * 2,
		frame,
		NULL, NULL, NULL);

	success = SetWindowSubclass(blockers_list, BlockersListWndProc, CLS_BLOCKERS_LIST, (DWORD_PTR)this);

	if (!success)
	{
		do_fail(L"Failed to subclass blockers list!", L"SetWindowSubclass");
	}

	kill_button = CreateWindow(
		WC_BUTTON,
		L"Stop all",
		WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON,
		x_size + ui_padding - 100, rcParent.bottom - rcParent.top , 100, ui_basic_height,
		frame,
		NULL, NULL, NULL);

	cancel_button = CreateWindow(
		WC_BUTTON,
		L"Cancel",
		WS_TABSTOP | WS_CHILD | BS_DEFPUSHBUTTON,
		x_size + ui_padding - 100 - ui_padding - 100, rcParent.bottom - rcParent.top , 100, ui_basic_height,
		frame,
		NULL, NULL, NULL);

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

void callbacks_impl::error(const char* error, const char* error_type)
{
	int error_sz = -1;

	this->error_buf = ConvertToUtf16(error, &error_sz);
	
	save_exit_error(error_type);

	PostMessage(frame, CUSTOM_ERROR_MSG, NULL, NULL);
}

void callbacks_impl::downloader_preparing()
{
	LONG_PTR data = GetWindowLongPtr(frame, GWLP_USERDATA);
	auto ctx = reinterpret_cast<callbacks_impl*>(data);

	SetWindowTextW(ctx->progress_label, L"Checking local files...");
}

void callbacks_impl::downloader_start(int num_threads, size_t num_files_)
{
	file_sizes.resize(num_threads, 0);
	this->num_files = num_files_;
	start_time = high_resolution_clock::now();

	SetTimer(frame, 1, static_cast<unsigned int>(average_bw_time_span), &bandwidth_tick);
}

void callbacks_impl::download_file(int thread_index, std::string &relative_path, size_t size)
{
	/* Our specific UI doesn't care when we start, we only
	 * care when we're finished. A more technical UI could show
	 * what each thread is doing if they so wanted. */
	file_sizes[thread_index] = size;
}

void callbacks_impl::bandwidth_tick(HWND hwnd, UINT uMsg, UINT_PTR idEvent, DWORD dwTime)
{
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

	std::wstring label(fmt::format(ctx->label_format, ctx->files_done, ctx->num_files, ctx->last_calculated_bandwidth));

	SetWindowTextW(ctx->progress_label, label.c_str());
	SetTimer(hwnd, idEvent, static_cast<unsigned int>(average_bw_time_span), &bandwidth_tick);
}

void callbacks_impl::download_progress(int thread_index, size_t consumed, size_t accum)
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

	std::wstring label(fmt::format(label_format, files_done, num_files, last_calculated_bandwidth));

	int pos = lround(percent * INT_MAX);
	PostMessage(progress_worker, PBM_SETPOS, pos, 0);
	SetWindowTextW(progress_label, label.c_str());
}

void callbacks_impl::downloader_complete()
{
	KillTimer(frame, 1);
}

void callbacks_impl::blocker_start()
{
	ShowWindow(progress_worker, SW_HIDE);

	SetWindowTextW(progress_label, L"The following programs are preventing Streamlabs OBS from updating :");
	SetWindowTextW(blockers_list, L"");

	SetWindowPos(frame, 0, 0, 0, width, height + ui_basic_height + ui_padding, SWP_NOMOVE | SWP_NOREPOSITION | SWP_ASYNCWINDOWPOS);

	ShowWindow(blockers_list, SW_SHOW);
	ShowWindow(kill_button, SW_SHOW);
	ShowWindow(cancel_button, SW_SHOW);
}

int callbacks_impl::blocker_waiting_for(const std::wstring & processes_list, bool list_changed)
{
	int ret = 0;
	if (list_changed)
	{
		SetWindowTextW(blockers_list, processes_list.c_str());
	}

	if (should_cancel)
	{
		should_cancel = false;
		ret = 2;
	} else if (should_kill_blockers)
	{
		should_kill_blockers = false;
		ret = 1;
	}
	return ret;
}

void callbacks_impl::blocker_wait_complete()
{
	ShowWindow(blockers_list, SW_HIDE);
	ShowWindow(kill_button, SW_HIDE);
	ShowWindow(cancel_button, SW_HIDE);
	SetWindowTextW(blockers_list, L"");
	SetWindowTextW(progress_label, L"");

	ShowWindow(progress_worker, SW_SHOW);

	SetWindowPos(frame, 0, 0, 0, width, height, SWP_NOMOVE | SWP_NOREPOSITION | SWP_ASYNCWINDOWPOS);
}

void callbacks_impl::updater_start()
{
	SetWindowTextW(progress_label, L"Copying files...");
}

LRESULT CALLBACK ProgressLabelWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR  uIdSubclass, DWORD_PTR dwRefData)
{
	switch (msg) {
	case WM_SETTEXT: {
		RECT rect;
		HWND parent = GetParent(hwnd);

		GetWindowRect(hwnd, &rect);
		MapWindowPoints(HWND_DESKTOP, parent, (LPPOINT)&rect, 2);

		RedrawWindow(parent, &rect, NULL, RDW_ERASE | RDW_INVALIDATE);
	}
	break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK BlockersListWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, UINT_PTR  uIdSubclass, DWORD_PTR dwRefData)
{
	switch (msg) {
	case WM_HSCROLL:
	case WM_VSCROLL:
	case WM_SETTEXT: {
		RECT rect;
		HWND parent = GetParent(hwnd);

		GetWindowRect(hwnd, &rect);
		MapWindowPoints(HWND_DESKTOP, parent, (LPPOINT)&rect, 2);

		RedrawWindow(parent, &rect, NULL, RDW_ERASE | RDW_INVALIDATE);
	}
	break;
	}

	return DefSubclassProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK FrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
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
		delete[] ctx->error_buf;
		ctx->error_buf = nullptr;
				
		DestroyWindow(hwnd);

		break;
	}
	case WM_COMMAND:
	{
		LONG_PTR user_data = GetWindowLongPtr(hwnd, GWLP_USERDATA);
		auto ctx = reinterpret_cast<callbacks_impl *>(user_data);

		if ((HWND)lParam == ctx->kill_button)
		{
			EnableWindow(ctx->kill_button, false);
			ctx->should_kill_blockers = true;
			break;
		}
		if ((HWND)lParam == ctx->cancel_button)
		{
			EnableWindow(ctx->kill_button, false);
			EnableWindow(ctx->cancel_button, false);
			ctx->should_kill_blockers = false;
			ctx->should_cancel = true;
			break;
		}
	}
		break;
	case WM_CTLCOLORSTATIC:
	{
		LONG_PTR user_data = GetWindowLongPtr(hwnd, GWLP_USERDATA);
		auto ctx = reinterpret_cast<callbacks_impl *>(user_data);

		if ((HWND)lParam != ctx->blockers_list)
		{
			SetTextColor((HDC)wParam, RGB(255, 255, 255));
			SetBkMode((HDC)wParam, TRANSPARENT);
			return (LRESULT)GetStockObject(HOLLOW_BRUSH);
		}
	}
	break;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

extern "C"
int wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLineUnused, int nCmdShow) {

	setup_crash_reporting();
	
	callbacks_impl cb_impl(hInstance, nCmdShow);

	MultiByteCommandLine command_line;

	update_completed = su_parse_command_line(command_line.argc(), command_line.argv(), &params);

	if (!update_completed)
	{
		ShowError(L"Failed to parse cli arguments!");
		save_exit_error("Failed parising arguments");
		handle_exit();
		return 0;
	}

	auto client_deleter = [](struct update_client *client) {
		destroy_update_client(client);
	};


	std::unique_ptr<struct update_client, decltype(client_deleter)>
		client(create_update_client(&params), client_deleter);

	update_client_set_client_events(client.get(), &cb_impl);
	update_client_set_downloader_events(client.get(), &cb_impl);
	update_client_set_updater_events(client.get(), &cb_impl);
	update_client_set_pid_events(client.get(), &cb_impl);
	update_client_set_blocker_events(client.get(), &cb_impl);

	update_client_start(client.get());

	MSG msg;

	while (GetMessage(&msg, NULL, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	update_client_flush(client.get());

	/* Don't attempt start if application failed to update */
	if (cb_impl.should_start || params.restart_on_fail)
	{
		if( params.restart_on_fail )
			update_completed = StartApplication(params.exec_no_update.c_str(), params.exec_cwd.c_str());
		else 
			update_completed = StartApplication(params.exec.c_str(), params.exec_cwd.c_str());

		if (!update_completed)
		{
			ShowInfo(L"The application has finished updating.\n"
				"Please manually start Streamlabs OBS.");
			save_exit_error("Failed to autorestart");
			handle_exit();
		}
	}  else {
		handle_exit();

		return 1;
	}

	return 0;
}
