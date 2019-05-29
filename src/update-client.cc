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
//#include <boost/filesystem.hpp>
#include <boost/iostreams/chain.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/traits.hpp>
#include <boost/exception/all.hpp>

#include <fmt/format.h>
#include <aclapi.h>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;
namespace fs = std::filesystem;
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
#include "update-http-request.hpp"

const std::string failed_to_revert_message = "Failed to move files.\nPlease make sure the application files are not in use and try again.";
const std::string failed_to_update_message = "Failed to move files.\nPlease make sure the application files are not in use and try again.";
const std::string failed_connect_to_server_message = "Failed to connect to update server.";
const std::string update_was_canceled_message = "Update was canceled.";
const std::string blocked_file_message = "Failed to move files.\nSome files may be blocked by other program. Please restart your PC and try to update again.";
const std::string locked_file_message = "Failed to move files.\nSome files could not be updated. Please download SLOBS installer from our site and run full installation.";
const std::string failed_boost_file_operation_message = "Failed to move files.\nSome files could not be updated. Please download SLOBS installer from our site and run full installation.";
const std::string restart_or_install_message = "Streamlabs OBS encountered an issue while downloading the update. \nPlease restart the application to finish updating. \nIf the issue persists, please download a new installer from www.streamlabs.com.";

/*##############################################
 *#
 *# Utility functions
 *#
 *############################################*/
namespace {
	std::string encimpl(std::string::value_type v)
	{
		if ( isascii(v) )
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
}

FileUpdater::~FileUpdater()
{
	std::error_code ec;

	fs::remove_all(m_old_files_dir, ec);
	if (ec)
	{
		wlog_warn(L"Failed to clean temp folder.");
	}
}

void FileUpdater::update()
{
	fs::create_directories(m_old_files_dir);

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

bool FileUpdater::update_entry_with_retries(update_client::manifest_map_t::iterator &iter, fs::path &new_files_dir)
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

bool FileUpdater::update_entry(update_client::manifest_map_t::iterator &iter, fs::path &new_files_dir)
{
	std::error_code ec;
	fs::path file_name_part = fs::u8path(iter->first.c_str());
	fs::path to_path(m_app_dir);
	to_path /= file_name_part;

	fs::path old_file_path(m_old_files_dir);
	old_file_path /= file_name_part;

	fs::path from_path(new_files_dir);
	from_path /= file_name_part;
	
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
		const std::wstring path_str = path.generic_wstring();
		DWORD result = SetNamedSecurityInfo((LPWSTR)path_str.c_str(), SE_FILE_OBJECT,
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
	std::error_code ec;
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

void update_client::start_file_update()
{
	log_info("Files downloaded and ready to start update.");

	FileUpdater updater(this);
	bool updated = false;

	try {
		updater.update();

		log_info("Finished updating files without errors.");
		client_events->success();
		updated = true;
	}
	catch (...) {
		log_info("Got error while updating files.");
	}

	if (!updated)
	{
		bool reverted = true;
		log_info("Going to revert.");
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

	reset_work_threads_gurards();
}

struct update_client::pid
{
	uint64_t id{ 0 };
	asio::windows::object_handle wrapper;

	pid(asio::io_context &ctx, HANDLE hProcess);
	~pid();
};

update_client::pid::pid(asio::io_context &io_ctx, HANDLE hProcess) : wrapper(io_ctx, hProcess)
{}

update_client::pid::~pid()
{
	wrapper.cancel();
}

void update_client::handle_pid(const boost::system::error_code& error, int pid_id)
{
	pid_events->pid_wait_finished(pid_id);

	if (--active_pids == 0)
	{
		pid_events->pid_wait_complete();
		process_manifest_results();
	}
}

void update_client::handle_pids()
{
	active_pids = params->pids.size();

	if (active_pids == 0)
	{
		process_manifest_results();
	}
	
	for (auto iter = params->pids.begin(); iter != params->pids.end(); ++iter)
	{
		HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)*iter);
		if(hProcess)
		{
			pid_events->pid_waiting_for(*iter);

			update_client::pid *pid_ctx = new update_client::pid(io_ctx, hProcess);
			// todo have to save them to be able to cancel 
			pid_ctx->id = *iter;
			pid_ctx->wrapper.async_wait([this, pid_ctx](auto ec) { this->handle_pid(ec, pid_ctx->id); });

			pids_waiters.push_back(pid_ctx);
		}
	}
}

void update_client::handle_network_error(const boost::system::error_code & error, const char * str)
{
	char error_buf[256];

	snprintf(error_buf, sizeof(error_buf), "%s\0", restart_or_install_message.c_str());

	client_events->error(error_buf);

	snprintf(error_buf, sizeof(error_buf), "%s - %s\0", str, error.message().c_str());

	log_error(error_buf);

	reset_work_threads_gurards();
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

		new_request_ctx->start_connect();
	}
}

void update_client::handle_file_download_canceled(file_request<http::dynamic_body>* request_ctx)
{
	auto index = request_ctx->worker_id;
	delete request_ctx;

	next_manifest_entry(index);
}

void update_client::handle_manifest_download_error(manifest_request<manifest_body> *request_ctx, const boost::system::error_code & error, const char * str)
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

		handle_manifest_download_canceled(request_ctx);
		return;
	}
	else {
		auto new_request_ctx = new manifest_request<manifest_body>{ this, request_ctx->target, request_ctx->worker_id };
		new_request_ctx->retries = request_ctx->retries + 1;

		delete request_ctx;

		Sleep(new_request_ctx->retries * 100);

		new_request_ctx->start_connect();
	}
}

