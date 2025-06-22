const addon = require(".");
const args = process.argv.splice(2);
//6360,2160
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, movflags: "faststart"});
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, frag_duration: 2}); // OK
let r = addon.record_screen(args[0], {
  width: 1024,
  height: 768,
  fps: 10,
  quality: 25,
  movflags: "frag_keyframe+empty_moov",
  password: "1234",
  gop: 60,
});
// let r = addon.record_screen(args[0], {fps: 2, quality: 30, frag_duration: 4, gop: 100 });
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, gop: 4, frag_duration: 2});

setTimeout(() => {
  console.log(r.stop());
}, 61000);
