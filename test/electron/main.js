/*
 *  Copyright (c) 2015 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree.
 */

"use strict";

const WIDTH = 1080;
const HEIGHT = 720;

// Put variables in global scope to make them available to the browser console.
const video = document.querySelector("video");
const canvas = (window.canvas = document.querySelector("canvas"));
canvas.width = WIDTH;
canvas.height = HEIGHT;

const addon = require("../../out/build32/audio.node");

let timer = null;
let record = false;
let mp4;

const btnStart = document.querySelector("#btn-start");
const btnStop = document.querySelector("#btn-stop");
btnStop.disabled = true;
btnStart.onclick = function () {
  if (record) {
    return;
  }

  if (mp4) {
    mp4.Close();
  }
  mp4rtmp = new addon.Recorder("rtmp://172.16.21.210/test/test1", {width: WIDTH, height: HEIGHT, comment: "hello, mp4", profile: "high", format: "flv", bitrate: 2400*1000});
  mp4 = new addon.Recorder("test.mp4", {
    width: WIDTH,
    height: HEIGHT,
    comment: "hello, mp4",
    profile: "high",
    bitrate: 2400 * 1000,
    meta: "title=abc"
  });
  record = true;
  btnStart.disabled = true;
  btnStop.disabled = false;
  timer = setInterval(() => {
    updateImage();
  }, 100);
};

btnStop.onclick = function () {
  clearInterval(timer);
  record = false;

  mp4.Close();
  mp4 = null;
  mp4rtmp.Close();
  mp4rtmp = null;

  btnStart.disabled = false;
  btnStop.disabled = true;
};

const constraints = {
  audio: false,
  video: {
    width: { ideal: WIDTH },
    height: { ideal: HEIGHT },
  },
};

function drawStroked(ctx, text, x, y) {
  ctx.font = "24px Sans-serif";
  ctx.strokeStyle = "black";
  ctx.lineWidth = 8;
  ctx.strokeText(text, x, y);
  ctx.fillStyle = "white";
  ctx.fillText(text, x, y);
}

function updateImage() {
  canvas.width = video.videoWidth;
  canvas.height = video.videoHeight;
  const ctx = canvas.getContext("2d", { alpha: false });

  ctx.clearRect(0, 0, canvas.width, canvas.height);
  ctx.fillStyle = "black";
  ctx.font = "bold 24px serif";
  ctx.drawImage(video, 0, 0, canvas.width, canvas.height);
  const now = new Date().toISOString();
  // ctx.fillText(now.substring(0, now.lastIndexOf(".")), 10, 50);
  drawStroked(ctx, now.substring(0, now.lastIndexOf(".")), 10, 50);
  const imageData = ctx.getImageData(0, 0, canvas.width, canvas.height);
  const buf = Buffer.from(imageData.data);
  if (record) {
    if (!mp4.AddImage(buf, canvas.width, canvas.height)) {
      console.log("write mp4 failed:", false);
      btnStop.click();
    }
    if (!mp4rtmp.AddImage(buf, canvas.width, canvas.height)) {
      console.log("rtmp: add failed", false);
      btnStop.click();
    }
  }
}

function handleSuccess(stream) {
  window.stream = stream; // make stream available to browser console
  video.srcObject = stream;

  timer = setInterval(() => {
    updateImage();
  }, 100);
}

function handleError(error) {
  console.log(
    "navigator.MediaDevices.getUserMedia error: ",
    error.message,
    error.name,
  );
}

navigator.mediaDevices
  .getUserMedia(constraints)
  .then(handleSuccess)
  .catch(handleError);