void update_client::handle_manifest_download_canceled(manifest_request<manifest_body>* request_ctx)
{
	auto index = request_ctx->worker_id;
	delete request_ctx;

	handle_network_error(cancel_error, cancel_message.c_str());
}

void update_client::handle_resolve(const boost::system::error_code &error, tcp::resolver::results_type results) 
{
	if (error) 
	{
		handle_network_error(error, failed_connect_to_server_message.c_str());
		return;
	}

	endpoints = results;

	/* TODO I should make hash type configurable. */
	std::string manifest_target{ params->version+".sha256" };

	auto *request_ctx = new manifest_request<manifest_body>(this, manifest_target, 0);
	
	request_ctx->start_connect();
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

	create_work_threads_guards();

	for (unsigned i = 0; i < num_workers; ++i)
	{
		thread_pool.emplace_back( std::thread([=]() 
		{ 
			io_ctx.run(); 
		}) );
	}
}

update_client::~update_client()
{
	reset_work_threads_gurards();
}

void update_client::create_work_threads_guards()
{
	work_thread_guard = new work_guard_type(asio::make_work_guard(io_ctx));
}

void update_client::reset_work_threads_gurards()
{
	if (work_thread_guard == nullptr)
		return;

	work_thread_guard->reset();
	delete work_thread_guard;
	work_thread_guard = nullptr;
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
}

/*##############################################
 *#
 *# Manifest Handlers
 *#
 *############################################*/

static std::string calculate_checksum(fs::path &path)
{
	std::string hex_digest = "";
	unsigned char hash[SHA256_DIGEST_LENGTH] = {0};

	std::ifstream file(path, std::ios::in | std::ios::binary );
	if (file.is_open())
	{
		SHA256_CTX sha256;
		SHA256_Init(&sha256);

		unsigned char buffer[4096];
		while (true)
		{
			file.read((char *)buffer, 4096);
			std::streamsize read_byte = file.gcount();
			if(read_byte!=0)
			{
				SHA256_Update(&sha256, buffer, read_byte);
			}
			if ( !file.good())
			{
				break;
			}
		}

		SHA256_Final(hash, &sha256);

		file.close();

		hex_digest.reserve(SHA256_DIGEST_LENGTH * 2);

		/* TODO 32 is hardcoded here for the size of an SHA-256 digest buffer */
		for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		{
			fmt::format_to(std::back_inserter(hex_digest), "{:02x}", hash[i]);
		}
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

		fs::path cleaned_file_name = key_path.make_preferred();
		std::string key = cleaned_file_name.u8string();

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
				std::string checksum = "";
				try {
					checksum = calculate_checksum(entry);
				}
				catch (const boost::exception &e)
				{
					log_warn("Failed to calculate checksum of local file. Try to update it. Exception: %s", boost::diagnostic_information(e).c_str());
				}
				catch (const std::exception &e)
				{
					log_warn("Failed to calculate checksum of local file. Try to update it. std::exception: %s", e.what());
				}

				/* If the checksum is the same as in the manifest,
				 * remove it from the manifest entirely, there's no need
				 * to download it (as it's already the same). */
				if (checksum.compare(manifest_iter->second.hash_sum) == 0)
				{
					this->manifest.erase(manifest_iter);
					continue;
				} else {
					manifest_iter->second.compared_to_local = true;
				}
			}

			check_file_updatable(entry, false, blockers);
		}
	}

	return true;
}

