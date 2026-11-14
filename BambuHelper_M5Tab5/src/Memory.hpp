// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <vector>
#if defined(ESP_PLATFORM)
#include <esp_heap_caps.h>
#include <new>
#include <limits>
template<class T> class BambuAllocator {
public:
  using value_type=T;
  BambuAllocator() noexcept = default;
  template<class U> BambuAllocator(const BambuAllocator<U>&) noexcept {}
  T* allocate(size_t n) {
    if(n>1024*1024/sizeof(T)) throw std::bad_alloc();
    auto* p=static_cast<T*>(heap_caps_malloc(n*sizeof(T),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    if(!p&&n)throw std::bad_alloc();
    return p;
  }
  void deallocate(T* p,size_t) noexcept {heap_caps_free(p);}
};
template<class T,class U> bool operator==(const BambuAllocator<T>&,const BambuAllocator<U>&){return true;}
template<class T,class U> bool operator!=(const BambuAllocator<T>&,const BambuAllocator<U>&){return false;}
template<class T> using BambuVector=std::vector<T,BambuAllocator<T>>;
#else
template<class T> using BambuVector=std::vector<T>;
#endif
