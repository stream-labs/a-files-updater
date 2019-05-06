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
#include <boost/bind.hpp>
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

const size_t file_buffer_size = 4096;

#include "update-blockers.hpp"

#include "update-client.hpp"
#include "checksum-filters.hpp"
#include "update-parameters.hpp"
#include "logger/log.h"

#include "update-client-internal.hpp"

const std::string failed_to_revert_message = "Failed to move files.\nPlease make sure the application files are not in use and try again.";
const std::string failed_to_update_message = "Failed to move files.\nPlease make sure the application files are not in use and try again.";
const std::string failed_connect_to_server_message = "Failed to connect to update server.";
const std::string blocked_file_message = "Failed to move files.\nSome files may be blocked by other program. Please restart your PC and try to update again.";
const std::string locked_file_message = "Failed to move files.\nSome files could not be updated. Please download SLOBS installer from our site and run full installation.";
const std::string restart_or_install_message = "Streamlabs OBS encountered an issue while downloading the update. \nPlease restart the application to finish updating. \nIf the issue persists, please download a new installer from www.streamlabs.com.";

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
	boost::system::error_code ec;

	fs::remove_all(m_old_files_dir, ec);
	if (ec)
	{
		wlog_warn(L"Failed to clean temp folder.");
	}
}

void FileUpdater::update()
{
	update_client::manifest_map_t::iterator iter;
	update_client::manifest_map_t &manifest = m_client_ctx->manifest;

	fs::path &new_files_dir = m_client_ctx->new_files_dir;
	std::string version_file_key = "resources\app.asar";

	for (iter = manifest.begin(); iter != manifest.end(); ++iter)
	{
		if (version_file_key.compare(iter->first) != 0)
		{
			update_entry_with_retries(iter, new_files_dir);
		}
	}

	auto version_file = manifest.find(version_file_key);
	if (version_file != manifest.end())
	{
		update_entry_with_retries(version_file, new_files_dir);
	}
}

bool FileUpdater::update_entry_with_retries(update_client::manifest_map_t::iterator &iter, boost::filesystem::path &new_files_dir)
{
	int retries = 0;
	const int max_retries = 5;
	bool ret = false;

	while (retries < max_retries && !ret)
	{
		retries++;
		ret = update_entry(iter, new_files_dir);
		if (!ret)
		{
			std::wstring wmsg(iter->first.begin(), iter->first.end());
			wlog_warn(L"Have failed to update file: %s, will retry", wmsg.c_str());
			Sleep(100*retries);
		}
	}

	if (!ret)
	{
		std::wstring wmsg(iter->first.begin(), iter->first.end());
		wlog_warn(L"Have failed to update file: %s", wmsg.c_str());
		throw std::runtime_error("Error: failed to update file");
	}
	return ret;
}

