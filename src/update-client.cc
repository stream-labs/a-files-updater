#include <atomic>
#include <algorithm>
#include <mutex>
#include <thread>
#include <regex>

#include <boost/algorithm/string/replace.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast.hpp>
#include <boost/filesystem.hpp>
#include <boost/iostreams/chain.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/traits.hpp>
#include <fmt/format.h>
#include <aclapi.h>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;
namespace fs = boost::filesystem;
namespace bio = boost::iostreams;

using std::regex;
using std::cmatch;
using std::regex_search;

#include "update-client.hpp"
#include "checksum-filters.hpp"
#include "update-parameters.hpp"
#include "logger/log.h"

/*##############################################
 *#
 *# Update exceptions
 *#
 *############################################*/

class update_exception_blocked : public std::exception
{};

class update_exception_failed : public std::exception
{};

/*##############################################
 *#
 *# Utility functions
 *#
 *############################################*/
namespace {

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

}

/*##############################################
 *#
 *# Data definitions
 *#
 *############################################*/
struct update_client {
	template <class Body, bool IncludeVersion>
	struct http_request;

	struct file;
	struct pid;

	using manifest_body = http::basic_dynamic_body<beast::flat_buffer>;

	template <class Body>
	using manifest_request = http_request<Body, false>;

	template <class Body>
	using file_request = http_request<Body, true>;

	template <class Type>
	using executor_work_guard = asio::executor_work_guard<Type>;
	using io_context = asio::io_context;
	using work_guard_type = executor_work_guard<io_context::executor_type>;
	using resolver_type = tcp::resolver;
	using manifest_map = std::unordered_map<std::string, std::string>;

	update_client() = delete;
	update_client(const update_client&) = delete;
	update_client(update_client&&) = delete;
	update_client &operator=(update_client&&) = delete;
	update_client &operator=(const update_client&) = delete;

	explicit update_client(struct update_parameters *params);
	~update_client();

	void set_client_events(client_callbacks *cbs)
	{
		client_events = cbs;
	}

	void set_downloader_events(downloader_callbacks *cbs)
	{
		downloader_events = cbs;
	}

	void set_updater_events(updater_callbacks *cbs)
	{
		updater_events = cbs;
	}

	void set_pid_events(pid_callbacks *cbs)
	{
		pid_events = cbs;
	}

	void do_stuff();
	void flush();

	/* These two are explicitly initialized in constructor */
	io_context io_ctx;
	update_parameters       *params;

	work_guard_type         *work{nullptr};
	fs::path                 new_files_dir;
	client_callbacks        *client_events{nullptr};
	downloader_callbacks    *downloader_events{nullptr};
	updater_callbacks       *updater_events{nullptr};
	pid_callbacks           *pid_events{nullptr};

	int                      active_workers{0};
	std::atomic_size_t       active_pids{0};
	manifest_map             manifest;
	std::mutex               manifest_mutex;
	manifest_map::const_iterator manifest_iterator;

	resolver_type            resolver;
	resolver_type::results_type endpoints;
	ssl::context ssl_context{ssl::context::method::sslv23_client};

	std::vector<std::thread> thread_pool;

private:
	inline void handle_error(
	  const boost::system::error_code &error,
	  const char* str
	) {
		char error_buf[256];

		snprintf(
			error_buf,
			sizeof(error_buf),
			"%s - %s",
			error.message().c_str(),
			str
		);

		client_events->error(error_buf);
	}

	void start_downloader();

	void handle_resolve(
	  const boost::system::error_code &error,
	  tcp::resolver::results_type results
	);

	void handle_manifest_connect(
	  const boost::system::error_code &error,
	  const tcp::endpoint &ep,
	  manifest_request<manifest_body> *request_ctx
	);

	void handle_manifest_handshake(
	  const boost::system::error_code &error,
	  manifest_request<manifest_body> *request_ctx
	);

	void handle_manifest_request(
	  boost::system::error_code &error,
	  size_t bytes,
	  manifest_request<manifest_body> *request_ctx
	);

