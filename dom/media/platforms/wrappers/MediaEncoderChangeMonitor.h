#ifndef DOM_MEDIA_PLATFORMS_WRAPPERS_MEDIAENCODERCHANGEMONITOR_H_
#define DOM_MEDIA_PLATFORMS_WRAPPERS_MEDIAENCODERCHANGEMONITOR_H_

#include "PlatformEncoderModule.h"

namespace mozilla {

// MediaEncoderChangeMonitor is a wrapper around MediaDataEncoder that tracks
// changes in the encoder's input format. It facilitates dynamic reconfiguration
// of the encoder without requiring manual shutdown at the callsite. This is
// particularly useful in scenarios where the encoder must adapt to varying
// input formats. For instance, WebCodecs' VideoEncoder allows encoding video
// frames with different pixel formats.
//
// Some platform encoders may internally convert the input format to a
// predefined format if they only support specific formats. However,
// AppleVTEncoder can be configured to accept multiple pixel formats and create
// a memory buffer pool for inputs, minimizing the need for image copies. In
// such cases, the encoder's input format should match its configured format.
// MediaEncoderChangeMonitor manages the encoder's reconfiguration in these
// situations.
class MediaEncoderChangeMonitor final : public MediaDataEncoder {
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(MediaEncoderChangeMonitor, final);

 public:
  static RefPtr<PlatformEncoderModule::CreateEncoderPromise> Create(
      PlatformEncoderModule* aPEM, const EncoderConfig& aConfig,
      const RefPtr<TaskQueue>& aTaskQueue);

  // MediaDataEncoder interfaces
  RefPtr<InitPromise> Init() override;
  RefPtr<EncodePromise> Encode(const MediaData* aSample) override;
  RefPtr<ReconfigurationPromise> Reconfigure(
      const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges)
      override;
  RefPtr<EncodePromise> Drain() override;
  RefPtr<ShutdownPromise> Shutdown() override;
  RefPtr<GenericPromise> SetBitrate(uint32_t aBitsPerSec) override;
  bool IsHardwareAccelerated(nsACString& aFailureReason) const override;
  nsCString GetDescriptionName() const override;

 private:
  MediaEncoderChangeMonitor(PlatformEncoderModule* aPEM,
                            const EncoderConfig& aConfig,
                            const RefPtr<TaskQueue>& aTaskQueue);
  virtual ~MediaEncoderChangeMonitor() = default;
};

}  // namespace mozilla

#endif  // DOM_MEDIA_PLATFORMS_WRAPPERS_MEDIAENCODERCHANGEMONITOR_H_
