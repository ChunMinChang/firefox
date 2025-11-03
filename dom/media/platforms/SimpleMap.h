/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_SimpleMap_h
#define mozilla_SimpleMap_h

#include <utility>

#include "mozilla/Maybe.h"
#include "mozilla/Mutex.h"
#include "mozilla/Result.h"
#include "mozilla/ResultVariant.h"
#include "nsTArray.h"

namespace mozilla {

// A simple, array-based map for key-value pairs.
//
// When `Counted` is true, `Get()` increments a use count, and `Release()`
// decrements it, only removing the element when the count reaches zero. `Put()`
// will fail if the key already exists.
//
// When `Counted` is false, `Put()` always inserts, even if the key exists, and
// `Release()` removes the first element matching the key.
//
// Be careful when using the pointers returned by `Get`, `GetOrInsertWith`, ...,
// etc and within the `Put`'s Result, as they may become dangling if the element
// is removed via `Release()`.
//
// TODO: Non-`Counted` maps permit duplicate keys, while `Counted` maps do not.
// This is legacy behavior. We should check call sites and consider disallowing
// duplicates for better consistency. Furthermore, This would also allow
// `Release()` to be optimized by using `UnorderedRemoveElementAt()`
// instead of `RemoveElementAt()`, as it would no longer need to remove the
// first of potentially multiple elements with the same key.
template <typename K, typename V, bool Counted = false>
class SimpleMapImpl {
  struct ElementType {
    K key;
    V value;
    size_t count;

    ElementType(const K& aKey, V&& aValue)
        : key(aKey), value(std::move(aValue)), count(1) {}
  };
  using MapType = AutoTArray<ElementType, 16>;

 public:
  SimpleMapImpl() = default;

  // Return true if aKey is in the map, otherwise false.
  bool Contains(const K& aKey) { return FindIndex(aKey).isSome(); }

  // If aKey is in the map, return the matching value pointer. Otherwise, return
  // nullptr.
  V* Get(const K& aKey) {
    if (Maybe<size_t> index = FindIndex(aKey)) {
      ElementType& element = mMap[*index];
      if (Counted) {
        element.count++;
      }
      return &element.value;
    }
    return nullptr;
  }

  // Inserts a key-value pair.
  // If `Counted` is true and the key exists, returns an error with the value.
  // Otherwise, inserts and returns a pointer to the value.
  Result<NotNull<V*>, V> Put(const K& aKey, V&& aValue) {
    if (Counted && Contains(aKey)) {
      return Err(std::move(aValue));
    }
    ElementType* element = mMap.EmplaceBack(aKey, std::move(aValue));
    return WrapNotNull(&element->value);
  }

  // If aKey exists, returns a reference to the existing value.
  // Otherwise, calls aFactory to construct a new value and returns a reference
  // to it.
  template <typename Factory>
  V& GetOrInsertWith(const K& aKey, Factory&& aFactory) {
    if (V* value = Get(aKey)) {
      return *value;
    }
    ElementType* element =
        mMap.EmplaceBack(aKey, std::forward<Factory>(aFactory)());
    return element->value;
  }

  // If aKey exists, returns a reference to the existing value.
  // Otherwise, constructs a new value in-place using aArgs and returns a
  // reference to it.
  template <typename... Args>
  V& GetOrInsert(const K& aKey, Args&&... aArgs) {
    return GetOrInsertWith(aKey,
                           [&]() { return V(std::forward<Args>(aArgs)...); });
  }

  // Removes and returns the value associated with the given key if it exists.
  // If the map is counted, decrements the count and only removes when it
  // reaches zero. Returns Nothing() if the key is not found or not removed.
  Maybe<V> Release(const K& aKey) {
    if (Maybe<size_t> index = FindIndex(aKey)) {
      ElementType& element = mMap[*index];
      if (!Counted) {
        Maybe<V> value = Some(std::move(element.value));
        mMap.RemoveElementAt(*index);
        return value;
      }
      if (--element.count == 0) {
        Maybe<V> value = Some(std::move(element.value));
        mMap.UnorderedRemoveElementAt(*index);
        return value;
      }
    }
    return Nothing();
  }

  nsTArray<V> Clear() {
    nsTArray<V> values;
    for (auto& element : mMap) {
      values.AppendElement(std::move(element.value));
    }
    mMap.Clear();
    return values;
  }

