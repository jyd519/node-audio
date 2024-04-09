const addon = require(".");

const args = process.argv.splice(2);
let r = addon.probe("-show_streams", "-show_format", "-v", "error", args[0]);
console.log(r);
