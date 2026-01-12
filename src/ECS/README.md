# Entity Component System (ECS)

A data-oriented Entity Component System designed for cache-efficient iteration and flexible entity composition. Components are stored in contiguous memory chunks, enabling fast traversal and good cache utilization.

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            ECS ARCHITECTURE                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ECSManager (Singleton)                                                     │
│  ┌────────────────────────────────────────────────────────────────────┐     │
│  │                                                                    │     │
│  │  Entities          Component Storages          Entity Masks        │     │
│  │  ┌─────────┐       ┌─────────────────┐        ┌─────────────┐      │     │
│  │  │ ID: 0   │       │ Transform       │───────►│ 0: 11010... │      │     │
│  │  │ ID: 1   │       │ ┌─────────────┐ │        │ 1: 10110... │      │     │
│  │  │ ID: 2   │       │ │ Chunk 0     │ │        │ 2: 11110... │      │     │
│  │  │ ...     │       │ │ [T,T,T,T,T] │ │        │ ...         │      │     │
│  │  └─────────┘       │ ├─────────────┤ │        └─────────────┘      │     │
│  │                    │ │ Chunk 1     │ │                             │     │
│  │                    │ │ [T,T,T,...] │ │        Bitset tracking      │     │
│  │                    │ └─────────────┘ │        which components     │     │
│  │                    ├─────────────────┤        each entity has      │     │
│  │                    │ MeshRenderer    │                             │     │
│  │                    │ ┌─────────────┐ │                             │     │
│  │                    │ │ Chunk 0     │ │                             │     │
│  │                    │ │ [M,M,M,...] │ │                             │     │
│  │                    │ └─────────────┘ │                             │     │
│  │                    └─────────────────┘                             │     │
│  │                                                                    │     │
│  └────────────────────────────────────────────────────────────────────┘     │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Core Concepts

### Entity

An entity is simply a unique identifier. It has no data or behavior of its own—it exists only to link components together. When you create an entity, you receive an integer ID that serves as a key for attaching and retrieving components.

### Component

Components are pure data structures. They contain no logic, only the state needed to describe some aspect of an entity. A Transform component holds position, rotation, and scale. A MeshRenderer component holds references to mesh and material resources. Each component also stores the ID of its owning entity, enabling navigation between related components.

### Component Mask

Each entity has a bitset that tracks which component types it possesses. This enables fast filtering when querying for entities with specific component combinations. Checking whether an entity has a particular set of components reduces to a single bitwise AND operation.

## Memory Layout: Chunks

Components are stored in fixed-size chunks for cache efficiency:

### Chunk Implementation

Each chunk is a fixed-capacity array of components. When a component is added, it goes into the first available slot—either a recycled slot from a previously removed component, or the next unused position. This approach maintains density while allowing O(1) insertion and removal.

The chunk tracks its current size, maximum capacity, and a list of free indices for recycling. When a chunk fills up, a new chunk is allocated. This avoids the cost of reallocating and copying a single large array.

### Component Storage

Each component type has its own storage that manages multiple chunks. A hash map tracks which chunk and index holds each entity's component, enabling O(1) lookup by entity ID.

When adding a component, the storage finds a chunk with available space (or creates one), inserts the component, and records the mapping. When removing, it marks the slot as free for recycling and removes the mapping.

## ECSManager API

The ECSManager is a singleton that provides the primary interface for working with entities and components.

### Entity Management

Creating an entity returns a unique ID that persists for the entity's lifetime. Destroying an entity removes all its components and frees the ID for potential reuse.

### Component Operations

Components can be registered with custom chunk sizes to optimize for expected usage patterns. Adding a component associates it with an entity and stores it in the appropriate typed storage. Getting a component returns a pointer to the stored data, or null if the entity lacks that component type. Removing a component frees the storage slot and updates the entity's component mask.

### Querying

The system supports querying for all entities that have a specific combination of component types. The query checks each entity's component mask against the requested types, returning only those that match. For bulk operations, a callback-based iteration interface provides the most cache-efficient access by processing components sequentially within each chunk.

## Component Types

### Core Components

| Component | Purpose |
|-----------|---------|
| Transform | Position, rotation, scale, and derived matrices |
| MeshRenderer | Mesh geometry and material references for rendering |
| Renderable | Combined Transform and MeshRenderer for convenience |
| Camera | View and projection matrices, field of view, clip planes |

### Light Components

| Component | Purpose |
|-----------|---------|
| Light | Base light properties: intensity, color, shadow settings |
| DirectionalLight | Sun-like light with cascaded shadow matrices |
| SpotLight | Cone-shaped light with range and cutoff angles |
| PointLight | Omnidirectional light with cubemap shadow matrices |

### Other Components

| Component | Purpose |
|-----------|---------|
| SkyboxComponent | Environment cubemap texture reference |

## Performance Considerations

### Cache Efficiency

The chunk-based storage ensures that iterating over all components of a type accesses memory sequentially. This plays well with CPU prefetching and minimizes cache misses. The callback-based iteration interface is the most efficient way to process components, as it traverses chunks in order without any indirection.

Random access by entity ID is less cache-friendly since it requires a hash lookup followed by indexed access into a potentially distant chunk. For bulk operations, prefer iteration over individual lookups.

### Chunk Size Tuning

The default chunk size of 1024 components works well for common component types. For rarely-used components, smaller chunks reduce memory waste. For very common components like Transform, larger chunks may improve cache utilization further.

### Query Optimization

The component mask enables fast entity filtering. Checking whether an entity has all required components is a single bitwise AND operation, regardless of how many component types are involved. This makes multi-component queries efficient even with many entities.

## Design Decisions

### Why Chunks Instead of Sparse Sets?

Chunks provide a simpler implementation with predictable memory allocation. While sparse sets offer O(1) component access with better memory efficiency for sparse component distributions, chunks are easier to reason about and provide good enough performance for typical game object counts in the thousands to tens of thousands range.

### Why Singleton ECSManager?

A singleton provides global access to the component system, which systems throughout the codebase need. This trades testability for simplicity—there's no need to pass the manager through function parameters or manage multiple instances. For this project's scope, the convenience outweighs the drawbacks.

### Why Owner ID in Components?

Each component stores its owning entity's ID. This enables navigation from one component to related components on the same entity. When processing a MeshRenderer, you can easily retrieve the corresponding Transform using the owner ID. This pattern supports scene graph relationships and aids debugging by making component ownership explicit.
