/*!
	@file		AudioUnitSDK/AUBase.cpp
	@copyright	© 2000-2024 Apple Inc. All rights reserved.
*/
// clang-format off
#include <AudioUnitSDK/AUConfig.h> // must come first
// clang-format on
#include <AudioUnitSDK/AUBase.h>
#include <AudioUnitSDK/AUInputElement.h>
#include <AudioUnitSDK/AUOutputElement.h>
#include <AudioUnitSDK/AUUtility.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <limits>
#include <span>

namespace ausdk {

#if TARGET_OS_MAC && (TARGET_CPU_X86 || TARGET_CPU_X86_64)

class DenormalDisabler {
public:
	DenormalDisabler() noexcept : mSavedMXCSR(GetCSR()) { SetCSR(mSavedMXCSR | 0x8040); }

	DenormalDisabler(const DenormalDisabler&) = delete;
	DenormalDisabler(DenormalDisabler&&) = delete;
	DenormalDisabler& operator=(const DenormalDisabler&) = delete;
	DenormalDisabler& operator=(DenormalDisabler&&) = delete;

	~DenormalDisabler() noexcept { SetCSR(mSavedMXCSR); }

private:
#if 0 // not sure if this is right: // #if __has_include(<xmmintrin.h>)
	static unsigned GetCSR() noexcept { return _mm_getcsr(); }
	static void SetCSR(unsigned x) noexcept { _mm_setcsr(x); }
#else
	// our compiler does ALL floating point with SSE
	static unsigned GetCSR() noexcept
	{
		unsigned result{};
		asm volatile("stmxcsr %0" : "=m"(*&result)); // NOLINT asm
		return result;
	}
	static void SetCSR(unsigned a) noexcept
	{
		unsigned temp = a;
		asm volatile("ldmxcsr %0" : : "m"(*&temp)); // NOLINT asm
	}
#endif

