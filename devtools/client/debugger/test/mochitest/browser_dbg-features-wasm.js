/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at <http://mozilla.org/MPL/2.0/>. */

/**
 * This test covers all specifics of debugging WASM/WebAssembly files.
 *
 * WebAssembly is a binary file format for the Web.
 * The binary files are loaded by Gecko and machine code runs.
 * In order to debug them ThreadConfiguration's `observeWasm` is set to true,
 * only once the debugger is opened. This will set DebuggerAPI's `allowUnobservedWasm` to false.
 * Then, the engine will compute a different machine code with debugging instruction.
 * This will use a lot more memory, but allow debugger to set breakpoint and break on WASM sources.
 *
 * This behavior introduces some limitations when opening the debugger against
 * and already loaded page. The WASM file won't be debuggable.
 */

"use strict";

add_task(async function () {
  // Load the test page before opening the debugger so that WASM are built
  // without debugging instructions. Opening the console still doesn't enable debugging instructions.
  const tab = await addTab(EXAMPLE_URL + "doc-wasm-sourcemaps.html");
  const toolbox = await openToolboxForTab(tab, "webconsole");

  // Reload once again, while the console is opened.
  // When opening the debugger, it will still miss the source content.
  // To see the sources, we have to reload while the debugger has been opened.
  await reloadBrowser();

  await toolbox.selectTool("jsdebugger");
  const dbg = createDebuggerContext(toolbox);

  // When opening against an already loaded page, WASM source loading doesn't work
  // And because of that we don't see sourcemap/original files.
  await waitForSourcesInSourceTree(dbg, [
    "doc-wasm-sourcemaps.html",
    "fib.wasm",
  ]);
  is(dbg.selectors.getSourceCount(), 2, "There are only these two sources");

  const source = findSource(dbg, "fib.wasm");
  is(source.isWasm, true, "The original source is flagged as Wasm source");

  // Note that there is no point in asserting breakable lines,
  // as we aren't fetching any source.
  await dbg.actions.selectLocation(createLocation({ source }), {
    keepContext: false,
  });
  is(getEditorContent(dbg), `Please refresh to debug this module`);

  info("Reload and assert that WASM files are then debuggable");
  await reload(dbg, "doc-wasm-sourcemaps.html", "fib.wasm", "fib.c");

  info("After reloading, original file lines are breakable");
  // Ensure selecting the source before asserting breakable lines
  // otherwise the gutter may not be yet updated
  await selectSource(dbg, "fib.c");
  await assertLineIsBreakable(dbg, source.url, 14, true);

  await waitForSourcesInSourceTree(dbg, [
    "doc-wasm-sourcemaps.html",
    "fib.wasm",
    "fib.c",
  ]);
  is(dbg.selectors.getSourceCount(), 3, "There is all these 3 sources");
  // (even if errno_location.c is still not functional)

  // The line in the original C file, where the for() loop starts
  const breakpointLine = 12;
  assertTextContentOnLine(dbg, breakpointLine, "for (i = 0; i < n; i++) {");

  info("Register and trigger a breakpoint from the original source in C");
  await addBreakpoint(dbg, "fib.c", breakpointLine);
  invokeInTab("runWasm");

  await waitForPausedInOriginalFileAndToggleMapScopes(dbg);

  await assertPausedAtSourceAndLine(
    dbg,
    findSource(dbg, "fib.c").id,
    breakpointLine
  );
  await assertBreakpoint(dbg, breakpointLine);
  // Capture the generated location line, so that we can better report
  // when the binary code changed later in this test
  const frames = dbg.selectors.getCurrentThreadFrames();
  const generatedLine = frames[0].generatedLocation.line;

  assertFirstFrameTitleAndLocation(dbg, "(wasmcall)", "fib.c");

  await removeBreakpoint(dbg, findSource(dbg, "fib.c").id, breakpointLine);
  await resume(dbg);

  info(
    "Now register and trigger the same breakpoint from the binary source file"
  );
  const binarySource = findSource(dbg, "fib.wasm");

  // There is two lines, the hexadecimal one is the "virtual line" displayed in the gutter.
  // While the decimal one is the line where the line appear in CodeMirror.
  // So while we set the breakpoint on the decimal line in CodeMirror gutter,
  // internaly, the engine sets the breakpoint on the "virtual line".
  const virtualBinaryLine = 0x11a;
  is(
    "0x" + virtualBinaryLine.toString(16),
    "0x" + generatedLine.toString(16),
    "The hardcoded binary line (0x" +
      generatedLine.toString(16) +
      ") matches the mapped location when we set the breakpoint on the original line. If you rebuilt the binary, you may just need to update the virtualBinaryLine variable to the new location."
  );

  const binaryLine = wasmOffsetToLine(dbg, binarySource.id, virtualBinaryLine);

  await dbg.actions.selectLocation(createLocation({ source: binarySource }), {
    keepContext: false,
  });

  // Make sure line is within viewport
  await scrollEditorIntoView(dbg, binaryLine, 0);
  await assertLineIsBreakable(dbg, binarySource.url, binaryLine, true);

  await addBreakpoint(dbg, binarySource, virtualBinaryLine);
  invokeInTab("runWasm");

  // We can't use waitForPaused test helper as the text content isn't displayed correctly
  // so only assert that we are in paused state.
  await waitForPaused(dbg);
  // We don't try to assert paused line as there is two types of line in wasm
  await assertPausedAtSourceAndLine(dbg, binarySource.id, virtualBinaryLine);

  // Switch to original source
  info(
    "Manually switch to original C source as we set the breakpoint on binary source, we paused on it"
  );
  await dbg.actions.jumpToMappedSelectedLocation();

  // But once we switch to original source, we should have the original text content and be able
  // to do all classic assertions for paused state.
  await waitForPausedInOriginalFileAndToggleMapScopes(dbg);

  await assertPausedAtSourceAndLine(
    dbg,
    findSource(dbg, "fib.c").id,
    breakpointLine
  );

  info("Reselect the binary source");
  await dbg.actions.selectLocation(createLocation({ source: binarySource }), {
    keepContext: false,
  });

  assertFirstFrameTitleAndLocation(dbg, "(wasmcall)", "fib.wasm");

  // We can't use this method as it uses internaly the breakpoint line, which isn't the line in CodeMirror
  // await assertPausedAtSourceAndLine(dbg, binarySource.id, binaryLine);
  await assertBreakpoint(dbg, binaryLine);

  await removeBreakpoint(dbg, binarySource.id, virtualBinaryLine);
  await resume(dbg);
});

function assertFirstFrameTitleAndLocation(dbg, title, location) {
  const frames = findAllElements(dbg, "frames");
  const firstFrameTitle = frames[0].querySelector(".title").textContent;
  is(firstFrameTitle, title, "First frame title is the expected one");
  const firstFrameLocation = frames[0].querySelector(".location").textContent;
  is(
    firstFrameLocation.includes(location),
    true,
    `First frame location '${firstFrameLocation}' includes '${location}'`
  );
}
