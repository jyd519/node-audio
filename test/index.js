const addon_dir =(function() {
  const rel = process.env.DEBUG ? "Debug" : "Release";
  console.log(rel, process.env.DEBUG);
  if (process.env.DEBUG && process.platform == "win32") {
    return '.\\out\\build64d\\Debug';
  }

  if (process.platform == "win32" || process.platform == "darwin") {
    return process.arch == "x64" ? `out/build64/${rel}` : `out/build32/${rel}`;
  }

  //linux
  if (process.arch ==="arm64") {
    return "linux-arm64";
  }
  return "linux-amd64";
}());

console.log(addon_dir);
const addon = require(`../${addon_dir}/audio.node`);

module.exports = addon;