	const unsigned mSavedMXCSR;
};

#else
// while denormals can be flushed to zero on ARM processors, there is no performance benefit
class DenormalDisabler {
public:
	DenormalDisabler() = default;
};
#endif

static CFStringRef GetPresetDefaultName() noexcept
{
	static const auto name = CFSTR("Untitled");
	return name;
}

static constexpr auto kNoLastRenderedSampleTime = std::numeric_limits<Float64>::lowest();

//_____________________________________________________________________________
//
AUBase::AUBase(AudioComponentInstance inInstance, UInt32 numInputElements, UInt32 numOutputElements,
	UInt32 numGroupElements)
	: ComponentBase(inInstance), mInitNumInputEls(numInputElements),
	  mInitNumOutputEls(numOutputElements), mInitNumGroupEls(numGroupElements),
	  mLogString(CreateLoggingString())
{
	ResetRenderTime();

	GlobalScope().Initialize(this, kAudioUnitScope_Global, 1);

	mCurrentPreset.presetNumber = -1;
	CFRetain(mCurrentPreset.presetName = GetPresetDefaultName());
}

//_____________________________________________________________________________
//
AUBase::~AUBase()
{
	if (mCurrentPreset.presetName != nullptr) {
		CFRelease(mCurrentPreset.presetName);
	}
}

//_____________________________________________________________________________
//
void AUBase::PostConstructorInternal()
{
	// invoked after construction because they are virtual methods and/or call virtual methods
	if (mMaxFramesPerSlice == 0) {
		SetMaxFramesPerSlice(kAUDefaultMaxFramesPerSlice);
	}
	CreateElements();
}

//_____________________________________________________________________________
//
void AUBase::PreDestructorInternal()
{
	// this is called from the ComponentBase dispatcher, which doesn't know anything about our
	// (optional) lock
	const AUEntryGuard guard(mAUMutex);
	DoCleanup();
}

//_____________________________________________________________________________
//
void AUBase::CreateElements()
{
	if (!mElementsCreated) {
		Inputs().Initialize(this, kAudioUnitScope_Input, mInitNumInputEls);
		Outputs().Initialize(this, kAudioUnitScope_Output, mInitNumOutputEls);
		Groups().Initialize(this, kAudioUnitScope_Group, mInitNumGroupEls);
		CreateExtendedElements();

		mElementsCreated = true;
	}
}

//_____________________________________________________________________________
//
void AUBase::SetMaxFramesPerSlice(UInt32 nFrames)
{
	if (nFrames == mMaxFramesPerSlice) {
		return;
	}

	mMaxFramesPerSlice = nFrames;
	if (mBuffersAllocated) {
		ReallocateBuffers();
	}
	PropertyChanged(kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, 0);
}

//_____________________________________________________________________________
//
OSStatus AUBase::CanSetMaxFrames() const
{
	return IsInitialized() ? kAudioUnitErr_Initialized : OSStatus(noErr);
}

//_____________________________________________________________________________
//
void AUBase::ReallocateBuffers()
{
	CreateElements();

	const UInt32 nOutputs = Outputs().GetNumberOfElements();
	for (UInt32 i = 0; i < nOutputs; ++i) {
		Output(i).AllocateBuffer(); // does no work if already allocated
	}
	const UInt32 nInputs = Inputs().GetNumberOfElements();
	for (UInt32 i = 0; i < nInputs; ++i) {
		Input(i).AllocateBuffer(); // does no work if already allocated
	}
	mBuffersAllocated = true;
}

//_____________________________________________________________________________
//
void AUBase::DeallocateIOBuffers()
{
	if (!mBuffersAllocated) {
		return;
	}

	const UInt32 nOutputs = Outputs().GetNumberOfElements();
	for (UInt32 i = 0; i < nOutputs; ++i) {
		Output(i).DeallocateBuffer();
	}
	const UInt32 nInputs = Inputs().GetNumberOfElements();
	for (UInt32 i = 0; i < nInputs; ++i) {
		Input(i).DeallocateBuffer();
	}
	mBuffersAllocated = false;
}

//_____________________________________________________________________________
//
OSStatus AUBase::DoInitialize()
{
	if (!mInitialized) {
		AUSDK_Require_noerr(Initialize());
		if (CanScheduleParameters()) {
			mParamEventList.reserve(24); // NOLINT magic #
		}
		mHasBegunInitializing = true;
		ReallocateBuffers(); // calls CreateElements()
		mInitialized = true; // signal that it's okay to render
		std::atomic_thread_fence(std::memory_order_seq_cst);
	}

	return noErr;
}

//_____________________________________________________________________________
//
OSStatus AUBase::Initialize() { return noErr; }

//_____________________________________________________________________________
//
void AUBase::DoCleanup()
{
	if (mInitialized) {
		Cleanup();
	}

	DeallocateIOBuffers();
	ResetRenderTime();

	mInitialized = false;
	mHasBegunInitializing = false;
}

//_____________________________________________________________________________
//
void AUBase::Cleanup() {}

//_____________________________________________________________________________
//
OSStatus AUBase::DoReset(AudioUnitScope inScope, AudioUnitElement inElement)
{
	ResetRenderTime();
	return Reset(inScope, inElement);
}

//_____________________________________________________________________________
//
OSStatus AUBase::Reset(AudioUnitScope /*inScope*/, AudioUnitElement /*inElement*/) { return noErr; }

//_____________________________________________________________________________
//
OSStatus AUBase::DispatchGetPropertyInfo(AudioUnitPropertyID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, UInt32& outDataSize, bool& outWritable)
{
	OSStatus result = noErr;
	bool validateElement = true;

	switch (inID) {
	case kAudioUnitProperty_MakeConnection:
		AUSDK_Require(inScope == kAudioUnitScope_Input || inScope == kAudioUnitScope_Global,
			kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(AudioUnitConnection);
		outWritable = true;
		break;

	case kAudioUnitProperty_SetRenderCallback:
		AUSDK_Require(inScope == kAudioUnitScope_Input || inScope == kAudioUnitScope_Global,
			kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(AURenderCallbackStruct);
		outWritable = true;
		break;

	case kAudioUnitProperty_StreamFormat:
		outDataSize = sizeof(AudioStreamBasicDescription);
		outWritable = IsStreamFormatWritable(inScope, inElement);
		break;

	case kAudioUnitProperty_SampleRate:
		outDataSize = sizeof(Float64);
		outWritable = IsStreamFormatWritable(inScope, inElement);
		break;

	case kAudioUnitProperty_ClassInfo:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(CFPropertyListRef);
		outWritable = true;
		break;

	case kAudioUnitProperty_FactoryPresets:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require_noerr(GetPresets(nullptr));
		outDataSize = sizeof(CFArrayRef);
		outWritable = false;
		break;

	case kAudioUnitProperty_PresentPreset:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(AUPreset);
		outWritable = true;
		break;

	case kAudioUnitProperty_ElementName:
		outDataSize = sizeof(CFStringRef);
		outWritable = true;
		break;

	case kAudioUnitProperty_ParameterList: {
		UInt32 nParams = 0;
		AUSDK_Require_noerr(GetParameterList(inScope, nullptr, nParams));
		outDataSize = sizeof(AudioUnitParameterID) * nParams;
		outWritable = false;
		validateElement = false;
		break;
	}

	case kAudioUnitProperty_ParameterInfo:
		outDataSize = sizeof(AudioUnitParameterInfo);
		outWritable = false;
		validateElement = false;
		break;

	case kAudioUnitProperty_ParameterHistoryInfo:
		outDataSize = sizeof(AudioUnitParameterHistoryInfo);
		outWritable = false;
		validateElement = false;
		break;

	case kAudioUnitProperty_ElementCount:
		outDataSize = sizeof(UInt32);
		outWritable = BusCountWritable(inScope);
		validateElement = false;
		break;

	case kAudioUnitProperty_Latency:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(Float64);
		outWritable = false;
		break;

	case kAudioUnitProperty_TailTime:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(SupportsTail(), kAudioUnitErr_InvalidProperty);
		outDataSize = sizeof(Float64);
		outWritable = false;
		break;

	case kAudioUnitProperty_MaximumFramesPerSlice:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(UInt32);
		outWritable = true;
		break;

	case kAudioUnitProperty_LastRenderError:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(OSStatus);
		outWritable = false;
		break;

	case kAudioUnitProperty_SupportedNumChannels: {
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		const UInt32 num = SupportedNumChannels(nullptr);
		AUSDK_Require(num != 0u, kAudioUnitErr_InvalidProperty);
		outDataSize = sizeof(AUChannelInfo) * num;
		outWritable = false;
		break;
	}

	case kAudioUnitProperty_SupportedChannelLayoutTags: {
		const auto tags = GetChannelLayoutTags(inScope, inElement);
		AUSDK_Require(!tags.empty(), kAudioUnitErr_InvalidProperty);
		outDataSize = static_cast<UInt32>(std::span(tags).size_bytes());
		outWritable = false;
		validateElement = false; // already done it
		break;
	}

	case kAudioUnitProperty_AudioChannelLayout: {
		outWritable = false;
		outDataSize = GetAudioChannelLayout(inScope, inElement, nullptr, outWritable);
		if (outDataSize != 0u) {
			result = noErr;
		} else {
			const auto tags = GetChannelLayoutTags(inScope, inElement);
			return tags.empty() ? kAudioUnitErr_InvalidProperty
								: kAudioUnitErr_InvalidPropertyValue;
		}
		validateElement = false; // already done it
		break;
	}

	case kAudioUnitProperty_ShouldAllocateBuffer:
		AUSDK_Require((inScope == kAudioUnitScope_Input || inScope == kAudioUnitScope_Output),
			kAudioUnitErr_InvalidScope);
		outWritable = true;
		outDataSize = sizeof(UInt32);
		break;

	case kAudioUnitProperty_ParameterValueStrings:
		AUSDK_Require_noerr(GetParameterValueStrings(inScope, inElement, nullptr));
		outDataSize = sizeof(CFArrayRef);
		outWritable = false;
		validateElement = false;
		break;

	case kAudioUnitProperty_HostCallbacks:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(mHostCallbackInfo);
		outWritable = true;
		break;

	case kAudioUnitProperty_ContextName:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(CFStringRef);
		outWritable = true;
		break;


#if AUSDK_HAVE_UI && !TARGET_OS_IPHONE
	case kAudioUnitProperty_IconLocation:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(HasIcon(), kAudioUnitErr_InvalidProperty);
		outWritable = false;
		outDataSize = sizeof(CFURLRef);
		break;
#endif

	case kAudioUnitProperty_ParameterClumpName:
		outDataSize = sizeof(AudioUnitParameterNameInfo);
		outWritable = false;
		break;

	case kAudioUnitProperty_LastRenderSampleTime:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(Float64);
		outWritable = false;
		break;

	case kAudioUnitProperty_NickName:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		outDataSize = sizeof(CFStringRef);
		outWritable = true;
		break;

	default:
		result = GetPropertyInfo(inID, inScope, inElement, outDataSize, outWritable);
		validateElement = false;
		break;
	}

	if ((result == noErr) && validateElement) {
		AUSDK_Require(GetElement(inScope, inElement) != nullptr, kAudioUnitErr_InvalidElement);
	}

	return result;
}

//_____________________________________________________________________________
//
// NOLINTNEXTLINE(misc-no-recursion) with SaveState
OSStatus AUBase::DispatchGetProperty(
	AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement, void* outData)
{
	// NOTE: We're currently only called from AUBase::ComponentEntryDispatch, which
	// calls DispatchGetPropertyInfo first, which performs validation of the scope/element,
	// and ensures that the outData buffer is non-null and large enough.
	OSStatus result = noErr;

	switch (inID) {
	case kAudioUnitProperty_StreamFormat:
		Serialize(GetStreamFormat(inScope, inElement), outData);
		break;

	case kAudioUnitProperty_SampleRate:
		Serialize(GetStreamFormat(inScope, inElement).mSampleRate, outData);
		break;

	case kAudioUnitProperty_ParameterList: {
		UInt32 parameterCount = 0;
		result = GetParameterList(inScope, nullptr, parameterCount);
		if (result == noErr) {
			std::vector<AudioUnitParameterID> parameterIDs(parameterCount);
			result = GetParameterList(inScope, parameterIDs.data(), parameterCount);
			if (result == noErr) {
				Serialize(std::span(parameterIDs), outData);
			}
		}
		break;
	}

	case kAudioUnitProperty_ParameterInfo: {
		AudioUnitParameterInfo parameterInfo{};
		result = GetParameterInfo(inScope, inElement, parameterInfo);
		Serialize(parameterInfo, outData);
		break;
	}

	case kAudioUnitProperty_ParameterHistoryInfo: {
		AudioUnitParameterHistoryInfo info{};
		result = GetParameterHistoryInfo(
			inScope, inElement, info.updatesPerSecond, info.historyDurationInSeconds);
		Serialize(info, outData);
		break;
	}

	case kAudioUnitProperty_ClassInfo: {
		CFPropertyListRef plist = nullptr;
		result = SaveState(&plist);
		Serialize(plist, outData);
		break;
	}

	case kAudioUnitProperty_FactoryPresets: {
		CFArrayRef array = nullptr;
		result = GetPresets(&array);
		Serialize(array, outData);
		break;
	}

	case kAudioUnitProperty_PresentPreset: {
		Serialize(mCurrentPreset, outData);

		// retain current string (as client owns a reference to it and will release it)
		if ((inID == kAudioUnitProperty_PresentPreset) && (mCurrentPreset.presetName != nullptr)) {
			CFRetain(mCurrentPreset.presetName);
		}

		result = noErr;
		break;
	}

	case kAudioUnitProperty_ElementName: {
		const AUElement* const element = GetElement(inScope, inElement);
		const CFStringRef name = *element->GetName();
		AUSDK_Require(name != nullptr, kAudioUnitErr_PropertyNotInUse);
		CFRetain(name); // must return a +1 reference
		Serialize(name, outData);
		break;
	}

	case kAudioUnitProperty_ElementCount:
		Serialize(GetScope(inScope).GetNumberOfElements(), outData);
		break;

	case kAudioUnitProperty_Latency:
		Serialize(GetLatency(), outData);
		break;

	case kAudioUnitProperty_TailTime:
		AUSDK_Require(SupportsTail(), kAudioUnitErr_InvalidProperty);
		Serialize(GetTailTime(), outData);
		break;

	case kAudioUnitProperty_MaximumFramesPerSlice:
		Serialize(mMaxFramesPerSlice, outData);
		break;

	case kAudioUnitProperty_LastRenderError:
		Serialize(mLastRenderError, outData);
		mLastRenderError = 0;
		break;

	case kAudioUnitProperty_SupportedNumChannels: {
		const AUChannelInfo* infos = nullptr;
		const auto count = SupportedNumChannels(&infos);
		if ((count > 0) && (infos != nullptr)) {
			Serialize(std::span(infos, count), outData);
		}
		break;
	}

	case kAudioUnitProperty_SupportedChannelLayoutTags: {
		const auto tags = GetChannelLayoutTags(inScope, inElement);
		AUSDK_Require(!tags.empty(), kAudioUnitErr_InvalidProperty);
		Serialize(std::span(tags), outData);
		break;
	}

	case kAudioUnitProperty_AudioChannelLayout: {
		auto* const acl = static_cast<AudioChannelLayout*>(outData);
		bool writable = false;
		const auto dataSize = GetAudioChannelLayout(inScope, inElement, acl, writable);
		AUSDK_Require(dataSize != 0, kAudioUnitErr_InvalidProperty);
		break;
	}

	case kAudioUnitProperty_ShouldAllocateBuffer: {
		const auto& element = IOElement(inScope, inElement);
		Serialize(static_cast<UInt32>(element.WillAllocateBuffer()), outData);
		break;
	}

	case kAudioUnitProperty_ParameterValueStrings: {
		CFArrayRef array = nullptr;
		result = GetParameterValueStrings(inScope, inElement, &array);
		Serialize(array, outData);
		break;
	}

	case kAudioUnitProperty_HostCallbacks:
		Serialize(mHostCallbackInfo, outData);
		break;

	case kAudioUnitProperty_ContextName: {
		const auto* const name = *mContextName;
		Serialize(name, outData);
		if (name) {
			CFRetain(name); // must return a +1 reference
			result = noErr;
		} else {
			result = kAudioUnitErr_PropertyNotInUse;
		}
		break;
	}

#if AUSDK_HAVE_UI && !TARGET_OS_IPHONE
	case kAudioUnitProperty_IconLocation: {
		const auto iconLocation = CopyIconLocation();
		AUSDK_Require(iconLocation != nullptr, kAudioUnitErr_InvalidProperty);
		Serialize(iconLocation, outData);
		break;
	}
#endif

	case kAudioUnitProperty_ParameterClumpName: {
		auto clumpInfo = Deserialize<AudioUnitParameterNameInfo>(outData);
		AUSDK_Require(clumpInfo.inID != kAudioUnitClumpID_System,
			kAudioUnitErr_InvalidPropertyValue); // this ID value is reserved
		result = CopyClumpName(inScope, clumpInfo.inID,
			static_cast<UInt32>(std::max(clumpInfo.inDesiredLength, SInt32(0))),
			&clumpInfo.outName);
		Serialize(clumpInfo, outData);

		// this is provided for compatbility with existing implementations that don't know
		// about this new mechanism
		if (result == kAudioUnitErr_InvalidProperty) {
			result = GetProperty(inID, inScope, inElement, outData);
		}
		break;
	}

	case kAudioUnitProperty_LastRenderSampleTime:
		Serialize(mCurrentRenderTime.mSampleTime, outData);
		break;

	case kAudioUnitProperty_NickName: {
		const auto* const name = *mNickName;
		Serialize(name, outData);
		// Ownership follows Core Foundation's 'Copy Rule'
		if (name) {
			CFRetain(name);
		}
		break;
	}

	default:
		result = GetProperty(inID, inScope, inElement, outData);
		break;
	}
	return result;
}


//_____________________________________________________________________________
//
// Note: We can be sure inData is non-null; otherwise RemoveProperty would have been called.
// NOLINTNEXTLINE(misc-no-recursion) with RestoreState
OSStatus AUBase::DispatchSetProperty(AudioUnitPropertyID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, const void* inData, UInt32 inDataSize)
{
	OSStatus result = noErr;

	switch (inID) {
	case kAudioUnitProperty_MakeConnection: {
		AUSDK_Require(
			inDataSize >= sizeof(AudioUnitConnection), kAudioUnitErr_InvalidPropertyValue);
		const auto connection = Deserialize<AudioUnitConnection>(inData);
		result = SetConnection(connection);
		break;
	}

	case kAudioUnitProperty_SetRenderCallback: {
		AUSDK_Require(
			inDataSize >= sizeof(AURenderCallbackStruct), kAudioUnitErr_InvalidPropertyValue);
		const auto callback = Deserialize<AURenderCallbackStruct>(inData);
		result = SetInputCallback(kAudioUnitProperty_SetRenderCallback, inElement,
			callback.inputProc, callback.inputProcRefCon);
		break;
	}

	case kAudioUnitProperty_ElementCount:
		AUSDK_Require(inDataSize == sizeof(UInt32), kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require(BusCountWritable(inScope), kAudioUnitErr_PropertyNotWritable);
		result = SetBusCount(inScope, Deserialize<UInt32>(inData));
		if (result == noErr) {
			PropertyChanged(inID, inScope, inElement);
		}
		break;

	case kAudioUnitProperty_MaximumFramesPerSlice:
		AUSDK_Require(inDataSize == sizeof(UInt32), kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require_noerr(CanSetMaxFrames());
		SetMaxFramesPerSlice(Deserialize<UInt32>(inData));
		break;

	case kAudioUnitProperty_StreamFormat: {
		constexpr static UInt32 kMinimumValidASBDSize = 36;
		AUSDK_Require(inDataSize >= kMinimumValidASBDSize, kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require(GetElement(inScope, inElement) != nullptr, kAudioUnitErr_InvalidElement);

		AudioStreamBasicDescription newDesc{};
		// now we're going to be ultra conservative! because of discrepancies between
		// sizes of this struct based on aligment padding inconsistencies
		memcpy(&newDesc, inData, kMinimumValidASBDSize);

		AUSDK_Require(ASBD::MinimalSafetyCheck(newDesc), kAudioUnitErr_FormatNotSupported);

		AUSDK_Require(ValidFormat(inScope, inElement, newDesc), kAudioUnitErr_FormatNotSupported);

		const auto curDesc = GetStreamFormat(inScope, inElement);

		if (!ASBD::IsEqual(curDesc, newDesc)) {
			AUSDK_Require(
				IsStreamFormatWritable(inScope, inElement), kAudioUnitErr_PropertyNotWritable);
			result = ChangeStreamFormat(inScope, inElement, curDesc, newDesc);
		}
		break;
	}

	case kAudioUnitProperty_SampleRate: {
		AUSDK_Require(inDataSize == sizeof(Float64), kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require(GetElement(inScope, inElement) != nullptr, kAudioUnitErr_InvalidElement);

		const auto curDesc = GetStreamFormat(inScope, inElement);
		AudioStreamBasicDescription newDesc = curDesc;
		newDesc.mSampleRate = Deserialize<Float64>(inData);

		AUSDK_Require(ValidFormat(inScope, inElement, newDesc), kAudioUnitErr_FormatNotSupported);

		if (!ASBD::IsEqual(curDesc, newDesc)) {
			AUSDK_Require(
				IsStreamFormatWritable(inScope, inElement), kAudioUnitErr_PropertyNotWritable);
			result = ChangeStreamFormat(inScope, inElement, curDesc, newDesc);
		}
		break;
	}

	case kAudioUnitProperty_AudioChannelLayout: {
		// Check the variable-size AudioChannelLayout object size:
		// - Is the memory area big enough so that AudioChannelLayout::mNumberChannelDescriptions
		// can be read?
		AUSDK_Require(
			inDataSize >= AUChannelLayout::DataByteSize(0), kAudioUnitErr_InvalidPropertyValue);
		// - Is the whole size consistent?
		using NumberChannelDescriptionsT = decltype(AudioChannelLayout::mNumberChannelDescriptions);
		constexpr auto numberChannelDescriptionsOffset =
			offsetof(AudioChannelLayout, mNumberChannelDescriptions);
		const auto numberChannelDescriptions = Deserialize<NumberChannelDescriptionsT>(
			static_cast<const std::byte*>(inData) + numberChannelDescriptionsOffset);
		AUSDK_Require(inDataSize >= AUChannelLayout::DataByteSize(numberChannelDescriptions),
			kAudioUnitErr_InvalidPropertyValue);

		// copy incoming data to storage aligned for the object type
		const size_t padding = ((inDataSize % sizeof(AudioChannelLayout)) > 0) ? 1 : 0;
		std::vector<AudioChannelLayout> layout((inDataSize / sizeof(AudioChannelLayout)) + padding);
		std::memcpy(layout.data(), inData, inDataSize);

		result = SetAudioChannelLayout(inScope, inElement, layout.data());
		if (result == noErr) {
			PropertyChanged(inID, inScope, inElement);
		}
		break;
	}

	case kAudioUnitProperty_ClassInfo:
		AUSDK_Require(inDataSize == sizeof(CFPropertyListRef), kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		result = RestoreState(Deserialize<CFPropertyListRef>(inData));
		break;

	case kAudioUnitProperty_PresentPreset: {
		AUSDK_Require(inDataSize == sizeof(AUPreset), kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		const auto newPreset = Deserialize<AUPreset>(inData);

		if (newPreset.presetNumber >= 0) {
			result = NewFactoryPresetSet(newPreset);
			// NewFactoryPresetSet SHOULD call SetAFactoryPreset if the preset is valid
			// from its own list of preset number->name
			if (result == noErr) {
				PropertyChanged(inID, inScope, inElement);
			}
		} else if (newPreset.presetName != nullptr) {
			result = NewCustomPresetSet(newPreset);
			if (result == noErr) {
				PropertyChanged(inID, inScope, inElement);
			}
		} else {
			result = kAudioUnitErr_InvalidPropertyValue;
		}
		break;
	}

	case kAudioUnitProperty_ElementName: {
		AUSDK_Require(GetElement(inScope, inElement) != nullptr, kAudioUnitErr_InvalidElement);
		AUSDK_Require(inDataSize == sizeof(CFStringRef), kAudioUnitErr_InvalidPropertyValue);
		const auto element = GetScope(inScope).GetElement(inElement);
		element->SetName(Deserialize<CFStringRef>(inData));
		PropertyChanged(inID, inScope, inElement);
		break;
	}

	case kAudioUnitProperty_ShouldAllocateBuffer: {
		AUSDK_Require((inScope == kAudioUnitScope_Input || inScope == kAudioUnitScope_Output),
			kAudioUnitErr_InvalidScope);
		AUSDK_Require(GetElement(inScope, inElement) != nullptr, kAudioUnitErr_InvalidElement);
		AUSDK_Require(inDataSize == sizeof(UInt32), kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require(!IsInitialized(), kAudioUnitErr_Initialized);

		auto& element = IOElement(inScope, inElement);
		element.SetWillAllocateBuffer(Deserialize<UInt32>(inData) != 0);
		break;
	}

	case kAudioUnitProperty_HostCallbacks: {
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		const auto availSize = std::min(static_cast<size_t>(inDataSize), sizeof(HostCallbackInfo));
		const bool hasChanged = memcmp(&mHostCallbackInfo, inData, availSize) != 0;
		mHostCallbackInfo = {};
		memcpy(&mHostCallbackInfo, inData, availSize);
		if (hasChanged) {
			PropertyChanged(inID, inScope, inElement);
		}
		break;
	}

	case kAudioUnitProperty_ContextName:
		AUSDK_Require(inDataSize == sizeof(CFStringRef), kAudioUnitErr_InvalidPropertyValue);
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		mContextName = Deserialize<CFStringRef>(inData);
		PropertyChanged(inID, inScope, inElement);
		break;

	case kAudioUnitProperty_NickName:
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		AUSDK_Require(inDataSize == sizeof(CFStringRef), kAudioUnitErr_InvalidPropertyValue);
		mNickName = Deserialize<CFStringRef>(inData);
		PropertyChanged(inID, inScope, inElement);
		break;

	default:
		result = SetProperty(inID, inScope, inElement, inData, inDataSize);
		if (result == noErr) {
			PropertyChanged(inID, inScope, inElement);
		}

		break;
	}
	return result;
}

//_____________________________________________________________________________
//
OSStatus AUBase::DispatchRemovePropertyValue(
	AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement)
{
	OSStatus result = noErr;
	switch (inID) {
	case kAudioUnitProperty_AudioChannelLayout: {
		result = RemoveAudioChannelLayout(inScope, inElement);
		if (result == noErr) {
			PropertyChanged(inID, inScope, inElement);
		}
		break;
	}

	case kAudioUnitProperty_HostCallbacks: {
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		constexpr decltype(mHostCallbackInfo) zeroInfo{};
		if (memcmp(&mHostCallbackInfo, &zeroInfo, sizeof(mHostCallbackInfo)) != 0) {
			mHostCallbackInfo = {};
			PropertyChanged(inID, inScope, inElement);
		}
		break;
	}

	case kAudioUnitProperty_ContextName:
		mContextName = nullptr;
		result = noErr;
		break;

	case kAudioUnitProperty_NickName: {
		AUSDK_Require(inScope == kAudioUnitScope_Global, kAudioUnitErr_InvalidScope);
		mNickName = nullptr;
		PropertyChanged(inID, inScope, inElement);
		break;
	}

	default:
		result = RemovePropertyValue(inID, inScope, inElement);
		break;
	}

	return result;
}

//_____________________________________________________________________________
//
OSStatus AUBase::GetPropertyInfo(AudioUnitPropertyID /*inID*/, AudioUnitScope /*inScope*/,
	AudioUnitElement /*inElement*/, UInt32& /*outDataSize*/, bool& /*outWritable*/)
{
	return kAudioUnitErr_InvalidProperty;
}


//_____________________________________________________________________________
//
OSStatus AUBase::GetProperty(AudioUnitPropertyID /*inID*/, AudioUnitScope /*inScope*/,
	AudioUnitElement /*inElement*/, void* /*outData*/)
{
	return kAudioUnitErr_InvalidProperty;
}


//_____________________________________________________________________________
//
OSStatus AUBase::SetProperty(AudioUnitPropertyID /*inID*/, AudioUnitScope /*inScope*/,
	AudioUnitElement /*inElement*/, const void* /*inData*/, UInt32 /*inDataSize*/)
{
	return kAudioUnitErr_InvalidProperty;
}

//_____________________________________________________________________________
//
OSStatus AUBase::RemovePropertyValue(
	AudioUnitPropertyID /*inID*/, AudioUnitScope /*inScope*/, AudioUnitElement /*inElement*/)
{
	return kAudioUnitErr_InvalidPropertyValue;
}

//_____________________________________________________________________________
//
OSStatus AUBase::AddPropertyListener(
	AudioUnitPropertyID inID, AudioUnitPropertyListenerProc inProc, void* inProcRefCon)
{
	const PropertyListener pl{
		.propertyID = inID, .listenerProc = inProc, .listenerRefCon = inProcRefCon
	};

	if (mPropertyListeners.empty()) {
		mPropertyListeners.reserve(32); // NOLINT magic#
	}
	mPropertyListeners.push_back(pl);

	return noErr;
}

//_____________________________________________________________________________
//
OSStatus AUBase::RemovePropertyListener(AudioUnitPropertyID inID,
	AudioUnitPropertyListenerProc inProc, void* inProcRefCon, bool refConSpecified)
{
	std::erase_if(mPropertyListeners, [&](auto& item) {
		return item.propertyID == inID && item.listenerProc == inProc &&
			   (!refConSpecified || item.listenerRefCon == inProcRefCon);
	});
	return noErr;
}

//_____________________________________________________________________________
//
void AUBase::PropertyChanged(
	AudioUnitPropertyID inID, AudioUnitScope inScope, AudioUnitElement inElement)
{
	for (const auto& pl : mPropertyListeners) {
		if (pl.propertyID == inID) {
			(pl.listenerProc)(pl.listenerRefCon, GetComponentInstance(), inID, inScope, inElement);
		}
	}
}

//_____________________________________________________________________________
//
OSStatus AUBase::SetRenderNotification(AURenderCallback inProc, void* inRefCon)
{
	if (inProc == nullptr) {
		return kAudio_ParamError;
	}

	mRenderCallbacksTouched = true;
	mRenderCallbacks.Add(RenderCallback(inProc, inRefCon));
	// this will do nothing if it's already in the list
	return noErr;
}

//_____________________________________________________________________________
//
OSStatus AUBase::RemoveRenderNotification(AURenderCallback inProc, void* inRefCon)
{
	mRenderCallbacks.Remove(RenderCallback(inProc, inRefCon));
	return noErr; // error?
}

//_____________________________________________________________________________
//
OSStatus AUBase::GetParameter(AudioUnitParameterID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, AudioUnitParameterValue& outValue)
{
	const auto& elem = Element(inScope, inElement);
	outValue = elem.GetParameter(inID);
	return noErr;
}


//_____________________________________________________________________________
//
OSStatus AUBase::SetParameter(AudioUnitParameterID inID, AudioUnitScope inScope,
	AudioUnitElement inElement, AudioUnitParameterValue inValue, UInt32 /*inBufferOffsetInFrames*/)
{
	auto& elem = Element(inScope, inElement);
	elem.SetParameter(inID, inValue);
	return noErr;
}

//_____________________________________________________________________________
//
OSStatus AUBase::ScheduleParameter(
	const AudioUnitParameterEvent* inParameterEvent, UInt32 inNumEvents)
{
	const bool canScheduleParameters = CanScheduleParameters();

	for (UInt32 i = 0; i < inNumEvents; ++i) {
		const auto& pe = inParameterEvent[i]; // NOLINT subscript
		if (pe.eventType == kParameterEvent_Immediate) {
			SetParameter(pe.parameter, pe.scope, pe.element,
				pe.eventValues.immediate.value,         // NOLINT union
				pe.eventValues.immediate.bufferOffset); // NOLINT union
		}
		if (canScheduleParameters) {
			mParamEventList.push_back(pe);
		}
	}

	return noErr;
}

// ____________________________________________________________________________
//
constexpr bool ParameterEventListSortPredicate(
	const AudioUnitParameterEvent& ev1, const AudioUnitParameterEvent& ev2) noexcept
{
	constexpr auto bufferOffset = [](const AudioUnitParameterEvent& event) {
		// ramp.startBufferOffset is signed
		return (event.eventType == kParameterEvent_Immediate)
				   ? static_cast<SInt32>(event.eventValues.immediate.bufferOffset) // NOLINT union
				   : event.eventValues.ramp.startBufferOffset;                     // NOLINT union
	};

	return bufferOffset(ev1) < bufferOffset(ev2);
}

// ____________________________________________________________________________
//
OSStatus AUBase::ProcessForScheduledParams(
	ParameterEventList& inParamList, UInt32 inFramesToProcess, void* inUserData)
{
	OSStatus result = noErr;

	UInt32 framesRemaining = inFramesToProcess;

	UInt32 currentStartFrame = 0; // start of the whole buffer


	// sort the ParameterEventList by startBufferOffset
	std::ranges::sort(inParamList, ParameterEventListSortPredicate);

	while (framesRemaining > 0) {
		// first of all, go through the ramped automation events and find out where the next
		// division of our whole buffer will be

		UInt32 currentEndFrame = inFramesToProcess; // start out assuming we'll process all the way
													// to the end of the buffer

		// find the next break point
		for (const auto& event : inParamList) {
			SInt32 offset =
				event.eventType == kParameterEvent_Immediate
					? static_cast<SInt32>(event.eventValues.immediate.bufferOffset) // NOLINT
					: event.eventValues.ramp.startBufferOffset;

			if (offset > static_cast<SInt32>(currentStartFrame) &&
				offset < static_cast<SInt32>(currentEndFrame)) {
				currentEndFrame = static_cast<UInt32>(offset);
				break;
			}

			// consider ramp end to be a possible choice (there may be gaps in the supplied ramp
			// events)
			if (event.eventType == kParameterEvent_Ramped) {
				offset = event.eventValues.ramp.startBufferOffset +
						 static_cast<SInt32>(event.eventValues.ramp.durationInFrames); // NOLINT

				if (offset > static_cast<SInt32>(currentStartFrame) &&
					offset < static_cast<SInt32>(currentEndFrame)) {
					currentEndFrame = static_cast<UInt32>(offset);
				}
			}
		}

		const UInt32 framesThisTime = currentEndFrame - currentStartFrame;

		// next, setup the parameter maps to be current for the ramp parameters active during
		// this time segment...

		for (const auto& event : inParamList) {
			bool eventFallsInSlice = false;

			if (event.eventType == kParameterEvent_Ramped) {
				const auto& ramp = event.eventValues.ramp;
				eventFallsInSlice =
					ramp.startBufferOffset < static_cast<SInt32>(currentEndFrame) &&
					(ramp.startBufferOffset + static_cast<SInt32>(ramp.durationInFrames)) >
						static_cast<SInt32>(currentStartFrame);
			} else { /* kParameterEvent_Immediate */
				// actually, for the same parameter, there may be future immediate events which
				// override this one, but it's OK since the event list is sorted in time order,
				// we're guaranteed to end up with the current one
				eventFallsInSlice = event.eventValues.immediate.bufferOffset <= currentStartFrame;
			}

			if (eventFallsInSlice) {
				AUElement* const element = GetElement(event.scope, event.element);

				if (element != nullptr) {
					element->SetScheduledEvent(event.parameter, event, currentStartFrame,
						currentEndFrame - currentStartFrame);
				}
			}
		}


		// Finally, actually do the processing for this slice.....

		result =
			ProcessScheduledSlice(inUserData, currentStartFrame, framesThisTime, inFramesToProcess);

		if (result != noErr) {
			break;
		}

		framesRemaining -= std::min(framesThisTime, framesRemaining);
		currentStartFrame = currentEndFrame; // now start from where we left off last time
	}

	return result;
}

//_____________________________________________________________________________
//
void AUBase::ResetRenderTime()
{
	mCurrentRenderTime = {};
	mCurrentRenderTime.mSampleTime = kNoLastRenderedSampleTime;
}

//_____________________________________________________________________________
//
void AUBase::SetWantsRenderThreadID(bool inFlag)
{
	if (inFlag == mWantsRenderThreadID) {
		return;
	}

	mWantsRenderThreadID = inFlag;
	if (!mWantsRenderThreadID) {
		mRenderThreadID = {};
	};
}

//_____________________________________________________________________________
//
OSStatus AUBase::DoRender(AudioUnitRenderActionFlags& ioActionFlags,
	const AudioTimeStamp& inTimeStamp, UInt32 inBusNumber, UInt32 inFramesToProcess,
	AudioBufferList& ioData)
{
	const auto errorExit = [this](OSStatus error) {
		AUSDK_LogError("  from %s, render err: %d", GetLoggingString(), static_cast<int>(error));
		SetRenderError(error);
		return error;
	};

	OSStatus theError = noErr;

	[[maybe_unused]] const DenormalDisabler denormalDisabler;

	try {
		AUSDK_Require(IsInitialized(), errorExit(kAudioUnitErr_Uninitialized));
		if (inFramesToProcess > mMaxFramesPerSlice) {
#ifndef AUSDK_NO_LOGGING
			const auto now = HostTime::Current();
			if (static_cast<double>(now - mLastTimeMessagePrinted) >
				mHostTimeFrequency) { // not more than once per second.
				mLastTimeMessagePrinted = now;
				AUSDK_LogError("kAudioUnitErr_TooManyFramesToProcess : inFramesToProcess=%u, "
							   "mMaxFramesPerSlice=%u",
					static_cast<unsigned>(inFramesToProcess),
					static_cast<unsigned>(mMaxFramesPerSlice));
			}
#endif
			return errorExit(kAudioUnitErr_TooManyFramesToProcess);
		}
		AUSDK_Require(!UsesFixedBlockSize() || inFramesToProcess == GetMaxFramesPerSlice(),
			errorExit(kAudio_ParamError));

		auto& output = Output(inBusNumber); // will throw if non-existant
		if (ASBD::NumberChannelStreams(output.GetStreamFormat()) != ioData.mNumberBuffers) {
			AUSDK_LogError(
				"ioData.mNumberBuffers=%u, "
				"ASBD::NumberChannelStreams(output.GetStreamFormat())=%u; kAudio_ParamError",
				static_cast<unsigned>(ioData.mNumberBuffers),
				static_cast<unsigned>(ASBD::NumberChannelStreams(output.GetStreamFormat())));
			return errorExit(kAudio_ParamError);
		}

		const unsigned expectedBufferByteSize =
			inFramesToProcess * output.GetStreamFormat().mBytesPerFrame;
		for (unsigned ibuf = 0; ibuf < ioData.mNumberBuffers; ++ibuf) {
			AudioBuffer& buf = ioData.mBuffers[ibuf]; // NOLINT
			if (buf.mData != nullptr) {
				// only care about the size if the buffer is non-null
				if (buf.mDataByteSize < expectedBufferByteSize) {
					// if the buffer is too small, we cannot render safely. kAudio_ParamError.
					AUSDK_LogError("%u frames, %u bytes/frame, expected %u-byte buffer; "
								   "ioData.mBuffers[%u].mDataByteSize=%u; kAudio_ParamError",
						static_cast<unsigned>(inFramesToProcess),
						static_cast<unsigned>(output.GetStreamFormat().mBytesPerFrame),
						expectedBufferByteSize, ibuf, static_cast<unsigned>(buf.mDataByteSize));
					return errorExit(kAudio_ParamError);
				}
				// Some clients incorrectly pass bigger buffers than expectedBufferByteSize.
				// We will generally set the buffer size at the end of rendering, before we return.
				// However we should ensure that no one, DURING rendering, READS a
				// potentially incorrect size. This can lead to doing too much work, or
				// reading past the end of an input buffer into unmapped memory.
				buf.mDataByteSize = expectedBufferByteSize;
			}
		}

		if (WantsRenderThreadID()) {
			mRenderThreadID = std::this_thread::get_id();
		}

		if (mRenderCallbacksTouched) {
			mRenderCallbacks.Update();

			AudioUnitRenderActionFlags flags = ioActionFlags | kAudioUnitRenderAction_PreRender;
			for (const RenderCallback& rc : mRenderCallbacks) {
				(*static_cast<AURenderCallback>(rc.mRenderNotify))(rc.mRenderNotifyRefCon, &flags,
					&inTimeStamp, inBusNumber, inFramesToProcess, &ioData);
			}
		}

		theError =
			DoRenderBus(ioActionFlags, inTimeStamp, inBusNumber, output, inFramesToProcess, ioData);

		SetRenderError(theError);

		if (mRenderCallbacksTouched) {
			AudioUnitRenderActionFlags flags = ioActionFlags | kAudioUnitRenderAction_PostRender;

			if (theError != noErr) {
				flags |= kAudioUnitRenderAction_PostRenderError;
			}

			for (const RenderCallback& rc : mRenderCallbacks) {
				(*static_cast<AURenderCallback>(rc.mRenderNotify))(rc.mRenderNotifyRefCon, &flags,
					&inTimeStamp, inBusNumber, inFramesToProcess, &ioData);
			}
		}

		// The vector is being emptied because these events should only apply to this Render cycle,
		// so anything left over is from a preceding cycle and should be dumped.
		// New scheduled parameters must be scheduled from the next pre-render callback.
		if (!mParamEventList.empty()) {
			mParamEventList.clear();
		}
	} catch (const OSStatus& err) {
		return errorExit(err);
	} catch (...) {
		return errorExit(-1);
	}
	return theError;
}

inline bool CheckRenderArgs(AudioUnitRenderActionFlags flags)
{
	return (flags & kAudioUnitRenderAction_DoNotCheckRenderArgs) == 0u;
}

//_____________________________________________________________________________
//
OSStatus AUBase::DoProcess(AudioUnitRenderActionFlags& ioActionFlags,
	const AudioTimeStamp& inTimeStamp, UInt32 inFramesToProcess, AudioBufferList& ioData)
{
	const auto errorExit = [this](OSStatus error) {
		AUSDK_LogError("  from %s, process err: %d", GetLoggingString(), static_cast<int>(error));
		SetRenderError(error);
		return error;
	};

	OSStatus theError = noErr;

	[[maybe_unused]] const DenormalDisabler denormalDisabler;

	try {
		if (CheckRenderArgs(ioActionFlags)) {
			AUSDK_Require(IsInitialized(), errorExit(kAudioUnitErr_Uninitialized));
			AUSDK_Require(inFramesToProcess <= mMaxFramesPerSlice,
				errorExit(kAudioUnitErr_TooManyFramesToProcess));
			AUSDK_Require(!UsesFixedBlockSize() || inFramesToProcess == GetMaxFramesPerSlice(),
				errorExit(kAudio_ParamError));

			const auto& input = Input(0); // will throw if non-existant
			if (ASBD::NumberChannelStreams(input.GetStreamFormat()) != ioData.mNumberBuffers) {
				AUSDK_LogError(
					"ioData.mNumberBuffers=%u, "
					"ASBD::NumberChannelStreams(input->GetStreamFormat())=%u; kAudio_ParamError",
					static_cast<unsigned>(ioData.mNumberBuffers),
					static_cast<unsigned>(ASBD::NumberChannelStreams(input.GetStreamFormat())));
				return errorExit(kAudio_ParamError);
			}

			const unsigned expectedBufferByteSize =
				inFramesToProcess * input.GetStreamFormat().mBytesPerFrame;
			for (unsigned ibuf = 0; ibuf < ioData.mNumberBuffers; ++ibuf) {
				AudioBuffer& buf = ioData.mBuffers[ibuf]; // NOLINT
				if (buf.mData != nullptr) {
					// only care about the size if the buffer is non-null
					if (buf.mDataByteSize < expectedBufferByteSize) {
						// if the buffer is too small, we cannot render safely. kAudio_ParamError.
						AUSDK_LogError("%u frames, %u bytes/frame, expected %u-byte buffer; "
									   "ioData.mBuffers[%u].mDataByteSize=%u; kAudio_ParamError",
							static_cast<unsigned>(inFramesToProcess),
							static_cast<unsigned>(input.GetStreamFormat().mBytesPerFrame),
							expectedBufferByteSize, ibuf, static_cast<unsigned>(buf.mDataByteSize));
						return errorExit(kAudio_ParamError);
					}
					// Some clients incorrectly pass bigger buffers than expectedBufferByteSize.
					// We will generally set the buffer size at the end of rendering, before we
					// return. However we should ensure that no one, DURING rendering, READS a
					// potentially incorrect size. This can lead to doing too much work, or
					// reading past the end of an input buffer into unmapped memory.
					buf.mDataByteSize = expectedBufferByteSize;
				}
			}
		}

		if (WantsRenderThreadID()) {
			mRenderThreadID = std::this_thread::get_id();
		}

		if (NeedsToRender(inTimeStamp)) {
			theError = ProcessBufferLists(ioActionFlags, ioData, ioData, inFramesToProcess);
		} else {
			theError = noErr;
		}

	} catch (const OSStatus& err) {
		return errorExit(err);
	} catch (...) {
		return errorExit(-1);
	}
	return theError;
}

OSStatus AUBase::DoProcessMultiple(AudioUnitRenderActionFlags& ioActionFlags,
	const AudioTimeStamp& inTimeStamp, UInt32 inFramesToProcess, UInt32 inNumberInputBufferLists,
	const AudioBufferList** inInputBufferLists, UInt32 inNumberOutputBufferLists,
	AudioBufferList** ioOutputBufferLists)
{
	const auto errorExit = [this](OSStatus error) {
		AUSDK_LogError(
			"  from %s, processmultiple err: %d", GetLoggingString(), static_cast<int>(error));
		SetRenderError(error);
		return error;
	};

	OSStatus theError = noErr;

	[[maybe_unused]] const DenormalDisabler denormalDisabler;

	try {
		if (CheckRenderArgs(ioActionFlags)) {
			AUSDK_Require(IsInitialized(), errorExit(kAudioUnitErr_Uninitialized));
			AUSDK_Require(inFramesToProcess <= mMaxFramesPerSlice,
				errorExit(kAudioUnitErr_TooManyFramesToProcess));
			AUSDK_Require(!UsesFixedBlockSize() || inFramesToProcess == GetMaxFramesPerSlice(),
				errorExit(kAudio_ParamError));

			for (unsigned ibl = 0; ibl < inNumberInputBufferLists; ++ibl) {
				if (inInputBufferLists[ibl] != nullptr) { // NOLINT
					const auto& input = Input(ibl);       // will throw if non-existant
					const unsigned expectedBufferByteSize =
						inFramesToProcess * input.GetStreamFormat().mBytesPerFrame;

					if (ASBD::NumberChannelStreams(input.GetStreamFormat()) !=
						inInputBufferLists[ibl]->mNumberBuffers) { // NOLINT
						AUSDK_LogError("inInputBufferLists[%u]->mNumberBuffers=%u, "
									   "ASBD::NumberChannelStreams(input.GetStreamFormat())=%u; "
									   "kAudio_ParamError",
							ibl, static_cast<unsigned>(inInputBufferLists[ibl]->mNumberBuffers),
							static_cast<unsigned>(
								ASBD::NumberChannelStreams(input.GetStreamFormat())));
						return errorExit(kAudio_ParamError);
					}

					for (unsigned ibuf = 0;
						 ibuf < inInputBufferLists[ibl]->mNumberBuffers; // NOLINT
						 ++ibuf) {
						const AudioBuffer& buf = inInputBufferLists[ibl]->mBuffers[ibuf]; // NOLINT
						if (buf.mData != nullptr) {
							if (buf.mDataByteSize < expectedBufferByteSize) {
								// the buffer is too small
								AUSDK_LogError(
									"%u frames, %u bytes/frame, expected %u-byte buffer; "
									"inInputBufferLists[%u].mBuffers[%u].mDataByteSize=%u; "
									"kAudio_ParamError",
									static_cast<unsigned>(inFramesToProcess),
									static_cast<unsigned>(input.GetStreamFormat().mBytesPerFrame),
									expectedBufferByteSize, ibl, ibuf,
									static_cast<unsigned>(buf.mDataByteSize));
								return errorExit(kAudio_ParamError);
							}
						} else {
							// the buffer must exist
							return errorExit(kAudio_ParamError);
						}
					}
				} else {
					// skip NULL input audio buffer list
				}
			}

			for (unsigned obl = 0; obl < inNumberOutputBufferLists; ++obl) {
				if (ioOutputBufferLists[obl] != nullptr) { // NOLINT
					const auto& output = Output(obl);      // will throw if non-existant
					const unsigned expectedBufferByteSize =
						inFramesToProcess * output.GetStreamFormat().mBytesPerFrame;

					if (ASBD::NumberChannelStreams(output.GetStreamFormat()) !=
						ioOutputBufferLists[obl]->mNumberBuffers) { // NOLINT
						AUSDK_LogError("ioOutputBufferLists[%u]->mNumberBuffers=%u, "
									   "ASBD::NumberChannelStreams(output.GetStreamFormat())=%u; "
									   "kAudio_ParamError",
							obl, static_cast<unsigned>(ioOutputBufferLists[obl]->mNumberBuffers),
							static_cast<unsigned>(
								ASBD::NumberChannelStreams(output.GetStreamFormat())));
						return errorExit(kAudio_ParamError);
					}

					for (unsigned obuf = 0;
						 obuf < ioOutputBufferLists[obl]->mNumberBuffers; // NOLINT
						 ++obuf) {
						AudioBuffer& buf = ioOutputBufferLists[obl]->mBuffers[obuf]; // NOLINT
						if (buf.mData != nullptr) {
							// only care about the size if the buffer is non-null
							if (buf.mDataByteSize < expectedBufferByteSize) {
								// if the buffer is too small, we cannot render safely.
								// kAudio_ParamError.
								AUSDK_LogError(
									"%u frames, %u bytes/frame, expected %u-byte buffer; "
									"ioOutputBufferLists[%u]->mBuffers[%u].mDataByteSize=%u; "
									"kAudio_ParamError",
									static_cast<unsigned>(inFramesToProcess),
									static_cast<unsigned>(output.GetStreamFormat().mBytesPerFrame),
									expectedBufferByteSize, obl, obuf,
									static_cast<unsigned>(buf.mDataByteSize));
								return errorExit(kAudio_ParamError);
							}
							// Some clients incorrectly pass bigger buffers than
							// expectedBufferByteSize. We will generally set the buffer size at the
							// end of rendering, before we return. However we should ensure that no
							// one, DURING rendering, READS a potentially incorrect size. This can
							// lead to doing too much work, or reading past the end of an input
							// buffer into unmapped memory.
							buf.mDataByteSize = expectedBufferByteSize;
						}
					}
				} else {
					// skip NULL output audio buffer list
				}
			}
		}

		if (WantsRenderThreadID()) {
			mRenderThreadID = std::this_thread::get_id();
		}

		if (NeedsToRender(inTimeStamp)) {
			theError = ProcessMultipleBufferLists(ioActionFlags, inFramesToProcess,
				inNumberInputBufferLists, inInputBufferLists, inNumberOutputBufferLists,
				ioOutputBufferLists);
		} else {
			theError = noErr;
		}
	} catch (const OSStatus& err) {
		return errorExit(err);
	} catch (...) {
		return errorExit(-1);
	}
	return theError;
}

//_____________________________________________________________________________
//
OSStatus AUBase::SetInputCallback(
	UInt32 inPropertyID, AudioUnitElement inElement, AURenderCallback inProc, void* inRefCon)
{
	auto& input = Input(inElement); // may throw

	input.SetInputCallback(inProc, inRefCon);
	PropertyChanged(inPropertyID, kAudioUnitScope_Input, inElement);

	return noErr;
}

//_____________________________________________________________________________
//
// NOLINTNEXTLINE(misc-no-recursion) with DispatchSetProperty
OSStatus AUBase::SetConnection(const AudioUnitConnection& inConnection)
{
	auto& input = Input(inConnection.destInputNumber); // may throw

	if (inConnection.sourceAudioUnit != nullptr) {
		// connecting, not disconnecting
		AudioStreamBasicDescription sourceDesc;
		UInt32 size = sizeof(AudioStreamBasicDescription);
		AUSDK_Require_noerr(
			AudioUnitGetProperty(inConnection.sourceAudioUnit, kAudioUnitProperty_StreamFormat,
				kAudioUnitScope_Output, inConnection.sourceOutputNumber, &sourceDesc, &size));
		AUSDK_Require_noerr(
			DispatchSetProperty(kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input,
				inConnection.destInputNumber, &sourceDesc, sizeof(AudioStreamBasicDescription)));
	}
	input.SetConnection(inConnection);

	PropertyChanged(
		kAudioUnitProperty_MakeConnection, kAudioUnitScope_Input, inConnection.destInputNumber);
	return noErr;
}

//_____________________________________________________________________________
//
UInt32 AUBase::SupportedNumChannels(const AUChannelInfo** /*outInfo*/) { return 0; }

//_____________________________________________________________________________
//
bool AUBase::ValidFormat(AudioUnitScope /*inScope*/, AudioUnitElement /*inElement*/,
	const AudioStreamBasicDescription& inNewFormat)
{
	return ASBD::IsCommonFloat32(inNewFormat) &&
		   (!ASBD::IsInterleaved(inNewFormat) || inNewFormat.mChannelsPerFrame == 1);
}

//_____________________________________________________________________________
//
bool AUBase::IsStreamFormatWritable(AudioUnitScope scope, AudioUnitElement element)
{
	switch (scope) {
	case kAudioUnitScope_Input: {
		const auto& input = Input(element);
		if (input.HasConnection()) {
			return false; // can't write format when input comes from connection
		}
		[[fallthrough]];
	}
	case kAudioUnitScope_Output:
		return StreamFormatWritable(scope, element);

		// #warning "aliasing of global scope format should be pushed to subclasses"
	case kAudioUnitScope_Global:
		return StreamFormatWritable(kAudioUnitScope_Output, 0);
	default:
		break;
	}
	return false;
}

//_____________________________________________________________________________
//
AudioStreamBasicDescription AUBase::GetStreamFormat(
	AudioUnitScope inScope, AudioUnitElement inElement)
{
	// #warning "aliasing of global scope format should be pushed to subclasses"
	AUIOElement* element = nullptr;

	switch (inScope) {
	case kAudioUnitScope_Input:
		element = Inputs().GetIOElement(inElement);
		break;
	case kAudioUnitScope_Output:
		element = Outputs().GetIOElement(inElement);
		break;
	case kAudioUnitScope_Global: // global stream description is an alias for that of output 0
		element = Outputs().GetIOElement(0);
		break;
	default:
		Throw(kAudioUnitErr_InvalidScope);
	}
	return element->GetStreamFormat();
}

OSStatus AUBase::SetBusCount(AudioUnitScope inScope, UInt32 inCount)
{
	AUSDK_Require(!IsInitialized(), kAudioUnitErr_Initialized);

	GetScope(inScope).SetNumberOfElements(inCount);
	return noErr;
}

//_____________________________________________________________________________
//
OSStatus AUBase::ChangeStreamFormat(AudioUnitScope inScope, AudioUnitElement inElement,
	const AudioStreamBasicDescription& inPrevFormat, const AudioStreamBasicDescription& inNewFormat)
{
	if (ASBD::IsEqual(inNewFormat, inPrevFormat)) {
		return noErr;
	}

	// #warning "aliasing of global scope format should be pushed to subclasses"
	AUIOElement* element = nullptr;

	switch (inScope) {
	case kAudioUnitScope_Input:
		element = Inputs().GetIOElement(inElement);
		break;
	case kAudioUnitScope_Output:
		element = Outputs().GetIOElement(inElement);
		break;
	case kAudioUnitScope_Global:
		element = Outputs().GetIOElement(0);
		break;
	default:
		Throw(kAudioUnitErr_InvalidScope);
	}
	element->SetStreamFormat(inNewFormat);
	PropertyChanged(kAudioUnitProperty_StreamFormat, inScope, inElement);
	return noErr;
}

std::vector<AudioChannelLayoutTag> AUBase::GetChannelLayoutTags(
	AudioUnitScope inScope, AudioUnitElement inElement)
{
	return IOElement(inScope, inElement).GetChannelLayoutTags();
}

UInt32 AUBase::GetAudioChannelLayout(AudioUnitScope inScope, AudioUnitElement inElement,
	AudioChannelLayout* outLayoutPtr, bool& outWritable)
{
	auto& element = IOElement(inScope, inElement);
	return element.GetAudioChannelLayout(outLayoutPtr, outWritable);
}

OSStatus AUBase::RemoveAudioChannelLayout(AudioUnitScope inScope, AudioUnitElement inElement)
{
	auto& element = IOElement(inScope, inElement);
	bool writable = false;
	if (element.GetAudioChannelLayout(nullptr, writable) > 0) {
		return element.RemoveAudioChannelLayout();
	}
	return noErr;
}

OSStatus AUBase::SetAudioChannelLayout(
	AudioUnitScope inScope, AudioUnitElement inElement, const AudioChannelLayout* inLayout)
{
	auto& element = IOElement(inScope, inElement);

	// the num channels of the layout HAS TO MATCH the current channels of the Element's stream
	// format
	const UInt32 currentChannels = element.GetStreamFormat().mChannelsPerFrame;
	const UInt32 numChannelsInLayout = AUChannelLayout::NumberChannels(*inLayout);
	AUSDK_Require(currentChannels == numChannelsInLayout, kAudioUnitErr_InvalidPropertyValue);

	const auto tags = GetChannelLayoutTags(inScope, inElement);
	AUSDK_Require(!tags.empty(), kAudioUnitErr_InvalidProperty);
	const auto iter = std::ranges::find_if(tags, [inTag = inLayout->mChannelLayoutTag](auto tag) {
		return tag == inTag || tag == kAudioChannelLayoutTag_UseChannelDescriptions;
	});
	AUSDK_Require(iter != tags.end(), kAudioUnitErr_InvalidPropertyValue);

	return element.SetAudioChannelLayout(*inLayout);
}

constexpr int kCurrentSavedStateVersion = 0;

static void AddNumToDictionary(CFMutableDictionaryRef dict, CFStringRef key, SInt32 value)
{
	const CFNumberRef num = CFNumberCreate(nullptr, kCFNumberSInt32Type, &value);
	CFDictionarySetValue(dict, key, num);
	CFRelease(num);
}

// NOLINTNEXTLINE(misc-no-recursion) with DispatchGetProperty
OSStatus AUBase::SaveState(CFPropertyListRef* outData)
{
	const AudioComponentDescription desc = GetComponentDescription();

	auto dict = Owned<CFMutableDictionaryRef>::from_create(CFDictionaryCreateMutable(
		nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));

	// first step -> save the version to the data ref
	SInt32 value = kCurrentSavedStateVersion;
	AddNumToDictionary(*dict, CFSTR(kAUPresetVersionKey), value);

	// second step -> save the component type, subtype, manu to the data ref
	value = static_cast<SInt32>(desc.componentType);
	AddNumToDictionary(*dict, CFSTR(kAUPresetTypeKey), value);

	value = static_cast<SInt32>(desc.componentSubType);
	AddNumToDictionary(*dict, CFSTR(kAUPresetSubtypeKey), value);

	value = static_cast<SInt32>(desc.componentManufacturer);
	AddNumToDictionary(*dict, CFSTR(kAUPresetManufacturerKey), value);

	// fourth step -> save the state of all parameters on all scopes and elements
	auto data = Owned<CFMutableDataRef>::from_create(CFDataCreateMutable(nullptr, 0));
	for (AudioUnitScope iscope = 0; iscope < 3; ++iscope) {
		const auto& scope = GetScope(iscope);
		scope.SaveState(*data);
	}

	SaveExtendedScopes(*data);

	// save all this in the data section of the dictionary
	CFDictionarySetValue(*dict, CFSTR(kAUPresetDataKey), *data);
	data = nullptr; // data can be large-ish, so destroy it now.

	// OK - now we're going to do some properties
	// save the preset name...
	CFDictionarySetValue(*dict, CFSTR(kAUPresetNameKey), mCurrentPreset.presetName);

	// Does the unit support the RenderQuality property - if so, save it...
	OSStatus result =
		DispatchGetProperty(kAudioUnitProperty_RenderQuality, kAudioUnitScope_Global, 0, &value);

	if (result == noErr) {
		AddNumToDictionary(*dict, CFSTR(kAUPresetRenderQualityKey), value);
	}

	// Do we have any element names for any of our scopes?
	// first check to see if we have any names...
	bool foundName = false;
	for (AudioUnitScope i = 0; i < kNumScopes; ++i) {
		foundName = GetScope(i).HasElementWithName();
		if (foundName) {
			break;
		}
	}
	// OK - we found a name away we go...
	if (foundName) {
		auto nameDict = Owned<CFMutableDictionaryRef>::from_create(CFDictionaryCreateMutable(
			nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks));
		for (AudioUnitScope i = 0; i < kNumScopes; ++i) {
			GetScope(i).AddElementNamesToDict(*nameDict);
		}

		CFDictionarySetValue(*dict, CFSTR(kAUPresetElementNameKey), *nameDict);
	}

	// we're done!!!
	*outData = static_cast<CFPropertyListRef>(dict.release()); // transfer ownership

	return noErr;
}

//_____________________________________________________________________________
//
// NOLINTNEXTLINE(misc-no-recursion) with DispatchSetProperty
OSStatus AUBase::RestoreState(CFPropertyListRef plist)
{
	AUSDK_Require(
		CFGetTypeID(plist) == CFDictionaryGetTypeID(), kAudioUnitErr_InvalidPropertyValue);

	const AudioComponentDescription desc = GetComponentDescription();

	const auto* const dict = static_cast<CFDictionaryRef>(plist);

	// zeroeth step - make sure the Part key is NOT present, as this method is used
	// to restore the GLOBAL state of the dictionary
	AUSDK_Require(!CFDictionaryContainsKey(dict, CFSTR(kAUPresetPartKey)),
		kAudioUnitErr_InvalidPropertyValue);

	// first step -> check the saved version in the data ref
	// at this point we're only dealing with version==0
	const auto* cfNum =
		static_cast<CFNumberRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetVersionKey)));
	AUSDK_Require(cfNum != nullptr, kAudioUnitErr_InvalidPropertyValue);
	AUSDK_Require(CFGetTypeID(cfNum) == CFNumberGetTypeID(), kAudioUnitErr_InvalidPropertyValue);
	SInt32 value = 0;
	CFNumberGetValue(cfNum, kCFNumberSInt32Type, &value);
	AUSDK_Require(value == kCurrentSavedStateVersion, kAudioUnitErr_InvalidPropertyValue);

	// second step -> check that this data belongs to this kind of audio unit
	// by checking the component subtype and manuID
	// We're not checking the type, since there may be different versions (effect, format-converter,
	// offline) of essentially the same AU
	cfNum = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetSubtypeKey)));
	AUSDK_Require(cfNum != nullptr, kAudioUnitErr_InvalidPropertyValue);
	AUSDK_Require(CFGetTypeID(cfNum) == CFNumberGetTypeID(), kAudioUnitErr_InvalidPropertyValue);
	CFNumberGetValue(cfNum, kCFNumberSInt32Type, &value);
	AUSDK_Require(
		static_cast<UInt32>(value) == desc.componentSubType, kAudioUnitErr_InvalidPropertyValue);

	cfNum = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetManufacturerKey)));
	AUSDK_Require(cfNum != nullptr, kAudioUnitErr_InvalidPropertyValue);
	AUSDK_Require(CFGetTypeID(cfNum) == CFNumberGetTypeID(), kAudioUnitErr_InvalidPropertyValue);
	CFNumberGetValue(cfNum, kCFNumberSInt32Type, &value);
	AUSDK_Require(static_cast<UInt32>(value) == desc.componentManufacturer,
		kAudioUnitErr_InvalidPropertyValue);

	// fourth step -> restore the state of all of the parameters for each scope and element
	const auto* const data =
		static_cast<CFDataRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetDataKey)));
	if ((data != nullptr) && (CFGetTypeID(data) == CFDataGetTypeID())) {
		const UInt8* p = CFDataGetBytePtr(data);
		const UInt8* const pend = p + CFDataGetLength(data); // NOLINT

		while (p < pend) {
			const auto scopeIndex = DeserializeBigUInt32AndAdvance(p);
			const auto& scope = GetScope(scopeIndex);
			p = scope.RestoreState(p);
		}
	}

	// OK - now we're going to do some properties
	// restore the preset name...
	const auto* const name =
		static_cast<CFStringRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetNameKey)));
	if (mCurrentPreset.presetName != nullptr) {
		CFRelease(mCurrentPreset.presetName);
	}
	if ((name != nullptr) && (CFGetTypeID(name) == CFStringGetTypeID())) {
		mCurrentPreset.presetName = name;
		mCurrentPreset.presetNumber = -1;
	} else { // no name entry make the default one
		mCurrentPreset.presetName = GetPresetDefaultName();
		mCurrentPreset.presetNumber = -1;
	}

	CFRetain(mCurrentPreset.presetName);
	PropertyChanged(kAudioUnitProperty_PresentPreset, kAudioUnitScope_Global, 0);

	// Does the dict contain render quality information?
	cfNum = static_cast<CFNumberRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetRenderQualityKey)));
	if (cfNum && (CFGetTypeID(cfNum) == CFNumberGetTypeID())) {
		CFNumberGetValue(cfNum, kCFNumberSInt32Type, &value);
		DispatchSetProperty(
			kAudioUnitProperty_RenderQuality, kAudioUnitScope_Global, 0, &value, sizeof(value));
	}

	// Do we have any element names for any of our scopes?
	const auto nameDict =
		static_cast<CFDictionaryRef>(CFDictionaryGetValue(dict, CFSTR(kAUPresetElementNameKey)));
	if (nameDict && (CFGetTypeID(nameDict) == CFDictionaryGetTypeID())) {
		for (AudioUnitScope i = 0; i < kNumScopes; ++i) {
			const CFStringRef key = CFStringCreateWithFormat(
				nullptr, nullptr, CFSTR("%u"), static_cast<unsigned>(i)); // NOLINT
			const auto elementDict =
				static_cast<CFDictionaryRef>(CFDictionaryGetValue(nameDict, key));
			if (elementDict && (CFGetTypeID(elementDict) == CFDictionaryGetTypeID())) {
				const auto restoredElements = GetScope(i).RestoreElementNames(elementDict);
				for (const auto& element : restoredElements) {
					PropertyChanged(kAudioUnitProperty_ElementName, i, element);
				}
			}
			CFRelease(key);
		}
	}

	return noErr;
}

