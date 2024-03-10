@ECHO OFF
@SET CUR=%~dp0
REM call node-gyp clean configure build --arch=ia32 -libwebm_include=%CUR%\libwebm -libwebm_lib_path=%CUR%\build32\webm\release -ffmpeg_root=E:\FFmpeg\out\ffbuild
REM call node-gyp clean configure build --arch=x64 -libwebm_include=%CUR%\libwebm -libwebm_lib_path=%CUR%\build64\webm\release -ffmpeg_root=E:\FFmpeg\out\ffbuild64

set VCPKG_ROOT=D:/dev/cpp/vcpkg
REM set FFMPEG_ROOT=E:\FFmpeg\out\ffbuild
REM cmake-js compile -a ia32 -O "out\build32" --CDCMAKE_TOOLCHAIN_FILE="D:/dev/cpp/vcpkg/scripts/buildsystems/vcpkg.cmake" --CDVCPKG_TARGET_TRIPLET=x86-windows-static

set FFMPEG_ROOT=E:\FFmpeg\out\ffbuild64
cmake-js compile -a x64 -O "out\build64"
