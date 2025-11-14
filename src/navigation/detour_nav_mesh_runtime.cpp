#include "navigation/detour_nav_mesh_runtime.h"

#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourStatus.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>

#include "navigation/nav_mesh_serialization.h"
#include "fastlz.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {

using slg::navigation::kNavMeshSetMagic;
using slg::navigation::kNavMeshSetVersion;
using slg::navigation::kTileCacheSetMagic;
using slg::navigation::kTileCacheSetVersion;
using slg::navigation::NavMeshSetHeader;
using slg::navigation::NavMeshTileHeader;
using slg::navigation::TileCacheSetHeader;
using slg::navigation::TileCacheTileHeader;
constexpr int kTileCacheAllocatorCapacity = 12800000;

enum SamplePolyAreas {
    kSamplePolyAreaGround,
    kSamplePolyAreaWater,
    kSamplePolyAreaRoad,
    kSamplePolyAreaDoor,
    kSamplePolyAreaGrass,
    kSamplePolyAreaJump
};

enum SamplePolyFlags {
    kSamplePolyFlagWalk = 0x01,
    kSamplePolyFlagSwim = 0x02,
    kSamplePolyFlagDoor = 0x04,
    kSamplePolyFlagJump = 0x08,
    kSamplePolyFlagDisabled = 0x10,
    kSamplePolyFlagAll = 0xffff
};

const float kDefaultHalfExtents[3] = {1.0f, 1.0f, 1.0f};

template <typename T>
bool ReadStruct(std::FILE* file, T& output) {
    return std::fread(&output, sizeof(T), 1, file) == 1;
}

class TileCacheAllocator : public dtTileCacheAlloc {
public:
    explicit TileCacheAllocator(size_t capacity)
        : buffer_(nullptr), capacity_(0), top_(0), high_(0) {
        Resize(capacity);
    }

    ~TileCacheAllocator() override {
        dtFree(buffer_);
        buffer_ = nullptr;
    }

    void reset() override {
        high_ = std::max(high_, top_);
        top_ = 0;
    }

    void* alloc(size_t size) override {
        if (!buffer_) {
            return nullptr;
        }
        if (top_ + size > capacity_) {
            return nullptr;
        }
        unsigned char* memory = buffer_ + top_;
        top_ += size;
        return memory;
    }

    void free(void*) override {}

private:
    void Resize(size_t capacity) {
        if (buffer_) {
            dtFree(buffer_);
            buffer_ = nullptr;
        }
        buffer_ = static_cast<unsigned char*>(dtAlloc(capacity, DT_ALLOC_PERM));
        capacity_ = capacity;
        top_ = 0;
        high_ = 0;
    }

    unsigned char* buffer_;
    size_t capacity_;
    size_t top_;
    size_t high_;
};

class TileCacheCompressor : public dtTileCacheCompressor {
public:
    int maxCompressedSize(int buffer_size) override {
        return static_cast<int>(buffer_size * 1.05f);
    }

    dtStatus compress(const unsigned char* buffer,
                      int buffer_size,
                      unsigned char* compressed,
                      int /*max_compressed_size*/,
                      int* compressed_size) override {
        *compressed_size = fastlz_compress(buffer, buffer_size, compressed);
        return *compressed_size == 0 ? DT_FAILURE : DT_SUCCESS;
    }

    dtStatus decompress(const unsigned char* compressed,
                        int compressed_size,
                        unsigned char* buffer,
                        int max_buffer_size,
                        int* buffer_size) override {
        *buffer_size = fastlz_decompress(compressed, compressed_size, buffer, max_buffer_size);
        return *buffer_size < 0 ? DT_FAILURE : DT_SUCCESS;
    }
};

