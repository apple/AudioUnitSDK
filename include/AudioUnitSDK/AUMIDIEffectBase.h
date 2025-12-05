/*!
	@file		AudioUnitSDK/AUMIDIEffectBase.h
	@copyright	© 2000-2025 Apple Inc. All rights reserved.
*/
#ifndef AudioUnitSDK_AUMIDIEffectBase_h
#define AudioUnitSDK_AUMIDIEffectBase_h

// clang-format off
#include <AudioUnitSDK/AUConfig.h> // must come first
// clang-format on
#include <AudioUnitSDK/AUEffectBase.h>
#include <AudioUnitSDK/AUMIDIBase.h>

namespace ausdk {

AUSDK_BEGIN_NO_RT_WARNINGS

/*!
	@class	AUMIDIEffectBase
	@brief	Subclass of AUEffectBase and AUMIDIBase, providing an abstract base class for
			music effects.
*/
class AUMIDIEffectBase : public AUEffectBase, public AUMIDIBase {
public:
	explicit AUMIDIEffectBase(AudioComponentInstance inInstance, bool inProcessesInPlace = false);
	OSStatus MIDIEvent(UInt32 inStatus, UInt32 inData1, UInt32 inData2,
		UInt32 inOffsetSampleFrame) AUSDK_RTSAFE override
	{
		return AUMIDIBase::MIDIEvent(inStatus, inData1, inData2, inOffsetSampleFrame);
	}
	OSStatus SysEx(const UInt8* inData, UInt32 inLength) AUSDK_RTSAFE override
	{
		return AUMIDIBase::SysEx(inData, inLength);
	}
	OSStatus GetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope,
		AudioUnitElement inElement, UInt32& outDataSize, bool& outWritable) override;
	OSStatus GetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
		AudioUnitElement inElement, void* outData) override;
	OSStatus SetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
		AudioUnitElement inElement, const void* inData, UInt32 inDataSize) override;
};

AUSDK_END_NO_RT_WARNINGS

} // namespace ausdk

#endif // AudioUnitSDK_AUMIDIEffectBase_h
