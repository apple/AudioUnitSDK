/*!
	@file		AudioUnitSDK/AUBuffer.h
	@copyright	© 2000-2025 Apple Inc. All rights reserved.
*/
#ifndef AudioUnitSDK_AUBuffer_h
#define AudioUnitSDK_AUBuffer_h

// clang-format off
#include <AudioUnitSDK/AUConfig.h> // must come first
// clang-format on
#include <AudioUnitSDK/AUUtility.h>

#include <cstddef>
#include <cstring>
#include <optional>

namespace ausdk {

AUSDK_BEGIN_NO_RT_WARNINGS

/// struct created/destroyed by allocator. Do not attempt to manually create/destroy.
struct AllocatedBuffer {
	const UInt32 mMaximumNumberBuffers;
	const UInt32 mMaximumBytesPerBuffer;
	const UInt32 mReservedA[2]{}; // NOLINT C-style array
	const UInt32 mHeaderSize;
	const UInt32 mBufferDataSize;
	const UInt32 mReservedB[2]{}; // NOLINT C-style array
	void* const mBufferData;
	void* const mReservedC{};

	AudioBufferList mAudioBufferList{};
	// opaque variable-length data may follow the AudioBufferList


	AllocatedBuffer(UInt32 maxBuffers, UInt32 maxBytesPerBuffer, UInt32 headerSize,
		UInt32 bufferDataSize, void* bufferData)
		: mMaximumNumberBuffers{ maxBuffers }, mMaximumBytesPerBuffer{ maxBytesPerBuffer },
		  mHeaderSize{ headerSize }, mBufferDataSize{ bufferDataSize }, mBufferData{ bufferData }
	{
	}

	AudioBufferList& Prepare(UInt32 channelsPerBuffer, UInt32 bytesPerBuffer);

	ExpectedPtr<AudioBufferList> PrepareOrError(
		UInt32 channelsPerBuffer, UInt32 bytesPerBuffer) AUSDK_RTSAFE;

	AudioBufferList& PrepareNull(UInt32 channelsPerBuffer, UInt32 bytesPerBuffer);

	ExpectedPtr<AudioBufferList> PrepareNullOrError(
		UInt32 channelsPerBuffer, UInt32 bytesPerBuffer) AUSDK_RTSAFE;
};

/*!
	@class	BufferAllocator
	@brief	Class which allocates memory for internal audio buffers.

	To customize, create a subclass and replace the BufferAllocator::instance() implementation.
*/
class BufferAllocator {
public:
	/// Obtain the global instance, creating it if necessary.
	static BufferAllocator& instance();

	BufferAllocator() = default;
	virtual ~BufferAllocator() = default;

	// Rule of 5
	BufferAllocator(const BufferAllocator&) = delete;
	BufferAllocator(BufferAllocator&&) = delete;
	BufferAllocator& operator=(const BufferAllocator&) = delete;
	BufferAllocator& operator=(BufferAllocator&&) = delete;

	// N.B. Must return zeroed memory aligned to at least 16 bytes.
	virtual AllocatedBuffer* Allocate(
		UInt32 numberBuffers, UInt32 maxBytesPerBuffer, UInt32 reservedFlags);
	virtual void Deallocate(AllocatedBuffer* allocatedBuffer);
};

/*!
	@class	AUBufferList
	@brief	Manages an `AudioBufferList` backed by allocated memory buffers.
*/
class AUBufferList {
	enum class EPtrState { Invalid, ToMyMemory, ToExternalMemory };

public:
	AUBufferList() = default;
	~AUBufferList() { Deallocate(); }

	AUBufferList(const AUBufferList&) = delete;
	AUBufferList(AUBufferList&&) = delete;
	AUBufferList& operator=(const AUBufferList&) = delete;
	AUBufferList& operator=(AUBufferList&&) = delete;

	AudioBufferList& PrepareBuffer(const AudioStreamBasicDescription& format, UInt32 nFrames)
	{
		const auto maybeABL = PrepareBufferOrError(format, nFrames);
		ThrowExceptionIfUnexpected(maybeABL);
		return *maybeABL;
	}

	ExpectedPtr<AudioBufferList> PrepareBufferOrError(
		const AudioStreamBasicDescription& format, UInt32 nFrames) AUSDK_RTSAFE;

	AudioBufferList& PrepareNullBuffer(const AudioStreamBasicDescription& format, UInt32 nFrames)
	{
		const auto maybeABL = PrepareNullBufferOrError(format, nFrames);
		ThrowExceptionIfUnexpected(maybeABL);
		return *maybeABL;
	}

	ExpectedPtr<AudioBufferList> PrepareNullBufferOrError(
		const AudioStreamBasicDescription& format, UInt32 nFrames) AUSDK_RTSAFE;

	AudioBufferList& SetBufferList(const AudioBufferList& abl)
	{
		const auto maybeABL = SetBufferListOrError(abl);
		ThrowExceptionIfUnexpected(maybeABL);
		return *maybeABL;
	}

