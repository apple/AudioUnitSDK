/*!
	@file		AudioUnitSDK/AUScopeElement.h
	@copyright	© 2000-2025 Apple Inc. All rights reserved.
*/
#ifndef AudioUnitSDK_AUScopeElement_h
#define AudioUnitSDK_AUScopeElement_h

// module
// clang-format off
#include <AudioUnitSDK/AUConfig.h> // must come first
// clang-format on
#include <AudioUnitSDK/AUBuffer.h>
#include <AudioUnitSDK/AUUtility.h>
#include <AudioUnitSDK/ComponentBase.h>

// OS
#include <AudioToolbox/AUComponent.h>

// std
#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>
#include <vector>

namespace ausdk {

AUSDK_BEGIN_NO_RT_WARNINGS

class AUBase;

/// Wrap an atomic in a copy-constructible/assignable object. This allows storing atomic values in a
/// vector (not directly possible since atomics are not copy-constructible/assignable).
template <typename T>
class AtomicValue {
public:
	AtomicValue() = default;
	explicit AtomicValue(T val) : mValue{ val } {}
	~AtomicValue() = default;

	AtomicValue(const AtomicValue& other) : mValue{ other.mValue.load() } {}
	AtomicValue(AtomicValue&& other) noexcept : mValue{ other.mValue.load() } {}

	AtomicValue& operator=(const AtomicValue& other)
	{
		if (&other != this) {
			mValue.store(other.mValue.load());
		}
		return *this;
	}

	AtomicValue& operator=(AtomicValue&& other) noexcept
	{
		mValue.store(other.mValue.load());
		return *this;
	}

	T load(std::memory_order m = std::memory_order_seq_cst) const { return mValue.load(m); }
	void store(T v, std::memory_order m = std::memory_order_seq_cst) { mValue.store(v, m); }

	operator T() const { return load(); } // NOLINT implicit conversions OK

	AtomicValue& operator=(T value)
	{
		store(value);
		return *this;
	}

private:
	std::atomic<T> mValue{};
	static_assert(decltype(mValue)::is_always_lock_free);
};

/// A bare-bones reinvention of boost::flat_map, just enough to hold parameters in sorted vectors.
template <typename Key, typename Value>
class flat_map {
	using KVPair = std::pair<Key, Value>;
	using Impl = std::vector<std::pair<Key, Value>>;

	static bool keyless(const KVPair& item, Key k) AUSDK_RTSAFE { return k > item.first; }

	Impl mImpl;

public:
	using iterator = typename Impl::iterator;
	using const_iterator = typename Impl::const_iterator;

	[[nodiscard]] bool empty() const noexcept { return mImpl.empty(); }
	[[nodiscard]] size_t size() const noexcept { return mImpl.size(); }
	[[nodiscard]] const_iterator begin() const noexcept { return mImpl.begin(); }
	[[nodiscard]] const_iterator end() const noexcept { return mImpl.end(); }
	iterator begin() noexcept { return mImpl.begin(); }
	iterator end() noexcept { return mImpl.end(); }
	const_iterator cbegin() noexcept { return mImpl.cbegin(); }
	const_iterator cend() noexcept { return mImpl.cend(); }

	[[nodiscard]] const_iterator lower_bound(Key k) const
	{
		return std::lower_bound(mImpl.cbegin(), mImpl.cend(), k, RTSafeFP{ keyless });
	}

	iterator lower_bound(Key k)
	{
		return std::lower_bound(mImpl.begin(), mImpl.end(), k, RTSafeFP{ keyless });
	}

	[[nodiscard]] const_iterator find(Key k) const
	{
		auto iter = lower_bound(k);
		if (iter != mImpl.end()) {
			if (iter->first != k) {
				iter = mImpl.end();
			}
		}
		return iter;
	}

	iterator find(Key k)
	{
		auto iter = lower_bound(k);
		if (iter != mImpl.end()) {
			if (iter->first != k) {
				iter = mImpl.end();
			}
		}
		return iter;
	}

	class ItemProxy {
	public:
		ItemProxy(flat_map& map, Key k) : mMap{ map }, mKey{ k } {}

		operator Value() const // NOLINT implicit conversion is OK
		{
			const auto iter = mMap.find(mKey);
			if (iter == mMap.end()) {
				throw std::runtime_error("Invalid map key");
			}
			return iter->second;
		}