OSStatus AUBase::GetPresets(CFArrayRef* /*outData*/) const { return kAudioUnitErr_InvalidProperty; }

OSStatus AUBase::NewFactoryPresetSet(const AUPreset& /*inNewFactoryPreset*/)
{
	return kAudioUnitErr_InvalidProperty;
}

OSStatus AUBase::NewCustomPresetSet(const AUPreset& inNewCustomPreset)
{
	CFRelease(mCurrentPreset.presetName);
	mCurrentPreset = inNewCustomPreset;
	CFRetain(mCurrentPreset.presetName);
	return noErr;
}

// set the default preset for the unit -> the number of the preset MUST be >= 0
// and the name should be valid, or the preset WON'T take
bool AUBase::SetAFactoryPresetAsCurrent(const AUPreset& inPreset)
{
	if (inPreset.presetNumber < 0 || inPreset.presetName == nullptr) {
		return false;
	}
	CFRelease(mCurrentPreset.presetName);
	mCurrentPreset = inPreset;
	CFRetain(mCurrentPreset.presetName);
	return true;
}

bool AUBase::HasIcon()
{
#if AUSDK_HAVE_UI
	const CFURLRef url = CopyIconLocation();
	if (url != nullptr) {
		CFRelease(url);
		return true;
	}
#endif // AUSDK_HAVE_UI
	return false;
}

