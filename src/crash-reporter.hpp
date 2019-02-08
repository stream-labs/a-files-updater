#pragma once

int send_crash_to_sentry( );

std::string prepare_crash_report(struct _EXCEPTION_POINTERS* ExceptionInfo) noexcept;
int send_crash_to_sentry_sync(const std::string& report_json) noexcept;

void printStack( CONTEXT* ctx ) ;
void HandleExit() noexcept;
void HandleCrash(struct _EXCEPTION_POINTERS* ExceptionInfo, bool callAbort = true) noexcept;