std::mutex manifest_result_mutex;

void update_client::process_manifest_results()
{
	std::unique_lock<std::mutex> lock(manifest_result_mutex, std::try_to_lock);
	if (!lock.owns_lock())
	{
		return;
	}

	wait_for_blockers.cancel();
	wait_for_blockers.expires_from_now(boost::posix_time::pos_infin);

	for ( auto pid_context : pids_waiters)
	{
		delete pid_context;
	}
	pids_waiters.clear();

	try
	{
		blockers_map_t blockers;
		this->clean_manifest(blockers);

		if (blockers.size() > 0)
		{
			std::wstring new_process_list_text;
			for (auto it = blockers.begin(); it != blockers.end(); it++)
			{
				//log_debug("Got blocker process info %i %ls", (*it).second.Process.dwProcessId, (*it).second.strAppName);

				new_process_list_text += (*it).second.strAppName;
				new_process_list_text += L" (";
				new_process_list_text += std::to_wstring((*it).second.Process.dwProcessId);
				new_process_list_text += L")";
				new_process_list_text += L"\r\n";
			}

			if (show_user_blockers_list)
			{
				show_user_blockers_list = false;
				this->blocker_events->blocker_start();
			}

			bool list_changed = process_list_text.compare(new_process_list_text) != 0;

			process_list_text = new_process_list_text;
			int command = this->blocker_events->blocker_waiting_for(process_list_text, list_changed);

			switch (command)
			{
			case 1:
				log_info("Got kill all command from ui");
				for (auto it = blockers.begin(); it != blockers.end(); it++)
				{
					if ((*it).second.Process.dwProcessId != 0)
					{
						HANDLE explorer = NULL;
						explorer = OpenProcess(PROCESS_TERMINATE, false, (*it).second.Process.dwProcessId);
						if (explorer == NULL)
						{
							log_error("Cannot open process %i to terminate it with error: %d", (*it).second.Process.dwProcessId, GetLastError());
						}
						else {
							if (TerminateProcess(explorer, 1))
							{
							}
							else {
								log_error("Failed to terminate process %i with error: %d", (*it).second.Process.dwProcessId, GetLastError());
							}
						}
					}
				}
				break;
			case 2:
			{
				log_info("Got cancel command from ui");
				client_events->error(update_was_canceled_message.c_str());
				return;
			}
			break;
			};

			wait_for_blockers.expires_from_now(boost::posix_time::seconds(1));

			wait_for_blockers.async_wait(boost::bind(&update_client::process_manifest_results, this));
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
	catch (std::exception & error)
	{
		client_events->error(failed_boost_file_operation_message.c_str());
		return;
	}
	catch (...)
	{
		client_events->error(failed_boost_file_operation_message.c_str());
		return;
	}

	start_downloading_files();
}

void update_client::start_downloading_files()
{
	/* TODO We should be able to make max configurable.*/
	int max_threads = 4;

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
			fixup_uri((*this->manifest_iterator).first)+".gz",
			i
		};

		request_ctx->start_connect();
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

void update_client::handle_manifest_result(manifest_request<manifest_body> *request_ctx)
{
	delete request_ctx;

	wait_for_blockers.expires_from_now(boost::posix_time::seconds(2));
	wait_for_blockers.async_wait(boost::bind(&update_client::process_manifest_results, this));

	/* let time for SLOBS process to quit and make files available for update */
	handle_pids();
};

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


update_file_t::update_file_t(const fs::path &file_path)
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

fs::path prepare_file_path(const fs::path &base, const fs::path &target)
{
	fs::path file_path(base);
	file_path /= target;
	
	file_path = fs::u8path( unfixup_uri(file_path.string()).c_str() );

	file_path.make_preferred();
	file_path.replace_extension();

	try 
	{
		fs::create_directories(file_path.parent_path());
		
		fs::remove(file_path);
	} catch (...)
	{
		file_path = "";
	}

	return file_path;
}

void update_client::handle_file_result(file_request<http::dynamic_body> *request_ctx, update_file_t *file_ctx, int index)
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

	 //todo calculate_checksum();

	delete file_ctx;
	delete request_ctx;

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
		fixup_uri(entry.first)+".gz",
		index
	};

	request_ctx->start_connect();
}

