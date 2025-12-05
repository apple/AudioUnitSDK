/*!
	@file		AudioUnitSDK/AUUtility.h
	@copyright	© 2000-2025 Apple Inc. All rights reserved.
*/
#ifndef AudioUnitSDK_AUUtility_h
#define AudioUnitSDK_AUUtility_h

// clang-format off
#include <AudioUnitSDK/AUConfig.h> // must come first
// clang-format on

// OS
#include <CoreFoundation/CFByteOrder.h>

#if AUSDK_HAVE_MACH_TIME
#include <mach/mach_time.h>
#endif

// std
#include <bitset>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <expected>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark Error-handling macros

#ifndef AUSDK_LOG_OBJECT
#define AUSDK_LOG_OBJECT OS_LOG_DEFAULT // NOLINT macro
#endif                                  // AUSDK_LOG_OBJECT

#ifdef AUSDK_NO_LOGGING
#define AUSDK_LogError(...) /* NOLINT macro */
#else
#include <os/log.h>
#include <syslog.h>
#define AUSDK_LogError(...) /* NOLINT macro */                                                     \
	if (__builtin_available(macOS 10.11, *)) {                                                     \
		os_log_error(AUSDK_LOG_OBJECT, __VA_ARGS__);                                               \
	} else {                                                                                       \
		syslog(LOG_ERR, __VA_ARGS__);                                                              \
	}
#endif

// logging from realtime thread
#define AUSDK_LogError_RT(...) AUSDK_RT_UNSAFE(AUSDK_LogError(__VA_ARGS__)) /* NOLINT macro */

#define AUSDK_Catch(result) /* NOLINT(cppcoreguidelines-macro-usage) */                            \
	catch (const ausdk::AUException& exc) { (result) = exc.mError; }                               \
	catch (const std::bad_alloc&) { (result) = kAudio_MemFullError; }                              \
	catch (const OSStatus& catch_err) { (result) = catch_err; }                                    \
	catch (const std::system_error& exc) { (result) = exc.code().value(); }                        \
	catch (...) { (result) = -1; }

#define AUSDK_Require(expr, error) /* NOLINT(cppcoreguidelines-macro-usage) */                     \
	do {                                                                                           \
		if (!(expr)) {                                                                             \
			return error;                                                                          \
		}                                                                                          \
	} while (0) /* NOLINT */

// Check an expected; if it holds an error, return that error.
#define AUSDK_RequireExpected(exp) /* NOLINT(cppcoreguidelines-macro-usage) */                     \
	AUSDK_Require(exp, exp.error())

// Evaluate an expression resulting in an Expected<T>. If the Expected contains an error, return
// the error. Otherwise, unwrap the Expected.
#define AUSDK_UnwrapOrReturnError(_expression)                                                     \
	*({                                                                                            \
		const auto e = (_expression);                                                              \
		if (!e) [[unlikely]]                                                                       \
			return e.error();                                                                      \
		e;                                                                                         \
	})

// TODO: deprecate this macro because it swallows the error
#define AUSDK_UnwrapOrReturnVoid(_expression)                                                      \
	*({                                                                                            \
		const auto e = (_expression);                                                              \
		if (!e) [[unlikely]]                                                                       \
			return;                                                                                \
		e;                                                                                         \
	})

#define AUSDK_CheckReturnError(_expression)                                                        \
	({                                                                                             \
		const auto e = (_expression);                                                              \
		if (!e) [[unlikely]]                                                                       \
			return e.error();                                                                      \
	})

// Evaluate an expression resulting in an Expected<T>. If the Expected contains an error, return
// the error as an Unexpected. Otherwise, unwrap the Expected.
#define AUSDK_UnwrapOrReturnUnexpected(_expression)                                                \
	*({                                                                                            \
		const auto e = (_expression);                                                              \
		if (!e) [[unlikely]]                                                                       \
			return ausdk::Unexpected{ e.error() };                                                 \
		e;                                                                                         \
	})

#define AUSDK_Require_noerr(_expression) /* NOLINT(cppcoreguidelines-macro-usage) */               \
	do {                                                                                           \
		const auto status_tmp_macro_detail_ = (_expression);                                       \
		if (status_tmp_macro_detail_ != noErr) [[unlikely]] {                                      \
			return status_tmp_macro_detail_;                                                       \
		}                                                                                          \
	} while (0)