	void handle_manifest_response(
	  boost::system::error_code &ec,
	  size_t bytes,
	  manifest_request<manifest_body> *request_ctx
	);

	void handle_manifest_results();
	void clean_manifest();
	void check_file(fs::path & check_path);

	void handle_file_connect(
	  const boost::system::error_code &error,
	  const tcp::endpoint &ep,
	  file_request<http::dynamic_body> *request_ctx
	);

	void handle_file_handshake(
	  const boost::system::error_code& error,
	  file_request<http::dynamic_body> *request_ctx
	);

	void handle_manifest_entry(
	  const manifest_map::value_type &entry,
	  int index
	);

	void handle_file_request(
	  boost::system::error_code &error,
	  size_t bytes,
	  file_request<http::dynamic_body> *request_ctx
	);

	void handle_file_response_header(
	  boost::system::error_code &error,
	  size_t bytes,
	  file_request<http::dynamic_body> *request_ctx
	);

	void handle_file_response_body(
	  boost::system::error_code &error,
	  size_t bytes_read,
	  file_request<http::dynamic_body> *request_ctx,
	  update_client::file *file_ctx
	);

	void handle_file_result(
	  update_client::file *file_ctx,
	  int index
	);

	void handle_pids();

	void handle_pid(
	  const boost::system::error_code& error,
	  update_client::pid* context
	);

	void start_file_update();

	void create_work();
	void reset_work();
};

template <class Body, bool IncludeVersion>
struct update_client::http_request {

	http_request(
	  update_client *client_ctx,
	  const std::string &target,
	  const int id
	);

	size_t download_accum{0};
	size_t content_length{0};
	int worker_id;
	update_client *client_ctx;
	std::string target;

	/* We used to support http and then I realized
	 * I was spending a lot of time supporting both.
	 * Our use case doesn't use it and boost doesn't
	 * allow any convenience to allow using them
	 * interchangeably. Really, my suggestion is that
	 * you should be using ssl regardless anyways. */
	ssl::stream<tcp::socket> ssl_socket;
	http::request<http::empty_body> request;

	beast::multi_buffer response_buf;
	http::response_parser<Body> response_parser;
};

struct FileUpdater {
	FileUpdater() = delete;
	FileUpdater(FileUpdater&&) = delete;
	FileUpdater(const FileUpdater&) = delete;
	FileUpdater &operator=(const FileUpdater&) = delete;
	FileUpdater &operator=(FileUpdater&&) = delete;

	explicit FileUpdater(update_client *client_ctx);
	~FileUpdater();

	void update();
	void revert();
	bool reset_rights(const fs::path& path);

private:
	update_client *m_client_ctx;
	fs::path m_old_files_dir;
	fs::path m_app_dir;
};

/*##############################################
 *#
 *# Class Implementation
 *#
 *############################################*/

FileUpdater::FileUpdater(update_client *client_ctx)
 : m_client_ctx(client_ctx),
   m_old_files_dir(client_ctx->params->temp_dir),
   m_app_dir(client_ctx->params->app_dir)
{
	m_old_files_dir /= "old-files";

	fs::create_directories(m_old_files_dir);
}

FileUpdater::~FileUpdater()
{
	fs::remove_all(m_old_files_dir);
}

void FileUpdater::update()
{
	update_client::manifest_map::iterator iter;
	update_client::manifest_map &manifest = m_client_ctx->manifest;

	fs::path &new_files_dir = m_client_ctx->new_files_dir;

	for (iter = manifest.begin(); iter != manifest.end(); ++iter) {
		fs::path to_path(m_app_dir);
		to_path /= iter->first;

		fs::path old_file_path(m_old_files_dir);
		old_file_path /= iter->first;

		fs::path from_path(new_files_dir);
		from_path /= iter->first;

		fs::create_directories(old_file_path.parent_path());
		fs::create_directories(to_path.parent_path());

		if (fs::exists(to_path))
			fs::rename(to_path, old_file_path);

		fs::rename(from_path, to_path);
		
		try {
			reset_rights(to_path);
		} catch (...) {
		}
	}
}

