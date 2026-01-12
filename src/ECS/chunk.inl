#pragma once
#include "chunk.hpp"

namespace ECS {

template<typename T>
bool Chunk<T>::isFull() const {
    return m_size >= m_capacity && m_freeIndices.empty();
}

template<typename T>
std::size_t Chunk<T>::addComponent(const T& component) {
    if (isFull()) {
        throw std::runtime_error("Chunk is full");
    }
    
    std::size_t index;
    if (!m_freeIndices.empty()) {
        index = m_freeIndices.back();
        m_freeIndices.pop_back();
        m_data[index] = component;
    } else {
        index = m_size;
        m_data[index] = component;
        ++m_size;
    }
    return index;
}

template<typename T>
void Chunk<T>::addComponentToPosition(const ComponentIndex& componentIndex, const T& component) {   
    if (componentIndex.componentIndex >= m_capacity) {
        throw std::out_of_range("Component index out of range");
    }
    m_data[componentIndex.componentIndex] = component;
}

template<typename T>
void Chunk<T>::removeComponent(std::size_t index) {
    if (index >= m_size) {
        throw std::out_of_range("Index out of range");
    }
    
    if (index == m_size - 1) {
        --m_size;
    } else {
        m_freeIndices.push_back(index);
    }
}

template<typename T>
T* Chunk<T>::getComponent(std::size_t index) {
    if (index >= m_size || 
        std::find(m_freeIndices.begin(), m_freeIndices.end(), index) != m_freeIndices.end()) {
        return nullptr;
    }
    return &m_data[index];
}

template<typename T>
std::size_t Chunk<T>::size() const {
    return m_size - m_freeIndices.size();
}


} // namespace ECS