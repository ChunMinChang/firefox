#include "MediaEncoderChangeMonitor.h"

namespace mozilla {

extern LazyLogModule sPEMLog;

#ifdef LOG_INTERNAL
#  undef LOG_INTERNAL
#endif  // LOG_INTERNAL
#define LOG_INTERNAL(level, msg, ...) \
  MOZ_LOG(sPEMLog, LogLevel::level, (msg, ##__VA_ARGS__))

#ifdef LOG
#  undef LOG
#endif  // LOG
#define LOG(msg, ...) LOG_INTERNAL(Debug, msg, ##__VA_ARGS__)

#ifdef LOGW
#  undef LOGW
#endif  // LOGE
#define LOGW(msg, ...) LOG_INTERNAL(Warning, msg, ##__VA_ARGS__)

#ifdef LOGE
#  undef LOGE
#endif  // LOGE
#define LOGE(msg, ...) LOG_INTERNAL(Error, msg, ##__VA_ARGS__)

#ifdef LOGV
#  undef LOGV
#endif  // LOGV
#define LOGV(msg, ...) LOG_INTERNAL(Verbose, msg, ##__VA_ARGS__)

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
MediaEncoderChangeMonitor::Create(PlatformEncoderModule* aPEM,
                                  const EncoderConfig& aConfig,
                                  const RefPtr<TaskQueue>& aTaskQueue) {
  RefPtr<MediaEncoderChangeMonitor> monitor =
      new MediaEncoderChangeMonitor(aPEM, aConfig, aTaskQueue);
  RefPtr<CreateEncoderPromise> promise =
      monitor->CreateEncoder(State::Creating)
          ->Then(
              GetCurrentSerialEventTarget(), __func__,
              [m = monitor](RefPtr<MediaDataEncoder> aEncoder) {
                MOZ_ASSERT(m->mEncoder);
                return CreateEncoderPromise::CreateAndResolve(m, __func__);
              },
              [](const MediaResult& aError) {
                return CreateEncoderPromise::CreateAndReject(aError, __func__);
              });
  return promise;
}

RefPtr<MediaDataEncoder::InitPromise> MediaEncoderChangeMonitor::Init() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return InitEncoder(State::Initializing);
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::Encode(
    const MediaData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return EncodeSample(aSample);
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
MediaEncoderChangeMonitor::Reconfigure(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return ReconfigureEncoder(aConfigurationChanges);
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::Drain() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return DrainEncoder();
}

RefPtr<ShutdownPromise> MediaEncoderChangeMonitor::Shutdown() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mShutdownWhileCreationPromise.IsEmpty());
  MOZ_ASSERT(mShutdownWhileReinitShutdownPromise.IsEmpty());

  auto r =
      MediaResult(NS_ERROR_DOM_MEDIA_CANCELED, "Canceled by encoder shutdown");

  // If the encoder creation has not been completed yet, wait until the decoder
  // being created has been shut down.
  if (mCreateRequest.Exists()) {
    MOZ_ASSERT(!mCreatePromise.IsEmpty());
    MOZ_ASSERT(!mEncoder);
    MOZ_ASSERT(mState == State::Creating || mState == State::Reinit_Creating);

    LOGW(
        "MediaEncoderChangeMonitor %p is shutting down during encoder "
        "creation. Rejecting the create promise and deferring shutdown until "
        "the created encoder is fully shut down.",
        this);

    if (mState == State::Reinit_Creating) {
      RejectPendingEncodePromiseIfAny(r);
    }

    mCreatePromise.Reject(r, __func__);
    SetState(State::ShuttingDown);
    return mShutdownWhileCreationPromise.Ensure(__func__);
  }

  if (mReinitShutdownRequest.Exists()) {
    MOZ_ASSERT(mState == State::Reinit_ShuttingDown);
    MOZ_ASSERT(!mEncodeAfterReinitPromise.IsEmpty());
    MOZ_ASSERT(!mEncoder);

    LOGW(
        "MediaEncoderChangeMonitor %p is shutting down while shutting down the "
        "encoder for re-initialization. The shutdown promise will be forwarded "
        "from the existing shutdown request.",
        this);
    RejectPendingEncodePromiseIfAny(r);
    return mShutdownWhileReinitShutdownPromise.Ensure(__func__);
  }

  if (mDrainForDryRequest.Exists()) {
    MOZ_ASSERT(mState == State::Reinit_Drying);
    MOZ_ASSERT(!mDryPromise.IsEmpty());
    MOZ_ASSERT(mEncoder);

    LOGW(
        "MediaEncoderChangeMonitor %p is shutting down while draining the "
        "encoder for re-initialization. Canceling the drain request.",
        this);
    mDrainForDryRequest.DisconnectIfExists();
    mDryData.Clear();
    mDryPromise.Reject(r, __func__);
  }

  // If encoder creation has been completed but failed, no encoder is set.
  if (!mEncoder) {
    LOG("MediaEncoderChangeMonitor %p is shutting down with no encoder", this);
    MOZ_ASSERT(mCreatePromise.IsEmpty());
    MOZ_ASSERT(mEncodeAfterReinitPromise.IsEmpty());
    MOZ_ASSERT(mDryPromise.IsEmpty());
    // ~MediaEncoderChangeMonitor() will ensure the state is set to Unset.
    SetState(State::Unset);
    return ShutdownPromise::CreateAndResolve(true, __func__);
  }

  RejectPendingEncodePromiseIfAny(r);

  // If encoder creation has succeeded, we must have the encoder now.

  MOZ_ASSERT(mCreatePromise.IsEmpty());
  MOZ_ASSERT(mShutdownWhileReinitShutdownPromise.IsEmpty());
  MOZ_ASSERT(mDryPromise.IsEmpty());

  SetState(State::Unset);
  RefPtr<MediaDataEncoder> encoder = std::move(mEncoder);
  return encoder->Shutdown();
}

RefPtr<GenericPromise> MediaEncoderChangeMonitor::SetBitrate(
    uint32_t aBitsPerSec) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  if (!mEncoder) {
    return GenericPromise::CreateAndReject(NS_ERROR_NOT_INITIALIZED,
                                           "No encoder");
  }
  return mEncoder->SetBitrate(aBitsPerSec);
}

bool MediaEncoderChangeMonitor::IsHardwareAccelerated(
    nsACString& aFailureReason) const {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  return mEncoder ? mEncoder->IsHardwareAccelerated(aFailureReason) : false;
}

nsCString MediaEncoderChangeMonitor::GetDescriptionName() const {
  MOZ_ASSERT(mThread->IsOnCurrentThread());

  if (!mEncoder) {
    return "MediaEncoderChangeMonitor (pending)"_ns;
  }
  return mEncoder->GetDescriptionName();
}

MediaEncoderChangeMonitor::MediaEncoderChangeMonitor(
    PlatformEncoderModule* aPEM, const EncoderConfig& aConfig,
    const RefPtr<TaskQueue>& aTaskQueue)
    : mThread(GetCurrentSerialEventTarget()),
      mPEM(aPEM),
      mTaskQueue(aTaskQueue),
      mConfig(aConfig),
      mEncoder(nullptr),
      mState(State::Unset) {
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(mPEM);
  MOZ_ASSERT(mTaskQueue);
  LOG("MediaEncoderChangeMonitor %p created", this);
}

MediaEncoderChangeMonitor::~MediaEncoderChangeMonitor() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mState == State::Unset);
  MOZ_ASSERT(!mEncoder, "Encoder should be null");
  LOG("MediaEncoderChangeMonitor %p destroyed", this);
}

