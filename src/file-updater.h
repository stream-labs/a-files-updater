#pragma once

#include "utils.hpp"

#include <filesystem>
namespace fs = std::filesystem;

struct update_client;

struct FileUpdater
{
	FileUpdater() = delete;
	FileUpdater(FileUpdater&&) = delete;
	FileUpdater(const FileUpdater&) = delete;
	FileUpdater &operator=(const FileUpdater&) = delete;
	FileUpdater &operator=(FileUpdater&&) = delete;

	explicit FileUpdater(fs::path old_files_dir, fs::path app_dir, fs::path new_files_dir, const manifest_map_t & manifest);
	~FileUpdater();

	void update();
	bool update_entry(manifest_map_t::const_iterator  &iter, fs::path & new_files_dir);
	bool update_entry_with_retries(manifest_map_t::const_iterator  &iter, fs::path & new_files_dir);
	void revert();
	bool reset_rights(const fs::path& path);

private:
	fs::path m_old_files_dir;
	fs::path m_app_dir;
	fs::path m_new_files_dir;
	const manifest_map_t & m_manifest;
};
