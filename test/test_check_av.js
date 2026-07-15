/**
 * test_check_av.js
 * 检测 webm 文件的音视频质量
 *
 * 用法: node test_check_av.js <文件路径> [--password <密码>] [--duration <秒>]
 */
const path = require("path");
const fs = require("fs");

function loadAudioAddon() {
  return require("..")
}

function parseArgs() {
  const args = process.argv.slice(2);
  const result = { files: [], password: "", duration: 0 };
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--password" && i + 1 < args.length) {
      result.password = args[++i];
    } else if (args[i] === "--duration" && i + 1 < args.length) {
      result.duration = parseFloat(args[++i]) || 0;
    } else {
      result.files.push(args[i]);
    }
  }
  return result;
}

function main() {
  const { files, password, duration } = parseArgs();
  if (files.length === 0) {
    console.log("用法: node test_check_av.js <文件路径...> [--password <密码>] [--duration <秒>]");
    process.exit(1);
  }

  const addon = loadAudioAddon();
  if (typeof addon.check_av !== "function") {
    console.error("[ERROR] addon.check_av 不存在，请确认编译时启用了 ENABLE_FFMPEG");
    process.exit(1);
  }

  for (const file of files) {
    if (!fs.existsSync(file)) {
      console.error(`[SKIP] 文件不存在: ${file}`);
      continue;
    }

    console.log(`\n========== ${path.basename(file)} ==========`);
    const opts = {};
    if (password) opts.password = password;
    if (duration > 0) opts.duration = duration;
    const r = addon.check_av(file, opts);

    console.log(`状态:     ${r.status === 0 ? "成功" : "失败(" + r.status + ")"}`);
    console.log(`时长:     ${r.duration.toFixed(2)}s`);
    console.log(`有视频:   ${r.has_video}`);
    console.log(`有音频:   ${r.has_audio}`);
    console.log(`---`);

    if (r.has_video) {
      console.log(`黑屏时长: ${r.black_total.toFixed(2)}s (${(r.black_ratio * 100).toFixed(1)}%)`);
      console.log(`冻结时长: ${r.freeze_total.toFixed(2)}s (${(r.freeze_ratio * 100).toFixed(1)}%)`);
      console.log(`视频正常: ${r.video_normal}`);
    }

    if (r.has_audio) {
      console.log(`静音时长: ${r.silence_total.toFixed(2)}s (${(r.silence_ratio * 100).toFixed(1)}%)`);
      console.log(`音频正常: ${r.audio_normal}`);
    }

    const overall = r.status === 0 && r.video_normal && r.audio_normal;
    console.log(`---`);
    console.log(`综合判定: ${overall ? "✓ 正常" : "✗ 异常"}`);
  }
}

main();
