const addon = require('.');

const args = process.argv.splice(2);
let r = addon.fixup_webm(args[0], args[1]);
console.log(r);
