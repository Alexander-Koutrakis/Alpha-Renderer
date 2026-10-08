#include "doctest.h"
#include "Math/octree.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <set>
#include <vector>

using Math::AABB;
using Math::Octree;
using Math::ViewFrustum;
using Intersection = ViewFrustum::Intersection;
using IntOctree = Octree<int>;

namespace {

IntOctree::Settings smallNodes() {
    IntOctree::Settings settings;
    settings.maxDepth = 4;
    settings.maxObjectsPerNode = 4; // force subdivision with few objects
    settings.minNodeSize = 1.0f;
    return settings;
}

const AABB kWorld{{0, 0, 0}, {100, 100, 100}};

AABB box(float x, float y, float z, float half = 1.0f) {
    return AABB{{x, y, z}, {half, half, half}};
}

std::multiset<int> asSet(const std::vector<int*>& pointers) {
    std::multiset<int> values;
    for (const int* p : pointers) {
        values.insert(*p);
    }
    return values;
}

// A deterministic spread of small boxes over the world, so subdivision is exercised.
struct Scatter {
    std::vector<int> ids;
    std::vector<AABB> bounds;
    explicit Scatter(int count) {
        uint32_t state = 12345u;
        auto next = [&state] {
            state = state * 1664525u + 1013904223u;
            return static_cast<float>((state >> 8) & 0xFFFF) / 65535.0f; // 0..1
        };
        for (int i = 0; i < count; ++i) {
            ids.push_back(i);
            bounds.push_back(box(-90 + next() * 180, -90 + next() * 180, -90 + next() * 180, 0.5f + next() * 2.0f));
        }
    }
};

} // namespace

TEST_CASE("an empty octree returns nothing") {
    IntOctree tree{kWorld};
    CHECK(tree.getIntersectingObjects(kWorld).empty());
    const ViewFrustum frustum =
        ViewFrustum::createFromViewProjection(glm::orthoLH_ZO(-100.f, 100.f, -100.f, 100.f, -100.f, 100.f));
    CHECK(tree.getVisibleObjects(frustum).empty());
}

TEST_CASE("intersection query returns exactly the overlapping objects") {
    IntOctree tree{kWorld, smallNodes()};
    int a = 0, b = 1, c = 2;
    tree.createObject(&a, box(-50, 0, 0));
    tree.createObject(&b, box(50, 0, 0));
    tree.createObject(&c, box(50, 50, 50));

    CHECK(asSet(tree.getIntersectingObjects(box(-50, 0, 0, 5))) == std::multiset<int>{0});
    CHECK(asSet(tree.getIntersectingObjects(box(50, 0, 0, 5))) == std::multiset<int>{1});
    CHECK(asSet(tree.getIntersectingObjects(box(50, 25, 25, 30))) == std::multiset<int>{1, 2});
    CHECK(tree.getIntersectingObjects(box(0, 80, -80, 5)).empty());
    CHECK(asSet(tree.getIntersectingObjects(kWorld)) == std::multiset<int>{0, 1, 2});
}

TEST_CASE("touching boxes count as intersecting") {
    IntOctree tree{kWorld, smallNodes()};
    int a = 0;
    tree.createObject(&a, box(0, 0, 0, 1));
    CHECK(tree.getIntersectingObjects(box(2, 0, 0, 1)).size() == 1); // faces touch
    CHECK(tree.getIntersectingObjects(box(2.1f, 0, 0, 1)).empty());  // small gap
}

TEST_CASE("many objects survive subdivision and are all found exactly once") {
    Scatter scatter{200};
    IntOctree tree{kWorld, smallNodes()};
    for (size_t i = 0; i < scatter.ids.size(); ++i) {
        tree.createObject(&scatter.ids[i], scatter.bounds[i]);
    }

    const std::multiset<int> found = asSet(tree.getIntersectingObjects(kWorld));
    CHECK(found.size() == scatter.ids.size());
    CHECK(std::set<int>(found.begin(), found.end()).size() == scatter.ids.size()); // no duplicates
}

TEST_CASE("intersection query matches brute force") {
    Scatter scatter{300};
    IntOctree tree{kWorld, smallNodes()};
    for (size_t i = 0; i < scatter.ids.size(); ++i) {
        tree.createObject(&scatter.ids[i], scatter.bounds[i]);
    }

    const AABB queries[] = {box(0, 0, 0, 20), box(-60, 40, 10, 15), box(70, -70, 70, 25), box(0, 0, 0, 100)};
    for (const AABB& query : queries) {
        std::multiset<int> expected;
        for (size_t i = 0; i < scatter.ids.size(); ++i) {
            const AABB& b = scatter.bounds[i];
            if (glm::all(glm::lessThanEqual(glm::abs(b.center - query.center), b.extents + query.extents))) {
                expected.insert(scatter.ids[i]);
            }
        }
        CHECK(asSet(tree.getIntersectingObjects(query)) == expected);
    }
}