  size_t Count() const { return mMap.Length(); }

 protected:
  // Return the index of the first element matching aKey, or Nothing() if not
  // found.
  Maybe<size_t> FindIndex(const K& aKey) const {
    for (size_t i = 0; i < mMap.Length(); ++i) {
      if (mMap[i].key == aKey) {
        return Some(i);
      }
    }
    return Nothing();
  }

  MapType mMap;
};

template <typename K, typename V>
using SimpleMapBase = SimpleMapImpl<K, V, false>;
template <typename K, typename V>
using SimpleRefCountedMapBase = SimpleMapImpl<K, V, true>;

struct ThreadSafePolicy {
  struct PolicyLock {
    explicit PolicyLock(const char* aName) : mMutex(aName) {}
    Mutex mMutex MOZ_UNANNOTATED;
  };
  PolicyLock& mPolicyLock;
  explicit ThreadSafePolicy(PolicyLock& aPolicyLock)
      : mPolicyLock(aPolicyLock) {
    mPolicyLock.mMutex.Lock();
  }
  ~ThreadSafePolicy() { mPolicyLock.mMutex.Unlock(); }
};

struct NoOpPolicy {
  struct PolicyLock {
    explicit PolicyLock(const char*) {}
  };
  explicit NoOpPolicy(PolicyLock&) {}
  ~NoOpPolicy() = default;
};

// A map employing an array instead of a hash table to optimize performance,
// particularly beneficial when the number of expected items in the map is
// small.
template <typename K, typename V, typename Policy = NoOpPolicy>
class SimpleMap final : protected SimpleMapBase<K, V> {
 public:
  SimpleMap() : SimpleMapBase<K, V>(), mLock("SimpleMap") {}

  // Check if aKey is in the map.
  bool Contains(const K& aKey) {
    Policy guard(mLock);
    return SimpleMapBase<K, V>::Contains(aKey);
  }

  // If aKey is in the map, return the matching value pointer. Otherwise, return
  // nullptr.
  V* Get(const K& aKey) {
    Policy guard(mLock);
    return SimpleMapBase<K, V>::Get(aKey);
  }

  // Insert Key and Value pair at the end of our map.
  void Insert(const K& aKey, V&& aValue) {
    Policy guard(mLock);
    auto result = SimpleMapBase<K, V>::Put(aKey, std::move(aValue));
    MOZ_RELEASE_ASSERT(result.isOk());
  }

  void Insert(const K& aKey, const V& aValue) {
    return Insert(aKey, V(aValue));
  }

  // Take the value matching aKey and remove it from the map if found.
  Maybe<V> Take(const K& aKey) {
    Policy guard(mLock);
    return SimpleMapBase<K, V>::Release(aKey);
  }

  // Sets aValue matching aKey and remove it from the map if found.
  // The element returned is the first one found.
  // Returns true if found, false otherwise.
  bool Find(const K& aKey, V& aValue) {
    if (Maybe<V> v = Take(aKey)) {
      aValue = std::move(*v);
      return true;
    }
    return false;
  }

  // Remove all elements of the map.
  void Clear() {
    Policy guard(mLock);
    SimpleMapBase<K, V>::Clear();
  }

  // Iterate through all elements of the map and call the function F.
  template <typename F>
  void ForEach(F&& aCallback) {
    Policy guard(mLock);
    for (const auto& element : this->mMap) {
      aCallback(element.key, element.value);
    }
  }

 private:
  typename Policy::PolicyLock mLock;
};

// No need to make this thread safe for now.
template <typename K, typename V>
class SimpleRefCountedMap final : protected SimpleRefCountedMapBase<K, V> {
 public:
  SimpleRefCountedMap() = default;

  template <typename Factory>
  const V& GetOrInsertWith(const K& aKey, Factory&& aFactory) {
    return SimpleRefCountedMapBase<K, V>::GetOrInsertWith(
        aKey, std::forward<Factory>(aFactory));
  }

  Maybe<V> Release(const K& aKey) {
    return SimpleRefCountedMapBase<K, V>::Release(aKey);
  }

  nsTArray<V> Clear() { return SimpleRefCountedMapBase<K, V>::Clear(); }

  size_t Count() const { return SimpleRefCountedMapBase<K, V>::Count(); }
};

}  // namespace mozilla

#endif  // mozilla_SimpleMap_h
