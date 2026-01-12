#pragma once

#include "ECS/components.hpp"
#include "Systems/bounding_box_system.hpp"
#include "AABB.hpp"
#include "view_frustum.hpp"
#include "ECS/ecs.hpp"
#include <vector>
#include <array>
#include <memory>
#include <type_traits>

namespace Math {

    // Forward declarations
    class OctreeObject;
    struct Settings {
                uint32_t maxDepth = 8;           // Maximum tree depth
                uint32_t maxObjectsPerNode = 16;  // Maximum objects before splitting
                float minNodeSize = 1.0f;        // Minimum size of node before stopping subdivision
            };

template <typename T>
class Octree {
public:
    struct Settings {
        uint32_t maxDepth = 8;
        uint32_t maxObjectsPerNode = 16;
        float minNodeSize = 1.0f;
    };

    class OctreeObject;
    class Node;

    class OctreeObject {
    public:
        OctreeObject(T* data, const AABB& bounds) 
            : data(data), bounds(bounds), currentNode(nullptr) {}

        T* getData() const { return data; }
        const AABB& getBounds() const { return bounds; }
        void updateBounds(const AABB& newBounds) { bounds = newBounds; }

    private:
        T* data;
        AABB bounds;
        Node* currentNode;

        friend class Octree;
        friend class Node;
    };

    class Node {
    public:
        Node(Octree* parent, const AABB& bounds, uint32_t depth);
        ~Node() = default;

        bool insert(OctreeObject* object);
        bool remove(OctreeObject* object);
        void subdivide();
        void collectVisible(const ViewFrustum& frustum, std::vector<OctreeObject*>& visibleObjects) const;
        void collectIntersecting(const AABB& bounds, std::vector<OctreeObject*>& intersectingObjects) const;

        const AABB& getBounds() const { return bounds; }
        bool isLeaf() const { return children[0] == nullptr; }
        Node* getParent() const { return parent; }
        Octree* getOctree() const { return octreeParent; }

    private:
        AABB bounds;
        uint32_t depth;
        std::vector<OctreeObject*> objects;
        std::array<std::unique_ptr<Node>, 8> children;
        Node* parent = nullptr;
        Octree* octreeParent = nullptr;

        friend class Octree;
    };

    Octree(const AABB& worldBounds, const Settings& settings = Settings());
    ~Octree() = default;

    OctreeObject* createObject(T* data, const AABB& bounds);
    void removeObject(OctreeObject* object);
    void updateObject(OctreeObject* object, const AABB& newBounds);
    
    std::vector<T*> getVisibleObjects(const ViewFrustum& frustum) const;
    std::vector<T*> getIntersectingObjects(const AABB& bounds) const;
    
    void clear();

    // Make these public so Node can access them
    bool shouldSubdivide(const Node* node) const;
    int getOctant(const AABB& objectBounds, const AABB& nodeBounds) const;
    bool intersects(const AABB& a, const AABB& b) const;

private:
    Settings settings;
    AABB worldBounds;
    std::unique_ptr<Node> root;
    std::vector<std::unique_ptr<OctreeObject>> objectPool;
};

// Include the template implementation
#include "octree.inl"

} // namespace Math