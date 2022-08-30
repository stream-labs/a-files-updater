REM get tools 
call ci\localization_get_tools.cmd

REM merge new lines to existing .po files
for %%F in (%locales_list%) do %GETTEXT_BIN%msgfmt.exe locale/%%~F/LC_MESSAGES/messages.po -o resources/%%~F_messages.mo