#if AUSDK_HAVE_UI
CFURLRef AUBase::CopyIconLocation() { return nullptr; }
#endif // AUSDK_HAVE_UI

//_____________________________________________________________________________
//
OSStatus AUBase::GetParameterList(
	AudioUnitScope inScope, AudioUnitParameterID* outParameterList, UInt32& outNumParameters)
{
	const auto& scope = GetScope(inScope);
	AUElement* elementWithMostParameters = nullptr;
	UInt32 maxNumParams = 0;

	const UInt32 nElems = scope.GetNumberOfElements();
	for (UInt32 ielem = 0; ielem < nElems; ++ielem) {
		AUElement* const element = scope.GetElement(ielem);
		const UInt32 nParams = element->GetNumberOfParameters();
		if (nParams > maxNumParams) {
			maxNumParams = nParams;
			elementWithMostParameters = element;
		}
	}

	if (outParameterList != nullptr && elementWithMostParameters != nullptr) {
		elementWithMostParameters->GetParameterList(outParameterList);
	}

	outNumParameters = maxNumParams;
	return noErr;
}

//_____________________________________________________________________________
//
OSStatus AUBase::GetParameterInfo(AudioUnitScope /*inScope*/,
	AudioUnitParameterID /*inParameterID*/, AudioUnitParameterInfo& /*outParameterInfo*/)
{
	return kAudioUnitErr_InvalidParameter;
}

