#pragma once

#include <array>
#include <vector>
#include <cstddef>
#include <stdexcept>
#include <optional>
#include <algorithm>
#include "ecs.hpp"
#include "components.hpp"
#include "ecs_types.hpp"

namespace ECS {

template<typename T>
class Chunk {
public:
    //static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");

    // Constructor that takes the chunk size
    explicit Chunk(std::size_t size) 
        : m_capacity(size)
        , m_data(std::make_unique<T[]>(size))
        , m_size(0) {}

    bool isFull() const;
    std::size_t addComponent(const T& component);
    void addComponentToPosition(const ComponentIndex& componentIndex, const T& component);
    void removeComponent(std::size_t index);
    T* getComponent(std::size_t index);
    std::size_t size() const;
   
private:
    const std::size_t m_capacity;
    std::unique_ptr<T[]> m_data;
    std::size_t m_size = 0;
    std::vector<std::size_t> m_freeIndices;
};

} // namespace ECS

#include "chunk.inl"