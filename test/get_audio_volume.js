const addon = require(".");

const args = process.argv.splice(2);
let r = addon.get_audio_volume_info(args[0], {
  // start: 0,
  // duration: 0,
  password: "1234",
});
console.log(r);


r = addon.get_meta_tags(args[0], "1234" );
console.log(r);
