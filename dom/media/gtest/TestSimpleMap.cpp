/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SimpleMap.h"
#include "gtest/gtest.h"
#include "nsString.h"

namespace mozilla {

TEST(SimpleMapTest, InsertAndGet)
{
  SimpleMap<int, nsCString> map;

  ASSERT_FALSE(map.Contains(1));

  map.Insert(1, "one"_ns);
  ASSERT_TRUE(map.Contains(1));
  nsCString* value = map.Get(1);
  ASSERT_TRUE(value->EqualsLiteral("one"));

  ASSERT_FALSE(map.Contains(2));
  ASSERT_EQ(map.Get(2), nullptr);
}

TEST(SimpleMapTest, Take)
{
  SimpleMap<int, nsCString> map;

  map.Insert(1, "one"_ns);
  map.Insert(2, "two"_ns);

  Maybe<nsCString> taken = map.Take(1);
  ASSERT_TRUE(taken.isSome());
  ASSERT_TRUE(taken.ref().EqualsLiteral("one"));
  ASSERT_FALSE(map.Contains(1));
  ASSERT_TRUE(map.Contains(2));

  Maybe<nsCString> notTaken = map.Take(3);
  ASSERT_TRUE(notTaken.isNothing());
}

TEST(SimpleMapTest, Find)
{
  SimpleMap<int, nsCString> map;

  map.Insert(1, "one"_ns);
  nsCString value;
  ASSERT_TRUE(map.Find(1, value));
  ASSERT_TRUE(value.EqualsLiteral("one"));
  ASSERT_FALSE(map.Contains(1));  // Find also removes the element.

  ASSERT_FALSE(map.Find(2, value));
}

TEST(SimpleMapTest, Clear)
{
  SimpleMap<int, nsCString> map;

  map.Insert(1, "one"_ns);
  map.Insert(2, "two"_ns);
  ASSERT_TRUE(map.Contains(1));
  ASSERT_TRUE(map.Contains(2));
  map.Clear();
  ASSERT_FALSE(map.Contains(1));
  ASSERT_FALSE(map.Contains(2));
}

TEST(SimpleMapTest, ForEach)
{
  SimpleMap<int, nsCString> map;

  map.Insert(1, "one"_ns);
  map.Insert(2, "two"_ns);

  nsTArray<int> keys;
  nsCString valueAccumulator;
  map.ForEach([&](int key, const nsCString& value) {
    keys.AppendElement(key);
    valueAccumulator += value;
  });

  // order is guaranteed
  ASSERT_EQ(keys.Length(), size_t(2));
  ASSERT_EQ(keys[0], 1);
  ASSERT_EQ(keys[1], 2);
  ASSERT_TRUE(valueAccumulator.EqualsLiteral("onetwo"));
}

// TODO: Check if we really need to insert duplicate keys.
TEST(SimpleMapTest, DuplicateInsert)
{
  SimpleMap<int, nsCString> map;

  auto orderChecker = [&](const int aKey,
                          const nsTArray<nsCString>& aExpected) {
    size_t index = 0;
    map.ForEach([&](int key, const nsCString& value) {
      if (key == aKey) {
        ASSERT_LT(index, aExpected.Length());
        ASSERT_TRUE(value.Equals(aExpected[index]));
        index++;
      }
    });
  };

  map.Insert(1, "first"_ns);
  map.Insert(1, "second"_ns);
  map.Insert(1, "third"_ns);

  // Check the order of inserted values
  orderChecker(1, {"first"_ns, "second"_ns, "third"_ns});

  // Check that Get returns the first inserted value
  Maybe<nsCString> taken = map.Take(1);
  ASSERT_TRUE(taken.isSome());
  ASSERT_TRUE(taken.ref().EqualsLiteral("first"));

  // Check the order after taking the first one
  orderChecker(1, {"second"_ns, "third"_ns});
}

TEST(SimpleRefCountedMapBaseTest, GetAndPut)
{
  SimpleRefCountedMapBase<int, nsCString> map;

  ASSERT_FALSE(map.Contains(1));
  auto putResult = map.Put(1, nsCString("one"_ns));
  ASSERT_TRUE(putResult.isOk());

  nsCString* insertedValue = putResult.unwrap();
  ASSERT_TRUE(insertedValue->EqualsLiteral("one"));

  ASSERT_TRUE(map.Contains(1));

  nsCString* value = map.Get(1);
  ASSERT_TRUE(value);
  ASSERT_EQ(insertedValue, value);
  ASSERT_TRUE(value->EqualsLiteral("one"));

  ASSERT_FALSE(map.Contains(2));
  ASSERT_FALSE(map.Get(2));
}

TEST(SimpleRefCountedMapBaseTest, RefCounting)
{
  SimpleRefCountedMapBase<int, nsCString> map;

  auto putResult = map.Put(1, nsCString("one"_ns));  // Count = 1
  ASSERT_TRUE(putResult.isOk());
  nsCString* insertedValue = putResult.unwrap();
  ASSERT_TRUE(insertedValue->EqualsLiteral("one"));
  ASSERT_TRUE(map.Contains(1));

  map.Get(1);  // Count = 2
  map.Get(1);  // Count = 3

  ASSERT_TRUE(map.Contains(1));

  Maybe<nsCString> r1 = map.Release(1);  // Count = 2
  ASSERT_TRUE(r1.isNothing());
  ASSERT_TRUE(map.Contains(1));

  Maybe<nsCString> r2 = map.Release(1);  // Count = 1
  ASSERT_TRUE(r2.isNothing());
  ASSERT_TRUE(map.Contains(1));

  Maybe<nsCString> r3 = map.Release(1);  // Count = 0, removed
  ASSERT_TRUE(r3.isSome());
  ASSERT_TRUE(r3.ref().EqualsLiteral("one"));
  ASSERT_FALSE(map.Contains(1));

  // insertedValue is now dangling; do not use it.

  Maybe<nsCString> nonExistent = map.Release(99);
  ASSERT_TRUE(nonExistent.isNothing());
}

TEST(SimpleRefCountedMapBaseTest, GetOrInsert)
{
  SimpleRefCountedMapBase<int, nsCString> map;

  // Insert new element
  nsCString& value1 = map.GetOrInsert(1, "one"_ns);
  ASSERT_TRUE(value1.EqualsLiteral("one"));
  ASSERT_TRUE(map.Contains(1));

  // Get existing element
  nsCString& value2 = map.GetOrInsert(1, "two"_ns);
  ASSERT_EQ(value1, value2);
  ASSERT_TRUE(
      value2.EqualsLiteral("one"));  // Original value should be preserved
}

TEST(SimpleRefCountedMapBaseTest, GetOrInsertWith)
{
  SimpleRefCountedMapBase<int, nsCString> map;

  // Insert new element
  nsCString& value1 =
      map.GetOrInsertWith(1, []() { return nsCString("one"_ns); });
  ASSERT_TRUE(value1.EqualsLiteral("one"));
  ASSERT_TRUE(map.Contains(1));

  // Get existing element
  nsCString& value2 =
      map.GetOrInsertWith(1, []() { return nsCString("two"_ns); });
  ASSERT_EQ(value1, value2);
  ASSERT_TRUE(
      value2.EqualsLiteral("one"));  // Original value should be preserved
}

}  // namespace mozilla
