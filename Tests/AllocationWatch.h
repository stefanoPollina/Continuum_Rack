#pragma once
#include <atomic>
namespace allocationWatch
{
extern thread_local bool active;
extern std::atomic<int> count;
}