		ItemProxy& operator=(const Value& v)
		{
			const auto iter = mMap.lower_bound(mKey);
			if (iter != mMap.end() && iter->first == mKey) {
				iter->second = v;
			} else {
				mMap.mImpl.insert(iter, { mKey, v });
			}
			return *this;
		}

	private:
		flat_map& mMap;
		const Key mKey;
	};

	ItemProxy operator[](Key k) { return ItemProxy{ *this, k }; }
};

// ____________________________________________________________________________
//
class AUIOElement;

/// An organizational unit for parameters, with a name.
class AUElement {
public:
	explicit AUElement(AUBase& audioUnit) : mAudioUnit(audioUnit), mUseIndexedParameters(false) {}

	AUSDK_DEPRECATED("Construct with a reference")
	explicit AUElement(AUBase* audioUnit) : AUElement(*audioUnit) {}

	AUElement(const AUElement&) = delete;
	AUElement(AUElement&&) = delete;
	AUElement& operator=(const AUElement&) = delete;
	AUElement& operator=(AUElement&&) = delete;

	virtual ~AUElement() = default;

	virtual UInt32 GetNumberOfParameters()
	{
		return mUseIndexedParameters ? static_cast<UInt32>(mIndexedParameters.size())
									 : static_cast<UInt32>(mParameters.size());
	}
	virtual void GetParameterList(AudioUnitParameterID* outList);
	[[nodiscard]] bool HasParameterID(AudioUnitParameterID paramID) const AUSDK_RTSAFE;

	// Use this from the control (non-realtime) context. Throws if the parameter doesn't exist.
	[[nodiscard]] AudioUnitParameterValue GetParameter(AudioUnitParameterID paramID) const
	{
		const auto res = GetParameterOrError(paramID);
		ThrowExceptionIfUnexpected(res);
		return *res;
	}

	// Use this from the render (realtime) context, when you are sure the parameter should exist.
	[[nodiscard]] AudioUnitParameterValue GetParameterRT(
		AudioUnitParameterID paramID) const AUSDK_RTSAFE
	{
		const auto res = GetParameterOrError(paramID);
		AUSDK_Assert(res);
		return *res;
	}

	// Primitive, returns an error if the parameter does not exist.
	[[nodiscard]] Expected<AudioUnitParameterValue> GetParameterOrError(
		AudioUnitParameterID paramID) const AUSDK_RTSAFE;

	// Use this from the control (non-realtime) context. Throws if the parameter doesn't exist.
	// Only set okWhenInitialized to true when you know the outside world cannot access this
	// element. Otherwise the parameter map could get corrupted.
	void SetParameter(
		AudioUnitParameterID paramID, AudioUnitParameterValue value, bool okWhenInitialized = false)
	{
		const auto res = SetParameterOrError(paramID, value, okWhenInitialized);
		ThrowExceptionIfUnexpected(res);
	}

	// Use this from the render (realtime) context, when you are sure the parameter should exist.
	void SetParameterRT(AudioUnitParameterID paramID, AudioUnitParameterValue value,
		bool okWhenInitialized = false) AUSDK_RTSAFE
	{
		const auto res = SetParameterOrError(paramID, value, okWhenInitialized);
		AUSDK_Assert(res);
	}

	// Primitive, returns an error if the parameter does not exist.
	Expected<void> SetParameterOrError(AudioUnitParameterID paramID, AudioUnitParameterValue value,
		bool okWhenInitialized = false) AUSDK_RTSAFE;

	// Only set okWhenInitialized to true when you know the outside world cannot access this
	// element. Otherwise the parameter map could get corrupted. N.B. This only handles
	// immediate parameters. Override to implement ramping. Called from
	// AUBase::ProcessForScheduledParams.
	[[nodiscard]] virtual OSStatus SetScheduledEvent(AudioUnitParameterID paramID,
		const AudioUnitParameterEvent& inEvent, UInt32 inSliceOffsetInBuffer,
		UInt32 inSliceDurationFrames, bool okWhenInitialized = false) AUSDK_RTSAFE;

	[[nodiscard]] AUBase& GetAudioUnit() const noexcept { return mAudioUnit; }

