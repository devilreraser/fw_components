@echo off

:GetBatchFilePath
set pathbatch=%~dp0
rem echo pathbatch=%pathbatch%

:GetFilename
set filename=%1
if /I "%filename%"=="" set filename=endpoint*.bin

:GetServername
set servername=%1
if /I "%servername%"=="" set servername=www.ivetell.com:22

set pathfilename=%pathbatch%build\%filename%
::"c:\Program Files (x86)\WinSCP\WinSCP.exe" /command "open sftp://hostd:loading23@%servername%" "put %pathfilename% /home/hostd/firmware/"
::"c:\Program Files (x86)\WinSCP\WinSCP.exe" /command "open sftp://lilov:123@%servername%" "put %pathfilename% /home/lilov/firmware/"
WinSCP /command "open sftp://lilov:123@%servername%" "put %pathfilename% /home/lilov/firmware/" < WinSCP.in
echo copy   "%pathfilename%" 
echo to     "sftp://%servername%/home/lilov/firmware/%filename%"

::pause