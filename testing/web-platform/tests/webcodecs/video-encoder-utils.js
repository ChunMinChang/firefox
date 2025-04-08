async function checkEncoderSupport(test, config) {
  assert_equals("function", typeof VideoEncoder.isConfigSupported);
  let supported = false;
  try {
    const support = await VideoEncoder.isConfigSupported(config);
    supported = support.supported;
  } catch (e) {}

  assert_implements_optional(supported, 'Unsupported config: ' +
                             JSON.stringify(config));
}

function fourColorsFrame(ctx, width, height, text) {
  const kYellow = "#FFFF00";
  const kRed = "#FF0000";
  const kBlue = "#0000FF";
  const kGreen = "#00FF00";

  ctx.fillStyle = kYellow;
  ctx.fillRect(0, 0, width / 2, height / 2);

  ctx.fillStyle = kRed;
  ctx.fillRect(width / 2, 0, width / 2, height / 2);

  ctx.fillStyle = kBlue;
  ctx.fillRect(0, height / 2, width / 2, height / 2);

  ctx.fillStyle = kGreen;
  ctx.fillRect(width / 2, height / 2, width / 2, height / 2);

  ctx.fillStyle = 'white';
  ctx.font = (height / 10) + 'px sans-serif';
  ctx.fillText(text, width / 2, height / 2);
}

// Paints |count| black dots on the |ctx|, so their presence can be validated
// later. This is an analog of the most basic bar code.
function putBlackDots(ctx, width, height, count) {
  ctx.fillStyle = 'black';
  const dot_size = 20;
  const step = dot_size * 2;

  for (let i = 1; i <= count; i++) {
    let x = i * step;
    let y = step * (x / width + 1);
    x %= width;
    ctx.fillRect(x, y, dot_size, dot_size);
  }
}

// Validates that frame has |count| black dots in predefined places.
function validateBlackDots(frame, count) {
  const width = frame.displayWidth;
  const height = frame.displayHeight;
  let cnv = new OffscreenCanvas(width, height);
  var ctx = cnv.getContext('2d', {willReadFrequently: true});
  ctx.drawImage(frame, 0, 0);
  const dot_size = 20;
  const step = dot_size * 2;

  for (let i = 1; i <= count; i++) {
    let x = i * step + dot_size / 2;
    let y = step * (x / width + 1) + dot_size / 2;
    x %= width;

    if (x)
      x = x -1;
    if (y)
      y = y -1;

    let rgba = ctx.getImageData(x, y, 2, 2).data;
    const tolerance = 60;
    if ((rgba[0] > tolerance || rgba[1] > tolerance || rgba[2] > tolerance)
      && (rgba[4] > tolerance || rgba[5] > tolerance || rgba[6] > tolerance)
      && (rgba[8] > tolerance || rgba[9] > tolerance || rgba[10] > tolerance)
      && (rgba[12] > tolerance || rgba[13] > tolerance || rgba[14] > tolerance)) {
      // The dot is too bright to be a black dot.
      return false;
    }
  }
  return true;
}

function createFrame(width, height, ts = 0, additionalOptions = {}) {
  let duration = 33333;  // 30fps
  let text = ts.toString();
  let cnv = new OffscreenCanvas(width, height);
  var ctx = cnv.getContext('2d');
  fourColorsFrame(ctx, width, height, text);

  // Merge the default options with the provided additionalOptions
  const videoFrameOptions = {
    timestamp: ts,
    duration,
    ...additionalOptions, // Spread the additional options to merge them
  };

  return new VideoFrame(cnv, videoFrameOptions);
}

function createDottedFrame(width, height, dots, ts) {
  if (ts === undefined)
    ts = dots;
  let duration = 33333;  // 30fps
  let text = ts.toString();
  let cnv = new OffscreenCanvas(width, height);
  var ctx = cnv.getContext('2d');
  fourColorsFrame(ctx, width, height, text);
  putBlackDots(ctx, width, height, dots);
  return new VideoFrame(cnv, { timestamp: ts, duration });
}

