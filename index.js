const addon_dir =(function() {
  if (process.platform == "win32" || process.platform == "darwin") {
    return process.arch == "x64" ? "out/build64/Release" : "out/build32";
  }

  //linux
  if (process.arch ==="arm64") {
    return "linux-arm64";
  }
  return "linux-amd64";
}());

const addon = require(`./${addon_dir}/audio.node`);

module.exports = addon;
