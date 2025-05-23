/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

"use strict";

let mockProvider;
add_setup(async function () {
  mockProvider = new MockProvider(["mlmodel"]);
});

/**
 * Test that model hub provider is conditionally shown in the sidebar.
 */
add_task(async function testModelHubProvider() {
  let win = await loadInitialView("extension");
  let modelHubCategory = win.document
    .getElementById("categories")
    .getButtonByName("mlmodel");

  ok(modelHubCategory.hidden, "Model hub category is hidden");

  await closeView(win);
  mockProvider.createAddons([
    {
      id: "mockmodel1@tests.mozilla.org",
      name: "Model Mock 1",
      permissions: AddonManager.PERM_CAN_UNINSTALL,
      type: "mlmodel",
    },
    {
      id: "mockmodel2@tests.mozilla.org",
      name: "Model Mock 2",
      permissions: AddonManager.PERM_CAN_UNINSTALL,
      type: "mlmodel",
    },
  ]);
  win = await loadInitialView("extension");
  let doc = win.document;
  modelHubCategory = doc
    .getElementById("categories")
    .getButtonByName("mlmodel");

  ok(!modelHubCategory.hidden, "Model hub category is shown");

  let mlmodelLoaded = waitForViewLoad(win);
  modelHubCategory.click();
  await mlmodelLoaded;

  let enabledSection = getSection(doc, "mlmodel-enabled-section");
  is(
    enabledSection.children.length,
    2,
    "Got the expected number of mlmodel entries"
  );
  ok(
    !enabledSection.querySelector(".list-section-heading"),
    "No heading for mlmodel"
  );
  is(
    enabledSection.previousSibling.localName,
    "mlmodel-list-intro",
    "Model hub custom section is shown"
  );

  let promiseListUpdated = BrowserTestUtils.waitForEvent(
    enabledSection.closest("addon-list"),
    "remove"
  );
  info("Uninstall one of the mlmodel entries");
  const mlmodelmock2 = await AddonManager.getAddonByID(
    "mockmodel2@tests.mozilla.org"
  );

  await mlmodelmock2.uninstall();
  info("Wait for the list of mlmodel entries to be updated");
  await promiseListUpdated;

  enabledSection = getSection(doc, "mlmodel-enabled-section");
  is(
    enabledSection.children.length,
    1,
    "Got the expected number of mlmodel entries"
  );

  promiseListUpdated = BrowserTestUtils.waitForEvent(
    enabledSection.closest("addon-list"),
    "remove"
  );
  info("Uninstall the last one of the mlmodel entries");
  const mlmodelmock1 = await AddonManager.getAddonByID(
    "mockmodel1@tests.mozilla.org"
  );

  await mlmodelmock1.uninstall();
  info("Wait for the list of mlmodel entries to be updated");
  await promiseListUpdated;

  enabledSection = getSection(doc, "mlmodel-enabled-section");
  is(
    enabledSection.children.length,
    0,
    "Expect mlmodel add-ons list view to be empty"
  );

  let emptyMessageFindModeOnAMO = doc.querySelector("#empty-addons-message");
  is(
    emptyMessageFindModeOnAMO,
    null,
    "Expect no #empty-addons-message element in the empty mlmodel list view"
  );

  await closeView(win);
});

/**
 * Test model hub card in the list view.
 */
add_task(async function testModelHubCard() {
  const id1 = "mockmodel1-without-size@tests.mozilla.org";
  const id2 = "mockmodel2-with-size@tests.mozilla.org";

  mockProvider.createAddons([
    {
      id: id1,
      name: "Model Mock 1",
      permissions: AddonManager.PERM_CAN_UNINSTALL,
      type: "mlmodel",
      totalSize: undefined,
    },
    {
      id: id2,
      name: "Model Mock 2",
      permissions: AddonManager.PERM_CAN_UNINSTALL,
      type: "mlmodel",
      totalSize: 5 * 1024 * 1024,
    },
  ]);

  let win = await loadInitialView("mlmodel");

  // Card No Size
  let card1 = getAddonCard(win, id1);
  ok(card1, `Found addon card for model ${id1}`);
  verifyAddonCard(card1, "0 bytes");

  // Card With Size
  let card2 = getAddonCard(win, id2);
  ok(card2, `Found addon card for model ${id2}`);
  verifyAddonCard(card2, "5.0 MB");

  await closeView(win);

  function verifyAddonCard(card, expectedTotalSize) {
    ok(!card.hasAttribute("expanded"), "The list card is not expanded");

    let mlmodelTotalSizeBubble = card.querySelector(
      ".mlmodel-total-size-bubble"
    );
    ok(mlmodelTotalSizeBubble, "Expect to see the mlmodel total size bubble");

    is(
      mlmodelTotalSizeBubble?.textContent.trim(),
      expectedTotalSize,
      "Got the expected total size text"
    );

    let mlmodelRemoveAddonButton = card.querySelector(
      ".mlmodel-remove-addon-button"
    );

    ok(
      !mlmodelRemoveAddonButton,
      "Expect to not see the mlmodel remove addon button"
    );

    ok(
      BrowserTestUtils.isVisible(card.optionsButton),
      "Expect the card options button to be visible in the list view"
    );
  }
});

