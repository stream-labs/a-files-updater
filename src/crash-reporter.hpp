#pragma once

void setup_crash_reporting();
void handle_exit() noexcept;
void save_exit_error(const char * error_type) noexcept;