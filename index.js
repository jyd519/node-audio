const fs = require('fs');
const path = require('path');

const addon_dir =(function() {
  const rel = process.env.DEBUG ? "Debug" : "Release";
  let p = '';
  console.log(rel, process.env.DEBUG);
  if (process.platform == "win32" || process.platform == "darwin") {
    p = process.arch == "x64" ? `out/build64/${rel}` : `out/build32/${rel}`;
  } else {
    //linux
    if (process.arch ==="arm64") {
      p = "linux-arm64";
    } else {
      p = "linux-amd64";
    }
  }

  if (fs.existsSync(`./${p}/audio.node`)) {
    return p;
  }

  if (fs.existsSync(`./${path.dirname(p)}/audio.node`)) {
    return path.dirname(p);
  }

}());

const addon = require(`./${addon_dir}/audio.node`);

module.exports = addon;
