param (
    [string]$ffmpeg="OFF"
)

conan install -of "out\build32" --build=missing -s compiler.runtime=static -s arch=x86 .
conan install -of "out\build64" --build=missing -s compiler.runtime=static -s arch=x86_64 .

$env:ELECTRON_MIRROR="https://electronjs.org/headers"


$env:FFMPEG_ROOT="C:\dev\cpp\FFmpeg\out\ffbuild"
$CONFIG_ARGS="--CDCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake"
cmake-js configure --runtime=electron -v 20.3.12 -a x86 -O "out\build32" $CONFIG_ARGS --CDENABLE_FFMPEG=$ffmpeg --CDFFMPEG_ROOT="$env:FFMPEG_ROOT"

$env:FFMPEG_ROOT="C:\dev\cpp\FFmpeg\out\ffbuild64"
$CONFIG_ARGS="--CDCMAKE_TOOLCHAIN_FILE=conan_toolchain.cmake"
cmake-js configure --runtime=electron -v 20.3.12 -a x64 -O "out\build64" $CONFIG_ARGS  --CDENABLE_FFMPEG=$ffmpeg --CDFFMPEG_ROOT="$env:FFMPEG_ROOT"

cmake --build "out\build32" --config Release
cmake --build "out\build64" --config Release
