#pragma once
#include <vector>
#include <memory>
#include <optional>
#include "chunk.hpp"
#include "ecs.hpp"
#include "ecs_types.hpp"
#include "components.hpp"



namespace ECS{

      class IComponentStorage {
    public:
        virtual ~IComponentStorage() = default;
        virtual void removeEntity(EntityID entity) = 0;
    };


    template<typename T>
    class ComponentStorage : public IComponentStorage {
    public:
        static_assert(std::is_base_of<Component, T>::value, "T must inherit from Component");
        
        // Default constructor uses default chunk size of 1024
        ComponentStorage() : m_chunkSize(1024) {}
        
        // Constructor with custom chunk size
        explicit ComponentStorage(std::size_t chunkSize) : m_chunkSize(chunkSize) {}

        void addComponent(EntityID entityId, const T& component);
        void removeEntity(ECS::EntityID entityId) override;
        T* getComponent(EntityID entityId);
        T* getFirstComponent();
        std::size_t getChunkCount() const;
        std::size_t getTotalComponentCount() const;
        Chunk<T>* getChunk(size_t index);
    private:
        const std::size_t m_chunkSize;
        std::vector<std::unique_ptr<Chunk<T>>> m_chunks;
        std::unordered_map<EntityID, ComponentIndex> m_entityComponentMap;
        std::unordered_map<ComponentIndex, EntityID> m_componentEntityMap;
        std::optional<ComponentIndex> m_lastComponentLocation;

        void moveLastComponentToLocation(const ComponentIndex& location);
        void mapEntity(EntityID entityId, const ComponentIndex& location);
        void unmapEntity(EntityID entityId);
    };

} // namespace ECS

#include "component_storage.inl"