call ci\locazation_get_tools.cmd

build\gettext_dist\bin\xgettext.exe --keyword=translate:1,1t src\main.cc src\update-client.cc -d messages -p locale
del locale\\messages.pot
rename locale\\messages.po messages.pot

cd locale
for %%F in (
"ar"
"cs"
"da"
"de"
"en"
"es"
"fr"
"hu"
"id"
"it"
"ja"
"ko"
"mk"
"nl"
"pl"
"pt"
"pt"
"ru"
"sk"
"sl"
"sv"
"th"
"tr"
"vi"
"zh"
"zh"
) do if not exist "%%~F\" mkdir "%%~F\\LC_MESSAGES"

