/*!
	@file		AudioUnitSDK/AUMIDIBase.cpp
	@copyright	© 2000-2024 Apple Inc. All rights reserved.
*/
#include <AudioUnitSDK/AUConfig.h>

#if AUSDK_HAVE_MIDI

#include <AudioUnitSDK/AUMIDIBase.h>
#include <AudioUnitSDK/AUUtility.h>

#include <CoreMIDI/CoreMIDI.h>

namespace ausdk {

// MIDI CC data bytes
constexpr uint8_t kMIDIController_AllSoundOff = 120u;
constexpr uint8_t kMIDIController_ResetAllControllers = 121u;
constexpr uint8_t kMIDIController_AllNotesOff = 123u;

OSStatus AUMIDIBase::DelegateGetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, UInt32& outDataSize, bool& outWritable)
{
	(void)inScope;
	(void)inElement;
	(void)outDataSize;
	(void)outWritable;

	switch (inID) { // NOLINT if/else?!
#if AUSDK_HAVE_XML_NAMES
	case kMusicDeviceProperty_MIDIXMLNames:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		AUSDK_Require(GetXMLNames(nullptr) == noErr, kAudioUnitErr_InvalidProperty);
		outDataSize = sizeof(CFURLRef);
		outWritable = false;
		return noErr;
#endif

#if AUSDK_HAVE_MIDI_MAPPING
	case kAudioUnitProperty_AllParameterMIDIMappings:
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		outWritable = true;
		outDataSize = sizeof(AUParameterMIDIMapping) * mMIDIMapper->GetNumberMaps();
		return noErr;

	case kAudioUnitProperty_HotMapParameterMIDIMapping:
	case kAudioUnitProperty_AddParameterMIDIMapping:
	case kAudioUnitProperty_RemoveParameterMIDIMapping:
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		outWritable = true;
		outDataSize = sizeof(AUParameterMIDIMapping);
		return noErr;
#endif

	default:
		return kAudioUnitErr_InvalidProperty;
	}
}

OSStatus AUMIDIBase::DelegateGetProperty(
	AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, void* outData)
{
	(void)inScope;
	(void)inElement;
	(void)outData;

	switch (inID) { // NOLINT if/else?!
#if AUSDK_HAVE_XML_NAMES
	case kMusicDeviceProperty_MIDIXMLNames: {
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		CFURLRef url = nullptr;
		const auto result = GetXMLNames(&url);
		Serialize(url, outData);
		return result;
	}
#endif

#if AUSDK_HAVE_MIDI_MAPPING
	case kAudioUnitProperty_AllParameterMIDIMappings: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		std::vector<AUParameterMIDIMapping> maps(mMIDIMapper->GetNumberMaps());
		mMIDIMapper->GetMaps(maps.data());
		Serialize(std::span(maps), outData);
		return noErr;
	}

	case kAudioUnitProperty_HotMapParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		AUParameterMIDIMapping map{};
		mMIDIMapper->GetHotParameterMap(map);
		Serialize(map, outData);
		return noErr;
	}
#endif

	default:
		return kAudioUnitErr_InvalidProperty;
	}
}

OSStatus AUMIDIBase::DelegateSetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, const void* inData, UInt32 inDataSize)
{
	(void)inScope;
	(void)inElement;
	(void)inData;
	(void)inDataSize;

	switch (inID) {

#if AUSDK_HAVE_MIDI_MAPPING
	case kAudioUnitProperty_AddParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto maps = DeserializeArray<AUParameterMIDIMapping>(inData, inDataSize);
		mMIDIMapper->AddParameterMapping(
			maps.data(), static_cast<UInt32>(maps.size()), mAUBaseInstance);
		mAUBaseInstance.PropertyChanged(
			kAudioUnitProperty_AllParameterMIDIMappings, kAudioUnitScope_Global, 0);
		return noErr;
	}

	case kAudioUnitProperty_RemoveParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto maps = DeserializeArray<AUParameterMIDIMapping>(inData, inDataSize);
		bool didChange = false;
		mMIDIMapper->RemoveParameterMapping(
			maps.data(), static_cast<UInt32>(maps.size()), didChange);
		if (didChange) {
			mAUBaseInstance.PropertyChanged(
				kAudioUnitProperty_AllParameterMIDIMappings, kAudioUnitScope_Global, 0);
		}
		return noErr;
	}

	case kAudioUnitProperty_HotMapParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto map = Deserialize<AUParameterMIDIMapping>(inData);
		mMIDIMapper->SetHotMapping(map);
		return noErr;
	}

	case kAudioUnitProperty_AllParameterMIDIMappings: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto maps = DeserializeArray<AUParameterMIDIMapping>(inData, inDataSize);
		mMIDIMapper->ReplaceAllMaps(maps.data(), static_cast<UInt32>(maps.size()), mAUBaseInstance);
		return noErr;
	}
