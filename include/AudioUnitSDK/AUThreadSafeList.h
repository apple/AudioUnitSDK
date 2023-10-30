/*!
	@file		AudioUnitSDK/AUThreadSafeList.h
	@copyright	© 2000-2023 Apple Inc. All rights reserved.
*/
#ifndef AudioUnitSDK_AUThreadSafeList_h
#define AudioUnitSDK_AUThreadSafeList_h

// module
// clang-format off
#include <AudioUnitSDK/AUConfig.h> // must come first
// clang-format on
#include <AudioUnitSDK/AUUtility.h>

// std
#include <atomic>
#include <concepts>
#include <type_traits>

namespace ausdk {
/*!
	@class	AUAtomicStack
	@brief	Linked list LIFO or FIFO (popAllReversed) stack, elements are pushed and popped
			atomically.
 */
#ifdef __cpp_lib_concepts
template <std::default_initializable T>
#else
template <typename T, std::enable_if_t<std::is_default_constructible_v<T>, bool> = true>
#endif
class AUAtomicStack {
public:
	AUAtomicStack() = default;

	// Non-atomic routines, for use when initializing/deinitializing, operate NON-atomically
	void PushNonAtomic(T* item) noexcept
	{
		item->Next() = mHead;
		mHead = item;
	}

	T* PopNonAtomic() noexcept
	{
		T* result = mHead;
		if (result)
			mHead = result->Next();
		return result;
	}

	// Atomic routines
	void PushAtomic(T* item) noexcept
	{
		T* head_ = {};
		do {
			head_ = mHead;
			item->Next() = head_;
		} while (!CompareAndSwap(head_, item));
	}

	// Pushes entire linked list headed by item
	void PushMultipleAtomic(T* item) noexcept
	{
		T *head_{}, *p = item, *tail{};
		// Find the last one -- when done, it will be linked to head
		do {
			tail = p;
			p = p->Next();
		} while (p);
		do {
			head_ = mHead;
			tail->Next() = head_;
		} while (!CompareAndSwap(head_, item));
	}

	// This may only be used when only one thread may potentially pop from the stack.
	// if multiple threads may pop, this suffers from the ABA problem.
	T* PopAtomicSingleReader() noexcept
	{
		T* result{};
		do {
			if ((result = mHead) == nullptr)
				break;
		} while (!CompareAndSwap(result, result->Next()));
		return result;
	}

	// This is inefficient for large linked lists.
	// prefer PopAll() to a series of calls to PopAtomic.
	// PushMultipleAtomic has to traverse the entire list.
	T* PopAtomic() noexcept
	{
		T* result = PopAll();
		if (result) {
			T* next = result->Next();
			if (next)
				// push all the remaining items back onto the stack
				PushMultipleAtomic(next);
		}
		return result;
	}

	T* PopAll() noexcept
	{
		T* result{};
		do {
			if ((result = mHead) == nullptr)
				break;
		} while (!CompareAndSwap(result, nullptr));
		return result;
	}

	T* PopAllReversed() noexcept
	{
		AUAtomicStack<T> reversed;
		T* p = PopAll();
		while (p != nullptr) {
			T* next = p->Next();
			reversed.PushNonAtomic(p);
			p = next;
		}
		return reversed.mHead;
	}

	bool CompareAndSwap(T* oldvalue, T* newvalue) noexcept
	{
		return std::atomic_compare_exchange_strong_explicit(
			&mHead, &oldvalue, newvalue, std::memory_order_seq_cst, std::memory_order_relaxed);
	}

	[[nodiscard]] bool empty() const noexcept { return mHead == nullptr; }

	T* head() const noexcept { return mHead; }

	void SetHead(T* newHead) { this->mHead.store(newHead); }

protected:
	std::atomic<T*> mHead{ nullptr };
	static_assert(decltype(mHead)::is_always_lock_free);
};

// -------------------------------------------------------------------------------------------------
/*!
 @class    AUThreadSafeList
 @brief    A thread-safe linked list.
 */
template <class T>
class AUThreadSafeList {
public:
	enum class EventType { Unknown, Add, Remove, Clear };

	class Node {
	public:
		Node* mNext{ nullptr };
		EventType mEventType{ EventType::Unknown };
		T mObject{};

		Node*& Next() { return mNext; }
	};
	using NodeStack = AUAtomicStack<Node>;

	class iterator {
	public:
		iterator() {}
		iterator(Node* n) : mNode(n) {}

		bool operator==(const iterator& other) const { return this->mNode == other.mNode; }
		bool operator!=(const iterator& other) const { return this->mNode != other.mNode; }

