#include <boost/filesystem.hpp>
#include <boost/filesystem/detail/utf8_codecvt_facet.hpp>

#include "argtable3.h"
#include "fmt/format.h"
#include "cli-parser.hpp"
#include "logger/log.h"

static boost::filesystem::detail::utf8_codecvt_facet utf8_facet;

/* Filesystem is implicitly included from cli-parser.h */
namespace fs = boost::filesystem;

static bool validate_https_uri(struct uri_components *components)
{
	if (components->scheme == "http") {
		log_debug("http isn't supported at this time");
		return false;
	}

	return !components->scheme.empty() && !components->authority.empty();
}

enum arg_type {
	ARG_LITERAL,
	ARG_STRING,
	ARG_INTEGER,
	ARG_END
};

template <typename Arg, typename Val>
static void print_generic_arg(const Arg *arg, Val *values, size_t length)
{
	const char *null_string = "(null)";
	const char *short_names = arg->hdr.shortopts;
	const char *long_names = arg->hdr.longopts;

	if (!short_names) short_names = null_string;
	if (!long_names) long_names = null_string;

	for (int i = 0; i < length; ++i) {
		log_debug(
			"%s,%s count: %s",
			short_names, long_names,
			fmt::format("{}", values[i]).c_str()
		);
	}
}

static void print_literal_arg(const struct arg_lit *arg)
{
	print_generic_arg(arg, &arg->count, 1);
}

static void print_string_arg(const struct arg_str *arg)
{
	print_generic_arg(arg, arg->sval, arg->count);
}

static void print_integer_arg(const struct arg_int *arg)
{
	print_generic_arg(arg, arg->ival, arg->count);
}

static void print_end_arg(const struct arg_end *arg)
{
	log_debug("end of arguments");
}

static void print_arg(const void *arg, enum arg_type type)
{
	switch (type) {
	case ARG_LITERAL:
		print_literal_arg((struct arg_lit*)arg);
		break;
	case ARG_STRING:
		print_string_arg((struct arg_str*)arg);
		break;
	case ARG_INTEGER:
		print_integer_arg((struct arg_int*)arg);
		break;
	case ARG_END:
		print_end_arg((struct arg_end*)arg);
		break;
	}
}

static void print_arg_table(
  void **arg_table,
  enum arg_type *arg_types,
  const int table_size
) {
	for (int i = 0; i < table_size; ++i) {
		const arg_type type = arg_types[i];

		print_arg(arg_table[i], type);
	}
}

static std::vector<int> make_vector_from_arg(struct arg_int *arg)
{
	std::vector<int> result;

	for (int i = 0; i < arg->count; ++i) {
		result.push_back(arg->ival[i]);
	}

	return result;
}

static fs::path fetch_path(const char *str, size_t length)
{
	/* Use the utf8_facet here for anything provided from argtable */
	fs::path path(str, str + length, utf8_facet);

	log_debug("Given to fetch path: %.*s", length, str);

	fs::path result(fs::absolute(path).make_preferred());

	/* We use the utf8_facet here one more time to print-out UTF-8.
	 * Otherwise, it will print-out the system native which on Windows
	 * is wchar_t (encoded in UTF-16LE) */
	log_debug("Result of fetch path: %s", result.string(utf8_facet).c_str());

	return result;
}

static fs::path fetch_default_temp_dir()
{
	boost::system::error_code ec{};
	fs::path temp_dir = fs::temp_directory_path();
	temp_dir /= "slobs-updater";
	
	time_t t = time(NULL);
	struct tm *lt = localtime(&t);
	
	srand(time(NULL));

	char buf[24];
	sprintf(buf, "%04i%c%03i%c%02i%c%02i%c%02i%c\0", lt->tm_year+1900, 'a'+rand()%20,lt->tm_yday, 'a' + rand() % 20,lt->tm_hour, 'a' + rand() % 20, lt->tm_min, 'a' + rand() % 20, lt->tm_sec, 'a' + rand() % 20);

	temp_dir /= buf;

	fs::create_directories(temp_dir);

	return temp_dir;
}

