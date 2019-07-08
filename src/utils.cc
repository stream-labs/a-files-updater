#include "utils.hpp"

#include <shellapi.h>
#include <fstream>
#include <iostream>
#include <sstream>

#include "logger/log.h" 
#include "checksum-filters.hpp"
#include <boost/algorithm/string/replace.hpp>

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
	
	if (!bSuccess)
	{
		HINSTANCE exec_result = ShellExecute(NULL,
			L"runas",
			lpWideCommandLine,
			nullptr,
			NULL, 
			SW_SHOWNORMAL
		);

		int exec_result_code = static_cast<int>(reinterpret_cast<uintptr_t>(exec_result));

		if (exec_result_code >= 32)
		{
			bSuccess = true;
		} else {
			LogLastError(L"ShellExecute");
		}
	}

	delete lpWideCommandLine;
	delete lpWideWorkingDir;

	return bSuccess;
}

fs::path prepare_file_path(const fs::path &base, const fs::path &target)
{
	fs::path file_path = "";
	try
	{
		file_path = base;
		file_path /= target;
	
		std::string un_urled_path = unfixup_uri(file_path.string());
		file_path = fs::u8path(un_urled_path.c_str());

		file_path.make_preferred();
		file_path.replace_extension();

		fs::create_directories(file_path.parent_path());

		fs::remove(file_path);
	}
	catch (...)
	{
		file_path = "";
	}

	return file_path;
}


std::string encimpl(std::string::value_type v)
{
	if (isascii(v))
		return std::string() + v;

	std::ostringstream enc;
	enc << '%' << std::setw(2) << std::setfill('0') << std::hex << std::uppercase << int(static_cast<unsigned char>(v));
	return enc.str();
}

std::string urlencode(const std::string& url)
{
	const std::string::const_iterator start = url.begin();

	std::vector<std::string> qstrs;

	std::transform(start, url.end(),
		std::back_inserter(qstrs),
		encimpl);

	std::ostringstream ostream;

	for (auto const& i : qstrs)
	{
		ostream << i;
	}

	return ostream.str();
}

std::string fixup_uri(const std::string &source)
{
	std::string result(source);

	boost::algorithm::replace_all(result, "\\", "/");
	boost::algorithm::replace_all(result, " ", "%20");

	return result;
}

std::string unfixup_uri(const std::string &source)
{
	std::string result(source);

	boost::algorithm::replace_all(result, "%20", " ");

	return result;
}

std::string calculate_files_checksum(fs::path &path)
{
	std::ostringstream hex_digest;
	unsigned char hash[SHA256_DIGEST_LENGTH] = { 0 };

	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (file.is_open())
	{
		SHA256_CTX sha256;
		SHA256_Init(&sha256);

		unsigned char buffer[4096];
		while (true)
		{
			file.read((char *)buffer, 4096);
			std::streamsize read_byte = file.gcount();
			if (read_byte != 0)
			{
				SHA256_Update(&sha256, buffer, read_byte);
			}
			if (!file.good())
			{
				break;
			}
		}

		SHA256_Final(hash, &sha256);

		file.close();

		hex_digest << std::nouppercase << std::setfill('0') << std::hex;

		for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		{
			hex_digest << std::setw(2) << static_cast<unsigned int>(hash[i]);
		}
	}

	return hex_digest.str();
}
