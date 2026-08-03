/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "GPUVideoImage.h"
#include "ImageContainer.h"
#include "gtest/gtest.h"
#include "mozilla/RemoteImageHolder.h"
#include "mozilla/gfx/Types.h"
#include "mozilla/ipc/Shmem.h"
#include "mozilla/layers/BufferTexture.h"
#include "mozilla/layers/ImageDataSerializer.h"
#include "mozilla/layers/LayersSurfaces.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::ipc;
using namespace mozilla::layers;

class TestGPUVideoSurfaceManager final : public IGPUVideoSurfaceManager {
 public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(TestGPUVideoSurfaceManager, override)

  already_AddRefed<SourceSurface> Readback(
      const SurfaceDescriptorGPUVideo&) override {
    return nullptr;
  }

  already_AddRefed<Image> ReadbackYCbCr(
      const SurfaceDescriptorGPUVideo&) override {
    return nullptr;
  }

  already_AddRefed<Image> TransferToImage(
      const SurfaceDescriptorGPUVideo&, const IntSize&,
      const ColorDepth& aColorDepth, YUVColorSpace aYUVColorSpace,
      ColorSpace2 aColorPrimaries, TransferFunction aTransferFunction,
      ColorRange aColorRange,
      Maybe<ChromaSubsampling> aChromaSubsampling) override {
    mTransferred = true;
    mColorDepth = aColorDepth;
    mYUVColorSpace = aYUVColorSpace;
    mColorPrimaries = aColorPrimaries;
    mTransferFunction = aTransferFunction;
    mColorRange = aColorRange;
    mChromaSubsampling = aChromaSubsampling;
    return nullptr;
  }

  void DeallocateSurfaceDescriptor(const SurfaceDescriptorGPUVideo&) override {}
  void OnSetCurrent(const SurfaceDescriptorGPUVideo&) override {}

  bool mTransferred = false;
  ColorDepth mColorDepth = ColorDepth::COLOR_8;
  YUVColorSpace mYUVColorSpace = YUVColorSpace::Default;
  ColorSpace2 mColorPrimaries = ColorSpace2::UNKNOWN;
  TransferFunction mTransferFunction = TransferFunction::BT709;
  ColorRange mColorRange = ColorRange::LIMITED;
  Maybe<ChromaSubsampling> mChromaSubsampling;

 private:
  ~TestGPUVideoSurfaceManager() = default;
};

static YCbCrDescriptor MakeInvalidDescriptor() {
  return YCbCrDescriptor(IntRect(0, 0, 4, 4), IntSize(1, 1), 4u, IntSize(1, 1),
                         4u, 8192u, 1u, 9192u, StereoMode::MONO,
                         ColorDepth::COLOR_8, YUVColorSpace::BT601,
                         ColorRange::LIMITED, TransferFunction::BT709,
                         ChromaSubsampling::HALF_WIDTH_AND_HEIGHT, Nothing());
}

static YCbCrDescriptor MakeValidDescriptor() {
  return YCbCrDescriptor(IntRect(0, 0, 4, 4), IntSize(4, 4), 4u, IntSize(2, 2),
                         2u, 0u, 16u, 20u, StereoMode::MONO,
                         ColorDepth::COLOR_8, YUVColorSpace::BT601,
                         ColorRange::LIMITED, TransferFunction::BT709,
                         ChromaSubsampling::HALF_WIDTH_AND_HEIGHT, Nothing());
}

static gfx::HDRMetadata MakeTestHDRMetadata() {
  gfx::HDRMetadata metadata;
  metadata.mContentLightLevel = Some(gfx::ContentLightLevel{1000, 400});
  return metadata;
}

static YCbCrDescriptor MakeMetadataDescriptor(
    const gfx::HDRMetadata& aHDRMetadata) {
  return YCbCrDescriptor(
      IntRect(0, 0, 4, 4), IntSize(4, 4), 4u, IntSize(2, 2), 2u, 0u, 16u, 20u,
      StereoMode::MONO, ColorDepth::COLOR_8, YUVColorSpace::BT2020,
      ColorRange::FULL, TransferFunction::PQ,
      ChromaSubsampling::HALF_WIDTH_AND_HEIGHT, Some(aHDRMetadata));
}