class TileCacheMeshProcess : public dtTileCacheMeshProcess {
public:
    void process(dtNavMeshCreateParams* params,
                 unsigned char* poly_areas,
                 unsigned short* poly_flags) override {
        for (int i = 0; i < params->polyCount; ++i) {
            if (poly_areas[i] == DT_TILECACHE_WALKABLE_AREA) {
                poly_areas[i] = kSamplePolyAreaGround;
            }

            if (poly_areas[i] == kSamplePolyAreaGround ||
                poly_areas[i] == kSamplePolyAreaGrass ||
                poly_areas[i] == kSamplePolyAreaRoad) {
                poly_flags[i] = kSamplePolyFlagWalk;
            } else if (poly_areas[i] == kSamplePolyAreaWater) {
                poly_flags[i] = kSamplePolyFlagSwim;
            } else if (poly_areas[i] == kSamplePolyAreaDoor) {
                poly_flags[i] = kSamplePolyFlagWalk | kSamplePolyFlagDoor;
            }
        }

        params->offMeshConVerts = off_mesh_connection_verts_.data();
        params->offMeshConRad = off_mesh_connection_radii_.data();
        params->offMeshConDir = off_mesh_connection_dirs_.data();
        params->offMeshConAreas = off_mesh_connection_areas_.data();
        params->offMeshConFlags = off_mesh_connection_flags_.data();
        params->offMeshConUserID = off_mesh_connection_ids_.data();
        params->offMeshConCount = static_cast<int>(off_mesh_connection_count_);
    }

private:
    static constexpr int kMaxOffMeshConnections = 256;
    std::array<float, kMaxOffMeshConnections * 3 * 2> off_mesh_connection_verts_{};
    std::array<float, kMaxOffMeshConnections> off_mesh_connection_radii_{};
    std::array<unsigned char, kMaxOffMeshConnections> off_mesh_connection_dirs_{};
    std::array<unsigned char, kMaxOffMeshConnections> off_mesh_connection_areas_{};
    std::array<unsigned short, kMaxOffMeshConnections> off_mesh_connection_flags_{};
    std::array<unsigned int, kMaxOffMeshConnections> off_mesh_connection_ids_{};
    size_t off_mesh_connection_count_ = 0;
};

bool AddTileToCache(dtTileCache* cache,
                    const TileCacheTileHeader& header,
                    std::FILE* file,
                    dtNavMesh* nav_mesh) {
    if (!header.tile_ref || !header.data_size) {
        return true;
    }

    unsigned char* data = static_cast<unsigned char*>(dtAlloc(header.data_size, DT_ALLOC_PERM));
    if (!data) {
        return false;
    }
    std::memset(data, 0, header.data_size);
    if (std::fread(data, header.data_size, 1, file) != 1) {
        dtFree(data);
        return false;
    }

    dtCompressedTileRef tile = 0;
    dtStatus status = cache->addTile(data, header.data_size, DT_COMPRESSEDTILE_FREE_DATA, &tile);
    if (dtStatusFailed(status)) {
        dtFree(data);
        return true;
    }

    if (tile) {
        cache->buildNavMeshTile(tile, nav_mesh);
    }
    return true;
}

}  // namespace

