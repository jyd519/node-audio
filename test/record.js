const addon = require("..");
const args = process.argv.splice(2);
//6360,2160
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, movflags: "faststart"});
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, frag_duration: 2}); // OK
addon.SetFFmpegLoggingLevel("debug");
let r = addon.record_screen(args[0], {
  width: Math.floor(3840/2),
  height: 720,
  fps: 10,
  gop: 30,
  meta: {
    title: "TITLE",
    comment: "COMMENT",
    other: "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",
  },
  demuxer: 'video_size=3840x2160',
  muxer: 'movflags=frag_keyframe+empty_moov+faststart+use_metadata_tags',
  // encoder: 'rc_mode=1;allow_skip_frames=1;bitrate=250k',  // 0: quality, 1: bitrate
  // encoder: 'rc_mode=1;allow_skip_frames=1;bitrate=250k',  // 0: quality, 1: bitrate
  encoder: 'profile=main',
});
// let r = addon.record_screen(args[0], {fps: 2, quality: 30, frag_duration: 4, gop: 100 });
// let r = addon.record_screen(args[0], {fps: 2, quality: 40, gop: 4, frag_duration: 2});

setTimeout(() => {
  console.log(r.stop());
}, 1000 * 10 * 60);