bool FileUpdater::reset_rights(const fs::path& path)
{
	ACL empty_acl;
	if (InitializeAcl(&empty_acl, sizeof(empty_acl), ACL_REVISION))
	{
		DWORD result = SetNamedSecurityInfo((LPWSTR)path.c_str(), SE_FILE_OBJECT,
			DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
			0, 0, &empty_acl, 0);
		if (result == ERROR_SUCCESS)
		{
			return true;
		}
	}
	return false;
}

void FileUpdater::revert()
{
	/* Generate the manifest for the current application directory */
	fs::recursive_directory_iterator iter(m_old_files_dir);
	fs::recursive_directory_iterator end_iter{};
	boost::system::error_code ec;

	for (; iter != end_iter; ++iter) {
		/* Fetch relative paths */
		fs::path rel_path(fs::relative(iter->path(), m_old_files_dir));

		fs::path to_path(m_app_dir);
		to_path /= rel_path;
		
		
		fs::remove(to_path, ec);

		fs::rename(iter->path(), to_path, ec);
	}
}

template <class Body, bool IncludeVersion>
update_client::http_request<Body, IncludeVersion>::http_request(
  update_client *client_ctx,
  const std::string &target,
  const int id
) :
  worker_id(id),
  client_ctx(client_ctx),
  target(target),
  ssl_socket(client_ctx->io_ctx, client_ctx->ssl_context)
{
	std::string full_target;

	if (IncludeVersion) {
		full_target = fmt::format("{}/{}/{}",
			client_ctx->params->host.path,
			client_ctx->params->version,
			target
		);
	} else {
		full_target = fmt::format("{}/{}",
			client_ctx->params->host.path,
			target
		);
	}

	request = {
		http::verb::get,
		full_target,
		11
	};

	request.set(http::field::host, client_ctx->params->host.authority);

	request.set(
		http::field::user_agent,
		BOOST_BEAST_VERSION_STRING
	);

	response_parser.body_limit(
		std::numeric_limits<unsigned long long>::max()
	);
}

struct update_client::pid {
	uint64_t id{0};
	asio::windows::object_handle wrapper;

	pid(asio::io_context &ctx, HANDLE hProcess);
};

update_client::pid::pid(asio::io_context &io_ctx, HANDLE hProcess)
 : wrapper(io_ctx, hProcess)
{
}

void update_client::start_file_update()
{
	FileUpdater updater(this);

	try {
		updater.update();
		client_events->success();
	} catch (...) {
		updater.revert();
		client_events->error(
			"Failed to move files.\n"
			"Please make sure the application files "
			"are not in use and try again."
		);
	}
}

void update_client::handle_pid(
  const boost::system::error_code& error,
  update_client::pid* context
) {
	pid_events->pid_wait_finished(context->id);

	if (--active_pids == 0) {
		pid_events->pid_wait_complete();
		start_file_update();
	}

	delete context;
}

void update_client::handle_pids()
{
	active_pids = params->pids.size();

	if (active_pids == 0) {
		start_file_update();
	}

	for (
	  auto iter = params->pids.begin();
	  iter != params->pids.end();
	  ++iter
	) {
		HANDLE hProcess = OpenProcess(
			PROCESS_ALL_ACCESS,
			FALSE, (DWORD)*iter
		);

		pid_events->pid_waiting_for(*iter);

		update_client::pid *pid_ctx =
			new update_client::pid(io_ctx, hProcess);

		pid_ctx->id = *iter;
		pid_ctx->wrapper.async_wait([this, pid_ctx] (auto ec) {
			this->handle_pid(ec, pid_ctx);
		});
	}
}

/* FIXME Make the other handlers part of the client class. */
void update_client::handle_resolve(
  const boost::system::error_code &error,
  tcp::resolver::results_type results
) {
	if (error) {
		client_events->error(
			"Failed to connect to update server."
		);
		return;
	}

	endpoints = results;

	/* TODO I should make hash type configurable. */
	std::string manifest_target{
		fmt::format("{}.sha256", params->version)
	};

	/* This seems like duplicate code but notice that
	 * request_ctx becomes two different types depending
	 * on the path. We only need to do this once, the rest
	 * is automatically deduced. */
	auto *request_ctx = new manifest_request<manifest_body>(
		this, manifest_target, 0
	);

	auto connect_handler =
	[this, request_ctx] (auto i, auto e) {
		this->handle_manifest_connect(i, e, request_ctx);
	};

	asio::async_connect(
		request_ctx->ssl_socket.lowest_layer(),
		results,
		connect_handler
	);
}

