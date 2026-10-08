#include "state_model.hpp"
#ifndef __cpp_pack_indexing
#error This prototype requires verified C++26 pack indexing
#endif
template<unsigned I,int... V> constexpr int pick(){return V...[I];}
int probe::language_probe(){return pick<1,0,26>();}