bool FileUpdater::update_entry(update_client::manifest_map_t::iterator &iter, boost::filesystem::path &new_files_dir)
{
	boost::system::error_code ec;

	fs::path to_path(m_app_dir);
	to_path /= iter->first;

	fs::path old_file_path(m_old_files_dir);
	old_file_path /= iter->first;

	fs::path from_path(new_files_dir);
	from_path /= iter->first;
	
	try
	{
		fs::create_directories(old_file_path.parent_path());
		fs::create_directories(to_path.parent_path());
   
 		if (fs::exists(to_path))
		{
			fs::rename(to_path, old_file_path, ec);
			if (ec)
			{
				std::string msg = ec.message();
				std::wstring wmsg(msg.begin(), msg.end());

				wlog_debug(L"Failed to move file %s %s, error %s", to_path.c_str(), old_file_path.c_str(), wmsg.c_str());
				return false;
			}
		}

		fs::rename(from_path, to_path, ec);
		if (ec)
		{
			std::string msg = ec.message();
			std::wstring wmsg(msg.begin(), msg.end());

			wlog_debug(L"Failed to move file %s %s, error %s", from_path.c_str(), to_path.c_str(), wmsg.c_str());
			return false;
		}

		try
		{
			reset_rights(to_path);
		}
		catch (...)
		{
			wlog_warn(L"Have failed to update file rights: %s", to_path.c_str());
		}

		return true;
	}
	catch (...)
	{
		wlog_warn(L"Have failed to update file in function: %s", to_path.c_str());
	}

	return false;
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
	int error_count = 0;

	for (; iter != end_iter; ++iter)
	{
		/* Fetch relative paths */
		fs::path rel_path(fs::relative(iter->path(), m_old_files_dir));

		fs::path to_path(m_app_dir);
		to_path /= rel_path;

		fs::remove(to_path, ec);
		if (ec)
		{
			wlog_warn(L"Revert have failed to correctly remove changed file: %s ", to_path.c_str());
			error_count++;
		}

		fs::rename(iter->path(), to_path, ec);
		if (ec)
		{
			wlog_warn(L"Revert have failed to correctly move file back: %s ", to_path.c_str());
			error_count++;
		}
	}

	if (error_count > 0)
	{
		wlog_warn(L"Revert have failed to correctly revert some files. Fails: %i", error_count);
	}
}

// limit response_buf buffer size so together with deadline timeout 
// it will create low speed limit for download 
// 4kb in 4 seconds is somewhere of old modems 
// and be recognized as stuck connection

template <class Body, bool IncludeVersion>
update_client::http_request<Body, IncludeVersion>::http_request(update_client *client_ctx, const std::string &target, const int id) :
	worker_id(id),
	client_ctx(client_ctx),
	target(target),
	ssl_socket(client_ctx->io_ctx, client_ctx->ssl_context),
	response_buf(file_buffer_size), // see reasons above ^
	deadline(client_ctx->io_ctx)
{
	std::string full_target;

	if (IncludeVersion)
	{
		full_target = fmt::format("{}/{}/{}", client_ctx->params->host.path, client_ctx->params->version, target);
	}
	else {
		full_target = fmt::format("{}/{}", client_ctx->params->host.path, target);
	}

	request = { http::verb::get, full_target, 11 };

	request.set(http::field::host, client_ctx->params->host.authority);

	request.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

	response_parser.body_limit(std::numeric_limits<unsigned long long>::max());

	deadline.expires_at(boost::posix_time::pos_infin);
}

template<class Body, bool IncludeVersion>
update_client::http_request<Body, IncludeVersion>::~http_request()
{
	deadline.cancel();
}

template<class Body, bool IncludeVersion>
void update_client::http_request<Body, IncludeVersion>::check_deadline_callback_err(const boost::system::error_code& error)
{
	if (error)
	{
		if (error == boost::asio::error::operation_aborted)
		{
			return;
		}
		else {
			log_error("File download operation deadline error %i", error.value());
			return;
		}
	}

	if (deadline.expires_at() <= boost::asio::deadline_timer::traits_type::now())
	{
		log_info("Timeout for file download operation trigered.");
		deadline_reached = true;

		boost::system::error_code ignored_ec;
		ssl_socket.lowest_layer().shutdown(boost::asio::ip::tcp::socket::shutdown_both, ignored_ec);

		deadline.expires_at(boost::posix_time::pos_infin);
	}
	else {
		// Put the actor back to sleep.
		deadline.async_wait(bind(&update_client::http_request<Body, IncludeVersion>::check_deadline_callback_err, this, std::placeholders::_1));
	}
}

template<class Body, bool IncludeVersion>
void update_client::http_request<Body, IncludeVersion>::set_deadline()
{
	deadline.expires_from_now(boost::posix_time::seconds(deadline_default_timeout));
	check_deadline_callback_err(make_error_code(boost::system::errc::success));
}

