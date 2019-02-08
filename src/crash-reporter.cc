#define DEMO_USING_SSL
#define BOOST_ASIO_ENABLE_HANDLER_TRACKING

#include <iostream>
#include <iomanip>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

#include "crash-reporter.hpp"

using boost::asio::ip::tcp;

class client
{
	FILE * pFile;

public:
	virtual ~client()
	{
		fclose(pFile);
	}
	client(boost::asio::io_service& io_service, boost::asio::ssl::context& context)
		: socket_(io_service
#ifdef DEMO_USING_SSL
			, context)
	{
		pFile = fopen("myfile_send.txt", "w");
		fprintf(pFile, "before set_verify_mode\n"); fflush(pFile);

		boost::asio::ip::tcp::resolver resolver(io_service);
		boost::asio::ip::tcp::resolver::query query("localhost", "8082");
		boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve(query);

		socket_.set_verify_mode(boost::asio::ssl::verify_peer);
		socket_.set_verify_callback(boost::bind(&client::verify_certificate, this, _1, _2));
#else
		)
	{
		(void)context;
#endif
		fprintf(pFile, "before async_connect\n"); fflush(pFile);
		boost::asio::connect(socket_.lowest_layer(), iterator);//, boost::bind(&client::handle_connect, this, boost::asio::placeholders::error));

	}

	bool verify_certificate(bool preverified, boost::asio::ssl::verify_context& ctx)
	{
		// The verify callback can be used to check whether the certificate that is
		// being presented is valid for the peer. For example, RFC 2818 describes
		// the steps involved in doing this for HTTPS. Consult the OpenSSL
		// documentation for more details. Note that the callback is called once
		// for each certificate in the certificate chain, starting from the root
		// certificate authority.

		// In this example we will simply print the certificate's subject name.
		char subject_name[256];
		X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
		X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
		fprintf(pFile, "Verifying %s\n", subject_name);
		return preverified;
	}

	void handle_connect(const boost::system::error_code& error)
	{
#ifdef DEMO_USING_SSL
		if (!error)
		{
			fprintf(pFile, "handle_connect \n"); fflush(pFile);
			socket_.async_handshake(boost::asio::ssl::stream_base::client, boost::bind(&client::handle_handshake, this, boost::asio::placeholders::error));
		} else
		{
			fprintf(pFile, "Connect failed: %s\n", error.message()); fflush(pFile);
		}
#else
		handle_handshake(error);
#endif
	}

	void handle_handshake(const boost::system::error_code& error)
	{
		if (!error)
		{
			fprintf(pFile, "handle_handshake\n"); fflush(pFile);
			static char const raw[] = "POST / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";

			static_assert(sizeof(raw) <= sizeof(request_), "too large");

			size_t request_length = strlen(raw);
			std::copy(raw, raw + request_length, request_);

			{
				// used this for debugging:
				std::ostream hexos(std::cout.rdbuf());
				for (auto it = raw; it != raw + request_length; ++it)
				{
					hexos << std::hex << std::setw(2) << std::setfill('0') << std::showbase << ((short unsigned)*it) << " ";
				}
				std::cout << "\n";
			}

			boost::asio::async_write(socket_,
				boost::asio::buffer(request_, request_length),
				boost::bind(&client::handle_write, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
			);

		} else
		{
			fprintf(pFile, "Handshake failed: %s\n", error.message()); fflush(pFile);
		}
	}

	void handle_write(const boost::system::error_code& error, size_t /*bytes_transferred*/)
	{
		if (!error)
		{
			fprintf(pFile, "starting read loop\n"); fflush(pFile);
			boost::asio::async_read_until(socket_, reply_, '\n',
				boost::bind(&client::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
			);
		} else
		{
			fprintf(pFile, "Write failed: %s\n", error.message()); fflush(pFile);
		}
	}

	void handle_read(const boost::system::error_code& error, size_t /*bytes_transferred*/)
	{
		if (!error)
		{
			//fprintf(pFile, "Reply: %s\n", &reply_. );
		} else
		{
			fprintf(pFile, "Read failed: %s\n", error.message()); fflush(pFile);
		}
	}

private:
#ifdef DEMO_USING_SSL
	boost::asio::ssl::stream<boost::asio::ip::tcp::socket> socket_;
#else
	boost::asio::ip::tcp::socket socket_;
#endif
	char request_[1024];
	boost::asio::streambuf reply_;
};

int send_crash_to_sentry()
{
	boost::asio::io_service  *io_service = new boost::asio::io_service();

	boost::asio::ssl::context *ctx = new boost::asio::ssl::context(boost::asio::ssl::context::sslv23);
	ctx->set_default_verify_paths();

	client c(*io_service, *ctx);

	io_service->run();

	return 0;
}


std::string prepare_crash_report(struct _EXCEPTION_POINTERS* ExceptionInfo) noexcept
{
	printStack(ExceptionInfo->ContextRecord);
	std::string report_json = "{\"menu\":\"test\", \"other\":{\"test\":\"value\"}}";
	return report_json;
}

int send_crash_to_sentry_sync(const std::string& report_json) noexcept
{
	std::string host = "localhost";
	//std::string host = "sentry.io";
	std::string protocol = "http";
	std::string api_path = "/api/1283431/minidump/?sentry_key=ec98eac4e3ce49c7be1d83c8fb2005ef";
    
	FILE * pFile;
	pFile = fopen("myfile_send.txt", "w");
	fprintf(pFile, "Begin \n"); fflush(pFile);
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

		request_stream << "POST " << api_path << " HTTP/1.1"<< "\r\n";
		request_stream << "Host: " << host << "\r\n";
		request_stream << "Accept: *"<< "\r\n";
		request_stream << "User-Agent: slobs updater "<< "\r\n";
		request_stream << "Accept-encoding: 'gzip, deflate, br'"<< "\r\n";
		request_stream << "Accept-language: 'en-US,en;q=0.9,ru;q=0.8'"<< "\r\n";
		request_stream << "Connection: close\r\n";  
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
			fprintf(pFile, "Invalid response \n"); fflush(pFile);
		} else if (status_code != 200)
		{
			fprintf(pFile, "Response returned with status code %d \n", status_code); fflush(pFile);
		} else
		{
			// Read the response headers, which are terminated by a blank line.
			boost::asio::read_until(socket, response, "\r\n\r\n");

			// Process the response headers.
			std::string header;
			while (std::getline(response_stream, header) && header != "\r\n")
			{
				fprintf(pFile, "%s \n", header.c_str()); fflush(pFile);
			}
			fprintf(pFile, "  \n"); fflush(pFile);

			// Write whatever content we already have to output.
			if (response.size() > 0)
			{
				std::cout << &response;
			}

			// Read until EOF, writing data to output as we go.
			while (boost::asio::read(socket, response, boost::asio::transfer_at_least(1), error))
			{
				std::cout << &response;
			}

			if (error != boost::asio::error::eof)
			{
				throw boost::system::system_error(error);
			}
		}
	} catch (std::exception& e)
	{
		fprintf(pFile, "Exception %s \n", e.what()); fflush(pFile);
	}
	fprintf(pFile, "End  \n"); fflush(pFile);
	fclose(pFile);

