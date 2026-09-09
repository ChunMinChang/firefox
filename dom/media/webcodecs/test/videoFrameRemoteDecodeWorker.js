/**
 * Runs the remote-decode checks in a dedicated worker and forwards every
 * assertion to the page, which replays it.
 */
/* global importScripts */
function ok(condition, message) {
  postMessage({ type: "ok", condition: !!condition, message });
}
function is(actual, expected, message) {
  postMessage({ type: "is", actual, expected, message });
}
function info(message) {
  postMessage({ type: "info", message });
}

importScripts("videoFrameFormatChecks.js", "videoFrameRemoteDecode.js");

onmessage = async ({ data: { os } }) => {
  try {
    for (const fixture of FIXTURES) {
      await testFixture(
        fixture,
        EXPECTED_FORMATS[os][fixture.name],
        `${fixture.name} (RDD, worker)`
      );
    }
  } catch (e) {
    ok(false, `worker threw: ${e}`);
  }
  postMessage({ type: "done" });
};
