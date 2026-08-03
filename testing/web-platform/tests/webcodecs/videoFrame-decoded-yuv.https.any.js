// META: global=window,dedicatedworker

const CONFIG = {
  codec: 'avc1.42e00a',
  codedWidth: 64,
  codedHeight: 64,
  hardwareAcceleration: 'prefer-software',
};

async function decodeFrame(t) {
  const frames = [];
  const decoder = new VideoDecoder({
    output(frame) {
      frames.push(frame);
    },
    error: t.unreached_func('decoder error'),
  });
  t.add_cleanup(() => {
    if (decoder.state !== 'closed') {
      decoder.close();
    }
    for (const frame of frames) {
      frame.close();
    }
  });

  const support = await VideoDecoder.isConfigSupported(CONFIG);
  assert_implements_optional(support.supported, CONFIG.codec + ' unsupported');

  const response = await fetch('h264-limited-range.annexb');
  assert_true(response.ok, 'fixture loaded');
  decoder.configure(CONFIG);
  decoder.decode(new EncodedVideoChunk({
    type: 'key',
    timestamp: 0,
    data: await response.arrayBuffer(),
  }));
  await decoder.flush();

  assert_equals(frames.length, 1, 'one decoded frame');
  return frames[0];
}

function assertPlaneEquals(actual, actualOffset, actualStride,
                           expected, expectedOffset, expectedStride,
                           width, height, description) {
  for (let y = 0; y < height; ++y) {
    for (let x = 0; x < width; ++x) {
      assert_equals(
          actual[actualOffset + y * actualStride + x],
          expected[expectedOffset + y * expectedStride + x],
          `${description} (${x}, ${y})`);
    }
  }
}

promise_test(async t => {
  const frame = await decodeFrame(t);

  assert_equals(frame.format, 'I420', 'decoded semantic format');
  assert_equals(frame.allocationSize(), 6144, 'full I420 allocation size');

  const full = new Uint8Array(frame.allocationSize());
  const fullLayout = await frame.copyTo(full);
  assert_array_equals(fullLayout.map(p => p.offset), [0, 4096, 5120],
                      'full plane offsets');
  assert_array_equals(fullLayout.map(p => p.stride), [64, 32, 32],
                      'full plane strides');
  assert_greater_than(new Set(full.subarray(0, 4096)).size, 1,
                      'luma contains decoded pixels');

  const repeated = new Uint8Array(full.length);
  await frame.copyTo(repeated);
  assert_array_equals(repeated, full, 'repeated copy is stable');

  const rect = {x: 2, y: 2, width: 4, height: 4};
  const options = {
    rect,
    layout: [
      {offset: 3, stride: 6},
      {offset: 30, stride: 4},
      {offset: 40, stride: 4},
    ],
  };
  const cropped = new Uint8Array(frame.allocationSize(options)).fill(0xcd);
  const croppedLayout = await frame.copyTo(cropped, options);
  assert_array_equals(croppedLayout.map(p => p.offset), [3, 30, 40],
                      'cropped plane offsets');
  assert_array_equals(croppedLayout.map(p => p.stride), [6, 4, 4],
                      'cropped plane strides');

  assertPlaneEquals(cropped, 3, 6, full, 2 * 64 + 2, 64, 4, 4, 'Y plane');
  assertPlaneEquals(cropped, 30, 4, full, 4096 + 32 + 1, 32, 2, 2,
                    'U plane');
  assertPlaneEquals(cropped, 40, 4, full, 5120 + 32 + 1, 32, 2, 2,
                    'V plane');

  await promise_rejects_js(
      t, TypeError, frame.copyTo(new Uint8Array(full.length - 1)),
      'undersized destination');
}, 'Remote-decoded VideoFrame exposes and copies its I420 planes');
