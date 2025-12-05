/*!
	@file		AudioUnitSDK/AUBuffer.cpp
	@copyright	© 2000-2025 Apple Inc. All rights reserved.
*/
#include <AudioUnitSDK/AUBuffer.h>
#include <AudioUnitSDK/AUUtility.h>

#include <AudioToolbox/AUComponent.h>

#include <cassert>
#include <cstddef>

namespace ausdk {

AUSDK_BEGIN_NO_RT_WARNINGS

inline void ThrowBadAlloc()
{
	AUSDK_LogError("AUBuffer throwing bad_alloc");
	throw std::bad_alloc();
}

// x: number to be rounded; y: the power of 2 to which to round
constexpr uint32_t RoundUpToMultipleOfPowerOf2(uint32_t x, uint32_t y) noexcept
{
	const uint32_t mask = y - 1u;
#if DEBUG
	assert((mask & y) == 0u); // verifies that y is a power of 2 NOLINT
#endif
	return (x + mask) & ~mask;
}

// a * b + c
static UInt32 SafeMultiplyAddUInt32(UInt32 a, UInt32 b, UInt32 c)
{
	if (a == 0 || b == 0) {
		return c; // prevent zero divide
	}

	if (a > (0xFFFFFFFF - c) / b) { // NOLINT magic
		ThrowBadAlloc();
	}

	return a * b + c;
}

AllocatedBuffer* BufferAllocator::Allocate(
	UInt32 numberBuffers, UInt32 maxBytesPerBuffer, UInt32 /*reservedFlags*/)
{
	constexpr size_t kAlignment = 16;
	constexpr size_t kMaxBufferListSize = 65536;

	// Check for a reasonable number of buffers (obviate a more complicated check with offsetof).
	if (numberBuffers > kMaxBufferListSize / sizeof(AudioBuffer)) {
		throw std::out_of_range("AudioBuffers::Allocate: Too many buffers");
	}

	maxBytesPerBuffer = RoundUpToMultipleOfPowerOf2(maxBytesPerBuffer, kAlignment);

	const auto bufferDataSize = SafeMultiplyAddUInt32(numberBuffers, maxBytesPerBuffer, 0);
	void* bufferData = nullptr;
	if (bufferDataSize > 0) {
		bufferData = malloc(bufferDataSize);
		// don't use calloc(); it might not actually touch the memory and cause a VM fault later
		memset(bufferData, 0, bufferDataSize);
	}

	const auto implSize = static_cast<uint32_t>(
		offsetof(AllocatedBuffer, mAudioBufferList.mBuffers[std::max(UInt32(1), numberBuffers)]));
	auto* const implMem = malloc(implSize);
	auto* const allocatedBuffer = new (implMem)
		AllocatedBuffer{ numberBuffers, maxBytesPerBuffer, implSize, bufferDataSize, bufferData };
	allocatedBuffer->mAudioBufferList.mNumberBuffers = numberBuffers;
	return allocatedBuffer;
}

void BufferAllocator::Deallocate(AllocatedBuffer* allocatedBuffer)
{
	if (allocatedBuffer->mBufferData != nullptr) {
		free(allocatedBuffer->mBufferData);
	}
	allocatedBuffer->~AllocatedBuffer();
	free(allocatedBuffer);
}


ExpectedPtr<AudioBufferList> AllocatedBuffer::PrepareOrError(
	UInt32 channelsPerBuffer, UInt32 bytesPerBuffer) AUSDK_RTSAFE
{
	if (mAudioBufferList.mNumberBuffers > mMaximumNumberBuffers) {
		// too many buffers
		return Unexpected(-1);
	}
	if (bytesPerBuffer > mMaximumBytesPerBuffer) {
		// insufficient capacity
		return Unexpected(-1);
	}

	auto* ptr = static_cast<std::byte*>(mBufferData);
	auto* const ptrend = ptr + mBufferDataSize;

	for (UInt32 bufIdx = 0, nBufs = mAudioBufferList.mNumberBuffers; bufIdx < nBufs; ++bufIdx) {
		auto& buf = mAudioBufferList.mBuffers[bufIdx]; // NOLINT
		buf.mNumberChannels = channelsPerBuffer;
		buf.mDataByteSize = bytesPerBuffer;
		buf.mData = ptr;
		ptr += mMaximumBytesPerBuffer; // NOLINT ptr math
	}
	if (ptr > ptrend) {
		// insufficient capacity
		return Unexpected(-1);
	}
	return mAudioBufferList;
}

ExpectedPtr<AudioBufferList> AllocatedBuffer::PrepareNullOrError(
	UInt32 channelsPerBuffer, UInt32 bytesPerBuffer) AUSDK_RTSAFE
{
	if (mAudioBufferList.mNumberBuffers > mMaximumNumberBuffers) {
		// too many buffers
		return Unexpected(-1);
	}
	for (UInt32 bufIdx = 0, nBufs = mAudioBufferList.mNumberBuffers; bufIdx < nBufs; ++bufIdx) {
		auto& buf = mAudioBufferList.mBuffers[bufIdx]; // NOLINT
		buf.mNumberChannels = channelsPerBuffer;
		buf.mDataByteSize = bytesPerBuffer;
		buf.mData = nullptr;
	}
	return mAudioBufferList;
}

ExpectedPtr<AudioBufferList> AUBufferList::PrepareBufferOrError(
	const AudioStreamBasicDescription& format, UInt32 nFrames) AUSDK_RTSAFE
{
	if (nFrames > mAllocatedFrames) {
		return Unexpected(kAudioUnitErr_TooManyFramesToProcess);
	}

	UInt32 nStreams = 0;
	UInt32 channelsPerStream = 0;
	if (ASBD::IsInterleaved(format)) {
		nStreams = 1;
		channelsPerStream = format.mChannelsPerFrame;
	} else {
		nStreams = format.mChannelsPerFrame;
		channelsPerStream = 1;
	}

	if (nStreams > mAllocatedStreams) {
		return Unexpected(kAudioUnitErr_FormatNotSupported);
	}
	auto maybeABL = mBuffers->PrepareOrError(channelsPerStream, nFrames * format.mBytesPerFrame);
	if (maybeABL) {
		mPtrState = EPtrState::ToMyMemory;
	}
	return maybeABL;
}

ExpectedPtr<AudioBufferList> AUBufferList::PrepareNullBufferOrError(
	const AudioStreamBasicDescription& format, UInt32 nFrames) AUSDK_RTSAFE
{
	UInt32 nStreams = 0;
	UInt32 channelsPerStream = 0;
	if (ASBD::IsInterleaved(format)) {
		nStreams = 1;
		channelsPerStream = format.mChannelsPerFrame;
	} else {
		nStreams = format.mChannelsPerFrame;
		channelsPerStream = 1;
	}
	if (nStreams > mAllocatedStreams) {
		return Unexpected(kAudioUnitErr_FormatNotSupported);
	}
	auto maybeABL =
		mBuffers->PrepareNullOrError(channelsPerStream, nFrames * format.mBytesPerFrame);
	if (maybeABL) {
		mPtrState = EPtrState::ToExternalMemory;
	}
	return maybeABL;
}

void AUBufferList::Allocate(const AudioStreamBasicDescription& format, UInt32 nFrames)
{
	auto& alloc = BufferAllocator::instance();
	if (mBuffers != nullptr) {
		alloc.Deallocate(mBuffers);
		mBuffers = nullptr;
	}
	const uint32_t nstreams = ASBD::IsInterleaved(format) ? 1 : format.mChannelsPerFrame;
	mBuffers = alloc.Allocate(nstreams, nFrames * format.mBytesPerFrame, 0u);
	mAllocatedFrames = nFrames;
	mAllocatedStreams = nstreams;
	mPtrState = EPtrState::Invalid;
}

void AUBufferList::Deallocate()
{
	if (mBuffers != nullptr) {
		BufferAllocator::instance().Deallocate(mBuffers);
		mBuffers = nullptr;
	}

	mAllocatedFrames = 0;
	mAllocatedStreams = 0;
	mPtrState = EPtrState::Invalid;
}

AUSDK_END_NO_RT_WARNINGS

} // namespace ausdk
