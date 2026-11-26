#include "AllocationWatch.h"
#include <cstdlib>
#include <new>
namespace allocationWatch
{
thread_local bool active = false;
std::atomic<int> count { 0 };
}
void* operator new (std::size_t n)
{
    if (allocationWatch::active) ++allocationWatch::count;
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t n) { return ::operator new (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
