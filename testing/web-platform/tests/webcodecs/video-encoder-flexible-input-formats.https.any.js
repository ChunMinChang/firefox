// META: global=window,dedicatedworker
// META: script=/webcodecs/video-encoder-utils.js
// META: variant=?av1
// META: variant=?vp8
// META: variant=?vp9_p0
// META: variant=?h264_avc
// META: variant=?h264_annexb

let BASECONFIG = null;
promise_setup(async () => {
  const config = {
    '?av1': { codec: 'av01.0.04M.08' },
    '?vp8': { codec: 'vp8' },
    '?vp9_p0': { codec: 'vp09.00.10.08' },
    '?h264_avc': { codec: 'avc1.42001E', avc: { format: 'avc' } },
    '?h264_annexb': { codec: 'avc1.42001E', avc: { format: 'annexb' } },
  }[location.search];
  BASECONFIG = config;
  BASECONFIG.framerate = 30;
  BASECONFIG.bitrate = 3000000;
  BASECONFIG.width = 100;
  BASECONFIG.height = 120;
});

function createVideoFrame(format, width, height, timestamp, colorSpace) {
  const data = createFourColorsImageData(format, width, height);
  let init = {
    format,
    codedWidth: width,
    codedHeight: height,
    timestamp,
  };
  if (colorSpace) {
    init.colorSpace = colorSpace;
  }
  return new VideoFrame(data, init);
}

function generateAllCombinations(colorSpaceSets) {
  const keys = Object.keys(colorSpaceSets);
  let colorSpaces = [];
  generateAllCombinationsHelper(keys, 0, {}, colorSpaces, colorSpaceSets);
  return colorSpaces;
}

function generateAllCombinationsHelper(
  keys,
  keyIndex,
  colorSpace,
  results,
  colorSpaceSets
) {
  if (keyIndex >= keys.length) {
    // Push the copied object since the colorSpace will be reused.
    results.push(Object.assign({}, colorSpace));
    return;
  }

  const prop = keys[keyIndex];
  // case 1: Skip this property.
  generateAllCombinationsHelper(
    keys,
    keyIndex + 1,
    colorSpace,
    results,
    colorSpaceSets
  );
  // case 2: Set this property with a valid value.
  for (const val of colorSpaceSets[prop]) {
    colorSpace[prop] = val;
    generateAllCombinationsHelper(
      keys,
      keyIndex + 1,
      colorSpace,
      results,
      colorSpaceSets
    );
    delete colorSpace[prop];
  }
}

function generateVideoFrames(width, height, formats, colorSpaces) {
  const frames = [];
  let timestamp = 0;
  for (const format of formats) {
    colorSpaces.forEach((colorSpace) => {
      frames.push(
        createVideoFrame(format, width, height, timestamp++, colorSpace)
      );
    });
  }
  return frames;
}

function test_flexible_input_formats(latencyMode) {
  const colorSpaceSets = {
    primaries: ["bt709"],
    transfer: ["bt709"],
    matrix: ["bt709"],
    fullRange: [true, false],
  };
  const colorSpaces = generateAllCombinations(colorSpaceSets);
  const formats = ["I420", "RGBX"];

  promise_test(async (t) => {
    const cfg = { ...BASECONFIG, latencyMode };
    const sourceFrames = generateVideoFrames(
      cfg.width,
      cfg.height,
      formats,
      colorSpaces
    );

    let encodedResults = [];
    const encoder = new VideoEncoder({
      output: (chunk, metadata) => {
        encodedResults.push({ chunk, metadata });
      },
      error: (error) => {
        assert_unreached(error.message);
      },
    });

    encoder.configure(cfg);
    for (const frame of sourceFrames) {
      encoder.encode(frame);
    }
    await encoder.flush();

    assert_equals(encodedResults.length, sourceFrames.length);
    assert_true(encodedResults[0].hasOwnProperty("metadata"));
    assert_true(encodedResults[0].metadata.hasOwnProperty("decoderConfig"));

    let decodedResults = [];
    const decoder = new VideoDecoder({
      output(frame) {
        decodedResults.push(frame);
      },
      error: (error) => {
        assert_unreached(error.message);
      },
    });

    for (let result of encodedResults) {
      assert_true(result.hasOwnProperty("chunk"));
      assert_not_equals(decoder.state, "closed");
      // configure decoder if needed
      if (result.metadata && result.metadata.decoderConfig) {
        if (decoder.state == "configured") {
          await decoder.flush();
        }
        decoder.configure(result.metadata.decoderConfig);
      }
      decoder.decode(result.chunk);
    }
    await decoder.flush();

    assert_equals(decodedResults.length, encodedResults.length);

    for (let i = 0; i < sourceFrames.length; ++i) {
      assert_equals(sourceFrames[i].codedWidth, decodedResults[i].codedWidth);
      assert_equals(
        sourceFrames[i].codedHeight,
        decodedResults[i].codedHeight
      );
      assert_equals(
        sourceFrames[i].displayWidth,
        decodedResults[i].displayWidth
      );
      assert_equals(
        sourceFrames[i].displayHeight,
        decodedResults[i].displayHeight
      );
      assert_equals(sourceFrames[i].timestamp, decodedResults[i].timestamp);

      sourceFrames[i].close();
      decodedResults[i].close();
    }
  }, `Verify ${latencyMode} video encoder accepts multiple input formats`);
}

test_flexible_input_formats("quality");
