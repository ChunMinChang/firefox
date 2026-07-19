// META: global=window,dedicatedworker
// META: script=/webcodecs/video-encoder-utils.js
// META: variant=?av1
// META: variant=?vp9
// META: variant=?vp8
// META: variant=?h264_avc
// META: variant=?h264_annexb

const INPUT_COLOR_SPACE = {
  primaries: 'bt2020',
  transfer: 'pq',
  matrix: 'bt2020-ncl',
  fullRange: true,
};

const SECOND_COLOR_SPACE = {
  primaries: 'bt470bg',
  transfer: 'smpte170m',
  matrix: 'bt470bg',
  fullRange: false,
};

const PARTIAL_COLOR_SPACE = {
  primaries: 'bt2020',
  transfer: null,
  matrix: null,
  fullRange: null,
};

const RGB_COMPATIBILITY_COLOR_SPACE = {
  primaries: 'bt709',
  transfer: 'bt709',
  matrix: 'bt709',
  fullRange: false,
};

function codecConfig() {
  return {
    '?av1': {codec: 'av01.0.04M.08'},
    '?vp9': {codec: 'vp09.00.10.08'},
    '?vp8': {codec: 'vp8'},
    '?h264_avc': {codec: 'avc1.42001E', avc: {format: 'avc'}},
    '?h264_annexb': {codec: 'avc1.42001E', avc: {format: 'annexb'}},
  }[location.search];
}

function makeConfig(width = 320, height = 200) {
  return {
    ...codecConfig(),
    width,
    height,
    bitrate: 1000000,
    framerate: 30,
    hardwareAcceleration: 'prefer-software',
  };
}

function makeI420Frame(width, height, colorSpace, timestamp = 0) {
  const ySize = width * height;
  const cSize = (width >> 1) * (height >> 1);
  const buf = new Uint8Array(ySize + 2 * cSize);
  buf.fill(128);
  buf.subarray(0, ySize).fill(120);
  return new VideoFrame(buf, {
    format: 'I420',
    codedWidth: width,
    codedHeight: height,
    timestamp,
    colorSpace,
  });
}

function makeRGBAFrame(width, height, timestamp = 0) {
  const buf = new Uint8Array(width * height * 4);
  buf.fill(128);
  return new VideoFrame(buf, {
    format: 'RGBA',
    codedWidth: width,
    codedHeight: height,
    timestamp,
  });
}

function assertColorSpace(actual, expected, message = '') {
  assert_not_equals(actual, undefined, `${message} colorSpace emitted`);
  assert_equals(actual.primaries, expected.primaries, `${message} primaries`);
  assert_equals(actual.transfer, expected.transfer, `${message} transfer`);
  assert_equals(actual.matrix, expected.matrix, `${message} matrix`);
  assert_equals(actual.fullRange, expected.fullRange, `${message} fullRange`);
}

function makeEncoder(t, output) {
  const encoder = new VideoEncoder({
    output,
    error: t.unreached_func('encoder error'),
  });
  t.add_cleanup(() => {
    try {
      encoder.close();
    } catch (e) {}
  });
  return encoder;
}

function encodeAndClose(encoder, frame, options) {
  encoder.encode(frame, options);
  frame.close();
}

promise_test(async t => {
  const config = makeConfig();
  await checkEncoderSupport(t, config);

  let emitted;
  const encoder = makeEncoder(t, (chunk, metadata) => {
    if (metadata.decoderConfig && emitted === undefined) {
      emitted = metadata.decoderConfig.colorSpace;
    }
  });

  encoder.configure(config);
  encodeAndClose(
      encoder,
      makeI420Frame(config.width, config.height, INPUT_COLOR_SPACE),
      {keyFrame: true});
  await encoder.flush();

  assertColorSpace(emitted, INPUT_COLOR_SPACE);
}, `VideoEncoder decoderConfig.colorSpace reflects input frame colorSpace (${location.search})`);

promise_test(async t => {
  const config = makeConfig();
  await checkEncoderSupport(t, config);

  let emitted;
  const encoder = makeEncoder(t, (chunk, metadata) => {
    if (metadata.decoderConfig && emitted === undefined) {
      emitted = metadata.decoderConfig.colorSpace;
    }
  });

  encoder.configure(config);
  encodeAndClose(
      encoder,
      makeI420Frame(config.width, config.height, PARTIAL_COLOR_SPACE),
      {keyFrame: true});
  await encoder.flush();

  assertColorSpace(emitted, PARTIAL_COLOR_SPACE);
}, `VideoEncoder preserves unspecified color components (${location.search})`);