void update_client::start_file_update()
{
	log_debug("Files downloaded and ready to start update.");

	FileUpdater updater(this);
	bool updated = false;

	try {
		updater.update();

		log_debug("Finished updating files without errors.");
		client_events->success();
		updated = true;
	}
	catch (...) {
		
	}

	if (!updated)
	{
		bool reverted = true;
		log_debug("Got error while updating files. Have to revert.");
		try {
			updater.revert();
			reverted = true;
			log_debug("Revert complited.");
		}
		catch (...) {
			log_debug("Revert failed.");
		}	
		
		if (reverted) 
		{
			client_events->error(failed_to_revert_message.c_str());
		}
		else {
			client_events->error(failed_to_update_message.c_str());
		}
	}
}

struct update_client::pid
{
	uint64_t id{ 0 };
	asio::windows::object_handle wrapper;

	pid(asio::io_context &ctx, HANDLE hProcess);
};

update_client::pid::pid(asio::io_context &io_ctx, HANDLE hProcess) : wrapper(io_ctx, hProcess)
{
}

void update_client::handle_pid(const boost::system::error_code& error, update_client::pid* context)
{
	pid_events->pid_wait_finished(context->id);

	if (--active_pids == 0)
	{
		pid_events->pid_wait_complete();
		handle_manifest_results();
	}

	delete context;
}

void update_client::handle_pids()
{
	active_pids = params->pids.size();

	if (active_pids == 0)
	{
		handle_manifest_results();
	}

	for (auto iter = params->pids.begin(); iter != params->pids.end(); ++iter)
	{
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)*iter);

		pid_events->pid_waiting_for(*iter);

		update_client::pid *pid_ctx = new update_client::pid(io_ctx, hProcess);

		pid_ctx->id = *iter;
		pid_ctx->wrapper.async_wait([this, pid_ctx](auto ec) { this->handle_pid(ec, pid_ctx); });
	}
}

inline void update_client::handle_network_error(const boost::system::error_code & error, const char * str)
{
	char error_buf[256];

	snprintf(error_buf, sizeof(error_buf), "%s\0", restart_or_install_message.c_str());

	client_events->error(error_buf);

	snprintf(error_buf, sizeof(error_buf), "%s - %s\0", str, error.message().c_str());

	log_error(error_buf);
}

void update_client::handle_file_download_error(file_request<http::dynamic_body> *request_ctx, const boost::system::error_code & error, const char * str)
{
	if (request_ctx->retries > 5)
	{
		boost::system::error_code ec = error;
		if (error == boost::asio::error::basic_errors::operation_aborted && request_ctx->deadline_reached)
		{
			ec = boost::asio::error::basic_errors::timed_out;
		}

		update_canceled = true;

		cancel_message = str;
		cancel_error = error;

		handle_file_download_canceled(request_ctx);
		return;
	}
	else {
		auto new_request_ctx = new file_request<http::dynamic_body>{ this, request_ctx->target, request_ctx->worker_id };
		new_request_ctx->retries = request_ctx->retries + 1;

		delete request_ctx;

		Sleep( new_request_ctx->retries*100 );

		handle_manifest_entry(new_request_ctx);
	}
}

void update_client::handle_file_download_canceled(file_request<http::dynamic_body>* request_ctx)
{
	auto index = request_ctx->worker_id;
	delete request_ctx;

	next_manifest_entry(index);
}

/* FIXME Make the other handlers part of the client class. */
void update_client::handle_resolve(const boost::system::error_code &error, tcp::resolver::results_type results) {
	if (error) {
		handle_network_error(error, failed_connect_to_server_message.c_str());
		return;
	}

	endpoints = results;

	/* TODO I should make hash type configurable. */
	std::string manifest_target{ fmt::format("{}.sha256", params->version) };

	/* This seems like duplicate code but notice that
	 * request_ctx becomes two different types depending
	 * on the path. We only need to do this once, the rest
	 * is automatically deduced. */
	auto *request_ctx = new manifest_request<manifest_body>(this, manifest_target, 0);

	auto connect_handler = [this, request_ctx](auto i, auto e) {
		this->handle_manifest_connect(i, e, request_ctx);
	};

	asio::async_connect(request_ctx->ssl_socket.lowest_layer(), results, connect_handler);
}

