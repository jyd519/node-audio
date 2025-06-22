const fs = require('fs');
const path = require('path');

const name = "audio.node";

const addon_dir =(function() {
  const rel = process.env.DEBUG ? "Debug" : "Release";
  let p = '';
  console.log(rel, process.env.DEBUG);
  if (process.env.DEBUG && process.platform == "win32") {
    return '.\\out\\build64d\\Debug';
  }
  if (process.platform == "win32" || process.platform == "darwin") {
    p = process.arch == "x64" ? `bin/build64/${rel}` : `bin/build32/${rel}`;
  } else {
    //linux
    if (process.arch ==="arm64") {
      p = "linux-arm64";
    } else {
      p = "linux-amd64";
    }
  }

  if (fs.existsSync(`./${p}/${name}`)) {
    return p;
  }

  if (fs.existsSync(`./${path.dirname(p)}/${name}`)) {
    return path.dirname(p);
  }

}());


console.log(`>>  ./${addon_dir}/${name}`);
const addon = require(`./${addon_dir}/${name}`);

module.exports = addon;
