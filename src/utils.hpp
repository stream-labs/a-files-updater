#pragma once

#include <windows.h>

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
#define CLS_BLOCKERS_LIST (2)

void LogLastError(LPCWSTR lpFunctionName);

BOOL StartApplication(const char *lpCommandLine, const char *lpWorkingDir);
BOOL StartApplication(LPWSTR lpCommandLine, LPCWSTR lpWorkingDirectory);

LPWSTR ConvertToUtf16(const char *from, int *from_size);

/* Because Windows doesn't provide us a Unicode
 * command line by default and the command line
 * it does provide us is in UTF-16LE. */
struct MultiByteCommandLine
{
	MultiByteCommandLine();
	~MultiByteCommandLine();

	MultiByteCommandLine(const MultiByteCommandLine&) = delete;
	MultiByteCommandLine(MultiByteCommandLine&&) = delete;
	MultiByteCommandLine &operator=(const MultiByteCommandLine&) = delete;
	MultiByteCommandLine &operator=(MultiByteCommandLine&&) = delete;

	LPSTR *argv() { return m_argv; };
	int argc() { return m_argc; };

private:
	int m_argc{ 0 };
	LPSTR *m_argv{ nullptr };
};