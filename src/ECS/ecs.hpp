#pragma once
#include <iostream>
#include <vector>
#include <unordered_map>
#include <memory>
#include <bitset>
#include <functional>
#include "component_storage.hpp"
#include "components.hpp"
#include "ecs_types.hpp"
#include "Systems/transform_system.hpp"

namespace ECS{
    constexpr std::size_t MAX_COMPONENTS = 32;
    using ComponentMask = std::bitset<MAX_COMPONENTS>;

   class Entity {
    public:
        Entity(EntityID id) : id(id) {}
        EntityID getId() const { return id; }

    private:
        EntityID id;
    };


}





namespace ECS{  

 
    class ECSManager{
        public:
            static ECSManager& getInstance() {
                static ECSManager instance;
                return instance;
            }


            ECSManager(const ECSManager&) = delete;  
            ECSManager& operator=(const ECSManager&) = delete;
            ECSManager(ECSManager&&) = default;
            ECSManager& operator=(ECSManager&&) = default;

            EntityID createEntity();
            void destroyEntity(EntityID entity);
            
            template<typename T>
            void registerComponentType(std::size_t chunkSize = 1024);
            
            template<typename T>
            void addComponent(EntityID entity, const T& component);

            template<typename T>
            void removeComponent(EntityID entity);

            template<typename T>
            T* getComponent(EntityID entity);

             template<typename T>
            T* getFirstComponent();

            template<typename T>
            ComponentTypeID getComponentTypeID();
            
            template<typename... ComponentTypes>
            std::vector<EntityID> queryEntities();
                  
            template<typename T>
            std::vector<T*> getAllComponents();

            template<typename T>
            void forEachComponent(std::function<void(T&)> callback);

        private:

            ECSManager() {
                registerComponents();    
            }
    
            std::vector<Entity> m_Entities;
            std::unordered_map<EntityID, ComponentMask> entityMasks;
            std::unordered_map<std::string, ComponentTypeID> componentTypes;
            
            std::unordered_map<ComponentTypeID, std::unique_ptr<IComponentStorage>> componentStorages;
            void registerComponents();
            EntityID nextEntityId = 0;
            ComponentTypeID nextComponentTypeId = 0;
   
    };

   
}

#include "ecs.inl"