promise_test(async t => {
  const config = makeConfig();
  await checkEncoderSupport(t, config);

  const emitted = [];
  const encoder = makeEncoder(t, (chunk, metadata) => {
    if (metadata.decoderConfig) {
      emitted.push(metadata.decoderConfig.colorSpace);
    }
  });

  encoder.configure(config);
  encodeAndClose(
      encoder,
      makeI420Frame(config.width, config.height, INPUT_COLOR_SPACE, 0),
      {keyFrame: true});
  encodeAndClose(
      encoder,
      makeI420Frame(config.width, config.height, SECOND_COLOR_SPACE, 1));
  await encoder.flush();

  assert_equals(emitted.length, 2, 'decoderConfig emitted on color transition');
  assertColorSpace(emitted[0], INPUT_COLOR_SPACE, 'first config');
  assertColorSpace(emitted[1], SECOND_COLOR_SPACE, 'second config');
}, `VideoEncoder emits decoderConfig on a color transition (${location.search})`);

promise_test(async t => {
  const config = makeConfig();
  await checkEncoderSupport(t, config);

  const outputs = [];
  const encoder = makeEncoder(t, (chunk, metadata) => {
    outputs.push({
      timestamp: chunk.timestamp,
      colorSpace: metadata.decoderConfig?.colorSpace,
    });
  });

  encoder.configure(config);
  const colors = [
    INPUT_COLOR_SPACE,
    SECOND_COLOR_SPACE,
    INPUT_COLOR_SPACE,
    SECOND_COLOR_SPACE,
  ];
  for (const colorSpace of colors) {
    encodeAndClose(
        encoder,
        makeI420Frame(config.width, config.height, colorSpace, 7));
  }
  await encoder.flush();

  assert_equals(outputs.length, colors.length, 'all queued frames encoded');
  for (let i = 0; i < outputs.length; ++i) {
    assert_equals(outputs[i].timestamp, 7, `output ${i} retains caller timestamp`);
    assertColorSpace(outputs[i].colorSpace, colors[i], `output ${i}`);
  }
}, `VideoEncoder associates queued colors independently of duplicate timestamps (${location.search})`);

promise_test(async t => {
  const initialConfig = makeConfig();
  await checkEncoderSupport(t, initialConfig);

  const emitted = [];
  const encoder = makeEncoder(t, (chunk, metadata) => {
    if (metadata.decoderConfig) {
      emitted.push(metadata.decoderConfig);
    }
  });

  encoder.configure(initialConfig);
  encodeAndClose(
      encoder,
      makeI420Frame(320, 200, INPUT_COLOR_SPACE, 0),
      {keyFrame: true});
  await encoder.flush();

  encoder.configure(initialConfig);
  encodeAndClose(
      encoder,
      makeI420Frame(320, 200, INPUT_COLOR_SPACE, 1),
      {keyFrame: true});
  await encoder.flush();

  encoder.configure({...initialConfig, bitrate: 500000});
  encodeAndClose(
      encoder,
      makeI420Frame(320, 200, INPUT_COLOR_SPACE, 2),
      {keyFrame: true});
  await encoder.flush();

  const resizedConfig = makeConfig(160, 100);
  encoder.configure(resizedConfig);
  encodeAndClose(
      encoder,
      makeI420Frame(160, 100, INPUT_COLOR_SPACE, 3),
      {keyFrame: true});
  await encoder.flush();

  assert_equals(emitted.length, 4, 'configure requests a fresh decoderConfig');
  assert_array_equals(
      emitted.map(config => config.codedWidth),
      [320, 320, 320, 160],
      'decoderConfig reflects each active configuration');
  for (let i = 0; i < emitted.length; ++i) {
    assertColorSpace(emitted[i].colorSpace, INPUT_COLOR_SPACE, `config ${i}`);
  }
}, `VideoEncoder retains configure emission semantics with packet color (${location.search})`);

promise_test(async t => {
  const config = makeConfig();
  await checkEncoderSupport(t, config);

  let emitted;
  const encoder = makeEncoder(t, (chunk, metadata) => {
    if (metadata.decoderConfig && emitted === undefined) {
      emitted = metadata.decoderConfig.colorSpace;
    }
  });

  encoder.configure(config);
  encodeAndClose(encoder, makeRGBAFrame(config.width, config.height), {keyFrame: true});
  await encoder.flush();

  assertColorSpace(emitted, RGB_COMPATIBILITY_COLOR_SPACE);
}, `VideoEncoder preserves the existing RGB compatibility colorSpace (${location.search})`);
