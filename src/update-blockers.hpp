#pragma once

#include <windows.h>

#include <RestartManager.h>
#include <list>
#include <map>

#include <filesystem>

namespace fs = std::filesystem;

using blockers_map_t = std::map<DWORD, RM_PROCESS_INFO>;

// === Update exceptions

class update_exception_blocked : public std::exception
{};

class update_exception_failed : public std::exception
{};

// === Update blockers check

// return true if successfuly get info on blocker process 
bool get_blockers_list(fs::path & check_path, blockers_map_t &blockers);

// check if file ok to read or write 
// return : false if file blocked 
// blockers list updated with blocker process info 
bool check_file_updatable(fs::path & check_path, bool check_read, blockers_map_t &blockers);

bool get_blockers_names(blockers_map_t &blockers);