RefPtr<PlatformEncoderModule::CreateEncoderPromise>
MediaEncoderChangeMonitor::CreateEncoder(State aState) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mCreatePromise.IsEmpty());
  MOZ_ASSERT(!mCreateRequest.Exists());
  MOZ_ASSERT(!mEncoder);
  MOZ_ASSERT_IF(mState == State::Unset, aState == State::Creating);
  MOZ_ASSERT_IF(mState == State::Reinit_Unset,
                aState == State::Reinit_Creating);

  SetState(aState);
  LOG("Creating encoder with %s", mConfig.ToString().get());

  RefPtr<CreateEncoderPromise> p = mCreatePromise.Ensure(__func__);
  mPEM->AsyncCreateEncoder(mConfig, mTaskQueue)
      ->Then(
          mThread, __func__,
          [self = RefPtr{this}](RefPtr<MediaDataEncoder>&& aEncoder) {
            self->mCreateRequest.Complete();

            // If MediaEncoderChangeMonitor has been shutdown, shut the created
            // decoder down and return. mCreatePromise should be empty now.
            if (!self->mShutdownWhileCreationPromise.IsEmpty()) {
              MOZ_ASSERT(self->mState == State::ShuttingDown);
              MOZ_ASSERT(self->mCreatePromise.IsEmpty(),
                         "create promise should have been rejected");

              LOGW(
                  "MediaEncoderChangeMonitor %p has been shut down. Proceeding "
                  "to shut down the newly created encoder",
                  self.get());

              aEncoder->Shutdown()->Then(
                  self->mThread, __func__,
                  [self](const ShutdownPromise::ResolveOrRejectValue& aValue) {
                    MOZ_ASSERT(self->mState == State::ShuttingDown);

                    LOGW(
                        "MediaEncoderChangeMonitor %p, newly created encoder "
                        "shutdown has been %s",
                        self.get(),
                        aValue.IsResolve() ? "resolved" : "rejected");

                    self->SetState(State::Unset);
                    self->mShutdownWhileCreationPromise.ResolveOrReject(
                        aValue, __func__);
                  });

              return;
            }

            self->mEncoder = std::move(aEncoder);
            LOG("MediaEncoderChangeMonitor %p created encoder %p", self.get(),
                self->mEncoder.get());
            self->SetState(self->mState == State::Creating
                               ? State::Uninit
                               : State::Reinit_Uninit);
            self->mCreatePromise.Resolve(self, __func__);
          },
          [self = RefPtr{this}](const MediaResult& aError) {
            self->mCreateRequest.Complete();

            // If MediaEncoderChangeMonitor has been shutdown, we should resolve
            // the shutdown promise.
            if (!self->mShutdownWhileCreationPromise.IsEmpty()) {
              MOZ_ASSERT(self->mState == State::ShuttingDown);
              MOZ_ASSERT(self->mCreatePromise.IsEmpty(),
                         "create promise should have been rejected");

              LOGW(
                  "MediaEncoderChangeMonitor %p was shut down. Resolving the "
                  "shutdown promise immediately as encoder creation did not "
                  "succeed",
                  self.get());

              self->SetState(State::Unset);
              self->mShutdownWhileCreationPromise.Resolve(true, __func__);
              return;
            }

            LOGE("MediaEncoderChangeMonitor %p failed to create encoder: %s",
                 self.get(), aError.Description().get());
            self->SetState(State::Unset);
            self->mCreatePromise.Reject(aError, __func__);
          })
      ->Track(mCreateRequest);

  return p;
}

