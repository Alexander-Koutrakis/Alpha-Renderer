#pragma once
#include "component_storage.hpp"
namespace ECS {

template<typename T>
    void ComponentStorage<T>::addComponent(EntityID entityId, const T& component) {
        if (m_chunks.empty() || m_chunks.back()->isFull()) {
            m_chunks.push_back(std::make_unique<Chunk<T>>(m_chunkSize));
        }

        std::size_t chunkIndex = m_chunks.size() - 1;
        std::size_t componentIndex = m_chunks.back()->addComponent(component);
        
        ComponentIndex location{chunkIndex, componentIndex};
        mapEntity(entityId, location);
        m_lastComponentLocation = location;
    }


 template<typename T>
    void ComponentStorage<T>::removeEntity(EntityID entityId) {
        auto it = m_entityComponentMap.find(entityId);
        if (it == m_entityComponentMap.end()) {
            return;
        }

        ComponentIndex locationToRemove = it->second;
        unmapEntity(entityId);
        
        if (m_lastComponentLocation == locationToRemove) {
            return;
        }   

        moveLastComponentToLocation(locationToRemove);      
    }

template<typename T>
void ComponentStorage<T>::moveLastComponentToLocation(const ComponentIndex& location) {
    if (!m_lastComponentLocation) {
        return;
    }

    // Get the last component
    auto& lastChunk = m_chunks[m_lastComponentLocation->chunkIndex];
    T* lastComponent = lastChunk->getComponent(m_lastComponentLocation->componentIndex);
    if (!lastComponent) {
        return;
    }

    // Find which entity owns the last component

    EntityID lastEntity=m_componentEntityMap[m_lastComponentLocation.value()];
    

    // Move the component
    m_chunks[location.chunkIndex]->addComponentToPosition(location,*lastComponent);
    lastChunk->removeComponent(m_lastComponentLocation->componentIndex);

    // Update the moved component's location in the map
    mapEntity(lastEntity,location);

    // Update last component location
    if (lastChunk->size() == 0) {
        m_chunks.pop_back();
        if (m_chunks.empty()) {
            m_lastComponentLocation = std::nullopt;
        } else {
            auto& newLastChunk = m_chunks.back();
            m_lastComponentLocation = ComponentIndex{m_chunks.size() - 1, newLastChunk->size() - 1};
        }
    } else {
        m_lastComponentLocation = ComponentIndex{m_lastComponentLocation->chunkIndex, lastChunk->size() - 1};
    }
}

template<typename T>
T* ComponentStorage<T>::getComponent(EntityID entityId) {
    auto it = m_entityComponentMap.find(entityId);
    if (it == m_entityComponentMap.end()) {
        return nullptr;
    }

    const ComponentIndex& location = it->second;
    if (location.chunkIndex >= m_chunks.size()) {
        return nullptr;
    }

    return m_chunks[location.chunkIndex]->getComponent(location.componentIndex);
}

template<typename T>
T* ComponentStorage<T>::getFirstComponent() {
    return m_chunks[0]->getComponent(0);
}


template<typename T>
std::size_t ComponentStorage<T>::getChunkCount() const {
    return m_chunks.size();
}

template<typename T>
std::size_t ComponentStorage<T>::getTotalComponentCount() const {
    std::size_t total = 0;
    for (const auto& chunk : m_chunks) {
        total += chunk->size();
    }
    return total;
}

template<typename T>
void ComponentStorage<T>::mapEntity(EntityID entityId, const ComponentIndex& location) {
    m_entityComponentMap[entityId] = location;
    m_componentEntityMap[location] = entityId;
}

template<typename T>
void ComponentStorage<T>::unmapEntity(EntityID entityId) {
    auto it = m_entityComponentMap.find(entityId);
    if (it != m_entityComponentMap.end()) {
        m_componentEntityMap.erase(it->second);
        m_entityComponentMap.erase(it);
    }
}

template<typename T>
Chunk<T>* ComponentStorage<T>::getChunk(size_t index) {
    if (index >= m_chunks.size()) {
        return nullptr;
    }
    return m_chunks[index].get();
}

} // namespace ECS