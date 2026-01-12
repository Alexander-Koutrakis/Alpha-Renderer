#pragma once

#include <algorithm>


template <typename T>
Octree<T>::Node::Node(Octree* octree, const AABB& bounds, uint32_t depth)
    : bounds(bounds), depth(depth), octreeParent(octree) {}

template <typename T>
bool Octree<T>::Node::insert(typename Octree<T>::OctreeObject* object) {
    // Check if object intersects this node
    if (!octreeParent->intersects(bounds, object->getBounds())) {
        return false;
    }

    // If leaf node, add object here
    if (isLeaf()) {
        objects.push_back(object);
        object->currentNode = this;
        return true;
    }

    // Try to insert into a specific child if possible
    int octant = octreeParent->getOctant(object->getBounds(), bounds);
    if (octant != -1) {
        return children[octant]->insert(object);
    }

    // Object spans multiple children, keep it at this level
    objects.push_back(object);
    object->currentNode = this;
    return true;
}

template <typename T>
bool Octree<T>::Node::remove(typename Octree<T>::OctreeObject* object) {
    auto it = std::find(objects.begin(), objects.end(), object);
    if (it != objects.end()) {
        objects.erase(it);
        object->currentNode = nullptr;
        return true;
    }
    return false;
}

template <typename T>
void Octree<T>::Node::subdivide() {
    // Create 8 child nodes
    glm::vec3 halfExtents = bounds.extents * 0.5f;
    glm::vec3 quarterExtents = halfExtents * 0.5f;
    
    for (int i = 0; i < 8; ++i) {
        glm::vec3 centerOffset(
            ((i & 1) ? quarterExtents.x : -quarterExtents.x),
            ((i & 2) ? quarterExtents.y : -quarterExtents.y),
            ((i & 4) ? quarterExtents.z : -quarterExtents.z)
        );
        
        AABB childBounds;
        childBounds.center = bounds.center + centerOffset;
        childBounds.extents = halfExtents;
        
        children[i] = std::make_unique<Node>(octreeParent, childBounds, depth + 1);
        children[i]->parent = this;
    }

    // Redistribute objects to children
    auto it = objects.begin();
    while (it != objects.end()) {
        OctreeObject* object = *it;
        int octant = octreeParent->getOctant(object->getBounds(), bounds);
        
        if (octant != -1 && children[octant]->insert(object)) {
            it = objects.erase(it);
        } else {
            ++it;
        }
    }
}

template <typename T>
void Octree<T>::Node::collectVisible(const ViewFrustum& frustum, 
                                   std::vector<typename Octree<T>::OctreeObject*>& visibleObjects) const {
    // Test node against frustum
    auto intersection = frustum.testAABB(bounds);
    if (intersection == ViewFrustum::Intersection::OUTSIDE) {
        return;
    }
    
    // If completely inside, add all objects without further testing
    if (intersection == ViewFrustum::Intersection::INSIDE) {
        visibleObjects.insert(visibleObjects.end(), objects.begin(), objects.end());
        
        // Add all objects from children as well
        if (!isLeaf()) {
            for (const auto& child : children) {
                if (child) {
                    child->collectVisible(frustum, visibleObjects);
                }
            }
        }
        return;
    }
    
    // Node partially intersects frustum, test objects individually
    for (auto* obj : objects) {
        if (frustum.testAABB(obj->getBounds()) != ViewFrustum::Intersection::OUTSIDE) {
            visibleObjects.push_back(obj);
        }
    }
    
    // Continue with children
    if (!isLeaf()) {
        for (const auto& child : children) {
            if (child) {
                child->collectVisible(frustum, visibleObjects);
            }
        }
    }
}

template <typename T>
void Octree<T>::Node::collectIntersecting(const AABB& queryBounds,
                                        std::vector<typename Octree<T>::OctreeObject*>& intersectingObjects) const {
    // Check if node intersects query bounds
    if (!octreeParent->intersects(bounds, queryBounds)) {
        return;
    }

    // Add intersecting objects from this node
    for (auto* obj : objects) {
        if (octreeParent->intersects(obj->getBounds(), queryBounds)) {
            intersectingObjects.push_back(obj);
        }
    }

    // Recurse into children
    if (!isLeaf()) {
        for (const auto& child : children) {
            if (child) {
                child->collectIntersecting(queryBounds, intersectingObjects);
            }
        }
    }
}

