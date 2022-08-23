//
//  Tests.mm
//  Tests
//

#import <XCTest/XCTest.h>

#import <AudioUnitSDK/AudioUnitSDK.h>

@interface Tests : XCTestCase

@end

@implementation Tests

- (void)testFlatMap
{
	ausdk::flat_map<AudioUnitParameterID, float> uut;
	XCTAssert(uut.empty());

	uut[5] = 5.0;
	XCTAssert(uut.size() == 1);
	XCTAssert(uut[5] == 5.0);

	uut[5] = 5.5;
	XCTAssert(uut.size() == 1);
	XCTAssert(uut[5] == 5.5);

	uut[1] = 1.0;
	XCTAssert(uut.size() == 2);
	XCTAssert(uut[1] == 1.0);

	uut[15] = 15.0;
	XCTAssert(uut.size() == 3);
	XCTAssert(uut[15] == 15.0);

	XCTAssert(uut.find(0) == uut.end());

	XCTAssert(uut[1] == 1.0);
	XCTAssert(uut[5] == 5.5);
	XCTAssert(uut[15] == 15.0);
}

- (void)testAUBufferList
{
	//	constexpr unsigned kLargeBufSize = 512;

	auto checkBuf = [](const AudioBuffer& buf, unsigned nch, unsigned nbytes, bool nullBuf) {
		XCTAssert(buf.mNumberChannels == nch);
		XCTAssert(buf.mDataByteSize == nbytes);
		if (nullBuf) {
			XCTAssert(buf.mData == nullptr);
		} else {
			XCTAssert(buf.mData != nullptr);
		}
	};
	auto checkABL = [&](const AudioBufferList& abl, unsigned nbufs, unsigned nch, unsigned nbytes,
						bool nullBuf) {
		XCTAssert(abl.mNumberBuffers == nbufs);
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

@end
