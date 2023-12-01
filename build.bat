@SET CUR=%~dp0
@rem node-gyp clean configure build --arch=ia32 -libwebm_root=%CUR%\libwebm -libwebm_lib=%CUR%\build32\webm\release
node-gyp clean configure build --arch=x64 -libwebm_root=%CUR%\libwebm -libwebm_lib=%CUR%\build64\webm\release

