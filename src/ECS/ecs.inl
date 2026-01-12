#pragma once
#include "ecs.hpp"
#include <algorithm>
#include <iostream>
namespace ECS {

  template<typename T>
void ECSManager::registerComponentType(std::size_t chunkSize) {
    const char* typeName = typeid(T).name();
    if (componentTypes.find(typeName) == componentTypes.end()) {
        ComponentTypeID typeId = nextComponentTypeId++;
        componentTypes[typeName] = typeId;
        componentStorages[typeId] = std::make_unique<ComponentStorage<T>>(chunkSize);
    }
}

    template<typename T>
    void ECSManager::addComponent(EntityID entity, const T& component) {

        const char* typeName = typeid(T).name();
        if (componentTypes.find(typeName) == componentTypes.end()) {
            registerComponentType<T>();
        }

        ComponentTypeID typeId = getComponentTypeID<T>();
        auto& storage = static_cast<ComponentStorage<T>&>(*componentStorages[typeId]);
        storage.addComponent(entity,component);
      
        entityMasks[entity].set(typeId);     
 
    }



    template<typename T>
    void ECSManager::removeComponent(EntityID entity) {
        ComponentTypeID typeId = getComponentTypeID<T>();
        auto& storage = *static_cast<ComponentStorage<T>*>(componentStorages[typeId].get());
        storage.removeEntity(entity);
        entityMasks[entity].reset(typeId);
    }

    template<typename T>
    T* ECSManager::getComponent(EntityID entity) {
        ComponentTypeID typeId = getComponentTypeID<T>();
        auto& storage= *static_cast<ComponentStorage<T>*>(componentStorages[typeId].get());
        auto component=storage.getComponent(entity);
        return component ? &(*component) : nullptr;        
    }

    template<typename T>
    T* ECSManager::getFirstComponent() {
        ComponentTypeID typeId = getComponentTypeID<T>();
        auto& storage= *static_cast<ComponentStorage<T>*>(componentStorages[typeId].get());
        return storage.getFirstComponent();
        //return component ? &(*component) : nullptr;        
    }

template<typename T>
ComponentTypeID ECSManager::getComponentTypeID() {
        const char* typeName = typeid(T).name();
        auto it = componentTypes.find(typeName);
        if (it == componentTypes.end()) {
            throw std::runtime_error("Component type not registered before use.");
        }
        return it->second;
    }

    template<typename... ComponentTypes>
    std::vector<EntityID> ECSManager::queryEntities() {
        std::vector<EntityID> result;
        ComponentMask queryMask;

        // Set the bits for each component type in the query mask
        (queryMask.set(getComponentTypeID<ComponentTypes>()), ...);

        // Iterate through all entities
        for (const auto& [entityId, entityMask] : entityMasks) {
            // Check if the entity has all the required components
            if ((entityMask & queryMask) == queryMask) {
                result.push_back(entityId);
            }
        }

        return result;
    }

    template<typename T>
    std::vector<T*> ECSManager::getAllComponents() {
        ComponentTypeID typeId = getComponentTypeID<T>();
        auto& storage = *static_cast<ComponentStorage<T>*>(componentStorages[typeId].get());
        
        std::vector<T*> allComponents;
        allComponents.reserve(storage.getTotalComponentCount());
        
        for (size_t chunkIndex = 0; chunkIndex < storage.getChunkCount(); chunkIndex++) {
            auto& chunk = *storage.getChunk(chunkIndex);
            for (size_t compIndex = 0; compIndex < chunk.size(); compIndex++) {
                T* component = chunk.getComponent(compIndex);
                if (component) {
                    allComponents.push_back(component);
                }
            }
        }
        
        return allComponents;
    }

    template<typename T>
    void ECSManager::forEachComponent(std::function<void(T&)> callback) {
        ComponentTypeID typeId = getComponentTypeID<T>();
        auto& storage = *static_cast<ComponentStorage<T>*>(componentStorages[typeId].get());
        
        for (size_t chunkIndex = 0; chunkIndex < storage.getChunkCount(); chunkIndex++) {
            auto* chunk = storage.getChunk(chunkIndex);
            if (!chunk) continue;
            
            // Process components in contiguous blocks for maximum cache efficiency
            for (size_t compIndex = 0; compIndex < chunk->size(); compIndex++) {
                T* component = chunk->getComponent(compIndex);
                if (component) {
                    callback(*component);
                }
            }
        }
    }
}