bool su_parse_command_line( int argc, char **argv, struct update_parameters *params) 
{
	bool success = true;
	fs::path log_path;

	const char *invalid_fn_str = "Invalid path given for %s";

	struct arg_lit *help_arg = arg_lit0("h", "help",
		"Print information about this program");

	struct arg_lit *dump_args_arg = arg_lit0(NULL, "dump-args",
		"Print all argument values, including this one");

	struct arg_lit *force_arg = arg_lit0(NULL, "force-temp",
		"Force use temporary directory even if it exists");

	struct arg_str *base_uri_arg = arg_str1("b", "base-url", "<url>",
		"The base URL to fetch updates from");

	struct arg_str *app_dir_arg = arg_str1("a", "app-dir", "<directory>",
		"The directory of which the application is located");

	struct arg_str *exec_arg = arg_str1("e", "exec", "<command line>",
		"The command-line used to start the application");

	struct arg_str *cwd_arg = arg_str0("c", "cwd", "<working directory>",
		"The working directory of which to start the application in");

	struct arg_str *temp_dir_arg = arg_str0("t", "temp-dir", "<directory>",
		"The directory to place temporary files to be deleted later");

	struct arg_str *version_arg = arg_str1("v", "version", "<version>",
		"The version of which to update to");

	struct arg_int *pids_arg = arg_intn("p", "pids", "<pid>", 0, 100,
		"The process ID's to wait on before starting the update");

	struct arg_int *interactive = arg_intn("i", "interactive", "<interactive>", 0, 1,
		"Show user modal message boxes");

	struct arg_end *end_arg = arg_end(255);

	void *arg_table[] = {
		help_arg,
		dump_args_arg,
		force_arg,
		base_uri_arg,
		app_dir_arg,
		exec_arg,
		cwd_arg,
		temp_dir_arg,
		version_arg,
		pids_arg,
		interactive,
		end_arg
	};

	const int arg_table_sz = sizeof(arg_table) / sizeof(arg_table[0]);

	int num_errors = arg_parse(argc, argv, arg_table);

	/* We need type information to dump parameters generically */
	enum arg_type arg_table_types[arg_table_sz] = {
		ARG_LITERAL,
		ARG_LITERAL,
		ARG_LITERAL,
		ARG_STRING,
		ARG_STRING,
		ARG_STRING,
		ARG_STRING,
		ARG_STRING,
		ARG_STRING,
		ARG_INTEGER,
		ARG_INTEGER,
		ARG_END
	};
	params->interactive = true;

	/* Here we assume that stdout is setup correctly, otherwise --help is pointless */
	if (help_arg->count > 0) {
		fprintf(stdout, "Usage:");
		arg_print_syntaxv(stdout, arg_table, "\n\n");
		fprintf(stdout, "Options: \n");
		arg_print_glossary(stdout, arg_table, NULL);
		success = false;
		goto success;
	}
		
	if (temp_dir_arg->count > 0) {
		params->temp_dir =
			fetch_path(
				temp_dir_arg->sval[0],
				strlen(temp_dir_arg->sval[0])
			);
	} else {
		params->temp_dir = fetch_default_temp_dir();

		log_info("Temporary directory not provided.");

		log_info("Generated temporary directory: %s", params->temp_dir.string().c_str() );

		if (params->temp_dir.empty())
			success = false;
	}

	log_path = fs::path(params->temp_dir);
	log_path /= "slobs-updater.log";

	params->log_file_path = log_path.string();
	params->log_file = fopen(log_path.string().c_str(), "w+");

	/* If we fail, we just won't get a log file unfortunately */
	if (params->log_file)
		log_set_fp(params->log_file);

	if (dump_args_arg->count > 0)
		print_arg_table(arg_table, arg_table_types, arg_table_sz);

	if (num_errors > 0) {
		arg_print_errors(params->log_file, end_arg, argv[0]);
		goto parse_error;
	}

	/* We have all of the required parameters
	 * and should be able to assume they exist
	 * along with how many instances there are. */
	success = su_parse_uri(base_uri_arg->sval[0], strlen(base_uri_arg->sval[0]), &params->host );

	if (success)
		success = validate_https_uri(&params->host);

	if (!success) {
		log_fatal("Invalid uri given for base_uri");
	}

	params->app_dir = fetch_path( app_dir_arg->sval[0], strlen(app_dir_arg->sval[0]) );

	params->exec.assign(exec_arg->sval[0]);

	if (cwd_arg->count > 0) {
		params->exec_cwd.assign(cwd_arg->sval[0]);
	}

	if (params->app_dir.empty()) {
		log_fatal(invalid_fn_str, "app_dir");
		success = false;
	}

	if (params->temp_dir.empty()) {
		log_fatal(invalid_fn_str, "temp_dir");
		success = false;
	}

	if (!params->app_dir.empty() && !fs::exists(params->app_dir)) {
		log_fatal("Application directory doesn't exist");
		success = false;
	}

	if (!params->temp_dir.empty() && fs::exists(params->temp_dir)) {
		if (force_arg->count == 0) {
			log_fatal("Temporary directory already exists.");
			success = false;
		} else {
			log_warn("Forcing temporary directory!");
		}
	}

	params->pids = make_vector_from_arg(pids_arg);
	params->version.assign(version_arg->sval[0]);

	if(interactive->count > 0)
	{
		params->interactive = interactive->ival[0];
	}

	if (!success) goto parse_error;

	fs::create_directory(params->temp_dir);

	success = true;

	goto success;

parse_error:
	success = false;

success:
	arg_freetable(arg_table, arg_table_sz);

	return success;
}