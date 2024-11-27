.PHONY: clean build linux-arm64 linux-amd64 linux-loong64 libwebm x64

UNAME := $(shell uname)

ifeq ($(UNAME), Linux)
	OS = linux
	buildtarget=linux-x64 linux-arm64 linux-loong64
	deploytarget=linux-deploy
else
	OS = mac
	buildtarget=mac-x64 mac-arm64
	deploytarget=mac-deploy
endif

ARM64FLAGS=CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++
LA64FLAGS=CC=loongarch64-linux-gnu-gcc CXX=loongarch64-linux-gnu-g++

FFMPEG_AMD64?=/opt/ffmpeg-n6.1-latest-linux64-lgpl-shared-6.1
FFMPEG_ARM64?=/opt/ffmpeg-n6.1-latest-linuxarm64-lgpl-shared-6.1


build: $(buildtarget)


deploy: $(deploytarget)


linux-x64:
	FFMPEG_ROOT=$(FFMPEG_AMD64) cmake-js rebuild -a x64 -O ./out/amd64


linux-arm64:
	FFMPEG_ROOT=$(FFMPEG_ARM64) cmake-js rebuild \
							--CDCMAKE_TOOLCHAIN_FILE=`pwd`/toolchains/linux-arm64-toolchain.cmake \
							--CDCMAKE_LIBRARY_ARCHITECTURE=aarch64-linux-gnu \
							--CDCMAKE_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
							-a arm64 -O ./out/arm64


linux-loong64:
	cmake-js rebuild \
							--CDCMAKE_TOOLCHAIN_FILE=`pwd`/toolchains/linux-loong64-toolchain.cmake \
							--CDENABLE_FFMPEG=on --CDENABLE_FFMPEG_FULL=off \
							--CDCMAKE_LIBRARY_ARCHITECTURE=loongarch64-linux-gnu \
							-a loong64 -O ./out/loong64
	cmake-js rebuild \
							--CDCMAKE_TOOLCHAIN_FILE=`pwd`/toolchains/linux-loong64-toolchain.cmake \
							--CDENABLE_FFMPEG=off --CDENABLE_FFMPEG_FULL=off \
							--CDCMAKE_LIBRARY_ARCHITECTURE=loongarch64-linux-gnu \
							-a loong64 -O ./out/loong64

mac-arm64:
	cmake-js rebuild \
		--CDCMAKE_TOOLCHAIN_FILE=`pwd`/build/deps-arm64/conan_toolchain.cmake \
		--CDENABLE_FFMPEG=off \
		-a arm64 -O ./out/arm64


mac-x64:
	cmake-js rebuild \
		--CDCMAKE_TOOLCHAIN_FILE=`pwd`/build/deps-x64/conan_toolchain.cmake \
		--CDENABLE_FFMPEG=off \
		-a x64 -O ./out/x64


mac-deploy:
	scp -r ./out/x64/Release/audio.node  root@172.16.21.222:/var/ata/joytest/DEV/osx/audio-x64.node
	scp -r ./out/arm64/Release/audio.node  root@172.16.21.222:/var/ata/joytest/DEV/osx/audio-arm64.node
	scp -r ./out/x64/Release/libwebm.dylib root@172.16.21.222:/var/ata/joytest/DEV/osx/libwebm-x64.dylib
	scp -r ./out/arm64/Release/libwebm.dylib  root@172.16.21.222:/var/ata/joytest/DEV/osx/libwebm-arm64.dylib

mac-ffmpeg:
	tar --exclude=libwebm.dylib --exclude=audio.node -cvJf ffmpeg-darwin-x64.tar.xz -C out/x64/Release/ .
	tar --exclude=libwebm.dylib --exclude=audio.node -cvJf ffmpeg-darwin-arm64.tar.xz -C out/arm64/Release/ .
	scp -r ffmpeg*.xz  root@172.16.21.222:/var/ata/joytest/DEV/ffmpeg/


linux-deploy:
	cp ./bin/amd64/libwebm.so ./linux-amd64/libwebm.so
	cp ./bin/amd64/audio.node ./linux-amd64/
	strip -s linux-amd64/*
	cp ./bin/arm64/libwebm.so ./linux-arm64/libwebm.so
	cp ./bin/arm64/audio.node ./linux-arm64/
	/usr/bin/aarch64-linux-gnu-strip -s linux-arm64/*
	cp ./bin/loong64/libwebm.so ./linux-loong64/libwebm.so
	cp ./bin/loong64/*.node ./linux-loong64/
	loongarch64-linux-gnu-strip -s linux-loong64/*
	scp -r ./linux-amd64  root@172.16.21.222:/var/ata/joytest/DEV/
	scp -r ./linux-arm64  root@172.16.21.222:/var/ata/joytest/DEV/
	scp -r ./linux-loong64  root@172.16.21.222:/var/ata/joytest/DEV/

clean:
	rm -rf libwebm/
	rm -rf build/


tar:
	patchelf --set-rpath '$$ORIGIN:$$ORIGIN/..' $(FFMPEG_AMD64)/bin/ffmpeg
	patchelf --set-rpath '$$ORIGIN:$$ORIGIN/..' $(FFMPEG_AMD64)/bin/ffprobe
	patchelf --set-rpath '$$ORIGIN:$$ORIGIN/..' $(FFMPEG_ARM64)/bin/ffmpeg
	patchelf --set-rpath '$$ORIGIN:$$ORIGIN/..' $(FFMPEG_ARM64)/bin/ffprobe
	cp $(FFMPEG_AMD64)/LICENSE.txt $(FFMPEG_AMD64)/lib/ffmpeg.LICENSE.txt
	cp $(FFMPEG_ARM64)/LICENSE.txt $(FFMPEG_ARM64)/lib/ffmpeg.LICENSE.txt
	tar --exclude=pkgconfig -cvJf  ffmpeg-linux-amd64.tar.xz -C $(FFMPEG_AMD64)/lib .  -C $(FFMPEG_AMD64)/bin ffmpeg ffprobe
	tar --exclude=pkgconfig -cvJf  ffmpeg-linux-arm64.tar.xz -C $(FFMPEG_ARM64)/lib .  -C $(FFMPEG_ARM64)/bin ffmpeg ffprobe
