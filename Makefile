.PHONY: clean build build-arm64 build-amd64 libwebm

ARM64FLAGS=CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++
FFMPEG_AMD64?=/opt/ffmpeg-n6.1-latest-linux64-lgpl-shared-6.1
FFMPEG_ARM64?=/opt/ffmpeg-n6.1-latest-linuxarm64-lgpl-shared-6.1

build: build-arm64 build-amd64


build-amd64:
	FFMPEG_ROOT=$(FFMPEG_AMD64) cmake-js rebuild -a x64 -O ./out/amd64

build-arm64:
	FFMPEG_ROOT=$(FFMPEG_ARM64) cmake-js rebuild \
		--CDCMAKE_TOOLCHAIN_FILE=`pwd`/linux-arm64-toolchain.cmake \
		--CDCMAKE_LIBRARY_ARCHITECTURE=aarch64-linux-gnu \
		--CDCMAKE_LIBRARY_PATH=/usr/lib/aarch64-linux-gnu \
		-a arm64 -O ./out/arm64


# libwebm: webm-amd64 webm-arm64
#
#
# webm-amd64:
# 	cmake -S ../libwebm/ -B libwebm/amd64/
# 	cmake --build libwebm/amd64/
#
#
# webm-arm64:
# 	cmake -DCMAKE_TOOLCHAIN_FILE=./linux-arm64-toolchain.cmake -S ../libwebm -B libwebm/arm64/
# 	cmake --build libwebm/arm64/
#

deploy:
	cp ./out/amd64/Release/libwebm.so ./linux-amd64/libwebm.so
	cp ./out/amd64/Release/audio.node ./linux-amd64/
	cp ./out/arm64/Release/libwebm.so ./linux-arm64/libwebm.so
	cp ./out/arm64/Release/audio.node ./linux-arm64/
	/usr/bin/aarch64-linux-gnu-strip -s linux-arm64/*
	strip -s linux-amd64/*
	scp -r ./linux-amd64  root@172.16.21.222:/var/ata/joytest/DEV/
	scp -r ./linux-arm64  root@172.16.21.222:/var/ata/joytest/DEV/

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