RefPtr<MediaDataEncoder::InitPromise> MediaEncoderChangeMonitor::InitEncoder(
    State aState) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT_IF(mState == State::Uninit, aState == State::Initializing);
  MOZ_ASSERT_IF(mState == State::Reinit_Uninit,
                aState == State::Reinit_Initializing);

  LOG("MediaEncoderChangeMonitor %p initializing encoder %p", this,
      mEncoder.get());
  SetState(aState);

  return mEncoder->Init()->Then(
      mThread, __func__,
      [self = RefPtr{this}](
          UniquePtr<MediaDataEncoder::InitResult>&& aInitResult) {
        MOZ_ASSERT(aInitResult->AsVideoInitResult());
        self->mCompatibleFormats.AppendElements(
            aInitResult->AsVideoInitResult()->GetCompatibleFormats());
        self->SetState(State::Inited);
        return InitPromise::CreateAndResolve(std::move(aInitResult), __func__);
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        LOGE("MediaEncoderChangeMonitor %p encoder initialization failed: %s",
             self.get(), aError.Description().get());
        self->SetState(State::Error);
        return InitPromise::CreateAndReject(aError, __func__);
      });
}

RefPtr<MediaDataEncoder::ReconfigurationPromise>
MediaEncoderChangeMonitor::ReconfigureEncoder(
    const RefPtr<const EncoderConfigurationChangeList>& aConfigurationChanges) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  LOG("MediaEncoderChangeMonitor %p reconfiguring encoder %p", this,
      mEncoder.get());
  SetState(State::Reconfiguring);

  return mEncoder->Reconfigure(aConfigurationChanges)
      ->Then(
          mThread, __func__,
          [self = RefPtr{this}](
              const ReconfigurationPromise::ResolveOrRejectValue& aValue) {
            if (aValue.IsResolve()) {
              LOG("MediaEncoderChangeMonitor %p encoder reconfigured",
                  self.get());
              self->SetState(State::Inited);
            } else {
              LOGW(
                  "MediaEncoderChangeMonitor %p encoder reconfiguration failed",
                  self.get());
              // The underlying encoder can reject the reconfiguration request
              // but still be in a valid state. We should not set the state to
              // Error.
              self->SetState(State::Inited);
            }
            return ReconfigurationPromise::CreateAndResolveOrReject(aValue,
                                                                    __func__);
          });
}