#define AUSDK_Assert(_expression)                                                                  \
	({                                                                                             \
		const auto e = (_expression);                                                              \
		if (!e) [[unlikely]]                                                                       \
			std::abort();                                                                          \
	})


// clang-format off

// The "loose" realtime-safety contract: exceptions are allowed.
#ifndef AUSDK_LOOSE_RT_SAFETY
	#define AUSDK_LOOSE_RT_SAFETY 1
#endif

// AUSDK adopts `noexcept` independently of `[[clang::nonblocking]]`; suppress diagnostics.
#define AUSDK_BEGIN_NO_RT_NOEXCEPT_WARNINGS \
	_Pragma("clang diagnostic push")                                           \
	_Pragma("clang diagnostic ignored \"-Wunknown-warning-option\"")           \
	_Pragma("clang diagnostic ignored \"-Wperf-constraint-implies-noexcept\"")
#define AUSDK_END_NO_RT_NOEXCEPT_WARNINGS  \
	_Pragma("clang diagnostic pop")

//  ASAN inserts blocking function calls in otherwise nonblocking functions.
#if defined(__has_feature) && __has_feature(address_sanitizer)
#define AUSDK_BEGIN_NO_RT_WARNINGS                                              \
    AUSDK_BEGIN_NO_RT_NOEXCEPT_WARNINGS                                         \
    _Pragma("clang diagnostic ignored \"-Wfunction-effects\"")
#else
#define AUSDK_BEGIN_NO_RT_WARNINGS                                              \
    AUSDK_BEGIN_NO_RT_NOEXCEPT_WARNINGS
#endif

#define AUSDK_END_NO_RT_WARNINGS                                                \
    AUSDK_END_NO_RT_NOEXCEPT_WARNINGS

/*!
	@macro	AUSDK_RTSAFE_TYPE
	@brief	A function type which is guaranteed / required to be realtime safe.
*/
#if defined(__has_attribute) && __has_attribute(nonblocking)
#  ifdef __cplusplus
#    define AUSDK_RTSAFE_TYPE [[clang::nonblocking]]
#  else
#    define AUSDK_RTSAFE_TYPE __attribute__((nonblocking))
#  endif
#else
#    define AUSDK_RTSAFE_TYPE
#endif

#ifndef AUSDK_RTSAFE_SECTION
#  define AUSDK_RTSAFE_SECTION
#endif

/*!
	@macro	AUSDK_RTSAFE
	@brief	Declares a function as `nonblocking`, with a section attribute.
	
	Example placement:
		`void func(int param) AUSDK_RTSAFE;`
*/
#define AUSDK_RTSAFE AUSDK_RTSAFE_TYPE AUSDK_RTSAFE_SECTION

#if AUSDK_LOOSE_RT_SAFETY
	#define AUSDK_RTSAFE_LOOSE AUSDK_RTSAFE
#else
	#define AUSDK_RTSAFE_LOOSE
#endif

/*!
	@macro	AUSDK_RTSAFE_LAMBDA
	@brief	Declares a lambda as `nonblocking`, with a section attribute.
	
	Example placement:
		`auto lambda = [captures](int param) AUSDK_RTSAFE_LAMBDA -> int { ... };`
	
	Note: It's weird and annoying that this has to be different from `AUSDK_RTSAFE`.
*/
#define AUSDK_RTSAFE_LAMBDA AUSDK_RTSAFE_SECTION AUSDK_RTSAFE_TYPE

/*!
	@macro	AUSDK_RT_UNSAFE_BEGIN
	@brief	Begins a region of code as exempt from nonblocking checks.

	N.B. Every use of this is a maintenance and safety liability; use as a last resort.
*/
#define AUSDK_RT_UNSAFE_BEGIN(reason)                                \
	_Pragma("clang diagnostic push")                                 \
	_Pragma("clang diagnostic ignored \"-Wunknown-warning-option\"") \
	_Pragma("clang diagnostic ignored \"-Wfunction-effects\"")

/*!
	@macro	AUSDK_RT_UNSAFE_END
	@brief	Ends a region of code which is exempt from nonblocking checks.
*/
#define AUSDK_RT_UNSAFE_END \
	_Pragma("clang diagnostic pop")

