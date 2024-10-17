param (
    [string]$ffmpeg="OFF"
)

$env:VCPKG_ROOT="D:/dev/cpp/vcpkg"

$env:FFMPEG_ROOT="I:\FFmpeg\out\ffbuild"
$CONFIG_ARGS="--CDCMAKE_TOOLCHAIN_FILE=./out/build32/conan_toolchain.cmake"
cmake-js configure -D -a x86 -O "out\build32" $CONFIG_ARGS --CDENABLE_FFMPEG=$ffmpeg --CDFFMPEG_ROOT="I:\FFmpeg\out\ffbuild"

$env:FFMPEG_ROOT="I:\FFmpeg\out\ffbuild64"
$CONFIG_ARGS="--CDCMAKE_TOOLCHAIN_FILE=./out/build64/conan_toolchain.cmake"
cmake-js configure -a x64 -O "out\build64" $CONFIG_ARGS  --CDENABLE_FFMPEG=$ffmpeg --CDFFMPEG_ROOT="I:\FFmpeg\out\ffbuild64"

cmake --build "out\build32" --config Release
cmake --build "out\build64" --config Release
