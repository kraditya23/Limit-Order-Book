#pragma once
#include <cstddef>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <new>
#include <vector>
#include <memory>

namespace lob {

template <typename T, std::size_t Capacity = 1000000>
class MemoryPool {
private:
    using FreeDeleter = void(*)(void*);
    std::unique_ptr<std::byte[], FreeDeleter> storage;
    std::vector<std::size_t> free_list;
    std::size_t bump_pointer = 0;

public:
    MemoryPool()
        : storage(
            static_cast<std::byte*>(std::aligned_alloc(alignof(T), Capacity * sizeof(T))),
            std::free
          )
    {
        if (!storage) throw std::bad_alloc();
        free_list.reserve(Capacity);
    }

    template<typename... Args>
    T* allocate(Args&&... args) {
        std::size_t index;

        if (!free_list.empty()) {
            index = free_list.back();
            free_list.pop_back();
        } else if (bump_pointer < Capacity) {
            index = bump_pointer++;
        } else {
            throw std::bad_alloc();
        }

        std::byte* address = storage.get() + (index * sizeof(T));
        return new (address) T(std::forward<Args>(args)...);
    }

    void deallocate(T* ptr) {
        if (!ptr) return;
        ptr->~T();
        std::size_t index = (reinterpret_cast<std::byte*>(ptr) - storage.get()) / sizeof(T);
        free_list.push_back(index);
    }
};

} // namespace lob
