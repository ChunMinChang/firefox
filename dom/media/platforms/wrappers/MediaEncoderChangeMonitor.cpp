#include "MediaEncoderChangeMonitor.h"

namespace mozilla {

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
MediaEncoderChangeMonitor::Create(PlatformEncoderModule* aPEM,
                                  const EncoderConfig& aConfig,
                                  const RefPtr<TaskQueue>& aTaskQueue) {
  // TODO: Implement this function
  return PlatformEncoderModule::CreateEncoderPromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

RefPtr<MediaDataEncoder::InitPromise> MediaEncoderChangeMonitor::Init() {
  // TODO: Implement this function
  return InitPromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::Encode(
    const MediaData* aSample) {
  // TODO: Implement this function
  return EncodePromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
MediaEncoderChangeMonitor::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  // TODO: Implement this function
  return ReconfigurationPromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::Drain() {
  // TODO: Implement this function
  return EncodePromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

RefPtr<ShutdownPromise> MediaEncoderChangeMonitor::Shutdown() {
  // TODO: Implement this function
  return ShutdownPromise::CreateAndReject(false, __func__);
}

RefPtr<GenericPromise> MediaEncoderChangeMonitor::SetBitrate(
    uint32_t aBitsPerSec) {
  // TODO: Implement this function
  return GenericPromise::CreateAndReject(
      MediaResult(NS_ERROR_NOT_IMPLEMENTED, "Not implemented"), __func__);
}

bool MediaEncoderChangeMonitor::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  // TODO: Implement this function
  return false;
}

nsCString MediaEncoderChangeMonitor::GetDescriptionName() const {
  // TODO: Implement this function
  return nsCString();
}

MediaEncoderChangeMonitor::MediaEncoderChangeMonitor(
    PlatformEncoderModule* aPEM, const EncoderConfig& aConfig,
    const RefPtr<TaskQueue>& aTaskQueue) {
  // TODO: Implement this constructor
}

}  // namespace mozilla
