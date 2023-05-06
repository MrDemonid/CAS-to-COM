echo off
del *.rel
del ..\*.h
cls

:: compile headers for COM-files
..\..\Bin\ma80.exe hex,hex=hex
if errorlevel == 1 goto error
..\..\Bin\mlink hex
if errorlevel == 1 goto error

..\..\Bin\ma80.exe hexa,hexa=hexa
if errorlevel == 1 goto error
..\..\Bin\mlink hexa
if errorlevel == 1 goto error

..\..\Bin\ma80.exe hexs,hexs=hexs
if errorlevel == 1 goto error
..\..\Bin\mlink hexs
if errorlevel == 1 goto error

..\..\Bin\ma80.exe bas,bas=bas
if errorlevel == 1 goto error
..\..\Bin\mlink bas
if errorlevel == 1 goto error


:: convert binary headers to C-headers file
..\..\Bin\bin2h.exe BLOAD < hex.com > ..\hex.h
..\..\Bin\bin2h.exe BPROT < hexa.com > ..\hexa.h
..\..\Bin\bin2h.exe BLOADS < hexs.com > ..\hexs.h
..\..\Bin\bin2h.exe CLOAD < bas.com > ..\bas.h

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
