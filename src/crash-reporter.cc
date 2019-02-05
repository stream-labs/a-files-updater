#define DEMO_USING_SSL
#define BOOST_ASIO_ENABLE_HANDLER_TRACKING

#include <iostream>
#include <iomanip>
#include <boost/bind.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

using boost::asio::ip::tcp;

class client
{
	FILE * pFile;
	
public:
	virtual ~client()
	{
		fclose (pFile);
	}
	client(boost::asio::io_service& io_service, boost::asio::ssl::context& context)
		: socket_(io_service
#ifdef DEMO_USING_SSL
			, context)
	{
		pFile = fopen ("myfile_send.txt","w");
		fprintf(pFile, "before set_verify_mode\n" );fflush (pFile);
	
		boost::asio::ip::tcp::resolver resolver(io_service);
		boost::asio::ip::tcp::resolver::query query("localhost", "8082");
		boost::asio::ip::tcp::resolver::iterator iterator = resolver.resolve(query);

		socket_.set_verify_mode(boost::asio::ssl::verify_peer);
		socket_.set_verify_callback( boost::bind(&client::verify_certificate, this, _1, _2));
#else
		)
	{
		(void)context;
#endif
		fprintf(pFile, "before async_connect\n" );fflush (pFile);
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
		fprintf(pFile, "Verifying %s\n", subject_name );
		return preverified;
	}

	void handle_connect(const boost::system::error_code& error)
	{
#ifdef DEMO_USING_SSL
		if (!error)
		{
			fprintf(pFile, "handle_connect \n" );fflush (pFile);
			socket_.async_handshake(boost::asio::ssl::stream_base::client, boost::bind(&client::handle_handshake, this, boost::asio::placeholders::error));
		} else
		{
			fprintf(pFile, "Connect failed: %s\n", error.message()  );fflush (pFile);
		}
#else
		handle_handshake(error);
#endif
	}

	void handle_handshake(const boost::system::error_code& error)
	{
		if (!error)
		{
			fprintf(pFile, "handle_handshake\n" );fflush (pFile);
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
			fprintf(pFile, "Handshake failed: %s\n", error.message() );fflush (pFile);
		}
	}

	void handle_write(const boost::system::error_code& error, size_t /*bytes_transferred*/)
	{
		if (!error)
		{
			fprintf(pFile, "starting read loop\n" );fflush (pFile);
			boost::asio::async_read_until(socket_, reply_, '\n',
				boost::bind(&client::handle_read, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred)
			);
		} else
		{
			fprintf(pFile, "Write failed: %s\n", error.message() );fflush (pFile);
		}
	}

	void handle_read(const boost::system::error_code& error, size_t /*bytes_transferred*/)
	{
		if (!error)
		{
			//fprintf(pFile, "Reply: %s\n", &reply_. );
		} else
		{
			fprintf(pFile, "Read failed: %s\n", error.message() );fflush (pFile);
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

int send_crash_to_sentry( )
{
	boost::asio::io_service  *io_service = new boost::asio::io_service ();

	boost::asio::ssl::context *ctx = new boost::asio::ssl::context(boost::asio::ssl::context::sslv23);
	ctx->set_default_verify_paths();

	client c(*io_service, *ctx);

	io_service->run();

	return 0;
}

int send_crash_to_sentry_sync( )
{
	FILE * pFile;
	pFile = fopen ("myfile_send.txt","w");
	fprintf(pFile, "before set_verify_mode\n" );fflush (pFile);
	try {

	boost::asio::io_service io_service;
    // Get a list of endpoints corresponding to the server name.
    tcp::resolver resolver(io_service);
    tcp::resolver::query query( "localhost", "http");
    tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);

    // Try each endpoint until we successfully establish a connection.

	tcp::socket socket(io_service);
	boost::asio::connect(socket, endpoint_iterator);

    // Form the request. We specify the "Connection: close" header so that the
    // server will close the socket after transmitting the response. This will
    // allow us to treat all data up until the EOF as the content.
    boost::asio::streambuf request;
    std::ostream request_stream(&request);
    request_stream << "GET " << "http" << " HTTP/1.0\r\n";
    request_stream << "Host: " << "localhost" << "\r\n";
    request_stream << "Accept: */*\r\n";
    request_stream << "Connection: close\r\n\r\n";

    // Send the request.
    boost::asio::write(socket, request);

    // Read the response status line. The response streambuf will automatically
    // grow to accommodate the entire line. The growth may be limited by passing
    // a maximum size to the streambuf constructor.
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
      std::cout << "Invalid response\n";
      return 1;
    }
    if (status_code != 200)
    {
      std::cout << "Response returned with status code " << status_code << "\n";
      return 1;
    }

    // Read the response headers, which are terminated by a blank line.
    boost::asio::read_until(socket, response, "\r\n\r\n");

    // Process the response headers.
    std::string header;
    while (std::getline(response_stream, header) && header != "\r")
      std::cout << header << "\n";
    std::cout << "\n";

    // Write whatever content we already have to output.
    if (response.size() > 0)
      std::cout << &response;

    // Read until EOF, writing data to output as we go.
    boost::system::error_code error;
    while (boost::asio::read(socket, response, boost::asio::transfer_at_least(1), error))
	{
		std::cout << &response;
	}
		    

	if (error != boost::asio::error::eof)
	{
      throw boost::system::system_error(error);
	}

	}  catch (std::exception& e) {
		std::cout << "Exception: " << e.what() << "\n";
	}

	fclose (pFile);
}