namespace slg::navigation {

void DetourNavMeshRuntime::DtNavMeshDeleter::operator()(dtNavMesh* nav_mesh) const noexcept {
    if (nav_mesh != nullptr) {
        dtFreeNavMesh(nav_mesh);
    }
}

void DetourNavMeshRuntime::DtNavMeshQueryDeleter::operator()(dtNavMeshQuery* query) const noexcept {
    if (query != nullptr) {
        dtFreeNavMeshQuery(query);
    }
}

void DetourNavMeshRuntime::DtTileCacheDeleter::operator()(dtTileCache* cache) const noexcept {
    if (cache != nullptr) {
        dtFreeTileCache(cache);
    }
}

class DetourNavMeshRuntime::TileCacheAllocator : public ::TileCacheAllocator {
public:
    TileCacheAllocator() : ::TileCacheAllocator(kTileCacheAllocatorCapacity) {}
};

class DetourNavMeshRuntime::TileCacheCompressor : public ::TileCacheCompressor {};

class DetourNavMeshRuntime::TileCacheMeshProcess : public ::TileCacheMeshProcess {};

DetourNavMeshRuntime::DetourNavMeshRuntime() = default;
DetourNavMeshRuntime::~DetourNavMeshRuntime() = default;

DetourNavMeshRuntime::DetourNavMeshRuntime(DetourNavMeshRuntime&&) noexcept = default;
DetourNavMeshRuntime& DetourNavMeshRuntime::operator=(DetourNavMeshRuntime&&) noexcept = default;

bool DetourNavMeshRuntime::InitializeStaticMesh(const std::string& nav_mesh_bin_path) {
    return LoadStaticNavMesh(nav_mesh_bin_path, 2048);
}

bool DetourNavMeshRuntime::InitializeWithTileCache(const std::string& tile_cache_bin_path) {
    return LoadTileCacheNavMesh(tile_cache_bin_path, 4096);
}

bool DetourNavMeshRuntime::FindStraightPath(const NavPosition& start,
                                            const NavPosition& end,
                                            std::vector<NavPosition>& path) {
    if (!HasQuery() || !filter_) {
        return false;
    }

    float start_pos[3] = {start.x, start.y, start.z};
    float end_pos[3] = {end.x, end.y, end.z};
    dtPolyRef start_ref = 0;
    dtPolyRef end_ref = 0;

    if (!FindPolyByPosition(start_pos, start_ref) || !FindPolyByPosition(end_pos, end_ref)) {
        return false;
    }

    std::array<dtPolyRef, kMaxPolygons> polys{};
    int poly_count = 0;
    dtStatus status = nav_mesh_query_->findPath(start_ref,
                                                end_ref,
                                                start_pos,
                                                end_pos,
                                                filter_.get(),
                                                polys.data(),
                                                &poly_count,
                                                kMaxPolygons);
    if (dtStatusFailed(status) || poly_count <= 0) {
        return false;
    }

    float actual_end[3];
    dtVcopy(actual_end, end_pos);
    if (polys[poly_count - 1] != end_ref) {
        nav_mesh_query_->closestPointOnPoly(polys[poly_count - 1], end_pos, actual_end, nullptr);
    }

    int straight_path_count = 0;
    status = nav_mesh_query_->findStraightPath(start_pos,
                                               actual_end,
                                               polys.data(),
                                               poly_count,
                                               straight_path_.data(),
                                               straight_path_flags_.data(),
                                               straight_path_polys_.data(),
                                               &straight_path_count,
                                               kMaxPolygons);
    if (dtStatusFailed(status) || straight_path_count <= 0) {
        return false;
    }

    path.clear();
    path.reserve(static_cast<size_t>(straight_path_count));
    for (int i = 0; i < straight_path_count; ++i) {
        const int index = i * 3;
        path.push_back(NavPosition{
            straight_path_[index],
            straight_path_[index + 1],
            straight_path_[index + 2],
        });
    }

    return true;
}

bool DetourNavMeshRuntime::CheckIdlePosition(const NavPosition& from, const NavPosition& to) const {
    if (!HasQuery() || !filter_) {
        return false;
    }

    float start_pos[3] = {from.x, from.y, from.z};
    float end_pos[3] = {to.x, to.y, to.z};
    dtPolyRef start_ref = 0;
    if (!FindPolyByPosition(start_pos, start_ref)) {
        return false;
    }

    float hit_fraction = 0.0f;
    std::array<dtPolyRef, kMaxPolygons> polys{};
    dtStatus status = nav_mesh_query_->raycast(start_ref,
                                               start_pos,
                                               end_pos,
                                               filter_.get(),
                                               &hit_fraction,
                                               nullptr,
                                               polys.data(),
                                               nullptr,
                                               kMaxPolygons);
    if (!dtStatusSucceed(status)) {
        return false;
    }

    return hit_fraction == FLT_MAX;
}

bool DetourNavMeshRuntime::AddObstacle(const NavPosition& position,
                                       float radius,
                                       unsigned int& reference,
                                       bool delay_update) {
    if (!HasTileCache()) {
        return false;
    }

    float pos[3] = {position.x, position.y, position.z};
    dtObstacleRef obstacle_ref = 0;
    dtStatus status = tile_cache_->addObstacle(pos, radius, 10.0f, &obstacle_ref);
    bool success = dtStatusSucceed(status);
    if (delay_update) {
        if (!success && dtStatusDetail(status, DT_BUFFER_TOO_SMALL)) {
            if (Tick()) {
                status = tile_cache_->addObstacle(pos, radius, 10.0f, &obstacle_ref);
                success = dtStatusSucceed(status);
            }
        }
    } else if (success) {
        success = Tick();
    }

    if (success) {
        reference = static_cast<unsigned int>(obstacle_ref);
    }

    return success;
}

bool DetourNavMeshRuntime::RemoveObstacle(unsigned int reference) {
    if (!HasTileCache()) {
        return false;
    }
    dtStatus status = tile_cache_->removeObstacle(static_cast<dtObstacleRef>(reference));
    if (!dtStatusSucceed(status)) {
        return false;
    }
    return Tick();
}

bool DetourNavMeshRuntime::Tick() {
    if (!HasTileCache() || !HasQuery()) {
        return false;
    }

    dtNavMesh* nav_mesh = const_cast<dtNavMesh*>(nav_mesh_query_->getAttachedNavMesh());
    if (!nav_mesh) {
        return false;
    }

    bool up_to_date = false;
    while (!up_to_date) {
        dtStatus status = tile_cache_->update(0.0f, nav_mesh, &up_to_date);
        if (!dtStatusSucceed(status)) {
            return false;
        }
    }
    return true;
}

dtNavMeshQuery* DetourNavMeshRuntime::GetNavMeshQuery() {
    return nav_mesh_query_.get();
}

const dtNavMeshQuery* DetourNavMeshRuntime::GetNavMeshQuery() const {
    return nav_mesh_query_.get();
}

dtTileCache* DetourNavMeshRuntime::GetTileCache() {
    return tile_cache_.get();
}

const dtTileCache* DetourNavMeshRuntime::GetTileCache() const {
    return tile_cache_.get();
}

bool DetourNavMeshRuntime::LoadStaticNavMesh(const std::string& nav_mesh_bin_path, int max_nodes) {
    nav_mesh_.reset();
    tile_cache_.reset();

    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::fopen(nav_mesh_bin_path.c_str(), "rb"), &std::fclose);
    if (!file) {
        return false;
    }

