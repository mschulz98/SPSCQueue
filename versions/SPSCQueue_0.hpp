#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <thread>

#ifndef SPIN_LIMIT
#define SPIN_LIMIT 16
#endif
#ifndef SPIN_WAIT_US
#define SPIN_WAIT_US 50
#endif

template<typename T, std::size_t N, typename Alloc=std::allocator<T>>
class SPSCQueue
{
  static_assert(N && !(N & (N - 1)), "Size N must be a power of 2.");

private:
  struct ControlBlock
  {
    std::atomic<std::size_t> index { 0 };
  };

  ControlBlock head;
  ControlBlock tail;
  T *data;

  [[no_unique_address]] Alloc allocator;

  static inline std::size_t map_index(std::size_t index);
  static inline void sleep(std::size_t &spin_counter);

public:
  using ValueType = T;

  SPSCQueue();
  ~SPSCQueue();

  SPSCQueue(const SPSCQueue &) = delete;
  SPSCQueue &operator=(const SPSCQueue &) = delete;

  SPSCQueue(SPSCQueue &&) = delete;
  SPSCQueue &operator=(SPSCQueue &&) = delete;

  void push(const T &val);
  T pop();

  void print_metadata() const;
};

template<typename T, std::size_t N, typename Alloc>
SPSCQueue<T, N, Alloc>::SPSCQueue()
{
  data = allocator.allocate(N);
}

template<typename T, std::size_t N, typename Alloc>
SPSCQueue<T, N, Alloc>::~SPSCQueue()
{
  while (head.index - tail.index)
  {
    pop();
  }

  allocator.deallocate(data, N);
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::push(const T &val)
{
  std::size_t spin_counter = 0;
  while (head.index - tail.index == N)
  {
    sleep(spin_counter);
  }

  data[map_index(head.index)] = val;
  ++head.index;
}

template<typename T, std::size_t N, typename Alloc>
T SPSCQueue<T, N, Alloc>::pop()
{
  std::size_t spin_counter = 0;
  while (head.index == tail.index)
  {
    sleep(spin_counter);
  }

  // with compiler optimizations enabled, reference/dtor
  // should be optimized out for trivially destructible types
  T &ref = data[map_index(tail.index)];
  T value = ref;
  ref.~T();

  ++tail.index;
  return value;
}

template<typename T, std::size_t N, typename Alloc>
std::size_t SPSCQueue<T, N, Alloc>::map_index(std::size_t i)
{
  return i % N;
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::sleep(std::size_t &spin_counter)
{
  if (spin_counter < SPIN_LIMIT)
  {
    ++spin_counter;
  }
  else
  {
    std::this_thread::sleep_for(std::chrono::microseconds(SPIN_WAIT_US));
  }
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::print_metadata() const
{
  std::cout << "Producer @ " << &head << "\n"
            << "Consumer @ " << &tail << "\n"
            << "Buffer   @ " << data << std::endl;
}