	ExpectedPtr<AudioBufferList> SetBufferListOrError(const AudioBufferList& abl) AUSDK_RTSAFE
	{
		if (mAllocatedStreams < abl.mNumberBuffers) {
			return Unexpected(-1);
		}
		mPtrState = EPtrState::ToExternalMemory;
		auto& myabl = mBuffers->mAudioBufferList;
		memcpy(&myabl, &abl,
			static_cast<size_t>(
				reinterpret_cast<const std::byte*>(&abl.mBuffers[abl.mNumberBuffers]) - // NOLINT
				reinterpret_cast<const std::byte*>(&abl)));                             // NOLINT
		return myabl;
	}

	void SetBuffer(UInt32 index, const AudioBuffer& ab)
	{
		const auto res = SetBufferOrError(index, ab);
		ThrowExceptionIfUnexpected(res);
	}

	Expected<void> SetBufferOrError(UInt32 index, const AudioBuffer& ab) AUSDK_RTSAFE
	{
		auto& myabl = mBuffers->mAudioBufferList;
		if (mPtrState == EPtrState::Invalid || index >= myabl.mNumberBuffers) {
			return Unexpected(-1);
		}
		mPtrState = EPtrState::ToExternalMemory;
		myabl.mBuffers[index] = ab; // NOLINT
		return {};
	}

	void InvalidateBufferList() noexcept { mPtrState = EPtrState::Invalid; }

	[[nodiscard]] AudioBufferList& GetBufferList() const
	{
		const auto maybeABL = GetBufferListOrError();
		ThrowExceptionIfUnexpected(maybeABL);
		return *maybeABL;
	}

	[[nodiscard]] ExpectedPtr<AudioBufferList> GetBufferListOrError() const AUSDK_RTSAFE
	{
		if (mPtrState == EPtrState::Invalid) {
			return Unexpected(-1);
		}
		return mBuffers->mAudioBufferList;
	}

	void CopyBufferListTo(AudioBufferList& abl) const
	{
		const auto res = CopyBufferListToOrError(abl);
		ThrowExceptionIfUnexpected(res);
	}

	Expected<void> CopyBufferListToOrError(AudioBufferList& abl) const AUSDK_RTSAFE
	{
		if (mPtrState == EPtrState::Invalid) {
			return Unexpected(-1);
		}
		memcpy(&abl, &mBuffers->mAudioBufferList,
			static_cast<size_t>(
				reinterpret_cast<std::byte*>(&abl.mBuffers[abl.mNumberBuffers]) - // NOLINT
				reinterpret_cast<std::byte*>(&abl)));                             // NOLINT
		return {};
	}

	void CopyBufferContentsTo(AudioBufferList& destabl) const
	{
		const auto res = CopyBufferContentsToOrError(destabl);
		ThrowExceptionIfUnexpected(res);
	}

	Expected<void> CopyBufferContentsToOrError(AudioBufferList& destabl) const AUSDK_RTSAFE
	{
		if (mPtrState == EPtrState::Invalid) {
			return Unexpected(-1);
		}
		const auto& srcabl = mBuffers->mAudioBufferList;
		const AudioBuffer* srcbuf = srcabl.mBuffers; // NOLINT
		AudioBuffer* destbuf = destabl.mBuffers;     // NOLINT

		for (UInt32 i = 0; i < destabl.mNumberBuffers; ++i, ++srcbuf, ++destbuf) { // NOLINT
			if (i >=
				srcabl.mNumberBuffers) { // duplicate last source to additional outputs [4341137]
				--srcbuf;                // NOLINT
			}

			const auto srcByteSize = srcbuf->mDataByteSize;
			const void* srcData = srcbuf->mData;
			void* dstData = destbuf->mData;

			if (srcByteSize > 0 && (srcData == nullptr || dstData == nullptr)) {
				return Unexpected(-1);
			}

			if (dstData != srcData) {
				memmove(dstData, srcData, srcByteSize);
			}
			destbuf->mDataByteSize = srcByteSize;
		}
		return {};
	}

	void Allocate(const AudioStreamBasicDescription& format, UInt32 nFrames);

	void Deallocate();

	// AudioBufferList utilities
	static void ZeroBuffer(AudioBufferList& abl)
	{
		AudioBuffer* buf = abl.mBuffers;                         // NOLINT
		for (UInt32 i = 0; i < abl.mNumberBuffers; ++i, ++buf) { // NOLINT
			memset(buf->mData, 0, buf->mDataByteSize);
		}
	}

	[[nodiscard]] UInt32 GetAllocatedFrames() const noexcept { return mAllocatedFrames; }

private:
	EPtrState mPtrState{ EPtrState::Invalid };
	AllocatedBuffer* mBuffers = nullptr; // only valid between Allocate and Deallocate

	UInt32 mAllocatedStreams{ 0 };
	UInt32 mAllocatedFrames{ 0 };
};

} // namespace ausdk

AUSDK_END_NO_RT_WARNINGS

#endif // AudioUnitSDK_AUBuffer_h