    NavMeshSetHeader header{};
    if (!ReadStruct(file.get(), header)) {
        return false;
    }
    if (header.magic != kNavMeshSetMagic || header.version != kNavMeshSetVersion) {
        return false;
    }

    std::unique_ptr<dtNavMesh, DtNavMeshDeleter> nav_mesh(dtAllocNavMesh());
    if (!nav_mesh) {
        return false;
    }
    if (dtStatusFailed(nav_mesh->init(&header.params))) {
        return false;
    }

    for (int i = 0; i < header.tile_count; ++i) {
        NavMeshTileHeader tile_header{};
        if (!ReadStruct(file.get(), tile_header)) {
            return false;
        }
        if (!tile_header.tile_ref || !tile_header.data_size) {
            break;
        }

        unsigned char* data = static_cast<unsigned char*>(dtAlloc(tile_header.data_size, DT_ALLOC_PERM));
        if (!data) {
            return false;
        }
        std::memset(data, 0, tile_header.data_size);
        if (std::fread(data, tile_header.data_size, 1, file.get()) != 1) {
            dtFree(data);
            return false;
        }

        nav_mesh->addTile(data, tile_header.data_size, DT_TILE_FREE_DATA, tile_header.tile_ref, 0);
    }

    nav_mesh_ = std::move(nav_mesh);
    if (!nav_mesh_query_) {
        nav_mesh_query_.reset(dtAllocNavMeshQuery());
    }
    if (!nav_mesh_query_) {
        nav_mesh_.reset();
        return false;
    }

    if (dtStatusFailed(nav_mesh_query_->init(nav_mesh_.get(), max_nodes))) {
        nav_mesh_.reset();
        return false;
    }

    if (!filter_) {
        filter_ = std::make_unique<dtQueryFilter>();
    }

    filter_->setIncludeFlags(kSamplePolyFlagAll ^ kSamplePolyFlagDisabled);
    filter_->setExcludeFlags(0);
    filter_->setAreaCost(kSamplePolyAreaGround, 1.0f);
    filter_->setAreaCost(kSamplePolyAreaWater, 10.0f);
    filter_->setAreaCost(kSamplePolyAreaRoad, 1.0f);
    filter_->setAreaCost(kSamplePolyAreaDoor, 1.0f);
    filter_->setAreaCost(kSamplePolyAreaGrass, 2.0f);
    filter_->setAreaCost(kSamplePolyAreaJump, 1.5f);

