REM get tools 
call ci\localization_get_tools.cmd

REM dump strings need to be translated from source code 
%GETTEXT_BIN%xgettext.exe --keyword=translate:1,1t src\main.cc src\update-client.cc -d messages -p locale
del locale\\messages.pot
rename locale\\messages.po messages.pot

REM make sure that the locale directory exists for each locale to be able to save files to it
for %%F in (%locales_list%) do if not exist "locale\%%~F\" mkdir "locale\%%~F\\LC_MESSAGES"

REM only generate new .po files if there is none. 
REM to prevernt loosing existing translations
for %%F in (%locales_list%) do if not exist "locale\%%~F\LC_MESSAGES\messages.po" %GETTEXT_BIN%msginit.exe -i locale/messages.pot --locale=%%~F -o locale/%%~F/LC_MESSAGES/messages.po

REM merge new lines to existing .po files
for %%F in (%locales_list%) do %GETTEXT_BIN%msgmerge.exe locale/%%~F/LC_MESSAGES/messages.po locale/messages.pot -o locale/%%~F/LC_MESSAGES/messages.po

del locale\\messages.pot