/*##############################################
 *#
 *# Template instances 
 *#
 *############################################*/

template<>
void update_http_request<manifest_body, false>::handle_download_canceled()
{
	client_ctx->io_ctx.post(boost::bind(&update_client::handle_manifest_download_canceled, client_ctx, this));
}

template<>
void update_http_request<http::dynamic_body, true>::handle_download_canceled()
{
	client_ctx->io_ctx.post(boost::bind(&update_client::handle_file_download_canceled, client_ctx, this));
}

template<>
void update_http_request<manifest_body, false>::handle_download_error(const boost::system::error_code & error, const char * str)
{
	client_ctx->io_ctx.post(boost::bind(&update_client::handle_manifest_download_error, client_ctx, this, error, str));
}

template<>
void update_http_request<http::dynamic_body, true>::handle_download_error(const boost::system::error_code & error, const char * str)
{
	client_ctx->io_ctx.post(boost::bind(&update_client::handle_file_download_error, client_ctx, this, error, str));
}

template<>
void update_http_request<manifest_body, false>::handle_result(update_file_t *file_ctx)
{
	client_ctx->io_ctx.post(boost::bind(&update_client::handle_manifest_result, client_ctx, this));
}

template<>
void update_http_request<http::dynamic_body, true>::handle_result(update_file_t *file_ctx)
{
	client_ctx->io_ctx.post(boost::bind(&update_client::handle_file_result, client_ctx, this, file_ctx, this->worker_id));
}

template<>
void update_http_request<http::dynamic_body, true>::start_reading()
{
	client_ctx->downloader_events->download_file(worker_id, target, content_length);

	fs::path file_path = prepare_file_path(client_ctx->new_files_dir, target);

	if (file_path.empty())
	{
		std::string msg = std::string("Failed to create file path for: ") + target;
		handle_download_error({}, msg.c_str());
		return;
	}

	auto file_ctx = new update_file_t(file_path);

	auto read_handler = [this, file_ctx](auto i, auto e) {
		this->handle_response_body(i, e, file_ctx);
	};

	switch_deadline_on();

	http::async_read_some(ssl_socket, response_buf, response_parser, read_handler);
}

template<>
void update_http_request<manifest_body, false>::start_reading()
{
	auto read_handler = [this](auto i, auto e) {
		this->handle_response_body(i, e, nullptr);
	};

	switch_deadline_on();

	http::async_read(ssl_socket, response_buf, response_parser, read_handler);
}

template<>
void update_http_request<http::dynamic_body, true>::handle_response_body(boost::system::error_code &error, size_t bytes_read, update_file_t *file_ctx)
{
	if (handle_callback_precheck(error, "get response body"))
	{
		delete file_ctx;
		return;
	}
	
	size_t consumed = 0;
	try {
		auto &body = response_parser.get().body();

		for (auto iter = asio::buffer_sequence_begin(body.data()); iter != asio::buffer_sequence_end(body.data()); ++iter)
		{
			file_ctx->output_chain.write((const char*)(*iter).data(), (*iter).size());
		}

		consumed = asio::buffer_size(body.data());
		body.consume(consumed);
		download_accum += consumed;
	} catch(...) {
		delete file_ctx;

		std::string msg = std::string("Failed to recieve file body correctly. for : ") + target;

		handle_download_error(boost::asio::error::basic_errors::connection_aborted, msg.c_str());

		return;
	}

	client_ctx->downloader_events->download_progress(worker_id, consumed, download_accum);

	if (response_parser.is_done())
	{
		handle_result(file_ctx);
		return;
	}

	auto read_handler = [this, file_ctx](auto i, auto e) {
		this->handle_response_body(i, e, file_ctx);
	};

	switch_deadline_on();

	http::async_read_some(ssl_socket, response_buf, response_parser, read_handler);
}

template<>
void update_http_request<manifest_body, false>::handle_response_body(boost::system::error_code &error, size_t bytes_read, update_file_t *file_ctx)
{
	if (handle_callback_precheck(error, "get response body"))
	{
		return;
	}
	
	auto &buffer = response_parser.get().body();

	handle_manifest_read_buffer(client_ctx->manifest, buffer.data());

	handle_result(nullptr);
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