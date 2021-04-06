/*
 * Copyright 2011-2021 Blender Foundation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "testing/testing.h"

#include "util/util_math.h"

CCL_NAMESPACE_BEGIN

TEST(math, next_power_of_two)
{
  EXPECT_EQ(next_power_of_two(0), 1);
  EXPECT_EQ(next_power_of_two(1), 2);
  EXPECT_EQ(next_power_of_two(2), 4);
  EXPECT_EQ(next_power_of_two(3), 4);
  EXPECT_EQ(next_power_of_two(4), 8);
}

CCL_NAMESPACE_END