/*!
	@macro	AUSDK_RT_UNSAFE_END
	@brief	Disables nonblocking checks during the evaluation of the expression.

	N.B. Every use of this is a maintenance and safety liability; use as a last resort.
*/
#define AUSDK_RT_UNSAFE(...)                                         \
	_Pragma("clang diagnostic push")                                 \
	_Pragma("clang diagnostic ignored \"-Wunknown-warning-option\"") \
	_Pragma("clang diagnostic ignored \"-Wfunction-effects\"")       \
	__VA_ARGS__                                                      \
	_Pragma("clang diagnostic pop")

// clang-format on

#pragma mark -

// -------------------------------------------------------------------------------------------------

namespace ausdk {

// -------------------------------------------------------------------------------------------------

/// A wrapper to preserve the nonblocking attribute on a function pointer. Copied from the
/// nonblocking proposal.
template <typename>
class RTSafeFP;

template <typename R, typename... Args>
class RTSafeFP<R(Args...)> {
public:
	using impl_t = R (*)(Args...) AUSDK_RTSAFE_TYPE;

private:
	impl_t mImpl;

public:
	RTSafeFP(impl_t f) : mImpl{ f } {}

	R operator()(Args... args) const { return mImpl(std::forward<Args>(args)...); }
};

// deduction guide (copied from std::function)
template <class R, class... ArgTypes>
RTSafeFP(R (*)(ArgTypes...)) -> RTSafeFP<R(ArgTypes...)>;

// -------------------------------------------------------------------------------------------------

/// A subclass of std::runtime_error that holds an OSStatus error.
class AUException : public std::runtime_error {
public:
	explicit AUException(OSStatus err)
		: std::runtime_error{ std::string("OSStatus ") + std::to_string(err) }, mError{ err }
	{
	}

	const OSStatus mError;
};

AUSDK_BEGIN_NO_RT_NOEXCEPT_WARNINGS
[[noreturn]] inline void Throw(OSStatus err) AUSDK_RTSAFE_LOOSE
{
	AUSDK_RT_UNSAFE_BEGIN("Can only log and throw under loose contract")
	AUSDK_LogError("throwing %d", static_cast<int>(err));
	throw AUException{ err };
	AUSDK_RT_UNSAFE_END
}
AUSDK_END_NO_RT_NOEXCEPT_WARNINGS

inline void ThrowExceptionIf(bool condition, OSStatus err)
{
	if (condition) [[unlikely]] {
		Throw(err);
	}
}

AUSDK_BEGIN_NO_RT_NOEXCEPT_WARNINGS
[[noreturn]] inline void ThrowQuiet(OSStatus err) AUSDK_RTSAFE_LOOSE
{
	AUSDK_RT_UNSAFE_BEGIN("Can only throw under loose realtime contract")
	throw AUException{ err };
	AUSDK_RT_UNSAFE_END
}
AUSDK_END_NO_RT_NOEXCEPT_WARNINGS

inline void ThrowQuietIf(bool condition, OSStatus err)
{
	if (condition) [[unlikely]] {
		ThrowQuiet(err);
	}
}

// -------------------------------------------------------------------------------------------------

/// Wrap a std::recursive_mutex in a C++ Mutex (named requirement). Methods are virtual to support
/// customization.
class AUMutex {
public:
	AUMutex() = default;
	virtual ~AUMutex() = default;

	AUMutex(const AUMutex&) = delete;
	AUMutex(AUMutex&&) = delete;
	AUMutex& operator=(const AUMutex&) = delete;
	AUMutex& operator=(AUMutex&&) = delete;

	virtual void lock() { mImpl.lock(); }
	virtual void unlock() noexcept { mImpl.unlock(); }
	virtual bool try_lock() noexcept { return mImpl.try_lock(); }

private:
	std::recursive_mutex mImpl;
};

// -------------------------------------------------------------------------------------------------

/// Implement optional locking at AudioUnit non-realtime entry points (required only for a small
/// number of plug-ins which must synchronize against external entry points).
class AUEntryGuard {
public:
	explicit AUEntryGuard(AUMutex* maybeMutex) : mMutex{ maybeMutex }
	{
		if (mMutex != nullptr) {
			mMutex->lock();
		}
	}

	~AUEntryGuard() noexcept
	{
		if (mMutex != nullptr) {
			mMutex->unlock();
		}
	}

