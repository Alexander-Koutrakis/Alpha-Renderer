#pragma once
#include <cstdint>

namespace ECS {
    using ComponentTypeID = std::uint8_t;
    using EntityID = std::uint32_t;

    struct ComponentIndex {
        std::size_t chunkIndex;
        std::size_t componentIndex;
       
        bool operator==(const ComponentIndex& other) const {
            return chunkIndex == other.chunkIndex && componentIndex == other.componentIndex;
        }
    };
}

namespace std{
     template<>
    struct hash<ECS::ComponentIndex> {
        std::size_t operator()(const ECS::ComponentIndex& k) const {
            return hash<std::size_t>()(k.chunkIndex) ^
                   (hash<std::size_t>()(k.componentIndex) << 1);
        }
    };
}