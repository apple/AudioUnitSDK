/*!
	@file		AUThreadSafeListTests.mm
	@copyright	© 2000-2024 Apple Inc. All rights reserved.
*/
#import <XCTest/XCTest.h>

#include <AudioUnitSDK/AUThreadSafeList.h>
#include <AudioUnitSDK/AudioUnitSDK.h>
#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

class FauxRenderCallback {
public:
	FauxRenderCallback() noexcept = default;
	bool operator==(const FauxRenderCallback& other) const noexcept = default;

	uint32_t mValue{ 0 };
};

@interface AUThreadSafeListTests : XCTestCase

@end

@implementation AUThreadSafeListTests

- (BOOL)continueAfterFailure
{
	return NO;
}

- (void)testAdd
{
	ausdk::AUThreadSafeList<FauxRenderCallback> list;

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list.Add(cb);
	XCTAssertEqual(list.begin(), list.end());

	list.Update();

	XCTAssertEqual(*list.begin(), cb);
}

- (void)testAddNoUpdate
{
	ausdk::AUThreadSafeList<FauxRenderCallback> list;

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list.Add(cb);

	// We should call list.Update() here

	XCTAssertEqual(list.begin(), list.end());
}

- (void)testRemove
{
	ausdk::AUThreadSafeList<FauxRenderCallback> list;

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list.Add(cb);
	XCTAssertEqual(list.begin(), list.end());

	list.Update();
	XCTAssertEqual(*list.begin(), cb);

	list.Remove(cb);
	XCTAssertEqual(*list.begin(), cb);

	list.Update();
	XCTAssertEqual(list.begin(), list.end());
}

- (void)testRemoveNoUpdate
{
	ausdk::AUThreadSafeList<FauxRenderCallback> list;

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list.Add(cb);

	list.Update();

	list.Remove(cb);

	// We should call list.Update() here

	XCTAssertEqual(*list.begin(), cb);
}

- (void)testRemoveOnEmptyList
{
	ausdk::AUThreadSafeList<FauxRenderCallback> list;

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list.Remove(cb);

	list.Update();
	XCTAssertEqual(list.begin(), list.end());
}

- (void)testSingleClear
{
	ausdk::AUThreadSafeList<FauxRenderCallback> list;

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list.Add(cb);

	list.Update();

	list.Clear();

	list.Update();

	XCTAssertEqual(list.begin(), list.end());
}

- (void)testBasicConsistency
{
	static int objCounter = 0;

	constexpr auto kTestElements = 10000;
	class CountedObject {
	public:
		CountedObject() noexcept { objCounter++; }
		CountedObject(const CountedObject&) = delete;
		CountedObject(CountedObject&&) = delete;
		CountedObject& operator=(const CountedObject&) = default;
		CountedObject& operator=(CountedObject&&) = default;
		~CountedObject() noexcept { objCounter--; }

		bool operator==(const CountedObject& other) const noexcept = default;

		uint32_t mValue{ 0 };
	};

	auto getListCount = [](const ausdk::AUThreadSafeList<CountedObject>& list) {
		return std::ranges::distance(list);
	};

	auto list = std::make_unique<ausdk::AUThreadSafeList<CountedObject>>();

	std::vector<uint32_t> mirrorState;
	// Add
	for (uint32_t i = 0; i < kTestElements; ++i) {
		CountedObject cb;
		cb.mValue = i;
		list->Add(cb);
		mirrorState.push_back(i);
	}

	list->Update();
	XCTAssertEqual(getListCount(*list), kTestElements);
	uint32_t counter = 0;
	for (auto& callback : *list) {
		XCTAssertEqual(counter, callback.mValue);
		counter++;
	}

	// Remove
	for (uint32_t i = 0; i < kTestElements; i += 1000) {
		CountedObject cb;
		cb.mValue = i;
		list->Remove(cb);
		std::erase(mirrorState, i);
	}

	list->Update();
	XCTAssertEqual(getListCount(*list), 9990);

	std::vector<uint32_t> removedState;
	for (auto& node : *list) {
		removedState.push_back(node.mValue);
	}

	XCTAssertTrue(std::ranges::equal(removedState, mirrorState));

	// Clear
	list->Clear();
	list->Update();

	XCTAssertEqual(getListCount(*list), 0);

	// Re-Add
	for (uint32_t i = 0; i < kTestElements; ++i) {
		CountedObject cb;
		cb.mValue = i;
		list->Add(cb);
	}

	list->Update();
	XCTAssertEqual(getListCount(*list), kTestElements);

	list.reset();
	XCTAssertEqual(objCounter, 0);
}

- (void)testAsyncConsistency
{
	using sys_clock = std::chrono::system_clock;

	static constexpr auto kTimeout = 5;
	constexpr auto kTestElements = 1000;

	ausdk::AUThreadSafeList<FauxRenderCallback> list;

	std::thread t([&list]() {
		for (uint32_t i = 0; i < kTestElements; i++) {
			FauxRenderCallback cb;
			cb.mValue = i;
			list.Add(cb);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	});
	t.detach();

	std::thread t2([&list]() {
		auto start = sys_clock::now();
		while (std::ranges::distance(list) < kTestElements) {
			list.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (sys_clock::now() - start > std::chrono::seconds(kTimeout)) {
				break;
			}
		}
	});
	t2.join();

	XCTAssertEqual(std::ranges::distance(list), kTestElements);

	std::thread t3([&list]() {
		for (uint32_t i = 0; i < kTestElements; i++) {
			FauxRenderCallback cb;
			cb.mValue = i;
			list.Remove(cb);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	});
	t3.detach();

	std::thread t4([&list]() {
		auto start = std::chrono::system_clock::now();
		while (std::ranges::distance(list) > 0) {
			list.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (sys_clock::now() - start > std::chrono::seconds(kTimeout)) {
				break;
			}
		}
	});
	t4.join();

	XCTAssertEqual(std::ranges::distance(list), 0);
}

@end
