/*!
	@file		Tests.mm
	@copyright	© 2020-2023 Apple Inc. All rights reserved.
*/

#import <XCTest/XCTest.h>

#import <AudioUnitSDK/AudioUnitSDK.h>
#import <array>

@interface Tests : XCTestCase

@end

@implementation Tests

- (void)testFlatMap
{
	ausdk::flat_map<AudioUnitParameterID, float> uut;
	XCTAssertTrue(uut.empty());

	uut[5] = 5.0;
	XCTAssertEqual(uut.size(), 1u);
	XCTAssertEqual(uut[5], 5.0);

	uut[5] = 5.5;
	XCTAssertEqual(uut.size(), 1u);
	XCTAssertEqual(uut[5], 5.5);

	uut[1] = 1.0;
	XCTAssertEqual(uut.size(), 2u);
	XCTAssertEqual(uut[1], 1.0);

	uut[15] = 15.0;
	XCTAssertEqual(uut.size(), 3u);
	XCTAssertEqual(uut[15], 15.0);

	XCTAssertEqual(uut.find(0), uut.end());

	XCTAssertEqual(uut[1], 1.0);
	XCTAssertEqual(uut[5], 5.5);
	XCTAssertEqual(uut[15], 15.0);
}

- (void)testAUBufferList
{
	//	constexpr unsigned kLargeBufSize = 512;

	auto checkBuf = [](const AudioBuffer& buf, unsigned nch, unsigned nbytes, bool nullBuf) {
		XCTAssertEqual(buf.mNumberChannels, nch);
		XCTAssertEqual(buf.mDataByteSize, nbytes);
		if (nullBuf) {
			XCTAssertEqual(buf.mData, nullptr);
		} else {
			XCTAssertNotEqual(buf.mData, nullptr);
		}
	};
	auto checkABL = [&](const AudioBufferList& abl, unsigned nbufs, unsigned nch, unsigned nbytes,
						bool nullBuf) {
		XCTAssertEqual(abl.mNumberBuffers, nbufs);
		for (unsigned idx = 0; idx < nbufs; ++idx) {
			checkBuf(abl.mBuffers[idx], nch, nbytes, nullBuf);
		}
	};
	auto test = [&](const unsigned kNumBufs, const unsigned kFrameCount) {
		const auto kBufSize = kFrameCount * sizeof(float);
		ausdk::AUBufferList uut;
		const auto asbd = ausdk::ASBD::CreateCommonFloat32(44100.0, kNumBufs);

		uut.Allocate(asbd, kFrameCount);

		// Prepare 0 bytes
		checkABL(uut.PrepareBuffer(asbd, 0), kNumBufs, 1, 0, kBufSize == 0);

		if (kBufSize == 0) {
			// XCTAssertThrows(uut.PrepareBuffer(asbd, kLargeBufSize));
		} else {
			checkABL(uut.PrepareBuffer(asbd, kFrameCount), kNumBufs, 1, kFrameCount * sizeof(float),
				false);
		}

		checkABL(uut.PrepareNullBuffer(asbd, kFrameCount), kNumBufs, 1, kFrameCount * sizeof(float),
			true);
	};

	constexpr unsigned kTypicalFrameCount = 512;

	test(0, 0);
	test(1, 0);
	test(1, kTypicalFrameCount);
	test(2, kTypicalFrameCount);
	test(3, kTypicalFrameCount);
	test(4, kTypicalFrameCount);
}

- (void)testExtractBigUInt32AndAdvance
{
	const std::array<UInt32, 5> data{ CFSwapInt32HostToBig(1), CFSwapInt32HostToBig(11),
		CFSwapInt32HostToBig(1'000'000'000), CFSwapInt32HostToBig(0), CFSwapInt32HostToBig(99) };
	auto pointer = reinterpret_cast<const UInt8*>(data.data());
	XCTAssertEqual(ausdk::ExtractBigUInt32AndAdvance(pointer), 1u);
	XCTAssertEqual(ausdk::ExtractBigUInt32AndAdvance(pointer), 11u);
	XCTAssertEqual(ausdk::ExtractBigUInt32AndAdvance(pointer), 1'000'000'000u);
	XCTAssertEqual(ausdk::ExtractBigUInt32AndAdvance(pointer), 0u);
	XCTAssertEqual(ausdk::ExtractBigUInt32AndAdvance(pointer), 99u);
	XCTAssertEqual(pointer, static_cast<const void*>(data.cend()));
}

- (void)testMakeStringFrom4CC
{
	XCTAssertEqual(ausdk::MakeStringFrom4CC('abcd'), "abcd");
	XCTAssertEqual(ausdk::MakeStringFrom4CC('1234' + 0x7F), "123.");
}

@end