	AUEntryGuard(const AUEntryGuard&) = delete;
	AUEntryGuard(AUEntryGuard&&) = delete;
	AUEntryGuard& operator=(const AUEntryGuard&) = delete;
	AUEntryGuard& operator=(AUEntryGuard&&) = delete;

private:
	AUMutex* mMutex;
};

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark ASBD

/// Utility functions relating to AudioStreamBasicDescription.
namespace ASBD {

constexpr bool IsInterleaved(const AudioStreamBasicDescription& format) noexcept
{
	return (format.mFormatFlags & kLinearPCMFormatFlagIsNonInterleaved) == 0u;
}

constexpr UInt32 NumberInterleavedChannels(const AudioStreamBasicDescription& format) noexcept
{
	return IsInterleaved(format) ? format.mChannelsPerFrame : 1;
}

constexpr UInt32 NumberChannelStreams(const AudioStreamBasicDescription& format) noexcept
{
	return IsInterleaved(format) ? 1 : format.mChannelsPerFrame;
}

constexpr bool IsCommonFloat32(const AudioStreamBasicDescription& format) noexcept
{
	return (
		format.mFormatID == kAudioFormatLinearPCM && format.mFramesPerPacket == 1 &&
		format.mBytesPerPacket == format.mBytesPerFrame
		// so far, it's a valid PCM format
		&& (format.mFormatFlags & kLinearPCMFormatFlagIsFloat) != 0 &&
		(format.mChannelsPerFrame == 1 ||
			(format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0) &&
		((format.mFormatFlags & kAudioFormatFlagIsBigEndian) == kAudioFormatFlagsNativeEndian) &&
		format.mBitsPerChannel == 32 // NOLINT
		&& format.mBytesPerFrame == NumberInterleavedChannels(format) * sizeof(float));
}

constexpr AudioStreamBasicDescription CreateCommonFloat32(
	Float64 sampleRate, UInt32 numChannels, bool interleaved = false) noexcept
{
	constexpr auto sampleSize = sizeof(Float32);

	AudioStreamBasicDescription asbd{};
	asbd.mFormatID = kAudioFormatLinearPCM;
	asbd.mFormatFlags = kAudioFormatFlagIsFloat |
						static_cast<AudioFormatFlags>(kAudioFormatFlagsNativeEndian) |
						kAudioFormatFlagIsPacked;
	asbd.mBitsPerChannel = 8 * sampleSize; // NOLINT magic number
	asbd.mChannelsPerFrame = numChannels;
	asbd.mFramesPerPacket = 1;
	asbd.mSampleRate = sampleRate;
	if (interleaved) {
		asbd.mBytesPerPacket = asbd.mBytesPerFrame = numChannels * sampleSize;
	} else {
		asbd.mBytesPerPacket = asbd.mBytesPerFrame = sampleSize;
		asbd.mFormatFlags |= kAudioFormatFlagIsNonInterleaved;
	}
	return asbd;
}

constexpr bool MinimalSafetyCheck(const AudioStreamBasicDescription& x) noexcept
{
	// This function returns false if there are sufficiently unreasonable values in any field.
	// It is very conservative so even some very unlikely values will pass.
	// This is just meant to catch the case where the data from a file is corrupted.

	return (x.mSampleRate >= 0.) && (x.mSampleRate < 3e6) // NOLINT SACD sample rate is 2.8224 MHz
		   && (x.mBytesPerPacket < 1000000)               // NOLINT
		   && (x.mFramesPerPacket < 1000000)              // NOLINT
		   && (x.mBytesPerFrame < 1000000)                // NOLINT
		   && (x.mChannelsPerFrame > 0) && (x.mChannelsPerFrame <= 1024) // NOLINT
		   && (x.mBitsPerChannel <= 1024)                                // NOLINT
		   && (x.mFormatID != 0) &&
		   !(x.mFormatID == kAudioFormatLinearPCM &&
			   (x.mFramesPerPacket != 1 || x.mBytesPerPacket != x.mBytesPerFrame));
}

inline bool IsEqual(
	const AudioStreamBasicDescription& lhs, const AudioStreamBasicDescription& rhs) noexcept
{
	return memcmp(&lhs, &rhs, sizeof(AudioStreamBasicDescription)) == 0;
}

} // namespace ASBD

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark ACL

/// Utility functions relating to AudioChannelLayout.
namespace ACL {

constexpr bool operator==(const AudioChannelLayout& lhs, const AudioChannelLayout& rhs) noexcept
{
	if (lhs.mChannelLayoutTag != rhs.mChannelLayoutTag) {
		return false;
	}
	if (lhs.mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelBitmap) {
		return lhs.mChannelBitmap == rhs.mChannelBitmap;
	}
	if (lhs.mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelDescriptions) {
		if (lhs.mNumberChannelDescriptions != rhs.mNumberChannelDescriptions) {
			return false;
		}
		for (UInt32 i = 0; i < lhs.mNumberChannelDescriptions; ++i) {
			const auto& lhdesc = lhs.mChannelDescriptions[i]; // NOLINT array subscript
			const auto& rhdesc = rhs.mChannelDescriptions[i]; // NOLINT array subscript

			if (lhdesc.mChannelLabel != rhdesc.mChannelLabel) {
				return false;
			}
			if (lhdesc.mChannelLabel == kAudioChannelLabel_UseCoordinates) {
				if (memcmp(&lhdesc, &rhdesc, sizeof(AudioChannelDescription)) != 0) {
					return false;
				}
			}
		}
	}
	return true;
}

} // namespace ACL

// -------------------------------------------------------------------------------------------------

/// Utility wrapper for the variably-sized AudioChannelLayout struct.
class AUChannelLayout {
public:
	AUChannelLayout() : AUChannelLayout(0, kAudioChannelLayoutTag_UseChannelDescriptions, 0) {}

