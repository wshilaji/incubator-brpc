// Copyright bilibili
// Author guoyi02@bilibili.com

#ifndef BASE_DELETE_COPY_AND_ASSIGN_MACRO_H_
#define BASE_DELETE_COPY_AND_ASSIGN_MACRO_H_

// C++ style disallow copy and assign
#define DELETE_COPY_AND_ASSIGN(TypeName) \
    TypeName(const TypeName&) = delete; \
    void operator=(const TypeName&) = delete

#endif // BASE_DELETE_COPY_AND_ASSIGN_MACRO_H_