	return 0;
}



void HandleCrash(struct _EXCEPTION_POINTERS* ExceptionInfo, bool callAbort) noexcept
{
	static bool insideCrashMethod = false;
	if (insideCrashMethod)
	{
		abort();
	}
	insideCrashMethod = true;

	//prepare data and 
	std::string report = prepare_crash_report(ExceptionInfo);
	//submit to sentry
	send_crash_to_sentry_sync(report);

	if(callAbort)
        abort();
	
 	insideCrashMethod = false;
}

void HandleExit() noexcept
{
	HandleCrash( nullptr, false);
}

const int MaxNameLen = 256;
#include "dbghelp.h"
#pragma comment(lib,"Dbghelp.lib")

void printStack( CONTEXT* ctx ) //Prints stack trace based on context record
{
    BOOL    result;
    HANDLE  process;
    HANDLE  thread;
    HMODULE hModule;

    STACKFRAME64        stack;
    ULONG               frame;    
    DWORD64             displacement;

    DWORD disp;
    IMAGEHLP_LINE64 *line;

    char buffer[sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(TCHAR)];
    char name[ MaxNameLen ];
    char module[MaxNameLen];
    PSYMBOL_INFO pSymbol = (PSYMBOL_INFO)buffer;

    memset( &stack, 0, sizeof( STACKFRAME64 ) );

    process                = GetCurrentProcess();
    thread                 = GetCurrentThread();
    displacement           = 0;
#if !defined(_M_AMD64)
    stack.AddrPC.Offset    = (*ctx).Eip;
    stack.AddrPC.Mode      = AddrModeFlat;
    stack.AddrStack.Offset = (*ctx).Esp;
    stack.AddrStack.Mode   = AddrModeFlat;
    stack.AddrFrame.Offset = (*ctx).Ebp;
    stack.AddrFrame.Mode   = AddrModeFlat;
#endif
	FILE * pFile;
	pFile = fopen ("myfile.txt","w");

    SymInitialize( process, NULL, TRUE ); //load symbols

    for( frame = 0; ; frame++ )
    {
        //get next call from stack
        result = StackWalk64
        (
#if defined(_M_AMD64)
            IMAGE_FILE_MACHINE_AMD64
#else
            IMAGE_FILE_MACHINE_I386
#endif
            ,
            process,
            thread,
            &stack,
            ctx,
            NULL,
            SymFunctionTableAccess64,
            SymGetModuleBase64,
            NULL
        );

        if( !result ) break;        

        //get symbol name for address
        pSymbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        pSymbol->MaxNameLen = MAX_SYM_NAME;
        SymFromAddr(process, ( ULONG64 )stack.AddrPC.Offset, &displacement, pSymbol);

        line = (IMAGEHLP_LINE64 *)malloc(sizeof(IMAGEHLP_LINE64));
        line->SizeOfStruct = sizeof(IMAGEHLP_LINE64);       
		//ShowError(L"step2");
        //try to get line
        if (SymGetLineFromAddr64(process, stack.AddrPC.Offset, &disp, line))
        {
            fprintf(pFile,"\tat %s in %s: line: %lu: address: 0x%0X\n", pSymbol->Name, line->FileName, line->LineNumber, pSymbol->Address);
        }
        else
        { 
            //failed to get line
            fprintf(pFile, "\tat %s, address 0x%0X.\n", pSymbol->Name, pSymbol->Address);
            hModule = NULL;
            lstrcpyA(module,"");        
            GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, 
                (LPCTSTR)(stack.AddrPC.Offset), &hModule);

            //at least print module name
            if(hModule != NULL)GetModuleFileNameA(hModule,module,MaxNameLen);       

            fprintf (pFile, "in %s\n",module);
        }       
        free(line);
        line = NULL;
    }
	fclose (pFile);
}