	/// Can construct from a layout tag.
	explicit AUChannelLayout(AudioChannelLayoutTag inTag) : AUChannelLayout(0, inTag, 0) {}

	AUChannelLayout(uint32_t inNumberChannelDescriptions, AudioChannelLayoutTag inChannelLayoutTag,
		AudioChannelBitmap inChannelBitMap)
		: mStorage(ObjectCountForByteSize(DataByteSize(inNumberChannelDescriptions)))
	{
		auto& acl = mStorage.front();
		acl.mChannelLayoutTag = inChannelLayoutTag;
		acl.mChannelBitmap = inChannelBitMap;
		acl.mNumberChannelDescriptions = inNumberChannelDescriptions;
	}

	/// Implicit conversion from AudioChannelLayout& is allowed.
	AUChannelLayout(const AudioChannelLayout& acl) // NOLINT
		: mStorage(ObjectCountForByteSize(DataByteSize(acl.mNumberChannelDescriptions)))
	{
		std::memcpy(mStorage.data(), &acl, DataByteSize(acl.mNumberChannelDescriptions));
	}

	~AUChannelLayout() noexcept = default;
	AUChannelLayout(AUChannelLayout&&) noexcept = default;
	AUChannelLayout& operator=(AUChannelLayout&&) noexcept = default;

	// copy overloads required because AudioChannelLayout deletes them
	AUChannelLayout(const AUChannelLayout& other) { CopyStorage(other); }

	AUChannelLayout& operator=(const AUChannelLayout& other)
	{
		CopyStorage(other);
		return *this;
	}

	bool operator==(const AUChannelLayout& other) const noexcept
	{
		return ACL::operator==(Layout(), other.Layout());
	}

	bool operator!=(const AUChannelLayout& other) const noexcept { return !(*this == other); }

	[[nodiscard]] bool IsValid() const noexcept { return NumberChannels() > 0; }

	[[nodiscard]] const AudioChannelLayout& Layout() const noexcept { return mStorage.front(); }

	[[nodiscard]] const AudioChannelLayout* LayoutPtr() const noexcept { return mStorage.data(); }

	/// After default construction, this method will return
	/// kAudioChannelLayoutTag_UseChannelDescriptions with 0 channel descriptions.
	[[nodiscard]] AudioChannelLayoutTag Tag() const noexcept { return Layout().mChannelLayoutTag; }

	[[nodiscard]] uint32_t NumberChannels() const noexcept { return NumberChannels(Layout()); }

	[[nodiscard]] uint32_t Size() const noexcept
	{
		return static_cast<uint32_t>(DataByteSize(Layout().mNumberChannelDescriptions));
	}

	static uint32_t NumberChannels(const AudioChannelLayout& inLayout) noexcept
	{
		if (inLayout.mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelDescriptions) {
			return inLayout.mNumberChannelDescriptions;
		}
		if (inLayout.mChannelLayoutTag == kAudioChannelLayoutTag_UseChannelBitmap) {
			constexpr size_t bitWidth = sizeof(inLayout.mChannelBitmap) * CHAR_BIT;
			return static_cast<uint32_t>(std::bitset<bitWidth>(inLayout.mChannelBitmap).count());
		}
		return AudioChannelLayoutTag_GetNumberOfChannels(inLayout.mChannelLayoutTag);
	}

