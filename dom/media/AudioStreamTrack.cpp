/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "AudioStreamTrack.h"

#include "MediaTrackGraph.h"
#include "nsContentUtils.h"

#ifdef LOG
#  undef LOG
#endif

static mozilla::LazyLogModule gMediaStreamTrackLog("MediaStreamTrack");
#define LOG(type, msg) MOZ_LOG(gMediaStreamTrackLog, type, msg)

namespace mozilla::dom {

RefPtr<GenericPromise> AudioStreamTrack::AddAudioOutput(
    void* aKey, AudioDeviceInfo* aSink) {
  if (Ended()) {
    return GenericPromise::CreateAndResolve(true, __func__);
  }

  mTrack->AddAudioOutput(aKey, aSink);
  return mTrack->Graph()->NotifyWhenDeviceStarted(aSink);
}

void AudioStreamTrack::RemoveAudioOutput(void* aKey) {
  if (Ended()) {
    return;
  }

  mTrack->RemoveAudioOutput(aKey);
}

void AudioStreamTrack::SetAudioOutputVolume(void* aKey, float aVolume) {
  if (Ended()) {
    return;
  }

  mTrack->SetAudioOutputVolume(aKey, aVolume);
}

already_AddRefed<MediaInputPort> AudioStreamTrack::ForwardContentsTo(
    ProcessedMediaTrack* aTrack) {
  printf("AudioStreamTrack %p ForwardTrackContents2 to ProcessedMediaTrack %p\n", this, aTrack);
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_RELEASE_ASSERT(aTrack);

  if (mTrack->Graph() == aTrack->Graph()) {
    return ForwardTrackContentsTo(aTrack);
  }

  LOG(LogLevel::Verbose,
      ("AudioStreamTrack %p forwarding cross-graph contents from track %p "
       "(graph %p) to track %p (graph %p)",
       this, mTrack.get(), mTrack->Graph(), aTrack, aTrack->Graph()));

  MOZ_ASSERT(aTrack->mSampleRate != mTrack->mSampleRate);

  // Route audio from mTrack through a cross-graph transmitter and receiver to
  // aTrack.
  MediaTrackGraph* rcvrGraph = aTrack->Graph();
  const auto& port = mCrossGraphs.GetOrInsertWith(rcvrGraph->GraphRate(), [&] {
    LOG(LogLevel::Verbose,
        ("AudioStreamTrack %p creating cross-graph port to graph (rate %u)",
         this, rcvrGraph->GraphRate()));
    return CrossGraphPort::Connect(RefPtr{this}, rcvrGraph);
  });
  return aTrack->AllocateInputPort(port->mReceiver);
}

void AudioStreamTrack::MaybeRemoveCrossGraphPort(TrackRate aRate) {
  MOZ_ASSERT(NS_IsMainThread());
  printf("AudioStreamTrack %p MaybeRemoveCrossGraphPort for rate %u\n", this, aRate);

  Maybe<UniquePtr<CrossGraphPort>> port = mCrossGraphs.Release(aRate);
  if (port) {
    printf("!! Removing CrossGraphPort for rate %u\n", aRate);
    LOG(LogLevel::Verbose, ("AudioStreamTrack %p removing cross-graph "
                            "forwarding to graph (rate %u)",
                            this, aRate));
    port->reset();
  }
  printf("%zu CrossGraphPorts remain for AudioStreamTrack %p\n", mCrossGraphs.Count(), this);
}

void AudioStreamTrack::GetLabel(nsAString& aLabel, CallerType aCallerType) {
  MediaStreamTrack::GetLabel(aLabel, aCallerType);
}

already_AddRefed<MediaStreamTrack> AudioStreamTrack::Clone() {
  return MediaStreamTrack::CloneInternal<AudioStreamTrack>();
}

void AudioStreamTrack::SetReadyState(MediaStreamTrackState aState) {
  MOZ_ASSERT(NS_IsMainThread());

  // When transitioning from Live to Ended, mTrack will be destroyed. Since
  // mTrack is the source for cross-graph data forwarding, keeping cross-graph
  // ports is unnecessary. Clearing them here ensures all related connections
  // are properly disconnected and prevents an assertion failure in
  // CrossGraphTransmitters::ProcessInput due to a missing source.
  //
  // This state transition may occur in various situations, such as when the
  // track is stopped by a user action, or when mTrack is ended during its
  // ProcessInput (because its source has ended), which is then detected by
  // MediaTrackGraph and ultimately notifies the ended-signal via MTGListener,
  // reaching this point.
  if (mCrossGraphs.Count() && !Ended() &&
      mReadyState == MediaStreamTrackState::Live &&
      aState == MediaStreamTrackState::Ended) {
    LOG(LogLevel::Verbose,
        ("AudioStreamTrack %p ending, destroying %zu cross-graph ports", this,
         mCrossGraphs.Count()));
    mCrossGraphs.Clear();
  }

  MediaStreamTrack::SetReadyState(aState);
}

}  // namespace mozilla::dom
