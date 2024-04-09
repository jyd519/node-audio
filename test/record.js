const addon = require('.');

const args = process.argv.splice(2);
//6360,2160
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, movflags: "faststart"});
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, frag_duration: 2}); // OK
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, movflags: "frag_keyframe+empty_moov", gop: 4});
let r = addon.record_screen(args[0], {fps: 2, quality: 30, frag_duration: 4, gop: 100 });
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, gop: 4, frag_duration: 2});

setTimeout(() => {
   console.log(r.stop());
 }, 61000);