// Valid plane layout but display rect extends beyond ySize.
static YCbCrDescriptor MakeOversizedDisplayDescriptor() {
  return YCbCrDescriptor(IntRect(0, 0, 64, 200), IntSize(64, 2), 64u,
                         IntSize(32, 1), 32u, 0u, 128u, 160u, StereoMode::MONO,
                         ColorDepth::COLOR_8, YUVColorSpace::BT601,
                         ColorRange::LIMITED, TransferFunction::BT709,
                         ChromaSubsampling::HALF_WIDTH_AND_HEIGHT, Nothing());
}

// cbCrSize is smaller than what chromaSubsampling implies.
static YCbCrDescriptor MakeInvalidChromaDimensionsDescriptor() {
  return YCbCrDescriptor(IntRect(0, 0, 64, 2), IntSize(64, 2), 64u,
                         IntSize(64, 1), 64u, 0u, 128u, 192u, StereoMode::MONO,
                         ColorDepth::COLOR_8, YUVColorSpace::BT601,
                         ColorRange::LIMITED, TransferFunction::BT709,
                         ChromaSubsampling::FULL, Nothing());
}

TEST(TestRemoteImageHolder, InvalidDescriptorValidation)
{
  auto desc = MakeInvalidDescriptor();

  Maybe<uint32_t> descriptorSize = ImageDataSerializer::ComputeYCbCrBufferSize(
      desc.display(), desc.ySize(), desc.yStride(), desc.cbCrSize(),
      desc.cbCrStride(), desc.yOffset(), desc.cbOffset(), desc.crOffset(),
      desc.colorDepth(), desc.chromaSubsampling());

  ASSERT_TRUE(descriptorSize.isNothing());
}

TEST(TestRemoteImageHolder, ValidDescriptorPassesValidation)
{
  auto desc = MakeValidDescriptor();

  Maybe<uint32_t> descriptorSize = ImageDataSerializer::ComputeYCbCrBufferSize(
      desc.display(), desc.ySize(), desc.yStride(), desc.cbCrSize(),
      desc.cbCrStride(), desc.yOffset(), desc.cbOffset(), desc.crOffset(),
      desc.colorDepth(), desc.chromaSubsampling());

  ASSERT_GT(descriptorSize.extract(), 0u);
}

TEST(TestRemoteImageHolder, RejectsInvalidShmemDescriptor)
{
  auto shmemBuilder = Shmem::Builder(128);
  ASSERT_TRUE(shmemBuilder)
  << "Failed to create Shmem::Builder";

  auto [msg, shmem] = shmemBuilder.Build(1, false, 0);

  ASSERT_TRUE(shmem.IsWritable())
  << "Shmem should be writable";
  ASSERT_EQ(shmem.Size<uint8_t>(), 128u) << "Shmem should be 128 bytes";

  auto invalidDesc = MakeInvalidDescriptor();

  BufferDescriptor bufferDesc(invalidDesc);
  MemoryOrShmem memOrShmem(shmem);
  SurfaceDescriptorBuffer sdBuffer(bufferDesc, memOrShmem);
  SurfaceDescriptor sd(sdBuffer);

  RemoteImageHolder holder(std::move(sd));

  RefPtr<BufferRecycleBin> recycleBin = new BufferRecycleBin();

  RefPtr<layers::Image> image = holder.TransferToImage(recycleBin);

  EXPECT_TRUE(image == nullptr) << "RemoteImageHolder::TransferToImage should "
                                   "return null for invalid descriptors";
}

