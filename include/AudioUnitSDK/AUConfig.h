/*!
	@file		AudioUnitSDK/AUConfig.h
	@copyright	© 2000-2023 Apple Inc. All rights reserved.
*/

#ifndef AudioUnitSDK_AUConfig_h
#define AudioUnitSDK_AUConfig_h

#include <TargetConditionals.h>

#if defined(__has_include) && __has_include(<AvailabilityVersions.h>)
#include <AvailabilityVersions.h>
#endif

#if defined(__has_include) && __has_include(<MacTypes.h>)
#include <MacTypes.h>
#else
enum { noErr = 0 };
#endif

#if defined(__has_include) && __has_include(<CoreAudioTypes/CoreAudioTypes.h>)
#include <CoreAudioTypes/CoreAudioTypes.h>
#else
#include <CoreAudio/CoreAudioTypes.h>
#endif

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark Version

#define AUSDK_VERSION_MAJOR 1
#define AUSDK_VERSION_MINOR 2
#define AUSDK_VERSION_PATCH 0

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark Deprecations

#ifdef AUSDK_NO_DEPRECATIONS
#define AUSDK_DEPRECATED(msg)
#else
#define AUSDK_DEPRECATED(msg) [[deprecated(msg)]] // NOLINT macro
#endif                                            // AUSDK_NO_DEPRECATIONS

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark UI

#if !defined(AUSDK_HAVE_UI)
#define AUSDK_HAVE_UI 1
#endif // !defined(AUSDK_HAVE_UI)

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark Mach

#if !defined(AUSDK_HAVE_MACH_TIME)
#if defined(__has_include) && __has_include(<mach/mach_time.h>)
#define AUSDK_HAVE_MACH_TIME 1
#else
#define AUSDK_HAVE_MACH_TIME 0
#endif
#endif // !defined(AUSDK_HAVE_MACH_TIME)

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark MIDI

#if !defined(AUSDK_HAVE_MIDI)
#if defined(__has_include) && __has_include(<CoreMIDI/CoreMIDI.h>)
#define AUSDK_HAVE_MIDI 1
#else
#define AUSDK_HAVE_MIDI 0
#endif
#endif // !defined(AUSDK_HAVE_MIDI)

#if !defined(AUSDK_HAVE_MIDI2)
#if defined(__MAC_12_0) || defined(__IPHONE_15_0)
#define AUSDK_HAVE_MIDI2 (AUSDK_HAVE_MIDI)
#else
#define AUSDK_HAVE_MIDI2 0
#endif
#endif // !defined(AUSDK_HAVE_MIDI2)

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark MusicDevice

#if !defined(AUSDK_HAVE_MUSIC_DEVICE)
#if defined(__has_include) && __has_include(<AudioToolbox/MusicDevice.h>)
#define AUSDK_HAVE_MUSIC_DEVICE 1
#else
#define AUSDK_HAVE_MUSIC_DEVICE 0
#endif
#endif // !defined(AUSDK_HAVE_MUSIC_DEVICE)

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark AudioOutputUnit

#if !defined(AUSDK_HAVE_IO_UNITS)
#if defined(__has_include) && __has_include(<AudioToolbox/AudioOutputUnit.h>)
#define AUSDK_HAVE_IO_UNITS 1
#else
#define AUSDK_HAVE_IO_UNITS 0
#endif
#endif // !defined(AUSDK_HAVE_MUSIC_DEVICE)


#endif /* AUConfig_h */
