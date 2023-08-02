.PHONY: clean build build-arm64 build-amd64 libwebm

ARM64FLAGS=CC=aarch64-linux-gnu-gcc CXX=aarch64-linux-gnu-g++
LIBWEBM_BUILD=/opt/works/node-audio/libwebm

build: libwebm build-arm64 build-amd64


build-amd64:
	npm_config_libwebm_lib_path=$(LIBWEBM_BUILD)/amd64 npm_config_targetarch=amd64 npm run config
	npm run build
	mkdir -p linux-amd64
	cp ./build/Release/webm.so ./linux-amd64/libwebm.so
	cp ./build/Release/audio.node ./linux-amd64/


build-arm64:
	rm -rf build
	npm_config_libwebm_lib_path=$(LIBWEBM_BUILD)/arm64 npm_config_arch=arm64 npm_config_targetarch=arm64 $(ARM64FLAGS) npm run config
	$(ARM64FLAGS) npm run build
	mkdir -p linux-arm64
	cp ./build/Release/webm.so ./linux-arm64/libwebm.so
	cp ./build/Release/audio.node ./linux-arm64/


libwebm: webm-amd64 webm-arm64


webm-amd64:
	cmake -S ../libwebm/ -B libwebm/amd64/
	cmake --build libwebm/amd64/


webm-arm64:
	cmake -DCMAKE_TOOLCHAIN_FILE=./linux-arm64-toolchain.cmake -S ../libwebm -B libwebm/arm64/
	cmake --build libwebm/arm64/


deploy:
	/usr/bin/aarch64-linux-gnu-strip -s linux-arm64/*
	strip -s linux-amd64/*
	scp -r ./linux-amd64  root@172.16.21.222:/var/ata/joytest/DEV/
	scp -r ./linux-arm64  root@172.16.21.222:/var/ata/joytest/DEV/

clean:
	rm -rf libwebm/
	rm -rf build/