TEST_CASE("visibility query matches brute force frustum testing") {
    Scatter scatter{300};
    IntOctree tree{kWorld, smallNodes()};
    for (size_t i = 0; i < scatter.ids.size(); ++i) {
        tree.createObject(&scatter.ids[i], scatter.bounds[i]);
    }

    const glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, 60.0f)); // camera at z = -60
    const ViewFrustum frustum = ViewFrustum::createPerspective(glm::radians(70.0f), 16.0f / 9.0f, 1.0f, 120.0f, view);

    std::multiset<int> expected;
    for (size_t i = 0; i < scatter.ids.size(); ++i) {
        if (frustum.testAABB(scatter.bounds[i]) != Intersection::OUTSIDE) {
            expected.insert(scatter.ids[i]);
        }
    }

    const std::multiset<int> visible = asSet(tree.getVisibleObjects(frustum));
    CHECK(visible == expected);
    // The frustum must cull some objects and keep some, or the comparison proves nothing.
    CHECK(expected.size() > 0);
    CHECK(expected.size() < scatter.ids.size());
}

TEST_CASE("an object larger than a child node stays reachable") {
    IntOctree tree{kWorld, smallNodes()};
    std::vector<int> filler(20);
    for (size_t i = 0; i < filler.size(); ++i) {
        filler[i] = static_cast<int>(i) + 1;
        tree.createObject(&filler[i], box(-80 + static_cast<float>(i) * 8, -80, -80, 0.5f));
    }
    int big = 0;
    tree.createObject(&big, box(0, 0, 0, 90));
    CHECK(asSet(tree.getIntersectingObjects(box(10, 10, 10, 1))) == std::multiset<int>{0});
}

TEST_CASE("removeObject removes only that object") {
    Scatter scatter{50};
    IntOctree tree{kWorld, smallNodes()};
    std::vector<IntOctree::OctreeObject*> handles;
    for (size_t i = 0; i < scatter.ids.size(); ++i) {
        handles.push_back(tree.createObject(&scatter.ids[i], scatter.bounds[i]));
    }

    tree.removeObject(handles[7]);
    tree.removeObject(handles[23]);

    const std::multiset<int> found = asSet(tree.getIntersectingObjects(kWorld));
    CHECK(found.size() == 48);
    CHECK(found.count(7) == 0);
    CHECK(found.count(23) == 0);
    CHECK(found.count(8) == 1);
}

TEST_CASE("updateObject moves an object to its new bounds") {
    IntOctree tree{kWorld, smallNodes()};
    std::vector<int> ids(30);
    std::vector<IntOctree::OctreeObject*> handles;
    for (size_t i = 0; i < ids.size(); ++i) {
        ids[i] = static_cast<int>(i);
        handles.push_back(tree.createObject(&ids[i], box(-80 + static_cast<float>(i) * 5, 0, 0, 0.5f)));
    }

    tree.updateObject(handles[3], box(60, 60, 60, 0.5f));

    CHECK(asSet(tree.getIntersectingObjects(box(60, 60, 60, 2))) == std::multiset<int>{3});
    CHECK(tree.getIntersectingObjects(box(-80 + 3 * 5, 0, 0, 0.6f)).empty()); // old position is empty
    CHECK(asSet(tree.getIntersectingObjects(kWorld)).size() == ids.size());
}

TEST_CASE("clear empties the tree and it can be reused") {
    Scatter scatter{60};
    IntOctree tree{kWorld, smallNodes()};
    for (size_t i = 0; i < scatter.ids.size(); ++i) {
        tree.createObject(&scatter.ids[i], scatter.bounds[i]);
    }
    tree.clear();
    CHECK(tree.getIntersectingObjects(kWorld).empty());

    int fresh = 99;
    tree.createObject(&fresh, box(0, 0, 0));
    CHECK(asSet(tree.getIntersectingObjects(kWorld)) == std::multiset<int>{99});
}

TEST_CASE("getOctant picks the octant by center and rejects boxes that do not fit") {
    IntOctree tree{kWorld};
    const AABB node{{0, 0, 0}, {10, 10, 10}};
    CHECK(tree.getOctant(box(-5, -5, -5, 1), node) == 0);
    CHECK(tree.getOctant(box(5, -5, -5, 1), node) == 1);
    CHECK(tree.getOctant(box(-5, 5, -5, 1), node) == 2);
    CHECK(tree.getOctant(box(-5, -5, 5, 1), node) == 4);
    CHECK(tree.getOctant(box(5, 5, 5, 1), node) == 7);
    CHECK(tree.getOctant(box(0, 0, 0, 6), node) == -1); // wider than half the node
}
