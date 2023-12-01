cmake -A Win32 -S libwebm -B "build32\webm"
cmake -A x64 -S libwebm -B "build64\webm"

cmake --build build32\webm --config Release
cmake --build build64\webm --config Release