template <typename T>
Octree<T>::Octree(const AABB& worldBounds, const Settings& settings)
    : settings(settings), worldBounds(worldBounds) {
    root = std::make_unique<Node>(this, worldBounds, 0);
}

template <typename T>
typename Octree<T>::OctreeObject* Octree<T>::createObject(T* data, const AABB& bounds) {
    objectPool.push_back(std::make_unique<OctreeObject>(data, bounds));
    OctreeObject* obj = objectPool.back().get();
    
    if (root->insert(obj)) {
        // Check if we need to subdivide
        if (shouldSubdivide(root.get())) {
            root->subdivide();
        }
    }
    
    return obj;
}

template <typename T>
void Octree<T>::removeObject(typename Octree<T>::OctreeObject* object) {
    if (object->currentNode) {
        object->currentNode->remove(object);
    }
    
    // Find and remove from object pool
    auto it = std::find_if(objectPool.begin(), objectPool.end(), 
                           [object](const std::unique_ptr<OctreeObject>& ptr) {
                               return ptr.get() == object;
                           });
    if (it != objectPool.end()) {
        objectPool.erase(it);
    }
}

template <typename T>
void Octree<T>::updateObject(typename Octree<T>::OctreeObject* object, const AABB& newBounds) {
    // Remove from current node
    if (object->currentNode) {
        object->currentNode->remove(object);
    }
    
    // Update bounds
    object->bounds = newBounds;
    
    // Reinsert into tree
    if (root->insert(object)) {
        // Check if we need to subdivide
        if (shouldSubdivide(root.get())) {
            root->subdivide();
        }
    }
}

template <typename T>
std::vector<T*> Octree<T>::getVisibleObjects(const ViewFrustum& frustum) const {
    std::vector<typename Octree<T>::OctreeObject*> objectPtrs;
    std::vector<T*> visibleObjects;
    
    if (root) {
        root->collectVisible(frustum, objectPtrs);
        
        // Convert OctreeObject* to T*
        visibleObjects.reserve(objectPtrs.size());
        for (auto* obj : objectPtrs) {
            visibleObjects.push_back(obj->getData());
        }
    }
    return visibleObjects;
}

template <typename T>
std::vector<T*> Octree<T>::getIntersectingObjects(const AABB& bounds) const {
    std::vector<typename Octree<T>::OctreeObject*> objectPtrs;
    std::vector<T*> intersectingObjects;
    
    if (root) {
        root->collectIntersecting(bounds, objectPtrs);
        
        // Convert OctreeObject* to T*
        intersectingObjects.reserve(objectPtrs.size());
        for (auto* obj : objectPtrs) {
            intersectingObjects.push_back(obj->getData());
        }
    }
    return intersectingObjects;
}

template <typename T>
void Octree<T>::clear() {
    objectPool.clear();
    root = std::make_unique<Node>(this, worldBounds, 0);
}

template <typename T>
bool Octree<T>::shouldSubdivide(const Node* node) const {
    return node->objects.size() > settings.maxObjectsPerNode &&
           node->depth < settings.maxDepth &&
           node->bounds.extents.x > settings.minNodeSize &&
           node->bounds.extents.y > settings.minNodeSize &&
           node->bounds.extents.z > settings.minNodeSize;
}

template <typename T>
int Octree<T>::getOctant(const AABB& objectBounds, const AABB& nodeBounds) const {
    // If object doesn't fit entirely in one octant, return -1
    if (objectBounds.extents.x > nodeBounds.extents.x * 0.5f ||
        objectBounds.extents.y > nodeBounds.extents.y * 0.5f ||
        objectBounds.extents.z > nodeBounds.extents.z * 0.5f) {
        return -1;
    }

    int octant = 0;
    if (objectBounds.center.x >= nodeBounds.center.x) octant |= 1;
    if (objectBounds.center.y >= nodeBounds.center.y) octant |= 2;
    if (objectBounds.center.z >= nodeBounds.center.z) octant |= 4;
    
    return octant;
}


template <typename T>
bool Octree<T>::intersects(const AABB& a, const AABB& b) const {
    return std::abs(a.center.x - b.center.x) <= (a.extents.x + b.extents.x) &&
           std::abs(a.center.y - b.center.y) <= (a.extents.y + b.extents.y) &&
           std::abs(a.center.z - b.center.z) <= (a.extents.z + b.extents.z);
}

