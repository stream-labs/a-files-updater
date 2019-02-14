#pragma once

std::string prepare_crash_report(struct _EXCEPTION_POINTERS* ExceptionInfo) noexcept;
int send_crash_to_sentry_sync(const std::string& report_json) noexcept;

void handle_exit() noexcept;
void handle_crash(struct _EXCEPTION_POINTERS* ExceptionInfo, bool callAbort = true) noexcept;

