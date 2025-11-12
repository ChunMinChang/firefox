/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-*/
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef AUDIOSTREAMTRACK_H_
#define AUDIOSTREAMTRACK_H_

#include "CrossGraphPort.h"
#include "DOMMediaStream.h"
#include "MediaStreamTrack.h"
#include "SimpleMap.h"
#include "nsClassHashtable.h"

namespace mozilla::dom {

class AudioStreamTrack : public MediaStreamTrack {
 public:
  AudioStreamTrack(
      nsPIDOMWindowInner* aWindow, mozilla::MediaTrack* aInputTrack,
      MediaStreamTrackSource* aSource,
      MediaStreamTrackState aReadyState = MediaStreamTrackState::Live,
      bool aMuted = false,
      const MediaTrackConstraints& aConstraints = MediaTrackConstraints())
      : MediaStreamTrack(aWindow, aInputTrack, aSource, aReadyState, aMuted,
                         aConstraints) {}

  already_AddRefed<MediaStreamTrack> Clone() override;

  AudioStreamTrack* AsAudioStreamTrack() override { return this; }
  const AudioStreamTrack* AsAudioStreamTrack() const override { return this; }

  // Returns a promise that resolves when the device is processing audio.
  RefPtr<GenericPromise> AddAudioOutput(void* aKey, AudioDeviceInfo* aSink);
  void RemoveAudioOutput(void* aKey);
  void SetAudioOutputVolume(void* aKey, float aVolume);

  // Prefer using ForwardContentsTo, as it will create a CrossGraphPort if
  // necessary.
  already_AddRefed<MediaInputPort> ForwardContentsTo(
      ProcessedMediaTrack* aTrack);
  void MaybeRemoveCrossGraphPort(TrackRate aRate);

  // WebIDL
  void GetKind(nsAString& aKind) override { aKind.AssignLiteral("audio"); }

  void GetLabel(nsAString& aLabel, CallerType aCallerType) override;

 protected:
  void SetReadyState(MediaStreamTrackState aState) override;

 private:
  // Main thread only
  SimpleRefCountedMap<TrackRate, UniquePtr<CrossGraphPort>> mCrossGraphs;
};

}  // namespace mozilla::dom

#endif /* AUDIOSTREAMTRACK_H_ */
