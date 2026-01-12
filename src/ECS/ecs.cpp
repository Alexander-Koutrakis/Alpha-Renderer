#include "ecs.hpp"
#include "components.hpp"

namespace ECS {

EntityID ECSManager::createEntity() {
    EntityID id = nextEntityId++;
    m_Entities.emplace_back(id);
    entityMasks[id] = ComponentMask();
    return id;
}

void ECSManager::destroyEntity(EntityID entity) {
    // Get the component mask for this entity
    auto maskIt = entityMasks.find(entity);
    if (maskIt == entityMasks.end()) {
        // Entity doesn't exist
        return;
    }

    ComponentMask& mask = maskIt->second;

    // Iterate through all bits in the mask
    for (std::size_t i = 0; i < MAX_COMPONENTS; ++i) {
        if (mask.test(i)) {
            auto storageIt = componentStorages.find(static_cast<ComponentTypeID>(i));
            if (storageIt != componentStorages.end() && storageIt->second) {
                storageIt->second->removeEntity(entity);
            }
        }
    }

   

    // Remove the entity's mask
    entityMasks.erase(entity);

    // Remove from entities vector if you're maintaining the entity objects
    auto entityIt = std::find_if(m_Entities.begin(), m_Entities.end(),
        [entity](const Entity& e) { return e.getId() == entity; });
    if (entityIt != m_Entities.end()) {
        m_Entities.erase(entityIt);
    }
}



void ECSManager::registerComponents(){
    registerComponentType<ECS::Transform>(100);
    registerComponentType<ECS::Camera>(1);
    registerComponentType<ECS::SkyboxComponent>(1);
    registerComponentType<ECS::DirectionalLight>(4);
    registerComponentType<ECS::Renderable>();
    registerComponentType<ECS::SpotLight>();
    registerComponentType<ECS::PointLight>();
}

} // namespace ECS