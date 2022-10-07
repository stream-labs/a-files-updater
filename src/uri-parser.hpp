#pragma once
#include <string>

struct uri_components {
	std::string scheme;
	std::string authority;
	std::string path;
	std::string query;
	std::string fragment;
};

bool su_parse_uri(const char *uri, size_t length, struct uri_components *components);