update_client::update_client(struct update_parameters *params)
	: params(params),
	wait_for_blockers(io_ctx),
	show_user_blockers_list( true),
	active_workers(0),
	resolver(io_ctx)
{
	new_files_dir = params->temp_dir;
	new_files_dir /= "new-files";

	this->ssl_context.set_default_verify_paths();

	fs::create_directories(new_files_dir);

	const unsigned num_workers = std::thread::hardware_concurrency();

	work = new work_guard_type(asio::make_work_guard(io_ctx));

	for (unsigned i = 0; i < num_workers; ++i)
	{
		thread_pool.emplace_back(
			std::thread([=]()
		{
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
	for (std::thread &thrd : thread_pool)
	{
		thrd.join();
	}
}

void update_client::do_stuff()
{
	auto cb = [=](auto e, auto i)
	{
		this->handle_resolve(e, i);
	};

	client_events->initialize();

	resolver.async_resolve( params->host.authority, params->host.scheme, cb );

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

	char useless_buffer[file_buffer_size];

	checksum_chain.push(boost::reference_wrapper<sha256_filter>(checksum_filter), file_buffer_size);

	checksum_chain.push(bio::file_descriptor_source(path), file_buffer_size);

	/* Drain our source */
	while (checksum_chain.read(&useless_buffer[0], file_buffer_size) != -1);

	/* Close it */
	checksum_chain.reset();

	std::string hex_digest;
	hex_digest.reserve(64);

	/* TODO 32 is hardcoded here for the size of an SHA-256 digest buffer */
	for (int i = 0; i < 32; ++i)
	{
		fmt::format_to(std::back_inserter(hex_digest), "{:02x}", checksum_filter.digest[i]);
	}

	return hex_digest;
}

/* TODO Read a book on how to properly name things.
 * This removes unneeded entries in the manifest */
bool update_client::clean_manifest(blockers_map_t &blockers)
{
	/* Generate the manifest for the current application directory */
	fs::recursive_directory_iterator app_dir_iter(this->params->app_dir);

	fs::recursive_directory_iterator end_iter{};

	for (; app_dir_iter != end_iter; ++app_dir_iter)
	{
		fs::path entry = app_dir_iter->path();

		fs::path key_path(fs::relative(entry, this->params->app_dir));

		std::string key = key_path.make_preferred().string();

		auto manifest_iter = this->manifest.find(key);

		/* If we don't know about the file,
		 * just leave it alone for now.
		 * TODO Should we delete unknown files? */
		if (manifest_iter == this->manifest.end())
			continue;
		
		if (check_file_updatable(entry, true, blockers))
		{
			if (!manifest_iter->second.compared_to_local)
			{
				std::string checksum = calculate_checksum(entry);

				/* If the checksum is the same as in the manifest,
				 * remove it from the manifest entirely, there's no need
				 * to download it (as it's already the same). */
				if (checksum.compare(manifest_iter->second.hash_sum) == 0)
				{
					this->manifest.erase(manifest_iter);
					continue;
				}
				else {
					manifest_iter->second.compared_to_local = true;
				}
			}

			check_file_updatable(entry, false, blockers);
		}
	}

	return true;
}

void update_client::handle_manifest_results()
{
	/* TODO We should be able to make max configurable.*/
	int max_threads = 4;
	try
	{
		blockers_map_t blockers;
		this->clean_manifest(blockers);

		if (blockers.size() > 0)
		{
			std::wstring new_process_list_text;
			for (auto it = blockers.begin(); it != blockers.end(); it++)
			{
				log_debug("Got blocker process info %i %ls", (*it).second.Process.dwProcessId, (*it).second.strAppName);

				new_process_list_text += std::to_wstring((*it).second.Process.dwProcessId);
				new_process_list_text += L": ";
				new_process_list_text += (*it).second.strAppName;
				new_process_list_text += L"\r\n";
			}
			
			if (show_user_blockers_list)
			{
				show_user_blockers_list = false;
				this->blocker_events->blocker_start();
			}
			
			if (process_list_text.compare(new_process_list_text) != 0)
			{
				process_list_text = new_process_list_text;
				this->blocker_events->blocker_waiting_for(process_list_text);
			}

			wait_for_blockers.expires_from_now(boost::posix_time::seconds(2));

			wait_for_blockers.async_wait(boost::bind(&update_client::handle_manifest_results, this) );

			return;
		}
		else {
			this->blocker_events->blocker_wait_complete();
			show_user_blockers_list = true;
			process_list_text = L"";
		}

	}
	catch (update_exception_blocked& error)
	{
		client_events->error(blocked_file_message.c_str());
		return;
	}
	catch (update_exception_failed& error)
	{
		client_events->error(locked_file_message.c_str());
		return;
	}

	this->manifest_iterator = this->manifest.cbegin();
	this->downloader_events->downloader_start(max_threads, this->manifest.size());

	/* To make sure we only have `max` number of
	 * of requests at any given time, we hold the
	 * mutex for the duration of this for loop.
	 * Otherwise a request could finish too fast
	 * and we would have `n` extra requests at once
	 * where n is the request that finished too fast. */
	std::unique_lock<std::mutex> manifest_lock(this->manifest_mutex);

	/* We have no work but we still need to provide the
	 * complete event so the UI may close correctly */
	if (this->manifest_iterator == this->manifest.end())
	{
		this->client_events->success();
		return;
	}

	for (int i = 0; this->manifest_iterator != this->manifest.end() && i < max_threads; ++this->manifest_iterator, ++i)
	{
		++this->active_workers;

		auto request_ctx = new file_request<http::dynamic_body>{
			this,
			/* FIXME Hardcoding gz here isn't very flexible */
			fmt::format("{}.gz", fixup_uri((*this->manifest_iterator).first)),
			i
		};

		this->handle_manifest_entry(request_ctx);
	}

	manifest_lock.unlock();
}

template <class ConstBuffer>
static size_t handle_manifest_read_buffer(update_client::manifest_map_t &map, const ConstBuffer &buffer)
{
	/* TODO: Hardcoded for SHA-256 checksums. */
	static const regex manifest_regex("([A-Fa-f0-9]{64}) ([^\r\n]+)\r?\n");

	size_t accum = 0;

	for (;;)
	{
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

		bool regex_result = regex_search(&buf[0], &buf[buf_size], matches, manifest_regex);

		if (!regex_result)
		{
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

		file.assign(matches[2].first, matches[2].length());
		checksum.assign(matches[1].first, matches[1].length());
		map.emplace(std::make_pair(file, manifest_entry_t( checksum) ));

		accum += matches.length();
	}

	return accum;
}

void update_client::handle_manifest_response(boost::system::error_code &ec, size_t bytes, manifest_request<manifest_body> *request_ctx)
{
	/* We're done with this request context after this function */
	std::unique_ptr<manifest_request<manifest_body>> safe_request_ctx(request_ctx);

	if (ec)
	{
		std::string msg = fmt::format("Failed manifest response ({})", safe_request_ctx->target);

		handle_network_error(ec, msg.c_str());
		return;
	}

	auto &response_parser = safe_request_ctx->response_parser;
	auto &buffer = response_parser.get().body();
	int status_code = response_parser.get().result_int();

	if (status_code != 200)
	{
		auto target = safe_request_ctx->request.target();

		std::string msg = fmt::format("Manifest response with code {} for ({})", status_code, safe_request_ctx->target);
		handle_network_error(boost::asio::error::basic_errors::connection_aborted, msg.c_str());
		return;
	}

	/* Flatbuffer doesn't return a buffer sequence.  */
	handle_manifest_read_buffer(this->manifest, buffer.data());
	
	/*  make sure that SLOBS process not blocking files from updatating before start of download  */
	handle_pids();
};

void update_client::handle_manifest_request(boost::system::error_code &error, size_t bytes, manifest_request<manifest_body> *request_ctx)
{
	if (error)
	{
		std::string msg = fmt::format("Failed manifest request ({})", request_ctx->target);

		handle_network_error(error, msg.c_str());
		return;
	}

	auto read_handler = [this, request_ctx](auto e, auto bt) {
		this->handle_manifest_response(e, bt, request_ctx);
	};

	if (request_ctx->response_parser.is_done())
	{
		std::string msg = fmt::format("No body message provided ({})", request_ctx->target);
		handle_network_error(boost::asio::error::basic_errors::connection_aborted, msg.c_str());
		delete request_ctx;
		return;
	}

	http::async_read(request_ctx->ssl_socket, request_ctx->response_buf, request_ctx->response_parser, read_handler);
}

void update_client::handle_manifest_handshake(const boost::system::error_code& error, manifest_request<manifest_body> *request_ctx)
{
	if (error)
	{
		std::string msg = fmt::format("Failed manifest handshake ({})", request_ctx->target);

		handle_network_error(error, msg.c_str());
	}

	auto request_handler = [this, request_ctx](auto e, auto b) {
		this->handle_manifest_request(e, b, request_ctx);
	};

	http::async_write(request_ctx->ssl_socket, request_ctx->request, request_handler);
}

void update_client::handle_manifest_connect(const boost::system::error_code &error, const tcp::endpoint &ep, manifest_request<manifest_body> *request_ctx)
{
	if (error)
	{
		std::string msg = fmt::format("Failed to connect to host for manifest ({})", request_ctx->target);

		handle_network_error(error, msg.c_str());
		return;
	}

	auto handshake_handler = [this, request_ctx](auto e) {
		this->handle_manifest_handshake(e, request_ctx);
	};

	request_ctx->ssl_socket.async_handshake(ssl::stream_base::handshake_type::client, handshake_handler);
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
	if (this->file_stream.bad())
	{
		log_info("Failed to create file output stream\n");
		/* TODO File failed to open here */
	}

	this->output_chain.push(boost::reference_wrapper< bio::gzip_decompressor >(this->decompress_filter), file_buffer_size);

	this->output_chain.push(boost::reference_wrapper< sha256_filter >(this->checksum_filter), file_buffer_size);

	this->output_chain.push(boost::reference_wrapper< std::ofstream >(this->file_stream), file_buffer_size);
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

void update_client::handle_file_result(update_client::file *file_ctx, int index)
{
	auto &filter = file_ctx->checksum_filter;

	try {
		file_ctx->output_chain.reset();

		std::string hex_digest;
		hex_digest.reserve(64);

		/* FIXME TODO 32 is hardcoded here for the size of
		 * an SHA-256 digest buffer */
		for (int i = 0; i < 32; ++i)
		{
			fmt::format_to(std::back_inserter(hex_digest), "{:02x}", filter.digest[i]);
		}
	}
	catch (...)
	{

	}
	/* We now have the file and the digest of both downloaded
	 * file and the wanted file. Do a string comparison. If it
	 * doesn't match, message and fail. */
	 //calculate_checksum();

	delete file_ctx;

	next_manifest_entry(index);
}

void update_client::next_manifest_entry(int index)
{
	std::unique_lock<std::mutex> manifest_lock(this->manifest_mutex);

	if (this->manifest_iterator == this->manifest.end() || update_canceled)
	{
		--this->active_workers;

		this->downloader_events->download_worker_finished(index);

		if (this->active_workers == 0)
		{
			this->downloader_events->downloader_complete();
			if (update_canceled)
			{
				handle_network_error(cancel_error, cancel_message.c_str());
			}
			else {
				this->start_file_update();
			}
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

	auto request_ctx = new file_request<http::dynamic_body>{
		this,
		/* FIXME Hardcoding gz here isn't very flexible */
		fmt::format("{}.gz", fixup_uri(entry.first)),
		index
	};

	handle_manifest_entry(request_ctx);
}

void handle_file_response_buffer(update_client::file *file_ctx, const asio::const_buffer &buffer)
{
	file_ctx->output_chain.write((const char*)buffer.data(), buffer.size());
}

void update_client::handle_file_response_body(boost::system::error_code &error, size_t bytes_read, file_request<http::dynamic_body> *request_ctx, update_client::file *file_ctx)
{
	request_ctx->deadline.cancel();

	if (update_canceled)
	{
		delete file_ctx;

		handle_file_download_canceled(request_ctx);
		return;
	}

	if (error)
	{
		delete file_ctx;

		std::string msg = fmt::format("Failed file response body ({})", request_ctx->target);
		handle_file_download_error(request_ctx, error, msg.c_str());
		return;
	}

	auto &body = request_ctx->response_parser.get().body();
	auto &response_parser = request_ctx->response_parser;

	for (auto iter = asio::buffer_sequence_begin(body.data()); iter != asio::buffer_sequence_end(body.data()); ++iter)
	{
		handle_file_response_buffer(file_ctx, *iter);
	}

	size_t consumed = asio::buffer_size(body.data());
	body.consume(consumed);
	request_ctx->download_accum += consumed;

	this->downloader_events->download_progress(request_ctx->worker_id, consumed, request_ctx->download_accum);

	if (response_parser.is_done())
	{
		int worker_id = request_ctx->worker_id;
		request_ctx->deadline.cancel();

		delete request_ctx;

		handle_file_result(file_ctx, worker_id);

		return;
	}

	auto read_handler = [this, request_ctx, file_ctx](auto i, auto e) {
		this->handle_file_response_body(i, e, request_ctx, file_ctx);
	};

	request_ctx->set_deadline();

	http::async_read_some(request_ctx->ssl_socket, request_ctx->response_buf, response_parser, read_handler);
}

void update_client::handle_file_response_header(boost::system::error_code &error, size_t bytes, file_request<http::dynamic_body> *request_ctx)
{
	request_ctx->deadline.cancel();

	if (update_canceled)
	{
		handle_file_download_canceled(request_ctx);
		return;
	}

	if (error)
	{
		std::string msg = fmt::format("Failed file response header ({})", request_ctx->target);

		handle_file_download_error(request_ctx, error, msg.c_str());
		return;
	}

	auto &response_parser = request_ctx->response_parser;

	int status_code = response_parser.get().result_int();
	if (status_code != 200)
	{
		auto target = request_ctx->request.target();

		std::string output_str = fmt::format("Server send status Code: {} for file: {}", status_code, fmt::string_view(target.data(), target.size()));

		handle_file_download_error(request_ctx, boost::asio::error::basic_errors::connection_aborted, output_str.c_str());
		return;
	}

	try {
		request_ctx->content_length = response_parser.content_length().value();
	}
	catch (...) {
		request_ctx->content_length = 0;
	}

	if (request_ctx->content_length == 0)
	{
		auto target = request_ctx->request.target();

		std::string output_str = fmt::format("Receive empty header ({})", fmt::string_view(target.data(), target.size()));

		handle_file_download_error(request_ctx, boost::asio::error::basic_errors::connection_aborted, output_str.c_str());
		return;
	}

	this->downloader_events->download_file(
		request_ctx->worker_id,
		request_ctx->target,
		request_ctx->content_length
	);

	fs::path file_path = generate_file_path(this->new_files_dir, fs::path(request_ctx->target));

	if (file_path.empty())
	{
		std::string msg = fmt::format("Failed to create file path ({})", request_ctx->target);
		handle_network_error({}, msg.c_str());
		return;
	}

	//check that we do not have file before writing in it 
	auto file_boost_path = fs::path(unfixup_uri(file_path.string()));
	boost::system::error_code ec;
	boost::filesystem::remove(file_boost_path, ec);

	auto file_ctx = new update_client::file(file_boost_path);

	auto read_handler = [this, request_ctx, file_ctx](auto i, auto e) {
		this->handle_file_response_body(i, e, request_ctx, file_ctx);
	};

	request_ctx->set_deadline();

	http::async_read_some(request_ctx->ssl_socket, request_ctx->response_buf, response_parser, read_handler);
}

void update_client::handle_file_request(boost::system::error_code &error, size_t bytes, file_request<http::dynamic_body> *request_ctx)
{
	request_ctx->deadline.cancel();

	if (update_canceled)
	{
		handle_file_download_canceled(request_ctx);
		return;
	}

	if (error)
	{
		std::string msg = fmt::format("Failed file request ({})", request_ctx->target);

		handle_file_download_error(request_ctx, error, msg.c_str());
		return;
	}

	auto read_handler = [this, request_ctx](auto i, auto e) {
		this->handle_file_response_header(i, e, request_ctx);
	};

	request_ctx->set_deadline();

	http::async_read_header(request_ctx->ssl_socket, request_ctx->response_buf, request_ctx->response_parser, read_handler);
}

void update_client::handle_file_handshake(const boost::system::error_code& error, file_request<http::dynamic_body> *request_ctx)
{
	request_ctx->deadline.cancel();

	if (update_canceled)
	{
		handle_file_download_canceled(request_ctx);
		return;
	}

	if (error)
	{
		std::string msg = fmt::format("Failed manifest handshake ({})", request_ctx->target);

		handle_file_download_error(request_ctx, error, msg.c_str());
		return;
	}

	auto request_handler = [this, request_ctx](auto e, auto b) {
		this->handle_file_request(e, b, request_ctx);
	};

	request_ctx->set_deadline();

	http::async_write(request_ctx->ssl_socket, request_ctx->request, request_handler);
}

void update_client::handle_file_connect(const boost::system::error_code &error, const tcp::endpoint &ep, file_request<http::dynamic_body> *request_ctx)
{
	request_ctx->deadline.cancel();


	if (update_canceled)
	{
		handle_file_download_canceled(request_ctx);
		return;
	}

	if (error)
	{
		std::string msg = fmt::format("Failed to connect to host for file ({})", request_ctx->target);

		handle_file_download_error(request_ctx, error, msg.c_str());
		return;
	}

	auto handshake_handler = [this, request_ctx](auto e) {
		this->handle_file_handshake(e, request_ctx);
	};

	request_ctx->set_deadline();

	request_ctx->ssl_socket.async_handshake(ssl::stream_base::handshake_type::client, handshake_handler);
}

void update_client::handle_manifest_entry(file_request<http::dynamic_body> *request_ctx)
{
	auto connect_handler = [this, request_ctx](auto e, auto b) {
		this->handle_file_connect(e, b, request_ctx);
	};

	request_ctx->set_deadline();

	asio::async_connect(request_ctx->ssl_socket.lowest_layer(), this->endpoints, connect_handler);
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


	void update_client_set_client_events(struct update_client *client, struct client_callbacks *events)
	{
		client->set_client_events(events);
	}


	void update_client_set_downloader_events(struct update_client *client, struct downloader_callbacks *events)
	{
		client->set_downloader_events(events);
	}


	void update_client_set_updater_events(struct update_client *client, struct updater_callbacks *events)
	{
		client->set_updater_events(events);
	}


	void update_client_set_pid_events(struct update_client *client, struct pid_callbacks *events)
	{
		client->set_pid_events(events);
	}

	void update_client_set_blocker_events(struct update_client *client, struct blocker_callbacks *events)
	{
		client->set_blocker_events(events);
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