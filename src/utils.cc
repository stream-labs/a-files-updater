#include "utils.hpp"

#include <shellapi.h>

#include "logger/log.h" 

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
	for (int i = 0; i < m_argc; ++i)
	{
		DWORD size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, NULL, 0, NULL, NULL);

		m_argv[i] = new CHAR[size];

		size = WideCharToMultiByte(CP_UTF8, 0, wargv[i], -1, m_argv[i], size, NULL, NULL);
	}

	LocalFree(wargv);
}

MultiByteCommandLine::~MultiByteCommandLine()
{
	for (int i = 0; i < m_argc; ++i)
	{
		delete[] m_argv[i];
	}

	delete[] m_argv;
}

LPWSTR ConvertToUtf16(const char *from, int *from_size)
{
	int to_size = MultiByteToWideChar(CP_UTF8, 0, from, *from_size, NULL, 0);

	auto to = new WCHAR[to_size];

	int size = MultiByteToWideChar(CP_UTF8, 0, from, *from_size, to, to_size);

	if (size == 0)
	{
		*from_size = 0;
		LogLastError(L"MultiByteToWideChar");
		return NULL;
	}

	*from_size = to_size;

	return to;
}

void LogLastError(LPCWSTR lpFunctionName)
{
	DWORD  dwError = GetLastError();
	DWORD  szMsgBuf = 1024;
	LPTSTR strMsgBuf = new wchar_t[szMsgBuf];

	FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, dwError, 0, strMsgBuf, szMsgBuf, NULL);

	wlog_debug(L"%s: %.*s", lpFunctionName, szMsgBuf, strMsgBuf);

	delete[] strMsgBuf;
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

	hShellProc = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, dwShellProcId);

	if (hShellProc == NULL)
	{
		LogLastError(L"OpenProcess");
		return FALSE;
	}

	bSuccess = OpenProcessToken(hShellProc, TOKEN_DUPLICATE, &hShellToken);

	if (bSuccess == 0)
	{
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

	if (bSuccess == 0)
	{
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

	if (bSuccess == 0)
	{
		LogLastError(L"CreateProcessWithTokenW");
		return FALSE;
	}

	return TRUE;
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
