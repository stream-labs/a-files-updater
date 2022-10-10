#include <regex>

#include "uri-parser.hpp"

using std::regex;
using std::cmatch;
using std::regex_match;

/*
 * @function parse_uri
 * @param uri uri string to parse
 * @param length Number of characters within the uri string
 * @param components The output of the parsed result.
 *
 * If parse_uri returns false, the state of components is
 * undefined. Otherwise, it contains each of the 5 standard
 * sections of a URI for further validation.
 */

bool su_parse_uri(const char *uri, size_t length, struct uri_components *components)
{
	static const regex uri_regex("^(([^:/?#]+):)?(//([^/?#]*))?([^?#]*)(\\?([^#]*))?(#(.*))?");

	cmatch matches;

	if (!regex_match(uri, matches, uri_regex)) {
		return false;
	}

	components->scheme.assign(matches[2].first, matches[2].length());
	components->authority.assign(matches[4].first, matches[4].length());
	components->path.assign(matches[5].first, matches[5].length());
	components->query.assign(matches[7].first, matches[7].length());
	components->fragment.assign(matches[9].first, matches[9].length());

	return true;
}