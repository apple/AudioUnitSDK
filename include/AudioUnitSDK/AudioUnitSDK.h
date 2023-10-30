/*!
	@file		AudioUnitSDK/AudioUnitSDK.h
	@copyright	© 2000-2023 Apple Inc. All rights reserved.
*/
#ifndef AudioUnitSDK_h
#define AudioUnitSDK_h

// clang-format off
#include <AudioUnitSDK/AUConfig.h> // must come first
// clang-format on
#include <AudioUnitSDK/AUBase.h>
#include <AudioUnitSDK/AUBuffer.h>
#include <AudioUnitSDK/AUEffectBase.h>
#include <AudioUnitSDK/AUInputElement.h>
#if AUSDK_HAVE_MIDI
#include <AudioUnitSDK/AUMIDIBase.h>
#include <AudioUnitSDK/AUMIDIEffectBase.h>
#endif // AUSDK_HAVE_MIDI
#include <AudioUnitSDK/AUOutputElement.h>
#include <AudioUnitSDK/AUPlugInDispatch.h>
#include <AudioUnitSDK/AUScopeElement.h>
#include <AudioUnitSDK/AUSilentTimeout.h>
#include <AudioUnitSDK/AUUtility.h>
#include <AudioUnitSDK/ComponentBase.h>
#if AUSDK_HAVE_MUSIC_DEVICE
#include <AudioUnitSDK/MusicDeviceBase.h>
#endif // AUSDK_HAVE_MUSIC_DEVICE

#endif /* AudioUnitSDK_h */
