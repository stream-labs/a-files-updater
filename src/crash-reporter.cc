
#include <iostream>
#include <iomanip>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "crash-reporter.hpp"

using boost::asio::ip::tcp;

#ifdef SENTRY_HOST_NAME
const std::string host = SENTRY_HOST_NAME
#else
const std::string host = "sentry.io";
#endif

const std::string protocol = "http";
const std::string api_path = "/api/1390326/store/";

extern bool update_completed;
bool report_unsuccessful = false;

std::string get_uuid() noexcept;
std::string get_timestamp() noexcept;

struct _EXCEPTION_POINTERS* generate_exception_info() noexcept;
void print_stacktrace(CONTEXT* ctx, std::ostringstream & report_stream) noexcept; //Prints stack trace based on context record
void escape_backslashes(std::string &report);

#include "dbghelp.h"
#pragma comment(lib,"Dbghelp.lib")

std::string prepare_crash_report(struct _EXCEPTION_POINTERS* ExceptionInfo) noexcept
{
	std::ostringstream json_report;

	json_report << "{";
	json_report << "	\"event_id\": \"" << get_uuid() << "\", ";
	json_report << "	\"timestamp\": \"" << get_timestamp() << "\", ";
	json_report << "	\"exception\": {\"values\":[{";
	if (ExceptionInfo)
	{
		json_report << "		\"type\": \"" << ExceptionInfo->ExceptionRecord->ExceptionCode << "\", ";
	}
	//	json_report << "		\"value\": \"" << "ERROR_VALUE" << "\", ";
	//	json_report << "		\"module\": \"" << "MODULE_NAME" << "\", ";
	json_report << "		\"thread_id\": \"" << std::this_thread::get_id() << "\", ";
	if (ExceptionInfo)
	{
		json_report << "		\"stacktrace\": { \"frames\" : [";
		print_stacktrace(ExceptionInfo->ContextRecord, json_report);
		json_report << "		] } ";
	}
	json_report << "	}]}, ";
	json_report << "	\"tags\": { ";
	json_report << "		\"app_build_timestamp\": \"" << __DATE__ << " " << __TIME__ << "\", ";
	json_report << "		\"os_version\": \"" << "win32" << "\" ";
	json_report << "	} ";
	json_report << "}";

	return json_report.str();
}

int send_crash_to_sentry_sync(const std::string& report_json) noexcept
{
	try
	{
		boost::asio::io_service io_service;
		tcp::resolver resolver(io_service);
		tcp::resolver::query query(host, protocol);
		tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
		tcp::resolver::iterator end;

		tcp::socket socket(io_service);
		boost::system::error_code error = boost::asio::error::host_not_found;
		while (error && endpoint_iterator != end)
		{
			socket.close();
			socket.connect(*endpoint_iterator++, error);
		}
		if (error)
		{
			throw boost::system::system_error(error);
		}

		boost::asio::streambuf request;
		std::ostream request_stream(&request);

		request_stream << "POST " << api_path << " HTTP/1.1" << "\r\n";
		request_stream << "Host: " << host << "\r\n";
		request_stream << "Accept: *" << "\r\n";
		request_stream << "User-Agent: slobs_updater " << "\r\n";
		request_stream << "Accept-encoding: 'gzip, deflate, br'" << "\r\n";
		request_stream << "Accept-language: 'en-US,en;q=0.9,ru;q=0.8'" << "\r\n";
		request_stream << "Connection: close" << "\r\n";
		request_stream << "X-Sentry-Auth: Sentry sentry_version=5,sentry_client=slobs_updater/";
		request_stream << "1.0.0" << ",sentry_timestamp=" << get_timestamp();
		request_stream << ",sentry_key=" << "7492ebea21f54618a550163938dc164d";
		request_stream << ",sentry_secret=" << "654ed1db5d93495284f66f3d6d195790" << "\r\n";

		request_stream << "Content-Length: " << report_json.length() << "\r\n";
		request_stream << "\r\n";
		request_stream << report_json;

		// Send the request.
		boost::asio::write(socket, request);

		// Read the response status line. 
		boost::asio::streambuf response;
		boost::asio::read_until(socket, response, "\r\n");

		// Check that response is OK.
		std::istream response_stream(&response);
		std::string http_version;
		response_stream >> http_version;
		unsigned int status_code;
		response_stream >> status_code;

		std::string status_message;
		std::getline(response_stream, status_message);

		if (!response_stream || http_version.substr(0, 5) != "HTTP/")
		{
		} else
		{
			// Read the response headers, which are terminated by a blank line.
			boost::asio::read_until(socket, response, "\r\n\r\n");

			// Process the response headers. 
			// Not much we can do if sentry response with error. 
			// Just read data to make connection finish correctly
			std::string header;
			while (std::getline(response_stream, header) && header != "\r\n")
			{
			}

			if (response.size() > 0)
			{

			}

			// Read until EOF, checking data as we go.
			while (boost::asio::read(socket, response, boost::asio::transfer_at_least(1), error))
			{

			}

			if (error != boost::asio::error::eof)
			{
				throw boost::system::system_error(error);
			}
		}
	} catch (std::exception& e)
	{
		//have to ignore exceptions as programm is in reporting exception already
	}


	return 0;
}