//_____________________________________________________________________________
//
OSStatus AUBase::GetParameterValueStrings(
	AudioUnitScope /*inScope*/, AudioUnitParameterID /*inParameterID*/, CFArrayRef* /*outStrings*/)
{
	return kAudioUnitErr_InvalidProperty;
}

//_____________________________________________________________________________
//
OSStatus AUBase::GetParameterHistoryInfo(AudioUnitScope /*inScope*/,
	AudioUnitParameterID /*inParameterID*/, Float32& /*outUpdatesPerSecond*/,
	Float32& /*outHistoryDurationInSeconds*/)
{
	return kAudioUnitErr_InvalidProperty;
}


//_____________________________________________________________________________
//
OSStatus AUBase::CopyClumpName(AudioUnitScope /*inScope*/, UInt32 /*inClumpID*/,
	UInt32 /*inDesiredNameLength*/, CFStringRef* /*outClumpName*/)
{
	return kAudioUnitErr_InvalidProperty;
}

//_____________________________________________________________________________
//
void AUBase::SetNumberOfElements(AudioUnitScope inScope, UInt32 numElements)
{
	if (inScope == kAudioUnitScope_Global && numElements != 1) {
		Throw(kAudioUnitErr_InvalidScope);
	}

	GetScope(inScope).SetNumberOfElements(numElements);
}

