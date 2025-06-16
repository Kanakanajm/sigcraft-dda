#include "world.h"

World::World(const char* filename) {
    allocator = enkl_get_malloc_free_allocator();
    enkl_world = cunk_open_mcworld(filename, &allocator);
}

World::~World() {
    for (auto& c : loaded_chunks()) {
        unload_chunk(c.get());
    }
    while (true) {
        auto locked = regions.lock();
        if (locked->size() == 0) {
            break;
        }
        fprintf(stderr, "Some chunks are still loaded, waiting...");
        sleep(1);
    }
    cunk_close_mcworld(enkl_world);
}

std::vector<std::shared_ptr<Chunk>> World::loaded_chunks() {
    std::vector<std::shared_ptr<Chunk>> list;
    auto guard = regions.lock();
    for (auto& [_, region] : *guard) {
        assert(region);
        for (auto& [_, chunk] : region->chunks) {
            list.push_back(chunk);
        }
    }
    return list;
}

template<typename Guard>
std::shared_ptr<Region> World::get_loaded_region(Guard& guard, int rx, int rz) {
    if (auto found = guard->find({ rx, rz}); found != guard->end()) {
        return found->second;
    }
    return nullptr;
}

template<typename Guard>
std::shared_ptr<Region> World::load_region(Guard& locked_regions, int rx, int rz) {
    assert(!get_loaded_region(locked_regions, rx, rz));
    Int2 pos = {rx, rz};
    auto& r = (*locked_regions)[pos] = std::make_shared<Region>(*this, rx, rz);
    assert(!r->loaded);
    assert(!r->unloaded);
    r->loaded = true;
    return r;
}

static int to_region_coordinate(int c) {
    if (c < 0)
        return (c - 31) / 32;
    return c / 32;
}

static std::tuple<int, int> to_region_coordinates(int cx, int cz) {
    int rx = to_region_coordinate(cx);
    int rz = to_region_coordinate(cz);
    return { rx, rz };
}

std::shared_ptr<Chunk> World::get_loaded_chunk(int cx, int cz) {
    auto [rx, rz] = to_region_coordinates(cx, cz);
    auto guard = regions.lock();
    auto found = get_loaded_region(guard, rx, rz);
    if (found)
        return found->get_chunk(cx & 0x1f, cz & 0x1f);
    return nullptr;
}

std::shared_ptr<Chunk> World::load_chunk(int cx, int cz) {
    auto [rx, rz] = to_region_coordinates(cx, cz);
    auto guard = regions.lock_mut();
    std::shared_ptr<Region> r = get_loaded_region(guard, rx, rz);
    if (!r)
        r = load_region(guard, rx, rz);
    return r->load_chunk(cx, cz);
}

void World::unload_chunk(Chunk* chunk) {
    Region* region = &chunk->region;
    region->unload_chunk(chunk);
}

template<typename Guard>
void World::unload_region(Guard guard, Region* region) {
    //assert(region->loaded);
    //assert(!region->unloaded);
    //region->unloaded = true;
    int rx = region->rx;
    int rz = region->rz;
    Int2 pos = {rx, rz};
    guard->erase(pos);
}

Region::Region(World& w, int rx, int rz) : world(w), rx(rx), rz(rz) {
    //printf("! %d %d\n", rx, rz);
    enkl_region = cunk_open_mcregion(w.enkl_world, rx, rz);
}

Region::~Region() {
    //printf("~ %d %d %zu\n", rx, rz, (size_t) enkl_region);
    assert(chunks.empty());
    //chunks.clear();
    if (enkl_region)
        enkl_close_region(enkl_region);
}

std::shared_ptr<Chunk> Region::get_chunk(unsigned int rcx, unsigned int rcz) {
    assert(rcx >= 0 && rcz >= 0);
    assert(rcx < 32 && rcz < 32);
    auto found = chunks.find({(int) rcx, (int) rcz});
    if (found != chunks.end())
        return found->second;
    return nullptr;
}

std::shared_ptr<Chunk> Region::load_chunk(int cx, int cz) {
    unsigned rcx = cx & 0x1f;
    unsigned rcz = cz & 0x1f;
    assert(rcx < 32 && rcz < 32);
    assert(!get_chunk(rcx, rcz));
    Int2 pos = {(int)rcx, (int)rcz};
    auto& r = chunks[pos] = std::make_shared<Chunk>(*this, cx, cz);
    return r;
}

void Region::unload_chunk(Chunk* chunk) {
    unsigned rcx = chunk->cx & 0x1f;
    unsigned rcz = chunk->cz & 0x1f;
    Int2 pos = {(int)rcx, (int)rcz};
    chunks.erase(pos);
}

Chunk::Chunk(Region& r, int cx, int cz) : region(r), cx(cx), cz(cz) {
    unsigned rcx = cx & 0x1f;
    unsigned rcz = cz & 0x1f;
    //printf("! %d %d\n", cx, cz);
    if (r.enkl_region) {
        enkl_chunk = cunk_open_mcchunk(r.enkl_region, rcx, rcz);
        if (enkl_chunk) {
            load_from_mcchunk(&data, enkl_chunk);
        }
    }
    auto users = r.users.lock_mut();
    (*users)++;
}

Chunk::~Chunk() {
    //printf("~ %d %d\n", cx, cz);
    enkl_destroy_chunk_data(&data);
    if (enkl_chunk)
        enkl_close_chunk(enkl_chunk);

    World& w = region.world;
    auto guard = w.regions.lock_mut();

    auto users = region.users.lock_mut();
    if ((*users)-- <= 0)
        w.unload_region(std::move(guard), &region);
}
