/*!
	@file		Tests.mm
	@copyright	© 2020-2024 Apple Inc. All rights reserved.
*/

#import <XCTest/XCTest.h>

#import <AudioUnitSDK/AudioUnitSDK.h>
#import <algorithm>
#import <array>
#import <cstddef>
#import <cstring>
#import <memory>
#import <span>
#import <vector>

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

- (void)testSerialize
{
	constexpr float value = 123456789.f;
	std::vector<std::byte> data(sizeof(value));
	ausdk::Serialize(value, data.data());
	XCTAssertEqual(std::memcmp(&value, data.data(), data.size()), 0);

	// unaligned memory
	constexpr size_t offset = 1;
	const auto valueMemory = std::make_unique<std::byte[]>(sizeof(value) + offset);
	const auto valueAddress = valueMemory.get() + offset;
	ausdk::Serialize<float>(value, valueAddress);
	XCTAssertEqual(std::memcmp(&value, valueAddress, sizeof(value)), 0);

	const std::vector<const int> values({ 1, 2, 3, 4, 5, 6, 7, 8, 9 });
	data.clear();
	data.resize(std::span(values).size_bytes());
	ausdk::Serialize(std::span(values), data.data());
	XCTAssertEqual(std::memcmp(values.data(), data.data(), data.size()), 0);
}

- (void)testDeserialize
{
	constexpr float value = 987654321.f;
	XCTAssertEqual(ausdk::Deserialize<float>(&value), value);

	// unaligned memory
	constexpr size_t offset = 1;
	const auto valueMemory = std::make_unique<std::byte[]>(sizeof(value) + offset);
	const auto valueAddress = valueMemory.get() + offset;
	std::memcpy(valueAddress, &value, sizeof(value));
	XCTAssertEqual(ausdk::Deserialize<float>(valueAddress), value);

	const std::vector<const int> values({ 9, 8, 7, 6, 5, 4, 3, 2, 1 });
	XCTAssertTrue(std::ranges::equal(
		ausdk::DeserializeArray<int>(values.data(), std::span(values).size_bytes()), values));
}

- (void)testDeserializeBigUInt32AndAdvance
{
	const std::array<const UInt32, 5> data{ CFSwapInt32HostToBig(1), CFSwapInt32HostToBig(11),
		CFSwapInt32HostToBig(1'000'000'000), CFSwapInt32HostToBig(0), CFSwapInt32HostToBig(99) };
	auto pointer = reinterpret_cast<const UInt8*>(data.data());
	XCTAssertEqual(ausdk::DeserializeBigUInt32AndAdvance(pointer), 1u);
	XCTAssertEqual(ausdk::DeserializeBigUInt32AndAdvance(pointer), 11u);
	XCTAssertEqual(ausdk::DeserializeBigUInt32AndAdvance(pointer), 1'000'000'000u);
	XCTAssertEqual(ausdk::DeserializeBigUInt32AndAdvance(pointer), 0u);
	XCTAssertEqual(ausdk::DeserializeBigUInt32AndAdvance(pointer), 99u);
	XCTAssertEqual(pointer, static_cast<const void*>(data.cend()));
}

- (void)testMakeStringFrom4CC
{
	XCTAssertEqual(ausdk::MakeStringFrom4CC('abcd'), "abcd");
	XCTAssertEqual(ausdk::MakeStringFrom4CC('1234' + 0x7F), "123.");
}

@end