#endif

	default:
		return kAudioUnitErr_InvalidProperty;
	}
}

constexpr uint8_t MIDIStatusNibbleValue(uint8_t status) noexcept { return (status & 0xF0U) >> 4u; }

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//	AUMIDIBase::HandleMIDIEvent
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
OSStatus AUMIDIBase::HandleMIDIEvent(
	UInt8 status, UInt8 channel, UInt8 data1, UInt8 data2, UInt32 inStartFrame)
{
	AUSDK_Require(mAUBaseInstance.IsInitialized(), kAudioUnitErr_Uninitialized);

#if AUSDK_HAVE_MIDI_MAPPING
	// you potentially have a choice to make here - if a param mapping matches, do you still want to
	// process the MIDI event or not. The default behaviour is to continue on with the MIDI event.
	if (mMIDIMapper) {
		if (mMIDIMapper->HandleHotMapping(status, channel, data1, mAUBaseInstance)) {
			mAUBaseInstance.PropertyChanged(
				kAudioUnitProperty_HotMapParameterMIDIMapping, kAudioUnitScope_Global, 0);
		} else {
			mMIDIMapper->FindParameterMapEventMatch(
				status, channel, data1, data2, inStartFrame, mAUBaseInstance);
		}
	}
#endif
	switch (MIDIStatusNibbleValue(status)) {
	case kMIDICVStatusNoteOn:
		if (data2 != 0u) {
			return HandleNoteOn(channel, data1, data2, inStartFrame);
		} else {
			// zero velocity translates to note off
			return HandleNoteOff(channel, data1, data2, inStartFrame);
		}

	case kMIDICVStatusNoteOff:
		return HandleNoteOff(channel, data1, data2, inStartFrame);

	default:
		return HandleNonNoteEvent(status, channel, data1, data2, inStartFrame);
	}
}

OSStatus AUMIDIBase::HandleNonNoteEvent(
	UInt8 status, UInt8 channel, UInt8 data1, UInt8 data2, UInt32 inStartFrame)
{
	switch (MIDIStatusNibbleValue(status)) {
	case kMIDICVStatusPitchBend:
		return HandlePitchWheel(channel, data1, data2, inStartFrame);

	case kMIDICVStatusProgramChange:
		return HandleProgramChange(channel, data1);

	case kMIDICVStatusChannelPressure:
		return HandleChannelPressure(channel, data1, inStartFrame);

	case kMIDICVStatusControlChange: {
		switch (data1) {
		case kMIDIController_AllNotesOff:
			return HandleAllNotesOff(channel);

		case kMIDIController_ResetAllControllers:
			return HandleResetAllControllers(channel);

		case kMIDIController_AllSoundOff:
			return HandleAllSoundOff(channel);

		default:
			return HandleControlChange(channel, data1, data2, inStartFrame);
		}
	}

	case kMIDICVStatusPolyPressure:
		return HandlePolyPressure(channel, data1, data2, inStartFrame);

	default:
		return noErr;
	}
}

OSStatus AUMIDIBase::SysEx(const UInt8* inData, UInt32 inLength)
{
	AUSDK_Require(mAUBaseInstance.IsInitialized(), kAudioUnitErr_Uninitialized);

	return HandleSysEx(inData, inLength);
}

} // namespace ausdk

#endif // AUSDK_HAVE_MIDI