		T& operator*() const { return mNode->mObject; }

		iterator& operator++()
		{
			mNode = mNode->Next();
			return *this;
		} // preincrement

		iterator operator++(int)
		{
			iterator tmp = *this;
			mNode = mNode->next();
			return tmp;
		} // postincrement

		using iterator_category = std::forward_iterator_tag;
		using difference_type = std::ptrdiff_t;
		using value_type = T;
		using reference = T&;
		using pointer = T*;

	private:
		Node* mNode{ nullptr };
	};

	AUThreadSafeList() = default;
	~AUThreadSafeList()
	{
		FreeAll(mActiveList);
		FreeAll(mPendingList);
		FreeAll(mFreeList);
	}

	AUThreadSafeList(const AUThreadSafeList&) = delete;
	AUThreadSafeList(AUThreadSafeList&&) = delete;
	AUThreadSafeList& operator=(const AUThreadSafeList&) = delete;
	AUThreadSafeList& operator=(AUThreadSafeList&&) = delete;

	// These may be called on any thread
	void Add(const T& obj)
	{
		Node* node = AllocNode();
		node->mEventType = EventType::Add;
		node->mObject = obj;
		mPendingList.PushAtomic(node);
	}

	// can be called on any thread
	void Remove(const T& obj)
	{
		Node* node = AllocNode();
		node->mEventType = EventType::Remove;
		node->mObject = obj;
		mPendingList.PushAtomic(node);
	}

	void Clear() // can be called on any thread
	{
		Node* node = AllocNode();
		node->mEventType = EventType::Clear;
		mPendingList.PushAtomic(node);
	}

	// These must be called from only one thread
	void Update() noexcept
	{
		NodeStack reversed;
		Node* event{};
		bool workDone = false;

		// reverse the events so they are in order
		event = mPendingList.PopAll();
		while (event != nullptr) {
			Node* next = event->mNext;
			reversed.PushNonAtomic(event);
			event = next;
			workDone = true;
		}

		if (workDone) {
			// now process them
			while ((event = reversed.PopNonAtomic()) != nullptr) {
				switch (event->mEventType) {
				case EventType::Add: {
					Node* endNode{};
					bool needToInsert = true;
					for (Node* node = mActiveList.head(); node != nullptr; node = node->mNext) {
						if (node->mObject == event->mObject) {
							FreeNode(event);
							needToInsert = false;
							break;
						}
						endNode = node;
					}
					if (needToInsert) {
						// link the new event in at the end of the active list
						if (!endNode) {
							mActiveList.PushNonAtomic(event);
						} else {
							endNode->Next() = event;
							event->mNext = nullptr;
						}
					}
				} break;
				case EventType::Remove: {
					// find matching node in the active list, remove it
					Node* previousNode{};
					for (Node* node = mActiveList.head(); node != nullptr; node = node->mNext) {
						if (node->mObject == event->mObject) {
							if (!previousNode) {
								mActiveList.SetHead(node->mNext);
							} else {
								previousNode->Next() = node->mNext; // remove from linked list
							}
							FreeNode(node);
							break;
						}
						previousNode = node;
					}

					// dispose the request node
					FreeNode(event);
				} break;
				case EventType::Clear: {
					Node* next{};
					for (Node* node = mActiveList.head(); node != nullptr;) {
						next = node->mNext;
						FreeNode(node);
						node = next;
					}
					FreeNode(event);

					if (mActiveList.head()) {
						mActiveList.SetHead(nullptr);
					}
				} break;
				default:
					AUSDK_LogError("Unknown AUThreadSafeList event type");
					break;
				}
			}
		}
	}

	iterator begin() const noexcept { return iterator(mActiveList.head()); }
	iterator end() const noexcept { return iterator(nullptr); }

private:
	Node* AllocNode()
	{
		Node* node = mFreeList.PopAtomic();
		if (node == nullptr)
			node = new Node();
		return node;
	}

	void FreeNode(Node* node) { mFreeList.PushAtomic(node); }
	static void FreeAll(AUAtomicStack<Node>& stack)
	{
		Node* node{};
		while ((node = stack.PopNonAtomic()) != nullptr) {
			delete node;
		}
	}

	NodeStack mActiveList;  // what's actually in the container - only accessed on one thread
	NodeStack mPendingList; // add or remove requests - threadsafe
	NodeStack mFreeList;    // free nodes for reuse - threadsafe
};

} // namespace ausdk

#endif // AudioUnitSDK_AUThreadSafeList_h
