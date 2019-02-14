#pragma once

#include <string>
#include <vector>

#include <boost/filesystem.hpp>

#include "uri-parser.hpp"

/* We want this to be C compatible eventually */
struct update_parameters {
	struct uri_components host;

	boost::filesystem::path temp_dir;
	boost::filesystem::path app_dir;
	std::string exec;
	std::string exec_cwd;
	std::vector<int> pids;
	std::string version;
	FILE *log_file;
	bool interactive;

	~update_parameters()
	{
		if (log_file) fclose(log_file);
	}
};