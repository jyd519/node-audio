const addon = require('.');

const fs = require("fs");

const args = process.argv.splice(2);
let r = addon.get_audio_duration(args[0], args[1]);
console.log(r);
// fs.readFile(args[0], (err, data) => {
//   let r = addon.get_audio_duration(data);
//   console.log(r);
// });
