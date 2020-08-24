/*!
	@file		AudioUnitSDK/AUMIDIBase.cpp
	@copyright	© 2000-2021 Apple Inc. All rights reserved.
*/
#include "AUMIDIBase.h"
#include <CoreMIDI/CoreMIDI.h>

namespace ausdk {

// MIDI status bytes
enum : uint8_t {
	kMIDIStatus_NoteOff = 0x80,
	kMIDIStatus_NoteOn = 0x90,
	kMIDIStatus_PolyPressure = 0xA0,
	kMIDIStatus_ControlChange = 0xB0,
	kMIDIStatus_ProgramChange = 0xC0,
	kMIDIStatus_ChannelPressure = 0xD0,
	kMIDIStatus_PitchWheel = 0xE0,
	kMIDIStatus_System = 0xF0,

	kMIDIController_AllSoundOff = 120,
	kMIDIController_ResetAllControllers = 121,
	kMIDIController_AllNotesOff = 123
};

constexpr bool isMIDIStatus(uint8_t x) noexcept { return (x & 0x80) == 0x0U; } // NOLINT

OSStatus AUMIDIBase::DelegateGetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, UInt32& outDataSize, bool& outWritable)
{
	OSStatus result = noErr;

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
		break;
#endif

#if AUSDK_HAVE_MIDI_MAPPING
	case kAudioUnitProperty_AllParameterMIDIMappings:
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		outWritable = true;
		outDataSize = sizeof(AUParameterMIDIMapping) * mMIDIMapper->GetNumberMaps();
		result = noErr;
		break;

	case kAudioUnitProperty_HotMapParameterMIDIMapping:
	case kAudioUnitProperty_AddParameterMIDIMapping:
	case kAudioUnitProperty_RemoveParameterMIDIMapping:
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		outWritable = true;
		outDataSize = sizeof(AUParameterMIDIMapping);
		result = noErr;
		break;
#endif

	default:
		result = kAudioUnitErr_InvalidProperty;
		break;
	}
	return result;
}

OSStatus AUMIDIBase::DelegateGetProperty(
	AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, void* outData)
{
	(void)inScope;
	(void)inElement;
	(void)outData;

	OSStatus result = noErr;

	switch (inID) { // NOLINT if/else?!
#if AUSDK_HAVE_XML_NAMES
	case kMusicDeviceProperty_MIDIXMLNames:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		result = GetXMLNames(static_cast<CFURLRef*>(outData));
		break;
#endif

#if AUSDK_HAVE_MIDI_MAPPING
	case kAudioUnitProperty_AllParameterMIDIMappings: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		AUParameterMIDIMapping* maps = (static_cast<AUParameterMIDIMapping*>(outData));
		mMIDIMapper->GetMaps(maps);
		result = noErr;
		break;
	}

	case kAudioUnitProperty_HotMapParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		AUParameterMIDIMapping* map = (static_cast<AUParameterMIDIMapping*>(outData));
		mMIDIMapper->GetHotParameterMap(*map);
		result = noErr;
		break;
	}
#endif

	default:
		result = kAudioUnitErr_InvalidProperty;
		break;
	}
	return result;
}

OSStatus AUMIDIBase::DelegateSetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, const void* inData, UInt32 inDataSize)
{
	(void)inScope;
	(void)inElement;
	(void)inData;
	(void)inDataSize;

	OSStatus result = noErr;

	switch (inID) {

#if AUSDK_HAVE_MIDI_MAPPING
	case kAudioUnitProperty_AddParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto* const maps = static_cast<const AUParameterMIDIMapping*>(inData);
		mMIDIMapper->AddParameterMapping(
			maps, (inDataSize / sizeof(AUParameterMIDIMapping)), mAUBaseInstance);
		mAUBaseInstance.PropertyChanged(
			kAudioUnitProperty_AllParameterMIDIMappings, kAudioUnitScope_Global, 0);
		result = noErr;
		break;
	}

	case kAudioUnitProperty_RemoveParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto* const maps = static_cast<const AUParameterMIDIMapping*>(inData);
		bool didChange = false;
		mMIDIMapper->RemoveParameterMapping(
			maps, (inDataSize / sizeof(AUParameterMIDIMapping)), didChange);
		if (didChange) {
			mAUBaseInstance.PropertyChanged(
				kAudioUnitProperty_AllParameterMIDIMappings, kAudioUnitScope_Global, 0);
		}
		result = noErr;
		break;
	}

	case kAudioUnitProperty_HotMapParameterMIDIMapping: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto& map = *static_cast<const AUParameterMIDIMapping*>(inData);
		mMIDIMapper->SetHotMapping(map);
		result = noErr;
		break;
	}

	case kAudioUnitProperty_AllParameterMIDIMappings: {
		AUSDK_Require(mMIDIMapper, kAudioUnitErr_InvalidProperty);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inElement == 0, kAudioUnitErr_InvalidElement);
		const auto* const mappings = static_cast<const AUParameterMIDIMapping*>(inData);
		mMIDIMapper->ReplaceAllMaps(
			mappings, (inDataSize / sizeof(AUParameterMIDIMapping)), mAUBaseInstance);
		result = noErr;
		break;
	}