RefPtr<MediaDataEncoder::EncodePromise> MediaEncoderChangeMonitor::EncodeSample(
    const MediaData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(aSample);
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  RefPtr<const VideoData> sample = aSample->As<const VideoData>();
  MOZ_ASSERT(sample, "MediaEncoderChangeMonitor is video-only now");

  if (!sample->mImage) {
    return EncodePromise::CreateAndReject(
        MediaResult(
            NS_ERROR_DOM_MEDIA_FATAL_ERR,
            nsPrintfCString("%s has no image", sample->ToString().get())),
        __func__);
  }

  auto r = EncoderConfig::VideoSampleFormat::FromImage(sample->mImage);
  if (r.isErr()) {
    MediaResult err = r.unwrapErr();
    LOGE("%s", err.Description().get());
    return EncodePromise::CreateAndReject(err, __func__);
  }

  EncoderConfig::VideoSampleFormat sf = r.unwrap();
  if (NeedReinit(sf)) {
    LOGW(
        "MediaEncoderChangeMonitor %p encoding sample %p with different "
        "format %s than encoder %s. Need to reinit encoder",
        this, sample.get(), sf.ToString().get(),
        mConfig.mFormat.ToString().get());
    return EncodeAfterReinit(sample);
  }

  LOGV("MediaEncoderChangeMonitor %p encoding sample %p", this, aSample);
  SetState(State::Encoding);

  // EncodedData's copy ctor is implicitly deleted, so we need to have two
  // separate lambdas to handle the success and error cases.
  return mEncoder->Encode(aSample)->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaDataEncoder::EncodedData&& aData) {
        LOGV("MediaEncoderChangeMonitor %p encoder encoded sample", self.get());
        self->SetState(State::Inited);
        return EncodePromise::CreateAndResolve(std::move(aData), __func__);
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        LOGE("MediaEncoderChangeMonitor %p encoder encoding failed",
             self.get());
        self->SetState(State::Error);
        return EncodePromise::CreateAndReject(aError, __func__);
      });
}

RefPtr<MediaDataEncoder::EncodePromise>
MediaEncoderChangeMonitor::DrainEncoder() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  LOG("MediaEncoderChangeMonitor %p draining encoder %p", this, mEncoder.get());
  SetState(State::Draining);

  // EncodedData's copy ctor is implicitly deleted, so we need to have two
  // separate lambdas to handle the success and error cases.
  return mEncoder->Drain()->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaDataEncoder::EncodedData&& aData) {
        LOG("MediaEncoderChangeMonitor %p encoder drained", self.get());
        self->SetState(State::Inited);
        return EncodePromise::CreateAndResolve(std::move(aData), __func__);
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        LOGE("MediaEncoderChangeMonitor %p encoder draining failed",
             self.get());
        self->SetState(State::Error);
        return EncodePromise::CreateAndReject(aError, __func__);
      });
}

bool MediaEncoderChangeMonitor::NeedReinit(
    const EncoderConfig::VideoSampleFormat& aSampleFormat) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);

  return mCompatibleFormats.Contains(aSampleFormat);
}

RefPtr<MediaDataEncoder::EncodePromise>
MediaEncoderChangeMonitor::EncodeAfterReinit(const MediaData* aSample) {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(aSample);
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);
  MOZ_ASSERT(mEncodeAfterReinitPromise.IsEmpty());
  MOZ_ASSERT(!mPendingSample);

  RefPtr<EncodePromise> p = mEncodeAfterReinitPromise.Ensure(__func__);
  LOG("MediaEncoderChangeMonitor %p needs to dry and reinit encoder before "
      "encoding again",
      this);

  mPendingSample = aSample;

  DryEncoder()->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaDataEncoder::EncodedData&& aData) {
        LOG("MediaEncoderChangeMonitor %p encoder dried", self.get());
        self->mPendingOutput.AppendElements(std::move(aData));
        self->SetState(State::Reinit_Dried);
        self->ShutdownThenReinit();
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        LOGE("MediaEncoderChangeMonitor %p failed to dry encoder", self.get());
        self->SetState(State::Error);
        self->RejectPendingEncodePromiseIfAny(aError);
      });

  return p;
}

