/*!
	@file		AudioUnitSDK/AUOutputElement.cpp
	@copyright	© 2000-2023 Apple Inc. All rights reserved.
*/
#include <AudioUnitSDK/AUBase.h>
#include <AudioUnitSDK/AUOutputElement.h>
#include <AudioUnitSDK/AUUtility.h>

namespace ausdk {

AUOutputElement::AUOutputElement(AUBase& audioUnit) : AUIOElement(audioUnit) { AllocateBuffer(); }

AUOutputElement::AUOutputElement(AUBase& audioUnit, const AudioStreamBasicDescription& format)
	: AUIOElement{ audioUnit, format }
{
	AllocateBuffer();
}

OSStatus AUOutputElement::SetStreamFormat(const AudioStreamBasicDescription& desc)
{
	AUSDK_Require_noerr(AUIOElement::SetStreamFormat(desc)); // inherited
	AllocateBuffer();
	return noErr;
}

} // namespace ausdk
