#include_next <bits/c++config.h>
#undef _GLIBCXX_USE_BUILTIN_TRAIT
#define _GLIBCXX_USE_BUILTIN_TRAIT(builtin_trait) 0
#define __builtin_operator_new(...)    ::operator new(__VA_ARGS__)
#define __builtin_operator_delete(...) ::operator delete(__VA_ARGS__)
