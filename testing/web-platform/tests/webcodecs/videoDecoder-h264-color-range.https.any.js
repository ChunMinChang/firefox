// META: global=window,dedicatedworker

const CONFIG = {
  codec: 'avc1.42e00a',
  codedWidth: 64,
  codedHeight: 64,
  hardwareAcceleration: 'prefer-software',
};

const CASES = [
  {
    name: 'full-range',
    src: 'h264-full-range.annexb',
    fullRange: true,
  },
  {
    name: 'limited-range',
    src: 'h264-limited-range.annexb',
    fullRange: false,
  },
];

for (const testCase of CASES) {
  promise_test(async t => {
    const support = await VideoDecoder.isConfigSupported(CONFIG);
    assert_implements_optional(support.supported, CONFIG.codec + ' unsupported');

    const response = await fetch(testCase.src);
    assert_true(response.ok, 'fixture loaded');
    const data = new Uint8Array(await response.arrayBuffer());
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

    decoder.configure(CONFIG);
    decoder.decode(new EncodedVideoChunk({
      type: 'key',
      timestamp: 0,
      data,
    }));
    await decoder.flush();

    assert_equals(frames.length, 1, 'one decoded frame');
    const colorSpace = frames[0].colorSpace;
    assert_equals(colorSpace.primaries, 'bt709', 'primaries');
    assert_equals(colorSpace.transfer, 'bt709', 'transfer');
    assert_equals(colorSpace.matrix, 'bt709', 'matrix');
    assert_equals(colorSpace.fullRange, testCase.fullRange, 'fullRange');
  }, 'VideoDecoder reports ' + testCase.name + ' H.264 VUI color range');
}
