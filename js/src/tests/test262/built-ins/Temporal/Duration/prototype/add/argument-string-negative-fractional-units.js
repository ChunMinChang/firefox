// |reftest| shell-option(--enable-temporal) skip-if(!this.hasOwnProperty('Temporal')||!xulRuntime.shell) -- Temporal is not enabled unconditionally, requires shell-options
// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.add
description: Strings with fractional duration units are treated with the correct sign
includes: [temporalHelpers.js]
features: [Temporal]
---*/

const instance = new Temporal.Duration();

const resultHours = instance.add("-PT24.567890123H");
TemporalHelpers.assertDuration(resultHours, 0, 0, 0, 0, -24, -34, -4, -404, -442, -800, "negative fractional hours");

const resultMinutes = instance.add("-PT1440.567890123M");
TemporalHelpers.assertDuration(resultMinutes, 0, 0, 0, 0, 0, -1440, -34, -73, -407, -380, "negative fractional minutes");

reportCompare(0, 0);
