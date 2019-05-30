#pragma once
#include "update-http-request.hpp"
#include <unordered_map>
#include <fstream>
/*##############################################
 *#
 *# Data definitions
 *#
 *############################################*/
struct manifest_entry_t
{
	std::string hash_sum;
	bool compared_to_local;
	
	manifest_entry_t(std::string &file_hash_sum) 
	{
		hash_sum = file_hash_sum;
		compared_to_local = false;
	}
};

struct update_file_t {
	fs::path file_path;
	std::ofstream file_stream;
	bio::gzip_decompressor decompress_filter;
	sha256_filter checksum_filter;

	bio::chain<bio::output> output_chain;

	explicit update_file_t(const fs::path &path);
};

fs::path prepare_file_path(const fs::path &base, const fs::path &target);

struct update_client {
	struct pid;	

	template <class Body>
	using manifest_request = update_http_request<Body, false>;

	template <class Body>
	using file_request = update_http_request<Body, true>;

	template <class Type>
	using executor_work_guard = asio::executor_work_guard<Type>;
	using io_context = asio::io_context;
	using work_guard_type = executor_work_guard<io_context::executor_type>;
	using resolver_type = tcp::resolver;
	using manifest_map_t = std::unordered_map<std::string, manifest_entry_t>;

	update_client() = delete;
	update_client(const update_client&) = delete;
	update_client(update_client&&) = delete;
	update_client &operator=(update_client&&) = delete;
	update_client &operator=(const update_client&) = delete;

	explicit update_client(struct update_parameters *params);
	~update_client();

	void set_client_events(client_callbacks *cbs) { client_events = cbs; }

	void set_downloader_events(downloader_callbacks *cbs) { downloader_events = cbs; }

	void set_updater_events(updater_callbacks *cbs) { updater_events = cbs; }

	void set_pid_events(pid_callbacks *cbs) { pid_events = cbs; }
	
	void set_blocker_events(blocker_callbacks *cbs) { blocker_events = cbs; }

	void do_stuff();
	void flush();

	/* These two are explicitly initialized in constructor */
	io_context io_ctx;
	update_parameters       *params;

	work_guard_type         *work_thread_guard{ nullptr };
	fs::path                 new_files_dir;

	client_callbacks        *client_events{ nullptr };
	downloader_callbacks    *downloader_events{ nullptr };
	updater_callbacks       *updater_events{ nullptr };
	pid_callbacks           *pid_events{ nullptr };
	blocker_callbacks       *blocker_events{ nullptr };

	int                      active_workers{ 0 };
	std::atomic_size_t       active_pids{ 0 };
	std::list<update_client::pid *> pids_waiters;

	manifest_map_t           manifest;
	std::mutex               manifest_mutex;
	manifest_map_t::const_iterator manifest_iterator;

	resolver_type            resolver;
	resolver_type::results_type endpoints;
	ssl::context ssl_context{ ssl::context::method::sslv23_client };

	std::vector<std::thread> thread_pool;

	boost::asio::deadline_timer wait_for_blockers;
	bool show_user_blockers_list;
	std::wstring process_list_text;

	bool					 update_canceled = false;
	std::string				 cancel_message;
	boost::system::error_code cancel_error;
	
	boost::asio::deadline_timer deadline;
	void check_deadline_callback_err(const boost::system::error_code& error);
	std::mutex               handle_error_mutex;

public:
	void handle_network_error(const boost::system::error_code &error, const std::string& str);
	void handle_file_download_error(file_request<http::dynamic_body> *request_ctx, const boost::system::error_code &error, const std::string & str);
	void handle_file_download_canceled(file_request<http::dynamic_body> *request_ctx);

	void handle_manifest_download_error(manifest_request<manifest_body> *request_ctx, const boost::system::error_code & error, const std::string& str);
	void handle_manifest_download_canceled(manifest_request<manifest_body>* request_ctx);

	//manifest 
	void handle_resolve(const boost::system::error_code &error, tcp::resolver::results_type results);
	void handle_manifest_result(manifest_request<manifest_body> *request_ctx);
	void process_manifest_results();
	bool clean_manifest(blockers_map_t &blockers);

	//files
	void start_downloading_files();
	void handle_file_result(file_request<http::dynamic_body> *request_ctx, update_file_t *file_ctx, int index);
	void next_manifest_entry(int index);

	//wait for slobs close
	void handle_pids();
	void handle_pid(const boost::system::error_code& error, int pid_id);

	//update 
	void start_file_update();

	void create_work_threads_guards();
	void reset_work_threads_gurards();
};

struct FileUpdater
{
	FileUpdater() = delete;
	FileUpdater(FileUpdater&&) = delete;
	FileUpdater(const FileUpdater&) = delete;
	FileUpdater &operator=(const FileUpdater&) = delete;
	FileUpdater &operator=(FileUpdater&&) = delete;

	explicit FileUpdater(update_client *client_ctx);
	~FileUpdater();

	void update();
	bool update_entry(update_client::manifest_map_t::iterator  &iter, fs::path & new_files_dir);
	bool update_entry_with_retries(update_client::manifest_map_t::iterator  &iter, fs::path & new_files_dir);
	void revert();
	bool reset_rights(const fs::path& path);

private:
	update_client *m_client_ctx;
	fs::path m_old_files_dir;
	fs::path m_app_dir;
};
