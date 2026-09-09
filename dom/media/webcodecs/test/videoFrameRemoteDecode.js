/**
 * Decodes the single-frame quadrants fixtures and checks the resulting
 * VideoFrame. Shared by test_videoFrame_remote_decode.html and its worker.
 */
/* global WIDTH, HEIGHT, checkFormatMatchesPlanes */
const FIXTURES = [
  {
    name: "vp9",
    file: "quadrants-vp9.ivf",
    codec: "vp09.00.10.08",
    chunk: parseSingleFrameIvf,
  },
  {
    name: "h264",
    file: "quadrants-h264.annexb",
    codec: "avc1.42001E",
    chunk: buffer => buffer,
  },
];

// The formats a software-decoded frame is expected to report per platform.
// VideoToolbox returns IOSurfaces even in software mode; they read back as RGB.
const EXPECTED_FORMATS = {
  Linux: { vp9: ["I420"], h264: ["I420"] },
  WINNT: { vp9: ["I420"], h264: ["I420"] },
  Darwin: { vp9: ["I420"], h264: ["BGRX"] },
};

function parseSingleFrameIvf(buffer) {
  const view = new DataView(buffer);
  is(
    String.fromCharCode(...new Uint8Array(buffer, 0, 4)),
    "DKIF",
    "IVF signature"
  );
  const headerSize = view.getUint16(6, true);
  const frameSize = view.getUint32(headerSize, true);
  return buffer.slice(headerSize + 12, headerSize + 12 + frameSize);
}

async function decodeFixture(fixture) {
  const config = {
    codec: fixture.codec,
    codedWidth: WIDTH,
    codedHeight: HEIGHT,
    hardwareAcceleration: "prefer-software",
  };
  const support = await VideoDecoder.isConfigSupported(config);
  if (!support.supported) {
    info(`${fixture.name}: ${fixture.codec} is not supported here`);
    return null;
  }

  const response = await fetch(fixture.file);
  ok(response.ok, `${fixture.name}: fixture loaded`);
  const data = fixture.chunk(await response.arrayBuffer());

  const frames = [];
  const errors = [];
  const decoder = new VideoDecoder({
    output: frame => frames.push(frame),
    error: e => errors.push(e),
  });
  decoder.configure(config);
  decoder.decode(new EncodedVideoChunk({ type: "key", timestamp: 0, data }));
  await decoder.flush();
  decoder.close();

  is(errors.length, 0, `${fixture.name}: no decoder errors`);
  is(frames.length, 1, `${fixture.name}: one decoded frame`);
  return frames[0];
}

async function testFixture(fixture, expectedFormats, label) {
  const frame = await decodeFixture(fixture);
  if (!frame) {
    return;
  }
  info(`${label}: format ${frame.format}`);
  ok(
    expectedFormats.includes(frame.format),
    `${label}: format ${frame.format} is one of ${expectedFormats}`
  );
  is(frame.codedWidth, WIDTH, `${label}: coded width`);
  is(frame.codedHeight, HEIGHT, `${label}: coded height`);
  await checkFormatMatchesPlanes(frame, label);
  frame.close();
}