    return true;
}

bool DetourNavMeshRuntime::LoadTileCacheNavMesh(const std::string& tile_cache_bin_path, int max_nodes) {
    nav_mesh_.reset();
    tile_cache_.reset();

    std::unique_ptr<std::FILE, decltype(&std::fclose)> file(std::fopen(tile_cache_bin_path.c_str(), "rb"), &std::fclose);
    if (!file) {
        return false;
    }

    TileCacheSetHeader header{};
    if (!ReadStruct(file.get(), header)) {
        return false;
    }
    if (header.magic != kTileCacheSetMagic || header.version != kTileCacheSetVersion) {
        return false;
    }

    std::unique_ptr<dtNavMesh, DtNavMeshDeleter> nav_mesh(dtAllocNavMesh());
    if (!nav_mesh) {
        return false;
    }
    if (dtStatusFailed(nav_mesh->init(&header.mesh_params))) {
        return false;
    }

    std::unique_ptr<dtTileCache, DtTileCacheDeleter> cache(dtAllocTileCache());
    if (!cache) {
        return false;
    }

    tile_cache_allocator_ = std::make_unique<TileCacheAllocator>();
    tile_cache_compressor_ = std::make_unique<TileCacheCompressor>();
    tile_cache_mesh_process_ = std::make_unique<TileCacheMeshProcess>();

    dtStatus status = cache->init(&header.cache_params,
                                  tile_cache_allocator_.get(),
                                  tile_cache_compressor_.get(),
                                  tile_cache_mesh_process_.get());
    if (dtStatusFailed(status)) {
        return false;
    }

    for (int i = 0; i < header.tile_count; ++i) {
        TileCacheTileHeader tile_header{};
        if (!ReadStruct(file.get(), tile_header)) {
            return false;
        }
        if (!tile_header.tile_ref || !tile_header.data_size) {
            break;
        }

        if (!AddTileToCache(cache.get(), tile_header, file.get(), nav_mesh.get())) {
            return false;
        }
    }

    nav_mesh_ = std::move(nav_mesh);
    tile_cache_ = std::move(cache);

    if (!nav_mesh_query_) {
        nav_mesh_query_.reset(dtAllocNavMeshQuery());
    }
    if (!nav_mesh_query_) {
        nav_mesh_.reset();
        tile_cache_.reset();
        return false;
    }

    if (dtStatusFailed(nav_mesh_query_->init(nav_mesh_.get(), max_nodes))) {
        nav_mesh_.reset();
        tile_cache_.reset();
        return false;
    }

    if (!filter_) {
        filter_ = std::make_unique<dtQueryFilter>();
    }

    filter_->setIncludeFlags(kSamplePolyFlagAll ^ kSamplePolyFlagDisabled);
    filter_->setExcludeFlags(0);
    filter_->setAreaCost(kSamplePolyAreaGround, 1.0f);
    filter_->setAreaCost(kSamplePolyAreaWater, 10.0f);
    filter_->setAreaCost(kSamplePolyAreaRoad, 1.0f);
    filter_->setAreaCost(kSamplePolyAreaDoor, 1.0f);
    filter_->setAreaCost(kSamplePolyAreaGrass, 2.0f);
    filter_->setAreaCost(kSamplePolyAreaJump, 1.5f);

    return true;
}

bool DetourNavMeshRuntime::FindPolyByPosition(const float* position, dtPolyRef& reference) const {
    if (!HasQuery() || !filter_) {
        return false;
    }

    dtPolyRef ref = 0;
    dtStatus status = nav_mesh_query_->findNearestPoly(position, kDefaultHalfExtents, filter_.get(), &ref, nullptr);
    if (!dtStatusSucceed(status)) {
        return false;
    }

    reference = ref;
    return true;
}

bool DetourNavMeshRuntime::HasTileCache() const noexcept {
    return static_cast<bool>(tile_cache_);
}

bool DetourNavMeshRuntime::HasQuery() const noexcept {
    return static_cast<bool>(nav_mesh_query_);
}

}  // namespace slg::navigation