update_client::update_client(struct update_parameters *params)
 : params(params),
   active_workers(0),
   resolver(io_ctx)
{
	new_files_dir = params->temp_dir;
	new_files_dir /= "new-files";

	this->ssl_context.set_default_verify_paths();

	fs::create_directories(new_files_dir);

	const unsigned num_workers = std::thread::hardware_concurrency();

	work = new work_guard_type(asio::make_work_guard(io_ctx));

	for (unsigned i = 0; i < num_workers; ++i) {
		thread_pool.emplace_back(
			std::thread([=] () {
				io_ctx.run();
			})
		);
	}
}

update_client::~update_client()
{
	reset_work();
}

void update_client::create_work()
{
	work = new work_guard_type(asio::make_work_guard(io_ctx));
}

void update_client::reset_work()
{
	if (work == nullptr)
		return;

	work->reset();
	delete work;
	work = nullptr;
}

void update_client::flush()
{
	for (std::thread &thrd : thread_pool) {
		thrd.join();
	}
}

void update_client::do_stuff()
{
	auto cb = [=] (auto e, auto i) {
		this->handle_resolve(e, i);
	};

	client_events->initialize();
	
	resolver.async_resolve(
		params->host.authority,
		params->host.scheme,
//		boost::asio::ip::tcp::resolver::query::address_configured,
		cb
	);

	reset_work();
}

/*##############################################
 *#
 *# Manifest Handlers
 *#
 *############################################*/

static std::string calculate_checksum(fs::path &path)
{
	bio::chain<bio::input> checksum_chain;
	sha256_filter checksum_filter;

	char useless_buffer[4096];

	checksum_chain.push(
		boost::reference_wrapper<sha256_filter>(checksum_filter),
		4096
	);

	checksum_chain.push(bio::file_descriptor_source(path), 4096);

	/* Drain our source */
	while (checksum_chain.read(&useless_buffer[0], 4096) != -1);

	/* Close it */
	checksum_chain.reset();

	std::string hex_digest;
	hex_digest.reserve(64);

	/* TODO 32 is hardcoded here for the size of an SHA-256 digest buffer */
	for (int i = 0; i < 32; ++i) {
		fmt::format_to(
			std::back_inserter(hex_digest),
			"{:02x}", checksum_filter.digest[i]
		);
	}

	return hex_digest;
}

/* TODO Read a book on how to properly name things.
 * This removes unneeded entries in the manifest */
void update_client::clean_manifest()
{
	/* Generate the manifest for the current application directory */
	fs::recursive_directory_iterator app_dir_iter(this->params->app_dir);

	fs::recursive_directory_iterator end_iter{};

	for (; app_dir_iter != end_iter; ++app_dir_iter) {
		fs::path entry = app_dir_iter->path();

		fs::path key_path(
			fs::relative(entry, this->params->app_dir)
		);

		std::string key = key_path.make_preferred().string();

		auto manifest_iter = this->manifest.find(key);

		/* If we don't know about the file,
		 * just leave it alone for now.
		 * TODO Should we delete unknown files? */
		if (manifest_iter == this->manifest.end())
			continue;

		std::string checksum = calculate_checksum(entry);

		/* If the checksum is the same as in the manifest,
		 * remove it from the manifest entirely, there's no need
		 * to download it (as it's already the same). */
		if (checksum.compare(manifest_iter->second) == 0) {
			this->manifest.erase(manifest_iter);
			continue;
		}

		check_file(entry);
	}
}

