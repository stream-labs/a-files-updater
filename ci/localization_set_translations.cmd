REM get tools 
call ci\localization_get_tools.cmd

set locales_list="ar_SA","cs_CZ","da_DK","de_DE","en_US","es_ES","fr_FR","hu_HU","id_ID","it_IT","ja_JP","ko_KR","mk_MK","nl_NL","pl_PL","pt_BR","pt_PT","ru_RU","sk_SK","sl_SI","sv_SE","th_TH","tr_TR","vi_VN","zh_CN","zh_TW"
set GETTEXT_BIN=build\gettext_dist\bin\

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
