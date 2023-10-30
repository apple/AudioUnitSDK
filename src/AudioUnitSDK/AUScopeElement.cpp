/*!
	@file		AudioUnitSDK/AUScopeElement.cpp
	@copyright	© 2000-2023 Apple Inc. All rights reserved.
*/
#include <AudioUnitSDK/AUBase.h>
#include <AudioUnitSDK/AUScopeElement.h>
#include <AudioUnitSDK/AUUtility.h>

#include <AudioToolbox/AudioUnitProperties.h>

#include <CoreFoundation/CFByteOrder.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>
#include <numeric>
#include <utility>

namespace ausdk {

//_____________________________________________________________________________
//
//	By default, parameterIDs may be arbitrarily spaced, and a flat map
//  will be used for access.  Calling UseIndexedParameters() will
//	instead use an STL vector for faster indexed access.
//	This assumes the paramIDs are numbered 0.....inNumberOfParameters-1
//	Call this before defining/adding any parameters with SetParameter()
//
void AUElement::UseIndexedParameters(UInt32 inNumberOfParameters)
{
	mIndexedParameters.resize(inNumberOfParameters);
	mUseIndexedParameters = true;
}

//_____________________________________________________________________________
//
//	Helper method.
//	returns whether the specified paramID is known to the element
//
bool AUElement::HasParameterID(AudioUnitParameterID paramID) const
{
	if (mUseIndexedParameters) {
		return paramID < mIndexedParameters.size();
	}

	return mParameters.find(paramID) != mParameters.end();
}

//_____________________________________________________________________________
//
//	caller assumes that this is actually an immediate parameter
//
AudioUnitParameterValue AUElement::GetParameter(AudioUnitParameterID paramID) const
{
	if (mUseIndexedParameters) {
		ausdk::ThrowExceptionIf(
			paramID >= mIndexedParameters.size(), kAudioUnitErr_InvalidParameter);
		return mIndexedParameters[paramID].load(std::memory_order_acquire);
	}
	const auto i = mParameters.find(paramID);
	ausdk::ThrowExceptionIf(i == mParameters.end(), kAudioUnitErr_InvalidParameter);
	return i->second.load(std::memory_order_acquire);
}

//_____________________________________________________________________________
//
void AUElement::SetParameter(
	AudioUnitParameterID paramID, AudioUnitParameterValue inValue, bool okWhenInitialized)
{
	if (mUseIndexedParameters) {
		ausdk::ThrowExceptionIf(
			paramID >= mIndexedParameters.size(), kAudioUnitErr_InvalidParameter);
		mIndexedParameters[paramID].store(inValue, std::memory_order_release);
	} else {
		const auto i = mParameters.find(paramID);

		if (i == mParameters.end()) {
			if (mAudioUnit.IsInitialized() && !okWhenInitialized) {
				// The AU should not be creating new parameters once initialized.
				// If a client tries to set an undefined parameter, we could throw as follows,
				// but this might cause a regression. So it is better to just fail silently.
				// Throw(kAudioUnitErr_InvalidParameter);
				AUSDK_LogError(
					"Warning: %s SetParameter for undefined param ID %u while initialized. "
					"Ignoring.",
					mAudioUnit.GetLoggingString(), static_cast<unsigned>(paramID));
			} else {
				// create new entry in map for the paramID (only happens first time)
				mParameters[paramID] = ParameterValue{ inValue };
			}
		} else {
			// paramID already exists in map so simply change its value
			i->second.store(inValue, std::memory_order_release);
		}
	}
}

//_____________________________________________________________________________
//
void AUElement::SetScheduledEvent(AudioUnitParameterID paramID,
	const AudioUnitParameterEvent& inEvent, UInt32 /*inSliceOffsetInBuffer*/,
	UInt32 /*inSliceDurationFrames*/, bool okWhenInitialized)
{
	if (inEvent.eventType != kParameterEvent_Immediate) {
		AUSDK_LogError("Warning: %s was passed a ramped parameter event but does not implement "
					   "them. Ignoring.",
			mAudioUnit.GetLoggingString());
		return;
	}
	SetParameter(paramID, inEvent.eventValues.immediate.value, okWhenInitialized); // NOLINT
}

//_____________________________________________________________________________
//
void AUElement::GetParameterList(AudioUnitParameterID* outList)
{
	if (mUseIndexedParameters) {
		const auto numParams = std::ssize(mIndexedParameters);
		std::iota(outList, std::next(outList, numParams), 0);
	} else {
		std::transform(mParameters.cbegin(), mParameters.cend(), outList,
			[](const auto& keyValue) { return keyValue.first; });
	}
}

//_____________________________________________________________________________
//
void AUElement::SaveState(AudioUnitScope scope, CFMutableDataRef data)
{
	AudioUnitParameterInfo paramInfo{};
	const CFIndex countOffset = CFDataGetLength(data);
	uint32_t paramsWritten = 0;

	const auto appendBytes = [data](const auto& value) {
		CFDataAppendBytes(data, reinterpret_cast<const UInt8*>(&value), sizeof(value)); // NOLINT
	};

	const auto appendParameter = [&](AudioUnitParameterID paramID, AudioUnitParameterValue value) {
		if (mAudioUnit.GetParameterInfo(scope, paramID, paramInfo) == noErr) {
			if ((paramInfo.flags & kAudioUnitParameterFlag_CFNameRelease) != 0u) {
				if (paramInfo.cfNameString != nullptr) {
					CFRelease(paramInfo.cfNameString);
				}
				if (paramInfo.unit == kAudioUnitParameterUnit_CustomUnit &&
					paramInfo.unitName != nullptr) {
					CFRelease(paramInfo.unitName);
				}
			}
			if (((paramInfo.flags & kAudioUnitParameterFlag_OmitFromPresets) != 0u) ||
				((paramInfo.flags & kAudioUnitParameterFlag_MeterReadOnly) != 0u)) {
				return;
			}
		}

		appendBytes(CFSwapInt32HostToBig(paramID));
		appendBytes(CFSwapInt32HostToBig(std::bit_cast<UInt32>(value)));

		++paramsWritten;
	};

	constexpr UInt32 placeholderCount = 0;
	appendBytes(placeholderCount);

	if (mUseIndexedParameters) {
		const auto numParams = static_cast<UInt32>(mIndexedParameters.size());
		for (UInt32 i = 0; i < numParams; i++) {
			appendParameter(i, mIndexedParameters[i]);
		}
	} else {
		for (const auto& item : mParameters) {
			appendParameter(item.first, item.second);
		}
	}

	const auto count_BE = CFSwapInt32HostToBig(paramsWritten);
	memcpy(CFDataGetMutableBytePtr(data) + countOffset, // NOLINT ptr math
		&count_BE, sizeof(count_BE));
}

//_____________________________________________________________________________
//
const UInt8* AUElement::RestoreState(const UInt8* state)
{
	const UInt8* p = state;
	const auto numParams = ExtractBigUInt32AndAdvance(p);

	for (UInt32 i = 0; i < numParams; ++i) {
		const auto parameterID = ExtractBigUInt32AndAdvance(p);
		const auto valueBytes = ExtractBigUInt32AndAdvance(p);
		const auto value = std::bit_cast<AudioUnitParameterValue>(valueBytes);

		SetParameter(parameterID, value);
	}
	return p;
}

//_____________________________________________________________________________
//
AUIOElement::AUIOElement(AUBase& audioUnit) : AUElement(audioUnit), mWillAllocate(true)
{
	mStreamFormat = AudioStreamBasicDescription{ .mSampleRate = AUBase::kAUDefaultSampleRate,
		.mFormatID = kAudioFormatLinearPCM,
		.mFormatFlags = AudioFormatFlags(kAudioFormatFlagsNativeFloatPacked) |
						AudioFormatFlags(kAudioFormatFlagIsNonInterleaved), // NOLINT
		.mBytesPerPacket = sizeof(float),
		.mFramesPerPacket = 1,
		.mBytesPerFrame = sizeof(float),
		.mChannelsPerFrame = 2,
		.mBitsPerChannel = 32, // NOLINT
		.mReserved = 0 };
}

//_____________________________________________________________________________
//
OSStatus AUIOElement::SetStreamFormat(const AudioStreamBasicDescription& format)
{
	mStreamFormat = format;

	// Clear the previous channel layout if it is inconsistent with the newly set format;
	// preserve it if it is acceptable, in case the new format has no layout.
	if (ChannelLayout().IsValid() && NumberChannels() != ChannelLayout().NumberChannels()) {
		RemoveAudioChannelLayout();
	}

	return noErr;
}

//_____________________________________________________________________________
// inFramesToAllocate == 0 implies the AudioUnit's max-frames-per-slice will be used
void AUIOElement::AllocateBuffer(UInt32 inFramesToAllocate)
{
	if (GetAudioUnit().HasBegunInitializing()) {
		UInt32 framesToAllocate =
			inFramesToAllocate > 0 ? inFramesToAllocate : GetAudioUnit().GetMaxFramesPerSlice();

		mIOBuffer.Allocate(
			mStreamFormat, (mWillAllocate && NeedsBufferSpace()) ? framesToAllocate : 0);
	}
}

//_____________________________________________________________________________
//
void AUIOElement::DeallocateBuffer() { mIOBuffer.Deallocate(); }

//_____________________________________________________________________________
//
//		AudioChannelLayout support

// return an empty vector (ie. NO channel layouts) if the AU doesn't require channel layout
// knowledge
std::vector<AudioChannelLayoutTag> AUIOElement::GetChannelLayoutTags() { return {}; }

// outLayoutPtr WILL be NULL if called to determine layout size
UInt32 AUIOElement::GetAudioChannelLayout(AudioChannelLayout* outLayoutPtr, bool& outWritable)
{
	outWritable = true;

	UInt32 size = mChannelLayout.IsValid() ? mChannelLayout.Size() : 0;
	if (size > 0 && outLayoutPtr != nullptr) {
		memcpy(outLayoutPtr, &mChannelLayout.Layout(), size);
	}

	return size;
}

// the incoming channel map will be at least as big as a basic AudioChannelLayout
// but its contents will determine its actual size
// Subclass should overide if channel map is writable
OSStatus AUIOElement::SetAudioChannelLayout(const AudioChannelLayout& inLayout)
{
	AUSDK_Require(NumberChannels() == AUChannelLayout::NumberChannels(inLayout),
		kAudioUnitErr_InvalidPropertyValue);
	mChannelLayout = inLayout;
	return noErr;
}

// Some units support optional usage of channel maps - typically converter units
// that can do channel remapping between different maps. In that optional case
// the user should be able to remove a channel map if that is possible.
// Typically this is NOT the case (e.g., the 3DMixer even in the stereo case
// needs to know if it is rendering to speakers or headphones)
OSStatus AUIOElement::RemoveAudioChannelLayout()
{
	mChannelLayout = {};
	return noErr;
}

//_____________________________________________________________________________
//
void AUScope::SetNumberOfElements(UInt32 numElements)
{
	if (mDelegate != nullptr) {
		return mDelegate->SetNumberOfElements(numElements);
	}

	if (numElements > mElements.size()) {
		mElements.reserve(numElements);
		while (numElements > mElements.size()) {
			auto elem = mCreator->CreateElement(GetScope(), static_cast<UInt32>(mElements.size()));
			mElements.push_back(std::move(elem));
		}
	} else {
		while (numElements < mElements.size()) {
			mElements.pop_back();
		}
	}
}

//_____________________________________________________________________________
//
bool AUScope::HasElementWithName() const
{
	for (UInt32 i = 0; i < GetNumberOfElements(); ++i) {
		AUElement* const el = GetElement(i);
		if ((el != nullptr) && el->HasName()) {
			return true;
		}
	}
	return false;
}

//_____________________________________________________________________________
//

void AUScope::AddElementNamesToDict(CFMutableDictionaryRef inNameDict) const
{
	if (HasElementWithName()) {
		const auto elementDict =
			Owned<CFMutableDictionaryRef>::from_create(CFDictionaryCreateMutable(
				nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
		for (UInt32 i = 0; i < GetNumberOfElements(); ++i) {
			AUElement* const el = GetElement(i);
			if (el != nullptr && el->HasName()) {
				const auto key = Owned<CFStringRef>::from_create(CFStringCreateWithFormat(
					nullptr, nullptr, CFSTR("%u"), static_cast<unsigned>(i)));
				CFDictionarySetValue(*elementDict, *key, *el->GetName());
			}
		}

		const auto key = Owned<CFStringRef>::from_create(
			CFStringCreateWithFormat(nullptr, nullptr, CFSTR("%u"), static_cast<unsigned>(mScope)));
		CFDictionarySetValue(inNameDict, *key, *elementDict);
	}
}

//_____________________________________________________________________________
//
std::vector<AudioUnitElement> AUScope::RestoreElementNames(CFDictionaryRef inNameDict) const
{
	// first we have to see if we have enough elements
	std::vector<AudioUnitElement> restoredElements;
	const auto maxElNum = GetNumberOfElements();

	const auto dictSize =
		static_cast<size_t>(std::max(CFDictionaryGetCount(inNameDict), CFIndex(0)));
	std::vector<CFStringRef> keys(dictSize);
	CFDictionaryGetKeysAndValues(
		inNameDict, reinterpret_cast<const void**>(keys.data()), nullptr); // NOLINT
	for (size_t i = 0; i < dictSize; i++) {
		unsigned int intKey = 0;
		std::array<char, 32> string{};
		CFStringGetCString(keys[i], string.data(), string.size(), kCFStringEncodingASCII);
		const int result = sscanf(string.data(), "%u", &intKey); // NOLINT
		// check if sscanf succeeded and element index is less than max elements.
		if ((result != 0) && (static_cast<UInt32>(intKey) < maxElNum)) {
			auto* const elName =
				static_cast<CFStringRef>(CFDictionaryGetValue(inNameDict, keys[i]));
			if ((elName != nullptr) && (CFGetTypeID(elName) == CFStringGetTypeID())) {
				AUElement* const element = GetElement(intKey);
				if (element != nullptr) {
					element->SetName(elName);
					restoredElements.push_back(intKey);
				}
			}
		}
	}

	return restoredElements;
}

void AUScope::SaveState(CFMutableDataRef data) const
{
	const AudioUnitElement nElems = GetNumberOfElements();
	for (AudioUnitElement ielem = 0; ielem < nElems; ++ielem) {
		AUElement* const element = GetElement(ielem);
		const UInt32 numParams = element->GetNumberOfParameters();
		if (numParams > 0) {
			const struct {
				const UInt32 scope;
				const UInt32 element;
			} hdr{ .scope = CFSwapInt32HostToBig(GetScope()),
				.element = CFSwapInt32HostToBig(ielem) };
			static_assert(sizeof(hdr) == (sizeof(hdr.scope) + sizeof(hdr.element)));
			CFDataAppendBytes(data, reinterpret_cast<const UInt8*>(&hdr), sizeof(hdr)); // NOLINT

			element->SaveState(mScope, data);
		}
	}
}

const UInt8* AUScope::RestoreState(const UInt8* state) const
{
	const UInt8* p = state;
	const auto elementIdx = ExtractBigUInt32AndAdvance(p);
	AUElement* const element = GetElement(elementIdx);
	if (element == nullptr) {
		const auto numParams = ExtractBigUInt32AndAdvance(p);
		constexpr auto entrySize = sizeof(AudioUnitParameterID) + sizeof(AudioUnitParameterValue);
		p += numParams * entrySize; // NOLINT
	} else {
		p = element->RestoreState(p);
	}

	return p;
}

} // namespace ausdk
