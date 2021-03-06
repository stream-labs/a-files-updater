#include <atomic>
#include <algorithm>
#include <mutex>
#include <thread>
#include <regex>

#include <boost/asio.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast.hpp>
#include <boost/bind.hpp>
#include <boost/iostreams/chain.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/iostreams/traits.hpp>
#include <boost/exception/all.hpp>

#include <fmt/format.h>
#include <aclapi.h>

#include <fstream>
#include <iostream>

namespace asio = boost::asio;
using tcp = asio::ip::tcp;
namespace beast = boost::beast;
namespace http = boost::beast::http;
namespace ssl = boost::asio::ssl;
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
#include "utils.hpp"
#include "file-updater.h"

const std::string failed_to_revert_message = "The automatic update failed to perform successfully.\nPlease install the latest version of Streamlabs OBS from https://streamlabs.com/";
const std::string failed_to_update_message = "Failed to move files.\nPlease make sure the application files are not in use and try again.";
const std::string failed_connect_to_server_message = "Failed to connect to update server.";
const std::string update_was_canceled_message = "Update was canceled.";
const std::string blocked_file_message = "Failed to move files.\nSome files may be blocked by other program. Please restart your PC and try to update again.";
const std::string locked_file_message = "Failed to move files.\nSome files could not be updated. Please download SLOBS installer from our site and run full installation.";
const std::string failed_boost_file_operation_message = "Failed to move files.\nSome files could not be updated. Please download SLOBS installer from our site and run full installation.";
const std::string restart_or_install_message = "Streamlabs OBS encountered an issue while downloading the update. \nPlease restart the application to finish updating. \nIf the issue persists, please download a new installer from www.streamlabs.com.";

/*##############################################
 *#
 *# Class Implementation
 *#
 *############################################*/

