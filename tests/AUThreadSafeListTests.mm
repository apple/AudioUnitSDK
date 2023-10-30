/*!
	@file		AUThreadSafeListTests.mm
	@copyright	© 2020-2023 Apple Inc. All rights reserved.
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
	FauxRenderCallback() = default;
	bool operator==(const FauxRenderCallback& other) const { return this->mValue == other.mValue; }

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
	auto list = std::make_unique<ausdk::AUThreadSafeList<FauxRenderCallback>>();

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list->Add(cb);
	XCTAssertEqual(list->begin(), list->end());

	list->Update();

	XCTAssertEqual(*list->begin(), cb);
}

- (void)testAddNoUpdate
{
	auto list = std::make_unique<ausdk::AUThreadSafeList<FauxRenderCallback>>();

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list->Add(cb);

	// We should call list->Update() here

	XCTAssertEqual(list->begin(), list->end());
}

- (void)testRemove
{
	auto list = std::make_unique<ausdk::AUThreadSafeList<FauxRenderCallback>>();

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list->Add(cb);
	XCTAssertEqual(list->begin(), list->end());

	list->Update();
	XCTAssertEqual(*list->begin(), cb);

	list->Remove(cb);
	XCTAssertEqual(*list->begin(), cb);

	list->Update();
	XCTAssertEqual(list->begin(), list->end());
}

- (void)testRemoveNoUpdate
{
	auto list = std::make_unique<ausdk::AUThreadSafeList<FauxRenderCallback>>();

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list->Add(cb);

	list->Update();

	list->Remove(cb);

	// We should call list->Update() here

	XCTAssertEqual(*list->begin(), cb);
}

- (void)testRemoveOnEmptyList
{
	auto list = std::make_unique<ausdk::AUThreadSafeList<FauxRenderCallback>>();

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list->Remove(cb);

	list->Update();
	XCTAssertEqual(list->begin(), list->end());
}

- (void)testSingleClear
{
	auto list = std::make_unique<ausdk::AUThreadSafeList<FauxRenderCallback>>();

	// Add
	FauxRenderCallback cb;
	cb.mValue = 56;
	list->Add(cb);

	list->Update();

	list->Clear();

	list->Update();

	XCTAssertEqual(list->begin(), list->end());
}

- (void)testBasicConsistency
{
	static int objCounter = 0;

	constexpr auto kTestElements = 10000;
	class CountedObject {
	public:
		CountedObject() { objCounter++; }
		CountedObject(CountedObject& t) = delete;
		~CountedObject() { objCounter--; }

		bool operator==(const CountedObject& other) const { return this->mValue == other.mValue; }

		uint32_t mValue{ 0 };
	};

	auto getListCount = [](ausdk::AUThreadSafeList<CountedObject>* list) {
		return std::distance(list->begin(), list->end());
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
	XCTAssertEqual(getListCount(list.get()), kTestElements);
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
	XCTAssertEqual(getListCount(list.get()), 9990);

	std::vector<uint32_t> removedState;
	for (auto& node : *list) {
		removedState.push_back(node.mValue);
	}

	XCTAssertTrue(std::equal(removedState.begin(), removedState.end(), mirrorState.begin()));

	// Clear
	list->Clear();
	list->Update();

	XCTAssertEqual(getListCount(list.get()), 0);

	// Re-Add
	for (uint32_t i = 0; i < kTestElements; ++i) {
		CountedObject cb;
		cb.mValue = i;
		list->Add(cb);
	}

	list->Update();
	XCTAssertEqual(getListCount(list.get()), kTestElements);

	list = nullptr;
	XCTAssertEqual(objCounter, 0);
}

- (void)testAsyncConsistency
{
	using sys_clock = std::chrono::system_clock;

	static constexpr auto kTimeout = 5;
	constexpr auto kTestElements = 1000;

	auto list = std::make_unique<ausdk::AUThreadSafeList<FauxRenderCallback>>();

	std::thread t([&list]() {
		for (uint32_t i = 0; i < kTestElements; i++) {
			FauxRenderCallback cb;
			cb.mValue = i;
			list->Add(cb);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	});
	t.detach();

	std::thread t2([&list]() {
		auto start = sys_clock::now();
		while (std::distance(list->begin(), list->end()) < kTestElements) {
			list->Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (sys_clock::now() - start > std::chrono::seconds(kTimeout)) {
				break;
			}
		}
	});
	t2.join();

	XCTAssertEqual(std::distance(list->begin(), list->end()), kTestElements);

	std::thread t3([&list]() {
		for (uint32_t i = 0; i < kTestElements; i++) {
			FauxRenderCallback cb;
			cb.mValue = i;
			list->Remove(cb);
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	});
	t3.detach();

	std::thread t4([&list]() {
		auto start = std::chrono::system_clock::now();
		while (std::distance(list->begin(), list->end()) > 0) {
			list->Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			if (sys_clock::now() - start > std::chrono::seconds(kTimeout)) {
				break;
			}
		}
	});
	t4.join();

	XCTAssertEqual(std::distance(list->begin(), list->end()), 0);
}

@end