	void SaveState(AudioUnitScope scope, CFMutableDataRef data);
	const UInt8* RestoreState(const UInt8* state);

	[[nodiscard]] Owned<CFStringRef> GetName() const noexcept { return mElementName; }
	void SetName(CFStringRef inName) noexcept { mElementName = inName; }

	[[nodiscard]] bool HasName() const noexcept { return *mElementName != nullptr; }

	virtual void UseIndexedParameters(UInt32 inNumberOfParameters);

	virtual AUIOElement* AsIOElement() AUSDK_RTSAFE { return nullptr; }

private:
	using ParameterValue = AtomicValue<float>;

	AUBase& mAudioUnit;
	flat_map<AudioUnitParameterID, ParameterValue> mParameters;
	bool mUseIndexedParameters;
	std::vector<ParameterValue> mIndexedParameters;
	Owned<CFStringRef> mElementName;
};


// ____________________________________________________________________________
//

/// A subclass of AUElement which represents an input or output bus, and has an associated
/// audio format and buffers.
class AUIOElement : public AUElement {
public:
	explicit AUIOElement(AUBase& audioUnit);

	AUIOElement(AUBase& audioUnit, const AudioStreamBasicDescription& format)
		: AUIOElement{ audioUnit }
	{
		mStreamFormat = format;
	}

	AUSDK_DEPRECATED("Construct with a reference")
	explicit AUIOElement(AUBase* audioUnit) : AUIOElement(*audioUnit) {}

	[[nodiscard]] const AudioStreamBasicDescription& GetStreamFormat() const noexcept
	{
		return mStreamFormat;
	}

	virtual OSStatus SetStreamFormat(const AudioStreamBasicDescription& format);

	virtual void AllocateBuffer(UInt32 inFramesToAllocate = 0);

	void DeallocateBuffer();

	/// Determines (via subclass override) whether the element's buffer list needs to be allocated.
	[[nodiscard]] virtual bool NeedsBufferSpace() const = 0;

	void SetWillAllocateBuffer(bool inFlag) noexcept { mWillAllocate = inFlag; }

	[[nodiscard]] bool WillAllocateBuffer() const noexcept { return mWillAllocate; }

	AudioBufferList& PrepareBuffer(UInt32 nFrames)
	{
		if (mWillAllocate) {
			return mIOBuffer.PrepareBuffer(mStreamFormat, nFrames);
		}
		Throw(kAudioUnitErr_InvalidPropertyValue);
	}

	ExpectedPtr<AudioBufferList> PrepareBufferOrError(UInt32 nFrames) AUSDK_RTSAFE
	{
		if (mWillAllocate) {
			return mIOBuffer.PrepareBufferOrError(mStreamFormat, nFrames);
		}
		return Unexpected(kAudioUnitErr_InvalidPropertyValue);
	}

	AudioBufferList& PrepareNullBuffer(UInt32 nFrames)
	{
		return mIOBuffer.PrepareNullBuffer(mStreamFormat, nFrames);
	}

	ExpectedPtr<AudioBufferList> PrepareNullBufferOrError(UInt32 nFrames) AUSDK_RTSAFE
	{
		return mIOBuffer.PrepareNullBufferOrError(mStreamFormat, nFrames);
	}

	AudioBufferList& SetBufferList(const AudioBufferList& abl)
	{
		return mIOBuffer.SetBufferList(abl);
	}

	ExpectedPtr<AudioBufferList> SetBufferListOrError(const AudioBufferList& abl) AUSDK_RTSAFE
	{
		return mIOBuffer.SetBufferListOrError(abl);
	}

	void SetBuffer(UInt32 index, AudioBuffer& ab) { mIOBuffer.SetBuffer(index, ab); }

	Expected<void> SetBufferOrError(UInt32 index, AudioBuffer& ab) AUSDK_RTSAFE
	{
		return mIOBuffer.SetBufferOrError(index, ab);
	}

	void InvalidateBufferList() { mIOBuffer.InvalidateBufferList(); }

	[[nodiscard]] AudioBufferList& GetBufferList() const { return mIOBuffer.GetBufferList(); }

	ExpectedPtr<AudioBufferList> GetBufferListOrError() const AUSDK_RTSAFE
	{
		return mIOBuffer.GetBufferListOrError();
	}