function createVideoEncoder(t, callbacks) {
  return new VideoEncoder({
    output(chunk, metadata) {
      if (callbacks && callbacks.output) {
        t.step(() => callbacks.output(chunk, metadata));
      } else {
        t.unreached_func('unexpected output()');
      }
    },
    error(e) {
      if (callbacks && callbacks.error) {
        t.step(() => callbacks.error(e));
      } else {
        t.unreached_func('unexpected error()');
      }
    }
  });
}

function RGBToYUV(rgb) {
  let y = rgb[0] * 0.299 + rgb[1] * 0.587 + rgb[2] * 0.114;
  let u = rgb[0] * -0.168736 + rgb[1] * -0.331264 + rgb[2] * 0.5 + 128;
  let v = rgb[0] * 0.5 + rgb[1] * -0.418688 + rgb[2] * -0.081312 + 128;

  y = Math.floor(y);
  u = Math.floor(u);
  v = Math.floor(v);
  return [y, u, v];
}

const RGB_FOUR_COLORS = [
  [255, 255, 0], // Yellow
  [255, 0, 0], // Red
  [0, 255, 0], // Green
  [0, 0, 255], // Blue
];

const YUV_FOUR_COLORS = RGB_FOUR_COLORS.map((color) => RGBToYUV(color));

function createFourColorsImageData(format, width, height) {
  if (!isFiniteNumber(width) || !isFiniteNumber(height)) {
    return undefined;
  }
  if (format.toLowerCase() == "rgbx") {
    return createRGB32ImageData(width, height, RGB_FOUR_COLORS);
  }
  if (format.toLowerCase() == "i420") {
    return createI420ImageData(width, height, YUV_FOUR_COLORS);
  }
  return undefined;
}

function createRGB32ImageData(width, height, fourColors) {
  let buffer = new Uint8Array(width * height * 4);
  for (let i of [0, 1, 2]) {
    for (let y = 0; y < height; ++y) {
      for (let x = 0; x < width; ++x) {
        const offset = (y * width + x) * 4;
        buffer[i + offset] = getQuadrantColor(x, y, width, height, fourColors, i);
      }
    }
  }
  return buffer;
}

function createI420ImageData(width, height, fourColors) {
  const halfWidth = Math.floor((width + 1) / 2);
  const halfHeight = Math.floor((height + 1) / 2);
  return createYUVImageData(
    [
      [width, height],
      [halfWidth, halfHeight],
      [halfWidth, halfHeight],
    ],
    fourColors
  );
}

function createYUVImageData(planeDimensions, fourColors) {
  let buffer = new Uint8Array(
    planeDimensions.reduce((acc, [width, height]) => acc + width * height, 0)
  );

  let index = 0;
  planeDimensions.forEach(([planeWidth, planeHeight], i) => {
    for (let y = 0; y < planeHeight; ++y) {
      for (let x = 0; x < planeWidth; ++x) {
        buffer[index] = getQuadrantColor(x, y, planeWidth, planeHeight, fourColors, i);
        index += 1;
      }
    }
  });
  return buffer;
}

// This function determines which quadrant of a rectangle (width * height)
// a point (x, y) falls into, and returns the corresponding color for that
// quadrant. The rectangle is divided into four quadrants:
//    <        w        >
//  ^ +--------+--------+
//    | (0, 0) | (1, 0) |
//  h +--------+--------+
//    | (0, 1) | (1, 1) |
//  v +--------+--------+
//
// The colors array must contain at least four colors, each corresponding
// to one of the quadrants:
// - colors[0] : top-left (0, 0)
// - colors[1] : top-right (1, 0)
// - colors[2] : bottom-left (0, 1)
// - colors[3] : bottom-right (1, 1)
//
// The channel parameter specifies which color channel to return:
// - For RGB colors, channel = 0, 1, or 2 corresponds to R, G, or B.
// - For YUV colors, channel = 0, 1, or 2 corresponds to Y, U, or V.
function getQuadrantColor(x, y, width, height, colors, channel) {
  // Determine which quadrant (x, y) belongs to.
  const xIndex = x * 2 >= width ? 1 : 0;
  const yIndex = y * 2 >= height ? 1 : 0;

  const index = yIndex * 2 + xIndex;
  return colors[index][channel];
}

function isFiniteNumber(value) {
  return typeof value === "number" && isFinite(value);
}
