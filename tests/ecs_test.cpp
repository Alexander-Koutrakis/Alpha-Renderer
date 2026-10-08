#include "doctest.h"
#include "ECS/ecs.hpp"
#include "ECS/chunk.hpp"
#include "ECS/component_storage.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

using namespace ECS;

namespace {

Transform transformAt(float x) {
    Transform t;
    t.position = glm::vec3(x, 0.0f, 0.0f);
    return t;
}

// Counts the components a storage reports when its chunks are walked, the way the engine iterates them.
template <typename T> size_t walkedCount(ComponentStorage<T>& storage) {
    size_t count = 0;
    for (size_t i = 0; i < storage.getChunkCount(); ++i) {
        Chunk<T>* chunk = storage.getChunk(i);
        for (size_t j = 0; j < chunk->size(); ++j) {
            if (chunk->getComponent(j)) {
                ++count;
            }
        }
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------- Chunk

TEST_CASE("Chunk fills to capacity and then reports full") {
    Chunk<Transform> chunk{3};
    CHECK_FALSE(chunk.isFull());
    chunk.addComponent(transformAt(1));
    chunk.addComponent(transformAt(2));
    chunk.addComponent(transformAt(3));
    CHECK(chunk.isFull());
    CHECK(chunk.size() == 3);
    CHECK_THROWS_AS(chunk.addComponent(transformAt(4)), std::runtime_error);
}

TEST_CASE("Chunk returns the stored component by index") {
    Chunk<Transform> chunk{4};
    const size_t a = chunk.addComponent(transformAt(10));
    const size_t b = chunk.addComponent(transformAt(20));
    REQUIRE(chunk.getComponent(a));
    REQUIRE(chunk.getComponent(b));
    CHECK(chunk.getComponent(a)->position.x == doctest::Approx(10.0f));
    CHECK(chunk.getComponent(b)->position.x == doctest::Approx(20.0f));
    CHECK(chunk.getComponent(2) == nullptr); // past the end
}

TEST_CASE("Chunk reuses a freed slot and hides it meanwhile") {
    Chunk<Transform> chunk{4};
    chunk.addComponent(transformAt(1));
    const size_t middle = chunk.addComponent(transformAt(2));
    chunk.addComponent(transformAt(3));

    chunk.removeComponent(middle);
    CHECK(chunk.size() == 2);
    CHECK(chunk.getComponent(middle) == nullptr);

    const size_t reused = chunk.addComponent(transformAt(9));
    CHECK(reused == middle);
    CHECK(chunk.size() == 3);
    CHECK(chunk.getComponent(reused)->position.x == doctest::Approx(9.0f));
}

TEST_CASE("Chunk removing the last slot shrinks it") {
    Chunk<Transform> chunk{4};
    chunk.addComponent(transformAt(1));
    const size_t last = chunk.addComponent(transformAt(2));
    chunk.removeComponent(last);
    CHECK(chunk.size() == 1);
    CHECK(chunk.getComponent(last) == nullptr);
}

TEST_CASE("Chunk rejects out-of-range removal") {
    Chunk<Transform> chunk{4};
    chunk.addComponent(transformAt(1));
    CHECK_THROWS_AS(chunk.removeComponent(5), std::out_of_range);
}

// ---------------------------------------------------------------- ComponentStorage

TEST_CASE("ComponentStorage maps entities to their components") {
    ComponentStorage<Transform> storage{4};
    storage.addComponent(10, transformAt(1));
    storage.addComponent(11, transformAt(2));
    REQUIRE(storage.getComponent(10));
    REQUIRE(storage.getComponent(11));
    CHECK(storage.getComponent(10)->position.x == doctest::Approx(1.0f));
    CHECK(storage.getComponent(11)->position.x == doctest::Approx(2.0f));
    CHECK(storage.getComponent(12) == nullptr);
    CHECK(storage.getTotalComponentCount() == 2);
}

TEST_CASE("ComponentStorage grows by chunks") {
    ComponentStorage<Transform> storage{4};
    for (EntityID e = 0; e < 10; ++e) {
        storage.addComponent(e, transformAt(static_cast<float>(e)));
    }
    CHECK(storage.getChunkCount() == 3);
    CHECK(storage.getTotalComponentCount() == 10);
    for (EntityID e = 0; e < 10; ++e) {
        REQUIRE(storage.getComponent(e));
        CHECK(storage.getComponent(e)->position.x == doctest::Approx(static_cast<float>(e)));
    }
}

TEST_CASE("removing a middle component keeps every other component intact") {
    ComponentStorage<Transform> storage{4};
    for (EntityID e = 0; e < 6; ++e) {
        storage.addComponent(e, transformAt(static_cast<float>(e)));
    }

    storage.removeEntity(2);

    CHECK(storage.getComponent(2) == nullptr);
    CHECK(storage.getTotalComponentCount() == 5);
    for (EntityID e : {0u, 1u, 3u, 4u, 5u}) {
        REQUIRE(storage.getComponent(e));
        CHECK(storage.getComponent(e)->position.x == doctest::Approx(static_cast<float>(e)));
    }
    CHECK(walkedCount(storage) == 5);
}

TEST_CASE("removing the last component leaves no stale component behind") {
    ComponentStorage<Transform> storage{4};
    for (EntityID e = 0; e < 3; ++e) {
        storage.addComponent(e, transformAt(static_cast<float>(e)));
    }

    storage.removeEntity(2); // the most recently added

    CHECK(storage.getComponent(2) == nullptr);
    CHECK(storage.getTotalComponentCount() == 2);
    CHECK(walkedCount(storage) == 2); // iteration must not still see the removed component
}

TEST_CASE("removing the last component and then another keeps the survivors intact") {
    ComponentStorage<Transform> storage{4};
    for (EntityID e = 0; e < 4; ++e) {
        storage.addComponent(e, transformAt(static_cast<float>(e)));
    }

    storage.removeEntity(3);
    storage.removeEntity(0);

    CHECK(storage.getComponent(0) == nullptr);
    CHECK(storage.getComponent(3) == nullptr);
    for (EntityID e : {1u, 2u}) {
        REQUIRE(storage.getComponent(e));
        CHECK(storage.getComponent(e)->position.x == doctest::Approx(static_cast<float>(e)));
    }
    CHECK(walkedCount(storage) == 2);
}

TEST_CASE("removing every component empties the storage") {
    ComponentStorage<Transform> storage{3};
    for (EntityID e = 0; e < 7; ++e) {
        storage.addComponent(e, transformAt(static_cast<float>(e)));
    }
    for (EntityID e = 0; e < 7; ++e) {
        storage.removeEntity(e);
    }
    CHECK(storage.getTotalComponentCount() == 0);
    CHECK(walkedCount(storage) == 0);
}

TEST_CASE("removing an unknown entity is a no-op") {
    ComponentStorage<Transform> storage{4};
    storage.addComponent(1, transformAt(1));
    storage.removeEntity(99);
    CHECK(storage.getTotalComponentCount() == 1);
}

// ---------------------------------------------------------------- ECSManager
// ECSManager is a singleton, so every test below destroys the entities it creates.

TEST_CASE("ECSManager creates distinct entities") {
    auto& ecs = ECSManager::getInstance();
    const EntityID a = ecs.createEntity();
    const EntityID b = ecs.createEntity();
    CHECK(a != b);
    ecs.destroyEntity(a);
    ecs.destroyEntity(b);
}

TEST_CASE("ECSManager adds, reads and removes components") {
    auto& ecs = ECSManager::getInstance();
    const EntityID e = ecs.createEntity();

    CHECK(ecs.getComponent<Transform>(e) == nullptr);
    ecs.addComponent(e, transformAt(7));
    REQUIRE(ecs.getComponent<Transform>(e));
    CHECK(ecs.getComponent<Transform>(e)->position.x == doctest::Approx(7.0f));

    ecs.removeComponent<Transform>(e);
    CHECK(ecs.getComponent<Transform>(e) == nullptr);
    ecs.destroyEntity(e);
}

TEST_CASE("ECSManager queries entities by component set") {
    auto& ecs = ECSManager::getInstance();
    const EntityID onlyTransform = ecs.createEntity();
    const EntityID both = ecs.createEntity();
    const EntityID onlyLight = ecs.createEntity();
    ecs.addComponent(onlyTransform, transformAt(1));
    ecs.addComponent(both, transformAt(2));
    ecs.addComponent(both, PointLight{});
    ecs.addComponent(onlyLight, PointLight{});

    auto contains = [](const std::vector<EntityID>& v, EntityID e) {
        return std::find(v.begin(), v.end(), e) != v.end();
    };

    const auto withTransform = ecs.queryEntities<Transform>();
    CHECK(contains(withTransform, onlyTransform));
    CHECK(contains(withTransform, both));
    CHECK_FALSE(contains(withTransform, onlyLight));

    const auto withBoth = ecs.queryEntities<Transform, PointLight>();
    CHECK(contains(withBoth, both));
    CHECK_FALSE(contains(withBoth, onlyTransform));
    CHECK_FALSE(contains(withBoth, onlyLight));

    for (EntityID e : {onlyTransform, both, onlyLight}) {
        ecs.destroyEntity(e);
    }
}

TEST_CASE("destroying an entity removes its components and its query membership") {
    auto& ecs = ECSManager::getInstance();
    const EntityID e = ecs.createEntity();
    ecs.addComponent(e, transformAt(3));
    ecs.addComponent(e, PointLight{});

    ecs.destroyEntity(e);

    CHECK(ecs.getComponent<Transform>(e) == nullptr);
    CHECK(ecs.getComponent<PointLight>(e) == nullptr);
    const auto withTransform = ecs.queryEntities<Transform>();
    CHECK(std::find(withTransform.begin(), withTransform.end(), e) == withTransform.end());
}

TEST_CASE("destroying an unknown entity is a no-op") {
    ECSManager::getInstance().destroyEntity(0xFFFFFF);
}

TEST_CASE("using an unregistered component type fails loudly") {
    struct NeverRegistered : Component {};
    auto& ecs = ECSManager::getInstance();
    const EntityID e = ecs.createEntity();
    CHECK_THROWS_AS(ecs.getComponent<NeverRegistered>(e), std::runtime_error);
    ecs.destroyEntity(e);
}

TEST_CASE("getAllComponents and forEachComponent visit exactly the live components") {
    auto& ecs = ECSManager::getInstance();
    const size_t baseline = ecs.getAllComponents<Transform>().size();

    std::vector<EntityID> entities;
    for (int i = 0; i < 250; ++i) { // Transform chunks hold 100, so this spans three chunks
        const EntityID e = ecs.createEntity();
        ecs.addComponent(e, transformAt(static_cast<float>(i)));
        entities.push_back(e);
    }
    CHECK(ecs.getAllComponents<Transform>().size() == baseline + 250);

    size_t visited = 0;
    ecs.forEachComponent<Transform>([&visited](Transform&) { ++visited; });
    CHECK(visited == baseline + 250);

    for (EntityID e : entities) {
        ecs.destroyEntity(e);
    }
    CHECK(ecs.getAllComponents<Transform>().size() == baseline);
}
