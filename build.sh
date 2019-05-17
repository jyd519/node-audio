SCRIPT_PATH="$( cd "$(dirname $0)" ; pwd -P )"

rm -rf $SCRIPT_PATH/bin
rm -rf $SCRIPT_PATH/build/Release

electron-rebuild -v 2.0.13