/**
 * Test model hub expanded details.
 */
add_task(async function testModelHubDetails() {
  const id1 = "mockmodel1-without-size@tests.mozilla.org";
  const id2 = "mockmodel2-with-size@tests.mozilla.org";

  const mockModel1 = {
    id: id1,
    model: "hostname/org/model-mock-1",
    permissions: AddonManager.PERM_CAN_UNINSTALL,
    type: "mlmodel",
    totalSize: undefined,
    lastUsed: new Date("2023-10-01T12:00:00Z"),
    modelHomepageURL: "https://huggingface.co/org/model-mock-1",
    modelIconURL: "chrome://mozapps/skin/extensions/extensionGeneric.svg",
  };
  const mockModel2 = {
    id: id2,
    model: "hostname/org/model-mock-2",
    permissions: AddonManager.PERM_CAN_UNINSTALL,
    type: "mlmodel",
    totalSize: 5 * 1024 * 1024,
    lastUsed: new Date("2023-10-01T12:00:00Z"),
    modelHomepageURL: "https://huggingface.co/org/model-mock-2",
    modelIconURL: "", // testing that empty icon sets to defult svg
  };

  mockProvider.createAddons([mockModel1, mockModel2]);

  await verifyAddonCardDetails({
    id: id1,
    expectedTotalSize: "0 bytes",
    expectedLastUsed: mockModel1.lastUsed,
    expectedModelHomepageURL: mockModel1.modelHomepageURL,
    expectedModelIconURL:
      "chrome://mozapps/skin/extensions/extensionGeneric.svg",
  });
  await verifyAddonCardDetails({
    id: id2,
    expectedTotalSize: "5.0 MB",
    expectedLastUsed: mockModel2.lastUsed,
    expectedModelHomepageURL: mockModel2.modelHomepageURL,
    expectedModelIconURL:
      "chrome://mozapps/skin/extensions/extensionGeneric.svg",
  });

  async function verifyAddonCardDetails({
    id,
    expectedTotalSize,
    expectedLastUsed,
    expectedModelHomepageURL,
    expectedModelIconURL,
  }) {
    let win = await loadInitialView("mlmodel");
    // Get the list view card DOM element for the given addon id.
    let card = getAddonCard(win, id);
    ok(card, `Found addon card for model ${id}`);
    ok(!card.hasAttribute("expanded"), "list view card is not expanded");

    // Load the detail view.
    let loaded = waitForViewLoad(win);
    card.querySelector('[action="expand"]').click();
    await loaded;

    // Get the detail view card DOM element for the given addon id.
    card = getAddonCard(win, id);
    ok(card.hasAttribute("expanded"), "detail view card is expanded");

    let mlmodelTotalSizeBubble = card.querySelector(
      ".mlmodel-total-size-bubble"
    );
    ok(
      !mlmodelTotalSizeBubble,
      "Expect to not see the mlmodel total size bubble"
    );

    let mlmodelRemoveAddonButton = card.querySelector(
      ".mlmodel-remove-addon-button"
    );
    ok(
      mlmodelRemoveAddonButton,
      "Expect to see the mlmodel remove addon button"
    );

    ok(
      !card.querySelector(".addon-detail-mlmodel").hidden,
      "Expect to see the mlmodel details"
    );

    ok(
      BrowserTestUtils.isHidden(card.optionsButton),
      "Expect the card options button to be hidden in the detail view"
    );

    // Check the model total size
    let totalsizeEl = card.querySelector(".addon-detail-row-mlmodel-totalsize");
    ok(totalsizeEl, "Expect to see the total size");
    is(
      totalsizeEl?.querySelector("span")?.textContent.trim(),
      expectedTotalSize,
      "Got the expected total size text"
    );

    // Check the last used date
    let lastUsedEl = card.querySelector(".addon-detail-row-mlmodel-lastused");
    ok(lastUsedEl, "Expect to see the last used date");
    is(
      lastUsedEl?.querySelector("span")?.textContent.trim(),
      expectedLastUsed.toLocaleDateString(undefined, {
        year: "numeric",
        month: "long",
        day: "numeric",
      }),
      "Got the expected last used date text"
    );

    // Check the model card link
    let modelCardEl = card.querySelector(".addon-detail-row-mlmodel-modelcard");
    ok(modelCardEl, "Expect to see the model card link");
    let modelHomepageURL = modelCardEl.querySelector("a");
    ok(modelHomepageURL, "Expect to see the model card link element");
    is(
      modelHomepageURL?.href,
      expectedModelHomepageURL,
      "Got the expected model card link"
    );

    // Check the model version
    let versionEl = card.querySelector(".addon-detail-row-version");
    ok(versionEl, "Expect to see the model version");

    // Check the model image
    let iconSrc = card.querySelector(".card-heading-icon").getAttribute("src");
    ok(iconSrc, "Expected to see model card image src");
    is(iconSrc, expectedModelIconURL, "Got expected model card icon value");

    await closeView(win);
  }
});
