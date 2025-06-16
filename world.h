#ifndef SIGCRAFT_WORLD_H
#define SIGCRAFT_WORLD_H

extern "C" {

#include "enklume/block_data.h"
#include "enklume/enklume.h"

}

#include "chunk_mesh.h"

#include <future>
#include <mutex>

struct Int2 {
    int32_t x, z;
    bool operator==(const Int2 &other) const {
        return x == other.x && other.z == z;
    };
};

template <>
struct std::hash<Int2> {
    std::size_t operator()(const Int2& k) const {
        using std::size_t;
        using std::hash;
        using std::string;
        return (hash<uint32_t>()(k.x) ^ (hash<uint32_t>()(k.z) << 1));
    }
};

struct World;
struct Region;

struct Chunk {
    Region& region;
    int cx, cz;
    McChunk* enkl_chunk = nullptr;
    ChunkData data = {};
    std::mutex mesh_lock;
    std::shared_ptr<ChunkMesh> mesh;

    Chunk(Region&, int x, int z);
    Chunk(const Chunk&) = delete;
    ~Chunk();
};

struct Region {
    World& world;
    int rx, rz;
    McRegion* enkl_region = nullptr;
    bool loaded = false;
    bool unloaded = false;
    std::unordered_map<Int2, std::unique_ptr<Chunk>> chunks;

    Region(World&, int rx, int rz);
    Region(const Region&) = delete;
    ~Region();

    Chunk* get_chunk(unsigned rcx, unsigned rcz);
protected:
    Chunk* load_chunk(int cx, int cz);
    void unload_chunk(Chunk*);
    friend World;
};

struct World {
    Enkl_Allocator allocator;
    McWorld* enkl_world;
    std::unordered_map<Int2, std::unique_ptr<Region>> regions;

    explicit World(const char*);
    World(const World&) = delete;
    ~World();

    Chunk* load_chunk(int x, int y);
    void unload_chunk(Chunk*);
    Chunk* get_loaded_chunk(int x, int z);
    std::vector<Chunk*> loaded_chunks();
private:
    Region* get_loaded_region(int rx, int rz);
    Region* load_region(int rx, int rz);
    void unload_region(Region*);

    friend Region;
};

//WorldChunk world[WORLD_SIZE][WORLD_SIZE];

#endif
