const addon_dir =(function() {
  const rel = process.env.DEBUG ? "Debug" : "Release";
  console.log(rel, process.env.DEBUG);
  if (process.platform == "win32" || process.platform == "darwin") {
    return process.arch == "x64" ? `out/build64/${rel}` : `out/build32/${rel}`;
  }

  //linux
  if (process.arch ==="arm64") {
    return "linux-arm64";
  }
  return "linux-amd64";
}());

const addon = require(`../${addon_dir}/audio.node`);

module.exports = addon;
