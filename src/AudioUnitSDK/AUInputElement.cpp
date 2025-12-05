/*!
	@file		AudioUnitSDK/AUInputElement.cpp
	@copyright	© 2000-2025 Apple Inc. All rights reserved.
*/
#include <AudioUnitSDK/AUInputElement.h>
#include <AudioUnitSDK/AUUtility.h>

namespace ausdk {

AUSDK_BEGIN_NO_RT_WARNINGS

constexpr bool HasGoodBufferPointers(const AudioBufferList& abl, UInt32 nBytes) noexcept
{
	const AudioBuffer* buf = abl.mBuffers;                   // NOLINT
	for (UInt32 i = abl.mNumberBuffers; i > 0; --i, ++buf) { // NOLINT
		if (buf->mData == nullptr || buf->mDataByteSize < nBytes) {
			return false;
		}
	}
	return true;
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//	AUInputElement::SetConnection
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void AUInputElement::SetConnection(const AudioUnitConnection& conn)
{
	if (conn.sourceAudioUnit == nullptr) {
		Disconnect();
		return;
	}

	mInputType = EInputType::FromConnection;
	mConnection = conn;
	AllocateBuffer();
}

void AUInputElement::Disconnect()
{
	mInputType = EInputType::NoInput;
	IOBuffer().Deallocate();
}


//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//	AUInputElement::SetInputCallback
//
//~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
void AUInputElement::SetInputCallback(AURenderCallback proc, void* refCon)
{
	if (proc == nullptr) {
		Disconnect();
	} else {
		mInputType = EInputType::FromCallback;
		mInputProc = proc;
		mInputProcRefCon = refCon;
		AllocateBuffer();
	}
}

OSStatus AUInputElement::SetStreamFormat(const AudioStreamBasicDescription& fmt)
{
	const OSStatus err = AUIOElement::SetStreamFormat(fmt);
	if (err == noErr) {
		AllocateBuffer();
	}
	return err;
}

OSStatus AUInputElement::PullInput(AudioUnitRenderActionFlags& ioActionFlags,
	const AudioTimeStamp& inTimeStamp, AudioUnitElement inElement, UInt32 nFrames) AUSDK_RTSAFE
{
	AUSDK_Require(IsActive(), kAudioUnitErr_NoConnection);

	auto& iob = IOBuffer();

	ExpectedPtr<AudioBufferList> pullBuffer =
		(HasConnection() || !WillAllocateBuffer())
			? iob.PrepareNullBufferOrError(GetStreamFormat(), nFrames)
			: iob.PrepareBufferOrError(GetStreamFormat(), nFrames);
	if (!pullBuffer) {
		return pullBuffer.error();
	}

	return PullInputWithBufferList(ioActionFlags, inTimeStamp, inElement, nFrames, *pullBuffer);
}

AUSDK_END_NO_RT_WARNINGS

} // namespace ausdk
