"use strict";
const {test} = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const vm = require("node:vm");

const program = fs.readFileSync(path.join(__dirname, "../bridge.js"), "utf8");
const logitech = {kind:"videoinput", label:"Logi C270 HD WebCam", deviceId:"physical-c270"};
const obs = {kind:"videoinput", label:"OBS Virtual Camera", deviceId:"obs-virtual"};
const other = {kind:"videoinput", label:"External Camera", deviceId:"external-camera"};

function environment(devices) {
  const calls = [];
  const mediaDevices = {
    enumerateDevices: async () => devices,
    getUserMedia: async (constraints) => {
      calls.push(constraints);
      return {trackLabel:"actual capture (not spoofed)", constraints};
    }
  };
  const navigator = {mediaDevices};
  const logs = [];
  const context = vm.createContext({
    navigator, DOMException, console:{info:(...args)=>logs.push(args)}
  });
  vm.runInContext(program, context);
  return {mediaDevices, calls, logs, context};
}

test("Logitech as browser default is routed only to OBS", async () => {
  const e = environment([logitech,obs,other]);
  const result = await e.mediaDevices.getUserMedia({video:true,audio:false});
  assert.equal(e.calls.length,1);
  assert.equal(e.calls[0].video.deviceId.exact,obs.deviceId);
  assert.equal(result.constraints.video.deviceId.exact,obs.deviceId);
  assert.equal(e.logs.length,1);
});

test("explicit Logitech C270 selection redirects to OBS and keeps requested audio", async () => {
  const e = environment([other,logitech,obs]);
  const result = await e.mediaDevices.getUserMedia({
    video:{deviceId:{exact:logitech.deviceId},width:{ideal:1280},advanced:[{zoom:2}]},
    audio:{echoCancellation:true}
  });
  assert.equal(result.constraints.video.deviceId.exact,obs.deviceId);
  assert.equal(result.constraints.video.width.ideal,1280);
  assert.equal(result.constraints.video.advanced,undefined);
  assert.equal(result.constraints.audio.echoCancellation,true);
});

test("unrelated physical camera selection is not redirected", async () => {
  const e = environment([logitech,obs,other]);
  const original = {video:{deviceId:{exact:other.deviceId}}};
  await e.mediaDevices.getUserMedia(original);
  assert.equal(e.calls.length,1);
  assert.equal(e.calls[0],original);
});

test("explicit OBS selection is not redirected", async () => {
  const e = environment([logitech,obs]);
  const original = {video:{deviceId:{exact:obs.deviceId}}};
  await e.mediaDevices.getUserMedia(original);
  assert.equal(e.calls[0],original);
});

test("microphone-only request is always unchanged", async () => {
  const e = environment([logitech,obs]);
  const original = {audio:true,video:false};
  await e.mediaDevices.getUserMedia(original);
  assert.equal(e.calls[0],original);
});

test("missing camera consent fails closed, never opens Logitech", async () => {
  const e = environment([
    {...logitech,label:""},
    {...obs,label:""}
  ]);
  await assert.rejects(e.mediaDevices.getUserMedia({video:true}),{name:"NotAllowedError"});
  assert.equal(e.calls.length,0);
});

test("missing OBS fails closed when Logitech is selected", async () => {
  const e = environment([logitech]);
  await assert.rejects(e.mediaDevices.getUserMedia({video:true}),{name:"NotFoundError"});
  assert.equal(e.calls.length,0);
});

test("site still enumerates physical Logitech unchanged; no USB identity spoofing", async () => {
  const devices = [logitech,obs,other];
  const e = environment(devices);
  assert.equal(await e.mediaDevices.enumerateDevices(),devices);
  const stream = await e.mediaDevices.getUserMedia({video:false,audio:true});
  assert.equal(stream.constraints.audio,true);
});

test("second injection does not wrap getUserMedia a second time", async () => {
  const e = environment([logitech,obs]);
  const first = e.mediaDevices.getUserMedia;
  vm.runInContext(program,e.context);
  assert.equal(e.mediaDevices.getUserMedia,first);
  await e.mediaDevices.getUserMedia({video:true});
  assert.equal(e.calls.length,1);
});