	static constexpr size_t DataByteSize(uint32_t inNumberChannelDescriptions) noexcept
	{
		constexpr size_t headerSize = offsetof(AudioChannelLayout, mChannelDescriptions);
		return headerSize + (inNumberChannelDescriptions * sizeof(AudioChannelDescription));
	}

private:
	// number of contiguous ACL objects required to accommodate a specific storage size
	static constexpr size_t ObjectCountForByteSize(size_t inDataByteSize) noexcept
	{
		const size_t padding = ((inDataByteSize % sizeof(AudioChannelLayout)) > 0) ? 1 : 0;
		return (inDataByteSize / sizeof(AudioChannelLayout)) + padding;
	}

	void CopyStorage(const AUChannelLayout& inSource)
	{
		mStorage = decltype(mStorage)(inSource.mStorage.size());
		std::memcpy(
			mStorage.data(), inSource.mStorage.data(), std::span(inSource.mStorage).size_bytes());
	}

	// the data representation is not literally an array of these entire objects, however using it
	// as the underlying storage data type ensures object lifetime and alignment of the data pointer
	std::vector<AudioChannelLayout> mStorage;
};

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark AudioBufferList

/// Utility functions relating to AudioBufferList.
namespace ABL {

// if the return result is odd, there was a null buffer.
constexpr uint32_t IsBogusAudioBufferList(const AudioBufferList& abl)
{
	const AudioBuffer *buf = abl.mBuffers, *const bufEnd = buf + abl.mNumberBuffers;
	uint32_t sum =
		0; // defeat attempts by the compiler to optimize away the code that touches the buffers
	uint32_t anyNull = 0;
	for (; buf < bufEnd; ++buf) {
		const uint32_t* const p = static_cast<const uint32_t*>(buf->mData);
		if (p == nullptr) {
			anyNull = 1;
			continue;
		}
		const auto dataSize = buf->mDataByteSize;
		if (dataSize >= sizeof(*p)) {
			const size_t frameCount = dataSize / sizeof(*p);
			sum += p[0];
			sum += p[frameCount - 1];
		}
	}
	return anyNull | (sum & ~1u);
}

} // namespace ABL

// -------------------------------------------------------------------------------------------------
#pragma mark -
#pragma mark HostTime

#if AUSDK_HAVE_MACH_TIME
AUSDK_BEGIN_NO_RT_WARNINGS

/// Utility functions relating to Mach absolute time.
namespace HostTime {

/// Returns the current host time
inline uint64_t Current() AUSDK_RTSAFE { return AUSDK_RT_UNSAFE(mach_absolute_time()); }

/// Returns the frequency of the host timebase, in ticks per second.
inline double Frequency()
{
	struct mach_timebase_info timeBaseInfo {}; // NOLINT
	mach_timebase_info(&timeBaseInfo);
	//	the frequency of that clock is: (sToNanosDenominator / sToNanosNumerator) * 10^9
	return static_cast<double>(timeBaseInfo.denom) / static_cast<double>(timeBaseInfo.numer) *
		   1.0e9; // NOLINT
}

} // namespace HostTime
AUSDK_END_NO_RT_WARNINGS
#endif // AUSDK_HAVE_MACH_TIME

// -------------------------------------------------------------------------------------------------
#pragma mark -

/// Basic RAII wrapper for CoreFoundation types
template <typename T>
	requires std::is_pointer_v<T>
class Owned {
	explicit Owned(T obj, bool fromget) noexcept : mImpl{ obj }
	{
		if (fromget) {
			retainRef();
		}
	}

public:
	static Owned from_get(T obj) noexcept { return Owned{ obj, true }; }

	static Owned from_create(T obj) noexcept { return Owned{ obj, false }; }
	static Owned from_copy(T obj) noexcept { return Owned{ obj, false }; }

	Owned() noexcept = default;
	~Owned() noexcept { releaseRef(); }

	Owned(const Owned& other) noexcept : mImpl{ other.mImpl } { retainRef(); }

	Owned(Owned&& other) noexcept : mImpl{ std::exchange(other.mImpl, nullptr) } {}

	Owned& operator=(const Owned& other) noexcept
	{
		if (this != &other) {
			releaseRef();
			mImpl = other.mImpl;
			retainRef();
		}
		return *this;
	}