void handle_crash(struct _EXCEPTION_POINTERS* ExceptionInfo, bool callAbort) noexcept
{
	static bool insideCrashMethod = false;
	if (insideCrashMethod)
	{
		abort();
	}
	insideCrashMethod = true;

	std::string report = prepare_crash_report(ExceptionInfo);
	escape_backslashes(report);
	send_crash_to_sentry_sync(report);

	if (callAbort)
	{
		abort();
	}

	insideCrashMethod = false;
}

void handle_exit() noexcept
{
	if (!update_completed && report_unsuccessful)
	{
		handle_crash(generate_exception_info(), false);
	}
}

struct _EXCEPTION_POINTERS* generate_exception_info() noexcept
{
	//todo
	return nullptr;
}

void print_stacktrace(CONTEXT* ctx, std::ostringstream & report_stream) noexcept
{
	BOOL    result;
	HANDLE  process;
	HANDLE  thread;
	HMODULE hModule;

	STACKFRAME64 stack;
	ULONG        frame;
	DWORD64      displacement;

	DWORD			disp;
	IMAGEHLP_LINE64 *line;
	const int		MaxNameLen = 256;

	char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
	char name[MaxNameLen];
	char module[MaxNameLen];
	PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

	memset(&stack, 0, sizeof(STACKFRAME64));

	process = GetCurrentProcess();
	thread = GetCurrentThread();
	displacement = 0;
#if !defined(_M_AMD64)
	stack.AddrPC.Offset = (*ctx).Eip;
	stack.AddrPC.Mode = AddrModeFlat;
	stack.AddrStack.Offset = (*ctx).Esp;
	stack.AddrStack.Mode = AddrModeFlat;
	stack.AddrFrame.Offset = (*ctx).Ebp;
	stack.AddrFrame.Mode = AddrModeFlat;
#endif

	SymInitialize(process, NULL, TRUE);
	bool first_element = true;
	for (frame = 0; ; frame++)
	{
		//get next call from stack
		result = StackWalk64
		(
#if defined(_M_AMD64)
			IMAGE_FILE_MACHINE_AMD64
#else
			IMAGE_FILE_MACHINE_I386
#endif
			, process, thread, &stack, ctx, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL);

		if (!result) break;

		//get symbol name for address
		pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
		pSymbol->MaxNameLen = MAX_SYM_NAME;
		SymFromAddr(process, (ULONG64)stack.AddrPC.Offset, &displacement, pSymbol);

		line = (IMAGEHLP_LINE64 *)malloc(sizeof(IMAGEHLP_LINE64));
		line->SizeOfStruct = sizeof(IMAGEHLP_LINE64);

		if (!first_element)
		{
			report_stream << ",";
		}
		report_stream << " { ";

		report_stream << " 	\"function\": \"" << pSymbol->Name << "\", ";
		report_stream << " 	\"instruction_addr\": \"" << "0x" << std::uppercase << std::setfill('0') << std::setw(12) << std::hex << pSymbol->Address << "\", ";
		if (SymGetLineFromAddr64(process, stack.AddrPC.Offset, &disp, line))
		{
			report_stream << " 	\"lineno\": \"" << line->LineNumber << "\", ";
			report_stream << " 	\"filename\": \"" << line->FileName << "\", ";
		} else
		{
			//failed to get line number
		}
		free(line);
		line = NULL;

		hModule = NULL;
		lstrcpyA(module, "");
		if (GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCTSTR)(stack.AddrPC.Offset), &hModule))
		{
			if (hModule != NULL)
			{
				if (GetModuleFileNameA(hModule, module, MaxNameLen))
				{
					report_stream << " 	\"module\": \"" << module << "\" ";
				}
			}
		}

		first_element = false;
		report_stream << " } ";
	}
}


std::string get_timestamp() noexcept
{
	char buf[sizeof "xxxx-xx-xxTxx:xx:xxx\0"];
	std::time_t curent_time;

	std::time(&curent_time);
	std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&curent_time));

	return std::string(buf);
}

std::string get_uuid() noexcept
{
	char result[33] = { '\0' }; //"fc6d8c0c43fc4630ad850ee518f1b9d0";
	std::srand(std::time(nullptr));

	for (std::size_t i = 0; i < sizeof(result) - 1; ++i)
	{
		const auto r = static_cast<char>(std::rand() % 16);
		if (r < 10)
		{
			result[i] = '0' + r;
		} else
		{
			result[i] = 'a' + r - static_cast<char>(10);
		}
	}

	return std::string(result);
}

void escape_backslashes(std::string &report)
{
	std::string search = "\\";
	std::string replace = "\\\\";
	size_t pos = 0;
	while ((pos = report.find(search, pos)) != std::string::npos)
	{
		report.replace(pos, search.length(), replace);
		pos += replace.length();
	}

}