#endif

	default:
		result = kAudioUnitErr_InvalidProperty;
		break;
	}
	return result;
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
#pragma mark ____MIDIDispatch


constexpr const Byte* NextMIDIEvent(const Byte* event, const Byte* end) noexcept
{
	const Byte c = *event;
	switch (c & 0xF0U) {                                 // NOLINT
	default:                                             // data byte -- assume in sysex
		while (!isMIDIStatus(*++event) && event < end) { // NOLINT
			;
		}
		break;
	case kMIDIStatus_NoteOff:
	case kMIDIStatus_NoteOn:
	case kMIDIStatus_PolyPressure:
	case kMIDIStatus_ControlChange:
	case kMIDIStatus_PitchWheel:
		event += 3; // NOLINT
		break;
	case kMIDIStatus_ProgramChange:
	case kMIDIStatus_ChannelPressure:
		event += 2; // NOLINT
		break;
	case kMIDIStatus_System:
		switch (c) {
		case 0xF0:                                           // NOLINT
			while (!isMIDIStatus(*++event) && event < end) { // NOLINT
				;
			}
			break;
		case 0xF1:      // NOLINT
		case 0xF3:      // NOLINT
			event += 2; // NOLINT
			break;
		case 0xF2:      // NOLINT
			event += 3; // NOLINT
			break;
		default:
			++event; // // NOLINT
			break;
		}
	}
	return (event >= end) ? end : event;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//	AUMIDIBase::HandleMIDIPacketList
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
OSStatus AUMIDIBase::HandleMIDIPacketList(const MIDIPacketList& pktlist)
{
	if (!mAUBaseInstance.IsInitialized()) {
		return kAudioUnitErr_Uninitialized;
	}

	auto nPackets = pktlist.numPackets;
	const MIDIPacket* pkt = pktlist.packet; // NOLINT

	while (nPackets-- > 0) {
		const Byte* event = pkt->data;               // NOLINT
		const Byte* packetEnd = event + pkt->length; // NOLINT
		const auto startFrame = static_cast<UInt32>(pkt->timeStamp);
		while (event < packetEnd) {
			const Byte status = event[0]; // NOLINT
			if (isMIDIStatus(status)) {
				// really a status byte (not sysex continuation)
				HandleMIDIEvent(status & 0xF0, status & 0x0F, event[1], event[2], // NOLINT
					startFrame);
				// note that we're generating a bogus channel number for system messages (0xF0-FF)
			}
			event = NextMIDIEvent(event, packetEnd);
		}
		pkt = reinterpret_cast<const MIDIPacket*>(packetEnd); // NOLINT
	}
	return noErr;
}

//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//	AUMIDIBase::HandleMIDIEvent
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
OSStatus AUMIDIBase::HandleMIDIEvent(
	UInt8 status, UInt8 channel, UInt8 data1, UInt8 data2, UInt32 inStartFrame)
{
	if (!mAUBaseInstance.IsInitialized()) {
		return kAudioUnitErr_Uninitialized;
	}

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

	OSStatus result = noErr;

	switch (status) {
	case kMIDIStatus_NoteOn:
		if (data2 != 0u) {
			result = HandleNoteOn(channel, data1, data2, inStartFrame);
		} else {
			// zero velocity translates to note off
			result = HandleNoteOff(channel, data1, data2, inStartFrame);
		}
		break;

	case kMIDIStatus_NoteOff:
		result = HandleNoteOff(channel, data1, data2, inStartFrame);
		break;

	default:
		result = HandleNonNoteEvent(status, channel, data1, data2, inStartFrame);
		break;
	}

	return result;
}

OSStatus AUMIDIBase::HandleNonNoteEvent(
	UInt8 status, UInt8 channel, UInt8 data1, UInt8 data2, UInt32 inStartFrame)
{
	OSStatus result = noErr;

	switch (status) {
	case kMIDIStatus_PitchWheel:
		result = HandlePitchWheel(channel, data1, data2, inStartFrame);
		break;

	case kMIDIStatus_ProgramChange:
		result = HandleProgramChange(channel, data1);
		break;

	case kMIDIStatus_ChannelPressure:
		result = HandleChannelPressure(channel, data1, inStartFrame);
		break;

	case kMIDIStatus_ControlChange: {
		switch (data1) {
		case kMIDIController_AllNotesOff:
			result = HandleAllNotesOff(channel);
			break;

		case kMIDIController_ResetAllControllers:
			result = HandleResetAllControllers(channel);
			break;

		case kMIDIController_AllSoundOff:
			result = HandleAllSoundOff(channel);
			break;

		default:
			result = HandleControlChange(channel, data1, data2, inStartFrame);
			break;
		}
		break;
	}

	case kMIDIStatus_PolyPressure:
		result = HandlePolyPressure(channel, data1, data2, inStartFrame);
		break;

	default:
		break;
	}
	return result;
}

OSStatus AUMIDIBase::SysEx(const UInt8* inData, UInt32 inLength)
{
	if (!mAUBaseInstance.IsInitialized()) {
		return kAudioUnitErr_Uninitialized;
	}

	return HandleSysEx(inData, inLength);
}

} // namespace ausdk
