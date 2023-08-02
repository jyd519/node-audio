const addon_dir =(function() {
  if (process.platform == "win32" || process.platform == "darwin") {
    return process.arch;
  }

  //linux
  if (process.arch ==="arm64") {
    return "linux-arm64";
  }
  return "linux-amd64";
}());

const addon = require(`./${addon_dir}/audio.node`);

const args = process.argv.splice(2);
let r = addon.fixup_webm(args[0], args[1]);
console.log(r);
