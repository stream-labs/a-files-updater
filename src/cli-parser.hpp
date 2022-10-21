#pragma once

#include "uri-parser.hpp"
#include "update-parameters.hpp"

bool su_parse_command_line(int argc, char **argv, struct update_parameters *params);