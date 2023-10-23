@echo off
setlocal ENABLEDELAYEDEXPANSION
rem Line @ Execute After Build:    ..\..\..\..\.\generate_version_build.bat 01_Source\verbuild.h 01_Source\datetimepc.h
rem execute line PreBuild ${workspaceFolder}\generate_version_build.bat components\drv_version\verbuild.h components\drv_version\datetimepc.h

:InputConfiguration
set fileversion=%1
set filedatetime=%2
set filefullpath=%3
set fixforwardslashes=%4

if "%fileversion%"=="" set fileversion=version.h
if "%filedatetime%"=="" set filedatetime=datetime.h
if "%filefullpath%"=="" set filefullpath=false
if "%fixforwardslashes%"=="" set fixforwardslashes=false

set /a debugecho=1

::sleep 2



:GetBatchFilePath
set pathbatch=%~dp0

if "%fixforwardslashes%"=="true" set "pathbatch=%pathbatch:/=\%"
if "%fixforwardslashes%"=="true" set "filedatetime=%filedatetime:/=\%"
if "%fixforwardslashes%"=="true" set "fileversion=%fileversion:/=\%"

if "%filefullpath%"=="false" set fileversion=%pathbatch%%fileversion%
if "%filefullpath%"=="false" set filedatetime=%pathbatch%%filedatetime%


if %debugecho% EQU 0 ( goto DEBUG_END_BATCH_PATH)
echo pathbatch       : %pathbatch%
echo datetimefile    : %filedatetime%
echo versionfile     : %fileversion%
:DEBUG_END_BATCH_PATH

@echo off
for /f "tokens=2 delims==" %%a in ('wmic OS Get localdatetime /value') do set "dt=%%a"
set "YY=%dt:~2,2%" & set "YYYY=%dt:~0,4%" & set "MM=%dt:~4,2%" & set "DD=%dt:~6,2%"
set "HH=%dt:~8,2%" & set "Min=%dt:~10,2%" & set "Sec=%dt:~12,2%"

set month-num=%MM%
if %month-num%==01 set mo-name=jan
if %month-num%==02 set mo-name=feb
if %month-num%==03 set mo-name=mar
if %month-num%==04 set mo-name=apr
if %month-num%==05 set mo-name=may
if %month-num%==06 set mo-name=jun
if %month-num%==07 set mo-name=jul
if %month-num%==08 set mo-name=aug
if %month-num%==09 set mo-name=sep
if %month-num%==10 set mo-name=oct
if %month-num%==11 set mo-name=nov
if %month-num%==12 set mo-name=dec
rem echo build filename using %mo-name%

set "datestamp=%mo-name% %DD% %YYYY%" & set "timestamp=%HH%:%Min%:%Sec%"
set "fullstamp=%YYYY%-%MM%-%DD%_%HH%-%Min%-%Sec%"
rem echo datestamp: "%datestamp%"
rem echo timestamp: "%timestamp%"
rem echo fullstamp: "%fullstamp%"
rem ::pause

echo.
echo Build Date and Time "%datestamp%" "%timestamp%" 
echo.

echo #define BUILD_DATE          "%datestamp%" >%filedatetime%
echo.
echo %filedatetime% Created
echo.
echo #define BUILD_TIME          "%timestamp%" >>"%filedatetime%"
echo. >>"%filedatetime%"
echo #define BUILD_PARSED >>"%filedatetime%"
echo. >>"%filedatetime%"
echo #define BUILD_YEAR_CH0      (uint32_t)(%YYYY:~0,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_YEAR_CH1      (uint32_t)(%YYYY:~1,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_YEAR_CH2      (uint32_t)(%YYYY:~2,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_YEAR_CH3      (uint32_t)(%YYYY:~3,1% + 0x30)>>"%filedatetime%"
echo. >>"%filedatetime%"
echo #define BUILD_MONTH_CH0     (uint32_t)(%MM:~0,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_MONTH_CH1     (uint32_t)(%MM:~1,1% + 0x30)>>"%filedatetime%"
echo. >>"%filedatetime%"
echo #define BUILD_DAY_CH0       (uint32_t)(%DD:~0,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_DAY_CH1       (uint32_t)(%DD:~1,1% + 0x30)>>"%filedatetime%"
echo. >>"%filedatetime%"
echo #define BUILD_HOUR_CH0      (uint32_t)(%HH:~0,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_HOUR_CH1      (uint32_t)(%HH:~1,1% + 0x30)>>"%filedatetime%"
echo. >>"%filedatetime%"
echo #define BUILD_MIN_CH0       (uint32_t)(%Min:~0,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_MIN_CH1       (uint32_t)(%Min:~1,1% + 0x30)>>"%filedatetime%"
echo. >>"%filedatetime%"
echo #define BUILD_SEC_CH0       (uint32_t)(%Sec:~0,1% + 0x30)>>"%filedatetime%"
echo #define BUILD_SEC_CH1       (uint32_t)(%Sec:~1,1% + 0x30)>>"%filedatetime%"


del "%fileversion:~0,-1%tmp" 2> NUL
del "%fileversion:~0,-1%new" 2> NUL


if exist "%fileversion%" (
    goto version_file_exists
) else (
    goto version_file_not_found
)

:version_file_exists


set /a addcarry=0



for /F "tokens=1,2,3 delims= " %%A in ('type "%fileversion%"') do (
	set /a versionnew=%%C+1
	set /a versionfix=!versionnew!+!addcarry!
	rem echo versionnew1:!versionfix!
	if !versionfix! GEQ 65536 ( set /a versionfix=0 )
	rem echo versionnew2:!versionfix!
	if !versionfix! EQU 0 ( set /a addcarry=0 )
	if !versionfix! NEQ 0 ( set /a addcarry=-1 )
	rem echo versionnew3:!versionfix!
	rem echo %%A %%B !versionnew!
	echo %%A %%B !versionfix! >"%fileversion:~0,-1%tmp"
	type "%fileversion:~0,-1%tmp" >> "%fileversion:~0,-1%new"
	)
	
echo Current Build Version Definition:
type "%fileversion%"
echo.

del "%fileversion:~0,-1%tmp" 2>Nul
del "%fileversion%"

copy "%fileversion:~0,-1%new" "%fileversion%" >NUL
del "%fileversion:~0,-1%new" 2>Nul
	
echo Changed Build Version Definition:
type "%fileversion%"
echo.
	
goto version_file_end

:version_file_not_found
echo #define APP_VERSION_BUILD 0 >"%fileversion%"
echo #define APP_VERSION_MINOR 0 >>"%fileversion%"

:version_file_end	

endlocal