//_____________________________________________________________________________
//
std::unique_ptr<AUElement> AUBase::CreateElement(AudioUnitScope scope, AudioUnitElement /*element*/)
{
	switch (scope) {
	case kAudioUnitScope_Global:
		return std::make_unique<AUElement>(*this);
	case kAudioUnitScope_Input:
		return std::make_unique<AUInputElement>(*this);
	case kAudioUnitScope_Output:
		return std::make_unique<AUOutputElement>(*this);
	case kAudioUnitScope_Group:
	case kAudioUnitScope_Part:
		return std::make_unique<AUElement>(*this);
	default:
		break;
	}
	Throw(kAudioUnitErr_InvalidScope);
}

const char* AUBase::GetLoggingString() const noexcept { return mLogString.c_str(); }

std::string AUBase::CreateLoggingString() const
{
	const auto desc = GetComponentDescription();
	std::array<char, 32> buf{};
	[[maybe_unused]] const int printCount = snprintf(
		buf.data(), buf.size(), "AU (%p): ", static_cast<void*>(GetComponentInstance())); // NOLINT
#if DEBUG
	assert(printCount < static_cast<int>(buf.size()));
#endif
	return buf.data() + MakeStringFrom4CC(desc.componentType) + '/' +
		   MakeStringFrom4CC(desc.componentSubType) + '/' +
		   MakeStringFrom4CC(desc.componentManufacturer);
}

} // namespace ausdk
