const addon = require('./x64/audio.node');

const args = process.argv.splice(2);
let r = addon.fixup_webm(args[0], args[1]);
console.log(r);