	Owned& operator=(Owned&& other) noexcept
	{
		std::swap(mImpl, other.mImpl);
		return *this;
	}

	T operator*() const noexcept { return get(); }
	T get() const noexcept { return mImpl; }

	/// As with `unique_ptr<T>::release()`, releases ownership of the reference to the caller (not
	/// to be confused with decrementing the reference count as with `CFRelease()`).
	T release() noexcept { return std::exchange(mImpl, nullptr); }

	/// This is a from_get operation.
	Owned& operator=(T cfobj) noexcept
	{
		if (mImpl != cfobj) {
			releaseRef();
			mImpl = cfobj;
			retainRef();
		}
		return *this;
	}

private:
	void retainRef() noexcept
	{
		if (mImpl != nullptr) {
			CFRetain(mImpl);
		}
	}

	void releaseRef() noexcept
	{
		if (mImpl != nullptr) {
			CFRelease(mImpl);
		}
	}

	T mImpl{ nullptr };
};

// -------------------------------------------------------------------------------------------------
#pragma mark -

template <typename T>
concept TriviallyCopySerializable = std::is_trivially_copyable_v<T> && std::is_standard_layout_v<T>;

// copy object data to arbitrary memory address that may not share object alignment
void Serialize(const TriviallyCopySerializable auto& inValue, void* outData) noexcept
{
	std::memcpy(outData, std::addressof(inValue), sizeof(inValue));
}

template <TriviallyCopySerializable T, std::size_t Extent>
void Serialize(std::span<T, Extent> inValues, void* outData) noexcept
{
	std::memcpy(outData, inValues.data(), inValues.size_bytes());
}

// ensure object lifetime and alignment of object data from opaque pointer
template <TriviallyCopySerializable T>
	requires std::is_nothrow_default_constructible_v<T> && (!std::is_const_v<T>)
T Deserialize(const void* inData) noexcept
{
	T result{};
	std::memcpy(std::addressof(result), inData, sizeof(result));
	return result;
}

template <TriviallyCopySerializable T>
	requires std::is_default_constructible_v<T> && (!std::is_const_v<T>)
std::vector<T> DeserializeArray(const void* inData, size_t inSizeBytes)
{
	std::vector<T> result(inSizeBytes / sizeof(T));
	std::memcpy(result.data(), inData, std::span(result).size_bytes());
	return result;
}

inline UInt32 DeserializeBigUInt32AndAdvance(const UInt8*& ioData) noexcept
{
	const auto value = Deserialize<UInt32>(ioData);
	ioData += sizeof(value); // NOLINT
	return CFSwapInt32BigToHost(value);
}

inline std::string MakeStringFrom4CC(uint32_t in4CC)
{
	in4CC = CFSwapInt32HostToBig(in4CC);

	constexpr auto safeIsPrint = [](char character) {
		return (character >= ' ') && (character <= '~');
	};

	char* const string = reinterpret_cast<char*>(&in4CC); // NOLINT
	for (size_t i = 0; i < sizeof(in4CC); ++i) {
		if (!safeIsPrint(string[i])) { // NOLINT
			string[i] = '.';           // NOLINT
		}
	}
	return std::string{ string, sizeof(in4CC) };
}

using Unexpected = std::unexpected<OSStatus>;

template <typename T>
using Expected = std::expected<T, OSStatus>;

// expected<reference_wrapper<T>> is unweildy.
// Instead, extend expected<T*> (with a guarantee that the pointer is non-null).
template <typename T>
class [[nodiscard]] ExpectedPtr : public std::expected<T*, OSStatus> {
public:
	using Base = std::expected<T*, OSStatus>;

	// Caution: default-constructed to null ptr.
	ExpectedPtr() = default;

	// Construct with a reference, to enforce non-null guarantee (unless default-constructed).
	ExpectedPtr(T& ref) : Base{ &ref } {} // NOLINT implicit OK

	ExpectedPtr(Unexpected&& u) : Base{ u } {} // NOLINT implicit OK

	// unchecked (parallel to expected::operator*); use operator bool to verify
	T* operator->() const { return Base::operator*(); }
	T* get() const { return Base::operator*(); }
	T& operator*() const { return *get(); }
};

template <typename T>
inline void ThrowExceptionIfUnexpected(const Expected<T>& e)
{
	if (!e) [[unlikely]] {
		Throw(e.error());
	}
}

} // namespace ausdk

#endif // AudioUnitSDK_AUUtility_h