	[[nodiscard]] float* GetFloat32ChannelData(UInt32 ch) const
	{
		const auto& abl = GetBufferList();
		if (IsInterleaved()) {
			return static_cast<float*>(abl.mBuffers[0].mData) + ch; // NOLINT
		}
		return static_cast<float*>(abl.mBuffers[ch].mData); // NOLINT
	}

	// N.B. Returns null pointers on failure.
	[[nodiscard]] float* GetFloat32ChannelDataRT(UInt32 ch) const AUSDK_RTSAFE
	{
		const auto abl = GetBufferListOrError();
		if (!abl) [[unlikely]] {
			return nullptr;
		}
		if (IsInterleaved()) {
			return static_cast<float*>(abl->mBuffers[0].mData) + ch; // NOLINT
		}
		return static_cast<float*>(abl->mBuffers[ch].mData); // NOLINT
	}

	void CopyBufferListTo(AudioBufferList& abl) const { mIOBuffer.CopyBufferListTo(abl); }
	void CopyBufferContentsTo(AudioBufferList& abl) const { mIOBuffer.CopyBufferContentsTo(abl); }

	Expected<void> CopyBufferListToOrError(AudioBufferList& abl) const AUSDK_RTSAFE
	{
		return mIOBuffer.CopyBufferListToOrError(abl);
	}
	Expected<void> CopyBufferContentsToOrError(AudioBufferList& abl) const AUSDK_RTSAFE
	{
		return mIOBuffer.CopyBufferContentsToOrError(abl);
	}

	[[nodiscard]] bool IsInterleaved() const noexcept { return ASBD::IsInterleaved(mStreamFormat); }
	[[nodiscard]] UInt32 NumberChannels() const noexcept { return mStreamFormat.mChannelsPerFrame; }
	[[nodiscard]] UInt32 NumberInterleavedChannels() const noexcept
	{
		return ASBD::NumberInterleavedChannels(mStreamFormat);
	}
	virtual std::vector<AudioChannelLayoutTag> GetChannelLayoutTags();

	[[nodiscard]] const AUChannelLayout& ChannelLayout() const { return mChannelLayout; }

	// Old layout methods
	virtual OSStatus SetAudioChannelLayout(const AudioChannelLayout& inLayout);
	virtual UInt32 GetAudioChannelLayout(AudioChannelLayout* outLayoutPtr, bool& outWritable);

	virtual OSStatus RemoveAudioChannelLayout();

	/*! @fn AsIOElement*/
	AUIOElement* AsIOElement() AUSDK_RTSAFE override { return this; }

protected:
	AUBufferList& IOBuffer() noexcept { return mIOBuffer; }
	void ForceSetAudioChannelLayout(const AudioChannelLayout& inLayout)
	{
		mChannelLayout = inLayout;
	}

private:
	AudioStreamBasicDescription mStreamFormat{};
	AUChannelLayout mChannelLayout{};
	AUBufferList mIOBuffer; // for input: input proc buffer, only allocated when needed
							// for output: output cache, usually allocated early on
	bool mWillAllocate{ false };
};

// ____________________________________________________________________________
//
/*!
	@class	AUScopeDelegate
	@brief	Provides a way to customize a scope, thereby obtaining virtual scopes.

	Can be used to implement scopes with variable numbers of elements.
*/
class AUScopeDelegate {
public:
	AUScopeDelegate() = default;

	virtual ~AUScopeDelegate() = default;

	AUScopeDelegate(const AUScopeDelegate&) = delete;
	AUScopeDelegate(AUScopeDelegate&&) = delete;
	AUScopeDelegate& operator=(const AUScopeDelegate&) = delete;
	AUScopeDelegate& operator=(AUScopeDelegate&&) = delete;

	void Initialize(AUBase* creator, AudioUnitScope scope, UInt32 numElements)
	{
		mCreator = creator;
		mScope = scope;
		SetNumberOfElements(numElements);
	}
	virtual void SetNumberOfElements(UInt32 numElements) = 0;
	virtual UInt32 GetNumberOfElements() AUSDK_RTSAFE = 0;
	virtual AUElement* GetElement(UInt32 elementIndex) AUSDK_RTSAFE = 0;

