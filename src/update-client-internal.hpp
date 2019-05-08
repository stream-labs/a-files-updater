#pragma once


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

	work_guard_type         *work{ nullptr };
	fs::path                 new_files_dir;

	client_callbacks        *client_events{ nullptr };
	downloader_callbacks    *downloader_events{ nullptr };
	updater_callbacks       *updater_events{ nullptr };
	pid_callbacks           *pid_events{ nullptr };
	blocker_callbacks       *blocker_events{ nullptr };

	int                      active_workers{ 0 };
	std::atomic_size_t       active_pids{ 0 };
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

private:
	inline void handle_network_error(const boost::system::error_code &error, const char* str);
	void handle_file_download_error(file_request<http::dynamic_body> *request_ctx, const boost::system::error_code &error, const char* str);
	void handle_file_download_canceled(file_request<http::dynamic_body> *request_ctx);

	void start_downloader();
	//manifest 
	void handle_resolve(const boost::system::error_code &error, tcp::resolver::results_type results);

	void handle_manifest_connect(const boost::system::error_code &error, const tcp::endpoint &ep, manifest_request<manifest_body> *request_ctx);

	void handle_manifest_handshake(const boost::system::error_code &error, manifest_request<manifest_body> *request_ctx);

	void handle_manifest_request(boost::system::error_code &error, size_t bytes, manifest_request<manifest_body> *request_ctx);

	void handle_manifest_response(boost::system::error_code &ec, size_t bytes, manifest_request<manifest_body> *request_ctx);

	void handle_manifest_results();

	bool clean_manifest(blockers_map_t &blockers);

	//files
	void start_downloading_files();

	void handle_manifest_entry(file_request<http::dynamic_body> *request_ctx);

	void handle_file_connect(const boost::system::error_code &error, const tcp::endpoint &ep, file_request<http::dynamic_body> *request_ctx);

	void handle_file_handshake(const boost::system::error_code& error, file_request<http::dynamic_body> *request_ctx);

	void handle_file_request(boost::system::error_code &error, size_t bytes, file_request<http::dynamic_body> *request_ctx);

	void handle_file_response_header(boost::system::error_code &error, size_t bytes, file_request<http::dynamic_body> *request_ctx);

	void handle_file_response_body(boost::system::error_code &error, size_t bytes_read, file_request<http::dynamic_body> *request_ctx, update_client::file *file_ctx);

	void handle_file_result(update_client::file *file_ctx, int index);

	void next_manifest_entry(int index);

	//update 
	void handle_pids();

	void handle_pid(const boost::system::error_code& error, update_client::pid* contexts);

	void start_file_update();

	void create_work();
	void reset_work();
};

template <class Body, bool IncludeVersion>
struct update_client::http_request
{
	http_request(update_client *client_ctx, const std::string &target, const int id);
	~http_request();

	size_t download_accum{ 0 };
	size_t content_length{ 0 };
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

	/* We need way to detect stuck connection.
	*  For that we use boost deadline timer what can limit
	*  time for each step of file downloader connection.
	*  Also it limits a recieve buffer so a timer limit a too slow fill of the buffer.
	*/
	boost::asio::deadline_timer deadline;
	int deadline_default_timeout = 5;
	bool deadline_reached = false;
	int retries = 0;

	void check_deadline_callback_err(const boost::system::error_code& error);
	void set_deadline();
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
	bool update_entry(update_client::manifest_map_t::iterator  &iter, boost::filesystem::path & new_files_dir);
	bool update_entry_with_retries(update_client::manifest_map_t::iterator  &iter, boost::filesystem::path & new_files_dir);
	void revert();
	bool reset_rights(const fs::path& path);

private:
	update_client *m_client_ctx;
	fs::path m_old_files_dir;
	fs::path m_app_dir;
};
