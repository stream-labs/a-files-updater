


c:\work\repos\a-files-updater>c:\work\tools\gettext\bin\xgettext.exe --keyword=translate:1,1t src\main.cc src\update-client.cc -d messages -p locale
del locale\\messages.pot
rename locale\\messages.po messages.pot

mkdir locale\\el\\LC_MESSAGES
c:\work\tools\gettext\bin\msginit.exe -i locale/messages.pot --locale=el_GR -o locale/el/LC_MESSAGES/messages.po
c:\work\tools\gettext\bin\msginit.exe -i locale/messages.pot --locale=en_EN -o locale/en/LC_MESSAGES/messages.po
c:\work\tools\gettext\bin\msginit.exe -i locale/messages.pot --locale=ru_RU -o locale/ru/LC_MESSAGES/messages.po
c:\work\tools\gettext\bin\msgfmt.exe locale/el/LC_MESSAGES/messages.po -o locale/el/LC_MESSAGES/messages.mo
c:\work\tools\gettext\bin\msgfmt.exe locale/en/LC_MESSAGES/messages.po -o locale/en/LC_MESSAGES/messages.mo
c:\work\tools\gettext\bin\msgfmt.exe locale/ru/LC_MESSAGES/messages.po -o locale/ru/LC_MESSAGES/messages.mo


c:\work\tools\gettext\bin\msgmerge.exe locale/el/LC_MESSAGES/messages.po locale/messages.pot -o locale/el/LC_MESSAGES/messages.po
c:\work\tools\gettext\bin\msgmerge.exe locale/en/LC_MESSAGES/messages.po locale/messages.pot -o locale/en/LC_MESSAGES/messages.po
c:\work\tools\gettext\bin\msgmerge.exe locale/ru/LC_MESSAGES/messages.po locale/messages.pot -o locale/ru/LC_MESSAGES/messages.po


ar-SA
cs-CZ
da-DK
de-DE
en-US
es-ES
fr-FR
hu-HU
id-ID
it-IT
ja-JP
ko-KR
mk-MK
nl-NL
pl-PL
pt-BR
pt-PT
ru-RU
sk-SK
sl-SI
sv-SE
th-TH
tr-TR
vi-VN
zh-CN
zh-TW