	[[nodiscard]] AUBase* GetCreator() const noexcept { return mCreator; }
	[[nodiscard]] AudioUnitScope GetScope() const noexcept { return mScope; }


private:
	AUBase* mCreator{ nullptr };
	AudioUnitScope mScope{ 0 };
};

// ____________________________________________________________________________
//
/*!
	@class	AUScope
	@brief	Organizes one or more elements into an addressable group (e.g. global, input, output).
*/
class AUScope {
public:
	AUScope() = default;

	~AUScope() = default;

	AUScope(const AUScope&) = delete;
	AUScope(AUScope&&) = delete;
	AUScope& operator=(const AUScope&) = delete;
	AUScope& operator=(AUScope&&) = delete;

	void Initialize(AUBase* creator, AudioUnitScope scope, UInt32 numElements)
	{
		mCreator = creator;
		mScope = scope;

		if (mDelegate != nullptr) {
			return mDelegate->Initialize(creator, scope, numElements);
		}

		SetNumberOfElements(numElements);
	}
	void SetNumberOfElements(UInt32 numElements);

	[[nodiscard]] UInt32 GetNumberOfElements() const
	{
		if (mDelegate != nullptr) {
			return mDelegate->GetNumberOfElements();
		}
		return static_cast<UInt32>(mElements.size());
	}

	[[nodiscard]] AUElement* GetElement(UInt32 elementIndex) const AUSDK_RTSAFE
	{
		if (mDelegate != nullptr) {
			return mDelegate->GetElement(elementIndex);
		}
		return elementIndex < mElements.size() ? mElements[elementIndex].get() : nullptr;
	}
	[[nodiscard]] AUElement* SafeGetElement(UInt32 elementIndex) const
	{
		AUElement* const element = GetElement(elementIndex);
		ausdk::ThrowExceptionIf(element == nullptr, kAudioUnitErr_InvalidElement);
		return element;
	}
	[[nodiscard]] AUIOElement* GetIOElement(UInt32 elementIndex) const
	{
		AUElement* const element = GetElement(elementIndex);
		AUIOElement* const ioel = element != nullptr ? element->AsIOElement() : nullptr;
		ausdk::ThrowExceptionIf(ioel == nullptr, kAudioUnitErr_InvalidElement);
		return ioel;
	}

	template <typename Elem = AUElement>
	[[nodiscard]] ExpectedPtr<Elem> GetElementOrError(UInt32 elementIndex) const AUSDK_RTSAFE
	{
		if (mDelegate != nullptr) {
			if (auto* elem = mDelegate->GetElement(elementIndex)) {
				return *static_cast<Elem*>(elem);
			}
		} else if (elementIndex < mElements.size()) {
			if (auto* elem = mElements[elementIndex].get()) {
				return *static_cast<Elem*>(elem);
			}
		}
		return Unexpected(kAudioUnitErr_InvalidElement);
	}

	[[nodiscard]] ExpectedPtr<AUIOElement> GetIOElementOrError(UInt32 elementIndex) const
	{
		auto& element = AUSDK_UnwrapOrReturnUnexpected(GetElementOrError(elementIndex));
		if (AUIOElement* const ioel = element.AsIOElement()) {
			return *ioel;
		}
		return Unexpected(kAudioUnitErr_InvalidElement);
	}

	[[nodiscard]] bool HasElementWithName() const;
	void AddElementNamesToDict(CFMutableDictionaryRef inNameDict) const;

	[[nodiscard]] std::vector<AudioUnitElement> RestoreElementNames(
		CFDictionaryRef inNameDict) const;

	[[nodiscard]] AudioUnitScope GetScope() const noexcept { return mScope; }

	void SetDelegate(AUScopeDelegate* inDelegate) noexcept { mDelegate = inDelegate; }
	void SaveState(CFMutableDataRef data) const;
	const UInt8* RestoreState(const UInt8* state) const;

private:
	using ElementVector = std::vector<std::unique_ptr<AUElement>>;

	AUBase* mCreator{ nullptr };
	AudioUnitScope mScope{ 0 };
	ElementVector mElements;
	AUScopeDelegate* mDelegate{ nullptr };
};

AUSDK_END_NO_RT_WARNINGS

} // namespace ausdk

#endif // AudioUnitSDK_AUScopeElement_h
