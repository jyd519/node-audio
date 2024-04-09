const addon = require('.');

const args = process.argv.splice(2);
let r = addon.get_audio_volume_info(args[0], {start: 15, duration: 0});
console.log(r);
