#include "klee/ADT/BitArray.h"
#include "gtest/gtest.h"

using klee::BitArray;

TEST(BitArrayTest, CopyAssignmentOwnsIndependentStorage) {
  BitArray source(64);
  source.set(0);
  source.set(31);
  source.set(32);
  source.set(63);
  BitArray target(32, true);
  EXPECT_EQ(&target, &(target = source));
  EXPECT_EQ(source.size(), target.size());
  for (unsigned i = 0; i < source.size(); ++i)
    EXPECT_EQ(source.get(i), target.get(i));
  target.unset(0);
  EXPECT_TRUE(source.get(0));
  EXPECT_FALSE(target.get(0));
}

TEST(BitArrayTest, SelfAssignmentPreservesBits) {
  BitArray mask(65);
  mask.set(0);
  mask.set(31);
  mask.set(32);
  mask.set(64);
  const BitArray *alias = &mask;
  mask = *alias;
  EXPECT_TRUE(mask.get(0));
  EXPECT_TRUE(mask.get(31));
  EXPECT_TRUE(mask.get(32));
  EXPECT_TRUE(mask.get(64));
  EXPECT_FALSE(mask.get(1));
}

TEST(BitArrayTest, RepeatedAndEmptyAssignmentsReleasePreviousStorage) {
  BitArray target(32, true);
  for (unsigned i = 1; i <= 64; ++i) {
    BitArray source(i);
    source.set(i - 1);
    target = source;
    EXPECT_EQ(source.size(), target.size());
    EXPECT_TRUE(target.get(i - 1));
    target = BitArray();
    EXPECT_EQ(0u, target.size());
  }
  target = BitArray(1, true);
  EXPECT_TRUE(target.get(0));
}