void update_client::check_file(fs::path & check_path)
{
	const std::wstring path_str = check_path.generic_wstring();

	HANDLE hFile = CreateFile(path_str.c_str(), GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (hFile == INVALID_HANDLE_VALUE)
	{
		DWORD errorCode = GetLastError();

		switch (errorCode)
		{
		case ERROR_SUCCESS:
		case ERROR_FILE_NOT_FOUND:
		case ERROR_PATH_NOT_FOUND:
			//its normal we can update file that not exist before
			break;
		case ERROR_SHARING_VIOLATION:
		case ERROR_LOCK_VIOLATION:
			//its bad, but it probaly zombie process and restart can help 
			throw update_exception_blocked();
			break;
		case ERROR_ACCESS_DENIED:
		case ERROR_WRITE_PROTECT:
		case ERROR_WRITE_FAULT:
		case ERROR_OPEN_FAILED:
		default:
			//its bad 
			throw update_exception_failed();
		}
	} else
	{
		CloseHandle(hFile);
	}
	return;
}

void update_client::handle_manifest_results()
{
	/* TODO We should be able to make max configurable.*/
	int max = 4;

	try
	{
		this->clean_manifest();
	} catch (update_exception_blocked& error)
	{
		client_events->error(
			"Failed to move files.\n"
			"Some files may be blocked by other program. Please restart "
			"your PC and try to update again."
		);
		return;
	} catch (update_exception_failed& error)
	{
		client_events->error(
			"Failed to move files.\n"
			"Some files could not be updated. Please download "
			"SLOBS installer from our site and run full installation"
		);
		return;
	}

	this->manifest_iterator = this->manifest.cbegin();
	this->downloader_events->downloader_start(
		max, this->manifest.size()
	);

	/* To make sure we only have `max` number of
	 * of requests at any given time, we hold the
	 * mutex for the duration of this for loop.
	 * Otherwise a request could finish too fast
	 * and we would have `n` extra requests at once
	 * where n is the request that finished too fast. */
	std::unique_lock<std::mutex> manifest_lock(this->manifest_mutex);

	/* We have no work but we still need to provide the
	 * complete event so the UI may close correctly */

	if (this->manifest_iterator == this->manifest.end()) {
		this->client_events->success();
		return;
	}

	for (
	  int i = 0;
	  this->manifest_iterator != this->manifest.end() && i < max;
	  ++this->manifest_iterator, ++i
	) {
		++this->active_workers;

		this->handle_manifest_entry(
			*this->manifest_iterator, i
		);
	}

	manifest_lock.unlock();
}

template <class ConstBuffer>
static size_t handle_manifest_read_buffer(
  update_client::manifest_map &map,
  const ConstBuffer &buffer
) {
	/* TODO: Hardcoded for SHA-256 checksums. */
	static const regex manifest_regex(
		"([A-Fa-f0-9]{64}) ([^\r\n]+)\r?\n"
	);

	size_t accum = 0;

	for (;;) {
		const char *buf;
		std::string checksum;
		std::string file;
		cmatch matches;
		size_t buf_size = buffer.size() - accum;

		/* Technically, this for loop will
		 * always iterate once and then hit this
		 * the second time around. */
		if (buf_size == 0) break;

		buf = (const char*)buffer.data() + accum;

		bool regex_result = regex_search(
			&buf[0], &buf[buf_size],
			matches, manifest_regex
		);

		if (!regex_result) {
			/* FIXME TODO
			 * This should never ever happen the way
			 * the code is currently formatted. If
			 * this happens, either the buffers are
			 * given incorrectly or the manifest is
			 * malformed. This should be a fatal error.
			 * That said, if we ever go back to dynamic
			 * buffers, we should instead figure out
			 * a way to store the section of the previous
			 * buffer that we weren't able to fully parse.
			 * Right now, we assume a singular contiguous
			 * buffer (usually around 20kB total for a
			 * 1000+ line manifest). */
			break;
		}

		checksum.assign(matches[2].first, matches[2].length());
		file.assign(matches[1].first, matches[1].length());
		map.emplace(std::make_pair(checksum, file));

		accum += matches.length();
	}

	return accum;
}

void update_client::handle_manifest_response(
  boost::system::error_code &ec,
  size_t bytes,
  manifest_request<manifest_body> *request_ctx
) {
	/* We're done with this request context after this function */
	std::unique_ptr<manifest_request<manifest_body>>
		safe_request_ctx(request_ctx);

	if (ec) {
		std::string msg = fmt::format(
			"Failed manifest response ({})",
			safe_request_ctx->target
		);

		handle_error(ec, msg.c_str());
		return;
	}

	auto &response_parser = safe_request_ctx->response_parser;
	auto &buffer = response_parser.get().body();
	int status_code = response_parser.get().result_int();

	if (status_code != 200) {
		auto target = safe_request_ctx->request.target();

		/* FIXME Signal failure */
		handle_error({}, "No manifest file on server");
		return;
	}

	/* Flatbuffer doesn't return a buffer sequence.  */
	handle_manifest_read_buffer(this->manifest, buffer.data());
	handle_manifest_results();
};

void update_client::handle_manifest_request(
  boost::system::error_code &error,
  size_t bytes,
  manifest_request<manifest_body> *request_ctx
) {
	if (error) {
		std::string msg = fmt::format(
			"Failed manifest request ({}): {}",
			request_ctx->target, error.message().c_str()
		);

		handle_error(error, msg.c_str());
		return;
	}

	auto read_handler = [this, request_ctx] (auto e, auto bt) {
		this->handle_manifest_response(e, bt, request_ctx);
	};

	if (request_ctx->response_parser.is_done()) {
		handle_error({}, "No body message provided");
		delete request_ctx;
		return;
	}

	http::async_read(
		request_ctx->ssl_socket,
		request_ctx->response_buf,
		request_ctx->response_parser,
		read_handler
	);
}

void update_client::handle_manifest_handshake(
  const boost::system::error_code& error,
  manifest_request<manifest_body> *request_ctx
) {
	if (error) {
		std::string msg = fmt::format(
			"Failed manifest handshake ({}): {}",
			request_ctx->target, error.message().c_str()
		);

		handle_error(error, msg.c_str());
	}

	auto request_handler = [this, request_ctx] (auto e, auto b) {
		this->handle_manifest_request(e, b, request_ctx);
	};

	http::async_write(
		request_ctx->ssl_socket,
		request_ctx->request,
		request_handler
	);
}

void update_client::handle_manifest_connect(
  const boost::system::error_code &error,
  const tcp::endpoint &ep,
  manifest_request<manifest_body> *request_ctx
) {
	if (error) {
		handle_error(error, "Failed to connect to host for manifest");
		return;
	}

	auto handshake_handler = [this, request_ctx] (auto e) {
		this->handle_manifest_handshake(e, request_ctx);
	};

	request_ctx->ssl_socket.async_handshake(
		ssl::stream_base::handshake_type::client,
		handshake_handler
	);
}

/*
 *##############################################
 *#
 *# File Handlers
 *#
 *############################################*/

static constexpr std::ios_base::openmode file_flags =
	std::ios_base::out |
	std::ios_base::binary |
	std::ios_base::trunc;

struct update_client::file {
	fs::path file_path;
	fs::ofstream file_stream;
	bio::gzip_decompressor decompress_filter;
	sha256_filter checksum_filter;

	bio::chain<bio::output> output_chain;

	explicit file(const fs::path &path);
};

update_client::file::file(const fs::path &file_path)
 : file_path(file_path), file_stream(file_path, file_flags)
{
	if (this->file_stream.bad()) {
		log_info("Failed to create file output stream\n");
		/* TODO File failed to open here */
	}

	this->output_chain.push(
		boost::reference_wrapper<
			bio::gzip_decompressor
		>(this->decompress_filter),
		4096
	);

	this->output_chain.push(
		boost::reference_wrapper<
			sha256_filter
		>(this->checksum_filter),
		4096
	);

	this->output_chain.push(
		boost::reference_wrapper<
			std::ofstream
		>(this->file_stream),
		4096
	);
}

fs::path generate_file_path(const fs::path &base, const fs::path &target)
{
	fs::path file_path(base);
	file_path /= target;

	file_path.make_preferred();
	file_path.replace_extension();

	fs::create_directories(file_path.parent_path());

	return file_path;
}

void update_client::handle_file_result(
  update_client::file *file_ctx,
  int index
) {
	auto &filter = file_ctx->checksum_filter;

	//file_ctx->output_chain.reset();

	std::string hex_digest;
	hex_digest.reserve(64);

	/* FIXME TODO 32 is hardcoded here for the size of
	 * an SHA-256 digest buffer */
	for (int i = 0; i < 32; ++i) {
		fmt::format_to(
			std::back_inserter(hex_digest),
			"{:02x}", filter.digest[i]
		);
	}

	/* We now have the file and the digest of both downloaded
	 * file and the wanted file. Do a string comparison. If it
	 * doesn't match, message and fail. */

	delete file_ctx;

	std::unique_lock<std::mutex> manifest_lock(this->manifest_mutex);

	if (this->manifest_iterator == this->manifest.end()) {
		--this->active_workers;

		this->downloader_events->download_worker_finished(index);

		if (this->active_workers == 0) {
			this->downloader_events->downloader_complete();
			this->handle_pids();
		}

		return;
	}

	auto entry = *this->manifest_iterator;

	++this->manifest_iterator;

	/* Only hold the lock until we can get a reference to
	 * the entry. We are guaranteed that the entry and
	 * manifest are no longer modified at this point so
	 * the reference will stay valid */
	manifest_lock.unlock();

	/* MAYBE FIXME If we ever want to support alternative protocols,
	 * we should make an explicit check for scheme here. */
	handle_manifest_entry(entry, index);
}


void handle_file_response_buffer(
  update_client::file *file_ctx,
  const asio::const_buffer &buffer
) {
	file_ctx->output_chain.write(
		(const char*)buffer.data(),
		buffer.size()
	);
}

void update_client::handle_file_response_body(
  boost::system::error_code &error,
  size_t bytes_read,
  file_request<http::dynamic_body> *request_ctx,
  update_client::file *file_ctx
) {
	if (error) {
		std::string msg = fmt::format(
			"Failed file response body ({})",
			request_ctx->target
		);

		handle_error(error, msg.c_str());
		delete request_ctx;
		return;
	}

	auto &body = request_ctx->response_parser.get().body();
	auto &response_parser = request_ctx->response_parser;

	for (
	  auto iter = asio::buffer_sequence_begin(body.data());
	  iter != asio::buffer_sequence_end(body.data());
	  ++iter
	) {
		handle_file_response_buffer(file_ctx, *iter);
	}

	size_t consumed = asio::buffer_size(body.data());

	body.consume(consumed);
	request_ctx->download_accum += consumed;

	this->downloader_events->download_progress(
		request_ctx->worker_id,
		consumed,
		request_ctx->download_accum
	);

	if (response_parser.is_done()) {
		int worker_id = request_ctx->worker_id;
		delete request_ctx;

		handle_file_result(file_ctx, worker_id);

		return;
	}

	auto read_handler = [this, request_ctx, file_ctx] (auto i, auto e) {
		this->handle_file_response_body(
			i, e,
			request_ctx,
			file_ctx
		);
	};

	http::async_read_some(
		request_ctx->ssl_socket,
		request_ctx->response_buf,
		response_parser,
		read_handler
	);
}

void update_client::handle_file_response_header(
  boost::system::error_code &error,
  size_t bytes,
  file_request<http::dynamic_body> *request_ctx
) {
	if (error) {
		std::string msg = fmt::format(
			"Failed file response header ({})",
			request_ctx->target
		);

		handle_error(error, msg.c_str());
		delete request_ctx;
		return;
	}

	auto &response_parser = request_ctx->response_parser;
	int status_code = response_parser.get().result_int();
	request_ctx->content_length = response_parser.content_length().value();

	this->downloader_events->download_file(
		request_ctx->worker_id,
		request_ctx->target,
		request_ctx->content_length
	);

	fs::path file_path = generate_file_path(
		this->new_files_dir,
		fs::path(request_ctx->target)
	);

	/* FIXME Signal failure */
	if (file_path.empty()) {
		log_error("Failed to create file path\n");
		return;
	}

	auto file_ctx = new update_client::file(
		fs::path(unfixup_uri(file_path.string()))
	);

	auto read_handler = [this, request_ctx, file_ctx] (auto i, auto e) {
		this->handle_file_response_body(
			i, e,
			request_ctx,
			file_ctx);
	};

	if (status_code != 200) {
		auto target = request_ctx->request.target();

		std::string output_str = fmt::format(
			"Status Code: {}\nFile: {}\n",
			status_code,
			fmt::string_view(target.data(), target.size())
		);

		handle_error({}, output_str.c_str());
		return;
	}

	http::async_read_some(
		request_ctx->ssl_socket,
		request_ctx->response_buf,
		response_parser,
		read_handler
	);
}

void update_client::handle_file_request(
  boost::system::error_code &error,
  size_t bytes,
  file_request<http::dynamic_body> *request_ctx
) {
	if (error) {
		std::string msg = fmt::format(
			"Failed file request ({})",
			request_ctx->target
		);

		handle_error(error, msg.c_str());
		return;
	}

	auto read_handler = [this, request_ctx] (auto i, auto e) {
		this->handle_file_response_header(i, e, request_ctx);
	};

	http::async_read_header(
		request_ctx->ssl_socket,
		request_ctx->response_buf,
		request_ctx->response_parser,
		read_handler
	);
}

void update_client::handle_manifest_entry(
  const update_client::manifest_map::value_type &entry,
  int index
) {
	auto request_ctx = new file_request<http::dynamic_body>{
		this,
		/* FIXME Hardcoding gz here isn't very flexible */
		fmt::format("{}.gz", fixup_uri(entry.first)),
		index
	};

	auto connect_handler = [this, request_ctx] (auto e, auto b) {
		this->handle_file_connect(e, b, request_ctx);
	};

	asio::async_connect(
		request_ctx->ssl_socket.lowest_layer(),
		this->endpoints,
		connect_handler
	);
}

void update_client::handle_file_handshake(
  const boost::system::error_code& error,
  file_request<http::dynamic_body> *request_ctx
) {
	if (error) {
		std::string msg = fmt::format(
			"Failed manifest handshake ({}): {}",
			request_ctx->target, error.message().c_str()
		);

		handle_error(error, msg.c_str());
		return;
	}

	auto request_handler = [this, request_ctx] (auto e, auto b) {
		this->handle_file_request(e, b, request_ctx);
	};

	http::async_write(
		request_ctx->ssl_socket,
		request_ctx->request,
		request_handler
	);
}

void update_client::handle_file_connect(
  const boost::system::error_code &error,
  const tcp::endpoint &ep,
  file_request<http::dynamic_body> *request_ctx
) {
	if (error) {
		handle_error(error, "Failed to connect to host for file");
		return;
	}

	auto handshake_handler = [this, request_ctx] (auto e) {
		this->handle_file_handshake(e, request_ctx);
	};

	request_ctx->ssl_socket.async_handshake(
		ssl::stream_base::handshake_type::client,
		handshake_handler
	);
}

/*##############################################
 *#
 *# C interface functionality
 *#
 *############################################*/
extern "C" {

struct update_client *create_update_client(struct update_parameters *params)
{
	update_client *client = new update_client(params);

	return client;
}

void destroy_update_client(struct update_client *client)
{
	delete client;
}


void update_client_set_client_events(
  struct update_client *client,
  struct client_callbacks *events
) {
	client->set_client_events(events);
}


void update_client_set_downloader_events(
  struct update_client *client,
  struct downloader_callbacks *events
) {
	client->set_downloader_events(events);
}


void update_client_set_updater_events(
  struct update_client *client,
  struct updater_callbacks *events
) {
	client->set_updater_events(events);
}


void update_client_set_pid_events(
  struct update_client *client,
  struct pid_callbacks *events
) {
	client->set_pid_events(events);
}

void update_client_start(struct update_client *client)
{
	client->do_stuff();
}

void update_client_flush(struct update_client *client)
{
	client->flush();
}


}