RefPtr<MediaDataEncoder::EncodePromise>
MediaEncoderChangeMonitor::DryEncoder() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);
  MOZ_ASSERT(mDryPromise.IsEmpty());

  LOG("MediaEncoderChangeMonitor %p drying encoder %p", this, mEncoder.get());
  SetState(State::Reinit_Drying);
  RefPtr<EncodePromise> p = mDryPromise.Ensure(__func__);
  DryEncoderInternal();
  return p;
}

void MediaEncoderChangeMonitor::DryEncoderInternal() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Reinit_Drying);
  MOZ_ASSERT(!mDryPromise.IsEmpty());
  MOZ_ASSERT(!mDrainForDryRequest.Exists());

  LOG("MediaEncoderChangeMonitor %p draining encoder %p", this, mEncoder.get());
  mEncoder->Drain()
      ->Then(
          mThread, __func__,
          [self = RefPtr{this}](MediaDataEncoder::EncodedData&& aData) {
            LOGV("MediaEncoderChangeMonitor %p encoder drained", self.get());
            self->mDrainForDryRequest.Complete();

            if (aData.IsEmpty()) {
              LOG("MediaEncoderChangeMonitor %p encoder drained with no data",
                  self.get());
              self->mDryPromise.Resolve(std::move(self->mDryData), __func__);
              return;
            }

            LOG("MediaEncoderChangeMonitor %p encoder got %zu data. Keep "
                "draining",
                self.get(), aData.Length());

            self->mDryData.AppendElements(std::move(aData));
            self->DryEncoderInternal();
          },
          [self = RefPtr{this}](const MediaResult& aError) {
            LOGE("MediaEncoderChangeMonitor %p failed to drain encoder",
                 self.get());
            self->mDrainForDryRequest.Complete();

            self->mDryData.Clear();
            self->mDryPromise.Reject(aError, __func__);
          })
      ->Track(mDrainForDryRequest);
}

void MediaEncoderChangeMonitor::ShutdownThenReinit() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Reinit_Dried);
  MOZ_ASSERT(!mEncodeAfterReinitPromise.IsEmpty());
  MOZ_ASSERT(mPendingSample);

  LOG("MediaEncoderChangeMonitor %p shutting down the old encoder %p", this,
      mEncoder.get());
  SetState(State::Reinit_ShuttingDown);
  RefPtr<MediaDataEncoder> encoder = std::move(mEncoder);
  encoder->Shutdown()
      ->Then(mThread, __func__,
             [self = RefPtr{this}](
                 const ShutdownPromise::ResolveOrRejectValue& aValue) {
               self->mReinitShutdownRequest.Complete();

               if (!self->mShutdownWhileReinitShutdownPromise.IsEmpty()) {
                 MOZ_ASSERT(self->mEncodeAfterReinitPromise.IsEmpty(),
                            "encode promise should have been rejected");
                 LOGW(
                     "MediaEncoderChangeMonitor %p encoder shutdown while "
                     "reinitializing. Forward the shutdown promise result",
                     self.get());

                 self->SetState(State::Unset);
                 self->mShutdownWhileReinitShutdownPromise.ResolveOrReject(
                     aValue, __func__);
                 return;
               }

               if (aValue.IsResolve()) {
                 LOG("MediaEncoderChangeMonitor %p encoder has been shutdown",
                     self.get());
                 self->SetState(State::Reinit_Unset);
                 self->ReinitThenEncode();
               } else {
                 LOGE("MediaEncoderChangeMonitor %p encoder shutdown failed",
                      self.get());
                 self->SetState(State::Error);
                 self->RejectPendingEncodePromiseIfAny(MediaResult(
                     NS_ERROR_DOM_MEDIA_FATAL_ERR,
                     "Failed to shutdown encoder for reinitialization"));
               }
             })
      ->Track(mReinitShutdownRequest);
}