TEST(TestRemoteImageHolder, AcceptsValidShmemDescriptor)
{
  auto shmemBuilder = Shmem::Builder(128);
  ASSERT_TRUE(shmemBuilder)
  << "Failed to create Shmem::Builder";

  auto [msg, shmem] = shmemBuilder.Build(2, false, 0);

  ASSERT_TRUE(shmem.IsWritable())
  << "Shmem should be writable";
  ASSERT_EQ(shmem.Size<uint8_t>(), 128u) << "Shmem should be 128 bytes";

  auto validDesc = MakeValidDescriptor();

  uint8_t* buffer = shmem.get<uint8_t>();
  memset(buffer, 0, 128);

  BufferDescriptor bufferDesc(validDesc);
  MemoryOrShmem memOrShmem(shmem);
  SurfaceDescriptorBuffer sdBuffer(bufferDesc, memOrShmem);
  SurfaceDescriptor sd(sdBuffer);

  RemoteImageHolder holder(std::move(sd));

  RefPtr<BufferRecycleBin> recycleBin = new BufferRecycleBin();

  RefPtr<layers::Image> image = holder.TransferToImage(recycleBin);

  ASSERT_TRUE(image);
  const PlanarYCbCrImage* planarImage = image->AsPlanarYCbCrImage();
  ASSERT_TRUE(planarImage);
  const PlanarYCbCrData* data = planarImage->GetData();
  ASSERT_TRUE(data);
  EXPECT_EQ(data->mYUVColorSpace, YUVColorSpace::BT601);
  EXPECT_EQ(data->mColorPrimaries, ColorSpace2::UNKNOWN);
  EXPECT_EQ(data->mTransferFunction, TransferFunction::BT709);
  EXPECT_EQ(data->mColorRange, ColorRange::LIMITED);
  EXPECT_EQ(data->mHDRMetadata, Nothing());
}

TEST(TestRemoteImageHolder, PreservesShmemColorMetadata)
{
  auto shmemBuilder = Shmem::Builder(128);
  ASSERT_TRUE(shmemBuilder);

  auto [msg, shmem] = shmemBuilder.Build(5, false, 0);
  ASSERT_TRUE(shmem.IsWritable());

  const gfx::HDRMetadata hdrMetadata = MakeTestHDRMetadata();
  BufferDescriptor bufferDesc(MakeMetadataDescriptor(hdrMetadata));
  MemoryOrShmem memOrShmem(shmem);
  SurfaceDescriptorBuffer sdBuffer(bufferDesc, memOrShmem);
  SurfaceDescriptor sd(sdBuffer);

  RemoteImageHolder holder(nullptr, VideoBridgeSource::RddProcess,
                           IntSize(4, 4), ColorDepth::COLOR_8, sd,
                           YUVColorSpace::BT2020, ColorSpace2::BT2020,
                           TransferFunction::PQ, ColorRange::FULL, Nothing());
  RefPtr<BufferRecycleBin> recycleBin = new BufferRecycleBin();
  RefPtr<layers::Image> image = holder.TransferToImage(recycleBin);

  ASSERT_TRUE(image);
  const PlanarYCbCrImage* planarImage = image->AsPlanarYCbCrImage();
  ASSERT_TRUE(planarImage);
  const PlanarYCbCrData* data = planarImage->GetData();
  ASSERT_TRUE(data);
  EXPECT_EQ(data->mYUVColorSpace, YUVColorSpace::BT2020);
  EXPECT_EQ(data->mColorPrimaries, ColorSpace2::BT2020);
  EXPECT_EQ(data->mTransferFunction, TransferFunction::PQ);
  EXPECT_EQ(data->mColorRange, ColorRange::FULL);
  EXPECT_EQ(data->mHDRMetadata, Some(hdrMetadata));
}

