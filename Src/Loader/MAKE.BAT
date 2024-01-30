echo off
del *.rel
del ..\*.h
cls

set BINARY=..\..\Bin
set ASM=..\..\MA80

:: compile headers for COM-files
%ASM%\ma80.exe hex,hex=hex
if errorlevel == 1 goto error
%ASM%\mlink hex
if errorlevel == 1 goto error

%ASM%\ma80.exe hexa,hexa=hexa
if errorlevel == 1 goto error
%ASM%\mlink hexa
if errorlevel == 1 goto error

%ASM%\ma80.exe hexs,hexs=hexs
if errorlevel == 1 goto error
%ASM%\mlink hexs
if errorlevel == 1 goto error

%ASM%\ma80.exe bas,bas=bas
if errorlevel == 1 goto error
%ASM%\Bin\mlink bas
if errorlevel == 1 goto error


:: convert binary headers to C-headers file
%ASM%\bin2h.exe BLOAD < hex.com > ..\hex.h
%ASM%\bin2h.exe BPROT < hexa.com > ..\hexa.h
%ASM%\bin2h.exe BLOADS < hexs.com > ..\hexs.h
%ASM%\bin2h.exe CLOAD < bas.com > ..\bas.h

echo ==================================================
echo = Yeehaw! All headers created! No Errors! Ready! =
echo ==================================================
goto end

:error
echo ######################################
echo ######### AN ERROR OCCURED! ##########
echo ######################################

:end
pause
del *.rel
del *.prn
del *.com