void update_client::start_file_update()
{
	log_info("Files downloaded and ready to start update.");

	reset_work_threads_guards();

	FileUpdater updater(params->temp_dir, params->app_dir, new_files_dir, manifest);
	bool updated = false;

	try {
		if (updater.backup()) {
			updater.update();

			log_info("Finished updating files without errors.");
			client_events->success();
			updated = true;
		}
	}
	catch(std::exception& e) {
		log_error("Got error while updating files: %s.", e.what());
	}
	catch (...) {
		log_error("Got error while updating files.");
	}

	if (!updated)
	{
		bool reverted = false;
		log_info("Going to revert.");
		try {
			updater.revert();
			reverted = true;
			log_info("Revert completed.");
		}
		catch(std::exception& e) {
			log_error("Revert failed: %s.", e.what());
		}
		catch (...) {
			log_error("Revert failed.");
		}	
		
		if (reverted) 
		{
			client_events->error(failed_to_update_message.c_str(), "Failed to update");
		}
		else {
			client_events->error(failed_to_revert_message.c_str(), "Failed to revert on fail");
		}
	}
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

void update_client::handle_network_error(const boost::system::error_code & error, const std::string & str)
{
	std::lock_guard<std::mutex> lock(handle_error_mutex);
	
	update_download_aborted = true;

	char error_buf[256]{0};

	snprintf(error_buf, sizeof(error_buf), "%s\0", restart_or_install_message.c_str());
	client_events->error(error_buf, "Network error");

	snprintf(error_buf, sizeof(error_buf), "%s - %s\0", str.c_str(), error.message().c_str());
	log_error(error_buf);

	reset_work_threads_guards();
}

void update_client::handle_file_download_error(file_request<http::dynamic_body> *request_ctx, const boost::system::error_code & error, const std::string & str)
{
	set_endpoint_fail(request_ctx->used_cdn_node_address);

	if (request_ctx->retries > 5)
	{
		boost::system::error_code ec = error;
		if (error == boost::asio::error::basic_errors::operation_aborted && request_ctx->deadline_reached)
		{
			ec = boost::asio::error::basic_errors::timed_out;
		}
		
		{
			std::lock_guard<std::mutex> lock(handle_error_mutex);
			if(!update_download_aborted)
			{
				update_download_aborted = true;

				download_abort_message = str;
				download_abort_error = error;
			}
		}

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

void update_client::handle_manifest_download_error(manifest_request<manifest_body> *request_ctx, const boost::system::error_code & error, const std::string & str)
{
	set_endpoint_fail(request_ctx->used_cdn_node_address);

	if (request_ctx->retries > 5)
	{
		boost::system::error_code ec = error;
		if (error == boost::asio::error::basic_errors::operation_aborted && request_ctx->deadline_reached)
		{
			ec = boost::asio::error::basic_errors::timed_out;
		}
		
		{
			std::lock_guard<std::mutex> lock(handle_error_mutex);
			if (!update_download_aborted)
			{
				update_download_aborted = true;

				download_abort_message = str;
				download_abort_error = error;
			}
		}

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

	handle_network_error(download_abort_error, download_abort_message);
}

void update_client::handle_resolve(const boost::system::error_code &error, resolver_type::results_type results)
{
	domain_resolve_timeout.cancel();

	if (error) 
	{
		handle_network_error(error, failed_connect_to_server_message);
		return;
	}

	log_info("Successfuly resolved update server domain name. Continue to download update manifest.");
	endpoints = results;

	auto first_ip = endpoints.cbegin();
	while(first_ip!=endpoints.cend())
	{
		log_info("Resolved cdn node address - %s", get_endpoint_address_string(first_ip).c_str());
		endpoint_fails_counts.emplace(get_endpoint_address_string(first_ip), std::make_pair<int,int>( 0,0) );
		first_ip++;
	}
	
	std::string manifest_target{ params->version+".sha256" };

	auto *request_ctx = new manifest_request<manifest_body>(this, manifest_target, 0);
	
	request_ctx->start_connect();
}

update_client::update_client(struct update_parameters *params)
	: params(params),
	wait_for_blockers(io_ctx),
	show_user_blockers_list( true),
	active_workers(0),
	resolver(io_ctx),
	domain_resolve_timeout(io_ctx)
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
	reset_work_threads_guards();
}

void update_client::create_work_threads_guards()
{
	work_thread_guard = new work_guard_type(asio::make_work_guard(io_ctx));
}

void update_client::reset_work_threads_guards()
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

	domain_resolve_timeout.expires_from_now(boost::posix_time::seconds(10));
	check_resolve_timeout_callback_err({});
	
	log_info("Ready to resolve cdn address \"%s\" and \"%s\" ", params->host.authority.c_str(), params->host.scheme.c_str());

	resolver.async_resolve( params->host.authority, params->host.scheme, cb);
}

void update_client::set_endpoint_fail(const std::string& used_cdn_node_address)
{
	auto counters = endpoint_fails_counts.find(used_cdn_node_address );
	if(counters != endpoint_fails_counts.end())
	{
		(*counters).second.first++;
		log_error("CDN node fail: \"%s\". fails: %d , gets: %d", used_cdn_node_address.c_str(), (*counters).second.first, (*counters).second.second);
	} else {
		log_error("CDN node fail: \"%s\". ", used_cdn_node_address.c_str() );
	}
}

const std::string update_client::get_endpoint_address_string(resolver_type::results_type::iterator &iter)
{
	std::string ret = "";
	if(iter != endpoints.end())
	{
		boost::system::error_code ec;
		ret = (*iter).endpoint().address().to_string(ec);
	}
	return ret;
}

tcp::resolver::results_type::iterator update_client::get_endpoint()
{
	auto iter = endpoints.begin();
	auto ret = endpoints.end();
	int ret_fails = -1;
	int ret_gets = -1;

	while(iter != endpoints.end())
	{
		auto counters = endpoint_fails_counts.find( get_endpoint_address_string(iter) );
		if( counters != endpoint_fails_counts.end() && (*counters).second.first <= 24 ) //ignore nodes with count of fails more than limit 
		{
			if (ret_fails < 0 || ret_fails >(*counters).second.first || (ret_fails == (*counters).second.first && ret_gets > (*counters).second.second)  )
			{
				ret = iter;
				ret_fails = (*counters).second.first;
				ret_gets = (*counters).second.second;
			} 
		}
		iter++;
	}
	
	if(ret != endpoints.end())
	{
		auto counters = endpoint_fails_counts.find(get_endpoint_address_string(ret));
		if(counters != endpoint_fails_counts.end())
			(*counters).second.second++;
	}

	return ret;
}

void update_client::check_resolve_timeout_callback_err(const boost::system::error_code& error)
{
	if (error)
	{
		if (error == boost::asio::error::operation_aborted)
		{
			return;
		} else {
			return;
		}
	}

	if (domain_resolve_timeout.expires_at() <= boost::asio::deadline_timer::traits_type::now())
	{
		resolver.cancel();
		log_info("Timeout for cdn resolve triggered.");
		handle_network_error(error, failed_connect_to_server_message);
	} else {
		domain_resolve_timeout.async_wait(bind(&update_client::check_resolve_timeout_callback_err, this, std::placeholders::_1));
	}
}

/*##############################################
 *#
 *# Manifest Handlers
 *#
 *############################################*/

void update_client::checkup_files(struct blockers_map_t& blockers, std::vector<fs::path> files, int from, int to) {
	for (int i = from; i < to; i++) {
		fs::path entry = files.at(i);
		fs::path key_path(fs::relative(entry, params->app_dir));

		fs::path cleaned_file_name = key_path.make_preferred();
		std::string key = cleaned_file_name.u8string();

		auto manifest_iter = manifest.find(key);

		if (manifest_iter == manifest.end())
		{
			if (params->enable_removing_old_files)
			{
				auto entry_update_info = manifest_entry_t(std::string(""));
				entry_update_info.compared_to_local = true;
				entry_update_info.remove_at_update = true;

				if (key.find("Uninstall") == 0 || key.find("installername") == 0)
				{
					entry_update_info.remove_at_update = false;
					entry_update_info.skip_update = true;
				}
				else {
					static int removed_files = 0;
					removed_files++;
					if (removed_files < 30)
					{
						log_info("Not found local file in new versions manifest. Try to remove it %s", key.c_str());
					}
					else if (removed_files == 30) {
						log_info("More than 30 files not found in manifest. Logging postponed.");
					}
				}

				manifest.emplace(std::make_pair(key, entry_update_info));
			}
			continue;
		}

		if (check_file_updatable(entry, true, blockers))
		{
			if (!manifest_iter->second.compared_to_local)
			{
				std::string checksum = "";
				try {
					checksum = calculate_files_checksum(entry);
				}
				catch (const boost::exception &e)
				{
					log_warn("Failed to calculate checksum of local file. Try to update it. Exception: %s", boost::diagnostic_information(e).c_str());
				}
				catch (const std::exception &e)
				{
					log_warn("Failed to calculate checksum of local file. Try to update it. std::exception: %s", e.what());
				}

				manifest_iter->second.compared_to_local = true;

				if (checksum.compare(manifest_iter->second.hash_sum) == 0)
				{
					manifest_iter->second.skip_update = true;
					continue;
				}
			}

			check_file_updatable(entry, false, blockers);
		}
	}
}

void update_client::checkup_manifest(blockers_map_t &blockers)
{
	int max_threads = std::thread::hardware_concurrency();

	/* Generate the manifest for the current application directory */
	fs::recursive_directory_iterator app_dir_iter(params->app_dir);
	fs::recursive_directory_iterator end_iter{};

	std::vector<fs::path> files;

	for (; app_dir_iter != end_iter; ++app_dir_iter) {
		fs::path entry = app_dir_iter->path();
		std::error_code ec;

		auto entry_status = fs::status(entry, ec);
		if (ec)
			continue;

		if (fs::is_directory(entry_status))
			continue;

		files.push_back(entry);
	}

	std::vector<std::thread*> workers;

	log_info("Full size: %d", files.size());

	for (int i = 0; i < max_threads; i++) {
		int from = files.size() / max_threads * i;
		int to;

		if (i + 1 != max_threads)
			to = files.size() / max_threads * (i + 1);
		else
			to = files.size();

		log_info("Begining work from: %d to: %d", from, to);
		workers.push_back(new std::thread(&update_client::checkup_files, this, std::ref(blockers), files, from, to));
	}

	for (auto worker : workers) {
		if (worker->joinable())
			worker->join();
	}

	return;
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
		checkup_manifest(blockers);

		if (blockers.list.size() > 0)
		{
			std::wstring new_process_list_text;
			for (auto it = blockers.list.begin(); it != blockers.list.end(); it++)
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
				for (auto it = blockers.list.begin(); it != blockers.list.end(); it++)
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
				client_events->error(update_was_canceled_message.c_str(), "Canceled");
				reset_work_threads_guards();
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
	catch (update_exception_blocked& )
	{
		client_events->error(blocked_file_message.c_str(), "File access error");
		return;
	}
	catch (update_exception_failed& )
	{
		client_events->error(locked_file_message.c_str(), "File access error");
		return;
	}
	catch (std::exception & )
	{
		client_events->error(failed_boost_file_operation_message.c_str(), "File operation error");
		return;
	}
	catch (...)
	{
		client_events->error(failed_boost_file_operation_message.c_str(), "File operation error");
		return;
	}

	start_downloading_files();
}

void update_client::start_downloading_files()
{
	int max_threads = 4;

	this->manifest_iterator = this->manifest.cbegin();
	auto to_download = std::count_if(this->manifest.cbegin(), this->manifest.cend(), [](const auto& entry) {return !entry.second.remove_at_update && !entry.second.skip_update; });
	log_info("Manifest cleaned and ready to download files. Files to download %d", to_download);
	this->downloader_events->downloader_start(max_threads, to_download);

	/* To make sure we only have `max` number of
	 * of requests at any given time, we hold the
	 * mutex for the duration of this for loop.
	 * Otherwise a request could finish too fast
	 * and we would have `n` extra requests at once
	 * where n is the request that finished too fast. */
	std::unique_lock<std::mutex> manifest_lock(this->manifest_mutex);

	for (int i = 0; this->manifest_iterator != this->manifest.end() && i < max_threads; ++this->manifest_iterator )
	{
		if((*this->manifest_iterator).second.remove_at_update || (*this->manifest_iterator).second.skip_update)
			continue;
		
		++this->active_workers;

		auto request_ctx = new file_request<http::dynamic_body>{
			this,
			fixup_uri((*this->manifest_iterator).first)+".gz",
			i
		};

		request_ctx->start_connect();

		++i;
	}
	
	if(this->active_workers == 0)
	{
		manifest_lock.unlock();
		this->start_file_update();
	}
}

template <class ConstBuffer>
static size_t handle_manifest_read_buffer(manifest_map_t &map, const ConstBuffer &buffer)
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

	log_info("Successfuly downloaded manifest. It has info about %d files", manifest.size());
	
	this->downloader_events->downloader_preparing();

	wait_for_blockers.expires_from_now(boost::posix_time::seconds(3));
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

void update_client::handle_file_result(file_request<http::dynamic_body> *request_ctx, update_file_t *file_ctx, int index)
{
	auto &filter = file_ctx->checksum_filter;

	try {
		file_ctx->output_chain.reset();

		std::ostringstream hex_digest;

		hex_digest << std::nouppercase << std::setfill('0') << std::hex;

		for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i)
		{
			hex_digest << std::setw(2) << static_cast<unsigned int>(filter.digest[i]);
		}
	}
	catch (...)
	{

	}

	delete file_ctx;
	delete request_ctx;

	next_manifest_entry(index);
}

void update_client::next_manifest_entry(int index)
{
	std::unique_lock<std::mutex> manifest_lock(this->manifest_mutex);

	while(true)
	{
		if (this->manifest_iterator == this->manifest.end() || update_download_aborted)
		{
			--this->active_workers;

			this->downloader_events->download_worker_finished(index);

			if (this->active_workers == 0)
			{
				this->downloader_events->downloader_complete();
				if (update_download_aborted)
				{
					handle_network_error(download_abort_error, download_abort_message);
				}
				else {
					this->start_file_update();
				}
			}

			return;
		} else {
			auto entry = *this->manifest_iterator;

			++this->manifest_iterator;
	
			if(entry.second.remove_at_update || entry.second.skip_update)
			{
				continue;
			} else {
				/* Only hold the lock until we can get a reference to
				* the entry. We are guaranteed that the entry and
				* manifest are no longer modified at this point so
				* the reference will stay valid */
				manifest_lock.unlock();

				auto request_ctx = new file_request<http::dynamic_body>{
					this,
					fixup_uri(entry.first) + ".gz",
					index
				};

				request_ctx->start_connect();
			}
		}
		break;
	}
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
void update_http_request<manifest_body, false>::handle_download_error(const boost::system::error_code & error, const std::string & str)
{
	client_ctx->io_ctx.post(boost::bind(&update_client::handle_manifest_download_error, client_ctx, this, error, str));
}

template<>
void update_http_request<http::dynamic_body, true>::handle_download_error(const boost::system::error_code & error, const std::string & str)
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
		handle_download_error({}, msg);
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

		handle_download_error(boost::asio::error::basic_errors::connection_aborted, msg);

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