TEST(TestRemoteImageHolder, ForwardsTextureColorMetadata)
{
  RefPtr<TestGPUVideoSurfaceManager> manager = new TestGPUVideoSurfaceManager();
  SurfaceDescriptorGPUVideo gpuDescriptor{SurfaceDescriptorRemoteDecoder()};
  SurfaceDescriptor sd(gpuDescriptor);

  RemoteImageHolder holder(manager, VideoBridgeSource::RddProcess,
                           IntSize(4, 4), ColorDepth::COLOR_10, sd,
                           YUVColorSpace::BT2020, ColorSpace2::BT2020,
                           TransferFunction::PQ, ColorRange::FULL,
                           Some(ChromaSubsampling::HALF_WIDTH_AND_HEIGHT));
  RefPtr<layers::Image> image = holder.TransferToImage();

  EXPECT_FALSE(image);
  EXPECT_TRUE(manager->mTransferred);
  EXPECT_EQ(manager->mColorDepth, ColorDepth::COLOR_10);
  EXPECT_EQ(manager->mYUVColorSpace, YUVColorSpace::BT2020);
  EXPECT_EQ(manager->mColorPrimaries, ColorSpace2::BT2020);
  EXPECT_EQ(manager->mTransferFunction, TransferFunction::PQ);
  EXPECT_EQ(manager->mColorRange, ColorRange::FULL);
  EXPECT_EQ(manager->mChromaSubsampling,
            Some(ChromaSubsampling::HALF_WIDTH_AND_HEIGHT));
}

TEST(TestRemoteImageHolder, RejectsOversizedDisplayRect)
{
  auto desc = MakeOversizedDisplayDescriptor();

  // ComputeYCbCrBufferSize itself rejects the oversized display rect, so size
  // a Shmem big enough to hold the planes from the descriptor's offsets.
  uint32_t descriptorSize =
      desc.crOffset() + desc.cbCrStride() * desc.cbCrSize().height;

  auto shmemBuilder = Shmem::Builder(descriptorSize);
  ASSERT_TRUE(shmemBuilder)
  << "Failed to create Shmem::Builder";

  auto [msg, shmem] = shmemBuilder.Build(3, false, 0);

  ASSERT_TRUE(shmem.IsWritable())
  << "Shmem should be writable";
  ASSERT_GE(shmem.Size<uint8_t>(), descriptorSize)
      << "Shmem should fit descriptor";

  memset(shmem.get<uint8_t>(), 0, descriptorSize);

  BufferDescriptor bufferDesc(desc);
  MemoryOrShmem memOrShmem(shmem);
  SurfaceDescriptorBuffer sdBuffer(bufferDesc, memOrShmem);
  SurfaceDescriptor sd(sdBuffer);

  RemoteImageHolder holder(std::move(sd));

  RefPtr<BufferRecycleBin> recycleBin = new BufferRecycleBin();

  RefPtr<layers::Image> image = holder.TransferToImage(recycleBin);

  EXPECT_TRUE(image == nullptr)
      << "TransferToImage should reject descriptor whose display rect exceeds "
         "plane dimensions";
}

TEST(TestRemoteImageHolder, RejectsInvalidChromaDimensions)
{
  auto desc = MakeInvalidChromaDimensionsDescriptor();

  // ComputeYCbCrBufferSize itself rejects mismatched chroma dimensions, so
  // size a Shmem big enough to hold the planes from the descriptor's offsets.
  uint32_t descriptorSize =
      desc.crOffset() + desc.cbCrStride() * desc.cbCrSize().height;

  auto shmemBuilder = Shmem::Builder(descriptorSize);
  ASSERT_TRUE(shmemBuilder)
  << "Failed to create Shmem::Builder";

  auto [msg, shmem] = shmemBuilder.Build(4, false, 0);

  ASSERT_TRUE(shmem.IsWritable())
  << "Shmem should be writable";
  ASSERT_GE(shmem.Size<uint8_t>(), descriptorSize)
      << "Shmem should fit descriptor";

  memset(shmem.get<uint8_t>(), 0, descriptorSize);

  BufferDescriptor bufferDesc(desc);
  MemoryOrShmem memOrShmem(shmem);
  SurfaceDescriptorBuffer sdBuffer(bufferDesc, memOrShmem);
  SurfaceDescriptor sd(sdBuffer);

  RemoteImageHolder holder(std::move(sd));

  RefPtr<BufferRecycleBin> recycleBin = new BufferRecycleBin();

  RefPtr<layers::Image> image = holder.TransferToImage(recycleBin);

  EXPECT_TRUE(image == nullptr)
      << "TransferToImage should reject descriptor with inconsistent chroma "
         "dimensions";
}