void MediaEncoderChangeMonitor::ReinitThenEncode() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(!mEncoder);
  MOZ_ASSERT(mState == State::Reinit_Unset);
  MOZ_ASSERT(!mEncodeAfterReinitPromise.IsEmpty());
  MOZ_ASSERT(mPendingSample);

  nsCString oldConfigStr = mConfig.ToString();
  RefPtr<const VideoData> sample = mPendingSample->As<const VideoData>();
  MOZ_ASSERT(sample, "MediaEncoderChangeMonitor is video-only now");
  MOZ_ASSERT(sample->mImage);
  mConfig.mFormat =
      EncoderConfig::VideoSampleFormat::FromImage(sample->mImage).unwrap();

  LOG("MediaEncoderChangeMonitor %p reinitializing encoder, changing config "
      "from %s to %s",
      this, oldConfigStr.get(), mConfig.ToString().get());

  // CreateEncoder() will handle the cancellation of the shutdown request.
  CreateEncoder(State::Reinit_Creating)
      ->Then(
          mThread, __func__,
          [self = RefPtr{this}](RefPtr<MediaDataEncoder> aEncoder) {
            LOG("MediaEncoderChangeMonitor %p encoder created for "
                "reinitialization",
                self.get());

            MOZ_ASSERT(self->mEncoder);
            self->SetState(State::Reinit_Uninit);

            self->InitEncoder(State::Reinit_Initializing)
                ->Then(
                    self->mThread, __func__,
                    [self]() {
                      LOG("MediaEncoderChangeMonitor %p encoder reinitialized",
                          self.get());
                      self->SetState(State::Inited);
                      self->EncodePendingSample();
                    },
                    [self](const MediaResult& aError) {
                      LOGE(
                          "MediaEncoderChangeMonitor %p failed to reinitialize "
                          "encoder",
                          self.get());
                      self->SetState(State::Error);
                      self->RejectPendingEncodePromiseIfAny(aError);
                    });
          },
          [self = RefPtr{this}](const MediaResult& aError) {
            LOGE("MediaEncoderChangeMonitor %p failed to reinitialize encoder",
                 self.get());
            self->SetState(State::Error);
            self->RejectPendingEncodePromiseIfAny(aError);
          });
}

void MediaEncoderChangeMonitor::EncodePendingSample() {
  MOZ_ASSERT(mThread->IsOnCurrentThread());
  MOZ_ASSERT(mEncoder);
  MOZ_ASSERT(mState == State::Inited);
  MOZ_ASSERT(!mEncodeAfterReinitPromise.IsEmpty());
  MOZ_ASSERT(mPendingSample);

  LOG("MediaEncoderChangeMonitor %p encoding pending sample %p", this,
      mPendingSample.get());

  RefPtr<const MediaData> sample = std::move(mPendingSample);
  EncodeSample(sample)->Then(
      mThread, __func__,
      [self = RefPtr{this}](MediaDataEncoder::EncodedData&& aData) {
        LOG("MediaEncoderChangeMonitor %p encoder encoded pending sample",
            self.get());
        self->SetState(State::Inited);
        // TODO: Notify about a new EncodedVideoChunkMetadata.decoderConfig if
        // necessary (e.g., due to a color space change)
        self->mPendingOutput.AppendElements(std::move(aData));
        self->mEncodeAfterReinitPromise.Resolve(std::move(self->mPendingOutput),
                                                __func__);
      },
      [self = RefPtr{this}](const MediaResult& aError) {
        LOGE("MediaEncoderChangeMonitor %p encoder encoding failed",
             self.get());
        self->SetState(State::Error);
        self->RejectPendingEncodePromiseIfAny(aError);
      });
}

void MediaEncoderChangeMonitor::RejectPendingEncodePromiseIfAny(
    const MediaResult& aError) {
  mEncodeAfterReinitPromise.RejectIfExists(aError, __func__);
  mPendingSample = nullptr;
  mPendingOutput.Clear();
}

void MediaEncoderChangeMonitor::SetState(State aState) {
  LOG("State changed from %s to %s", EnumValueToString(mState),
      EnumValueToString(aState));
  mState = aState;
}

#undef LOG
#undef LOGW
#undef LOGE
#undef LOGV
#undef LOG_INTERNAL

}  // namespace mozilla
