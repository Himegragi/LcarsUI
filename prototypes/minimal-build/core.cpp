#include "core.hpp"
#ifndef __cpp_pack_indexing
#error C++26 pack indexing is required by this probe
#endif
static_assert(__cpp_pack_indexing >= 202311L);
template <unsigned I, int... Values>
constexpr int pick() { return Values...[I]; }
static_assert(pick<1, 7, 26, 99>() == 26);
int verified_cxx26_value() { return pick<1, 7, 26, 99>(); }
