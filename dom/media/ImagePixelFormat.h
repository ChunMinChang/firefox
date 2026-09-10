/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef DOM_MEDIA_IMAGEPIXELFORMAT_H_
#define DOM_MEDIA_IMAGEPIXELFORMAT_H_

#include "mozilla/DefineEnum.h"
#include "mozilla/Maybe.h"

namespace mozilla {

namespace layers {
class Image;
}  // namespace layers

// The sample layout of a layers::Image: I4xx[A][Pn] is packed planar YCbCr[A]
// at 4:2:0, 4:2:2 or 4:4:4 with n-bit samples in 16-bit words (8-bit bytes
// when Pn is absent), NV12 and NV21 interleave chroma Cb or Cr first, and the
// RGB members are 8-bit channels in memory order (X carries no alpha). The
// dom::VideoPixelFormat members come first, in its order, so WebCodecs
// converts them by value.
MOZ_DEFINE_ENUM_CLASS_WITH_BASE_AND_TOSTRING(
    ImagePixelFormat, uint8_t,
    (I420, I420P10, I420P12, I420A, I420AP10, I420AP12, I422, I422P10, I422P12,
     I422A, I422AP10, I422AP12, I444, I444P10, I444P12, I444A, I444AP10,
     I444AP12, NV12, RGBA, RGBX, BGRA, BGRX, NV21));

// The layout of the samples aImage yields, from metadata alone; a remote image
// is named by its readback. Nothing() when the layout has no name.
Maybe<ImagePixelFormat> ImageToPixelFormat(layers::Image* aImage);

bool IsRGB(ImagePixelFormat aFormat);

}  // namespace mozilla

#endif  // DOM_MEDIA_IMAGEPIXELFORMAT_H_
