// Copyright bilibili
// Author guoyi02@guoyi02.com

#ifndef BASE_BUILTIN_EXPECT_H_
#define BASE_BUILTIN_EXPECT_H_

#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

#endif // BASE_BUILTIN_EXPECT_H_

