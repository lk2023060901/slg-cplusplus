#pragma once

#include <DetourNavMesh.h>
#include <DetourTileCache.h>

namespace slg::navigation {

/// 公共的 navmesh/tile cache 二进制文件头定义，供构建与运行时共享。

constexpr int kNavMeshSetMagic = 'M' << 24 | 'S' << 16 | 'E' << 8 | 'T';
constexpr int kNavMeshSetVersion = 1;

/// NavMesh 二进制的文件头，记录整体 tile 数与 dtNavMeshParams。
struct NavMeshSetHeader {
    int magic = 0;
    int version = 0;
    int tile_count = 0;
    dtNavMeshParams params{};
};

/// 每个 tile 数据块前的头部，存储 tile 引用及数据尺寸。
struct NavMeshTileHeader {
    dtTileRef tile_ref = 0;
    int data_size = 0;
};

constexpr int kTileCacheSetMagic = 'T' << 24 | 'S' << 16 | 'E' << 8 | 'T';
constexpr int kTileCacheSetVersion = 1;

/// TileCache 二进制的文件头，包含 tile cache/navmesh 参数以及 tile 数量。
struct TileCacheSetHeader {
    int magic = 0;
    int version = 0;
    int tile_count = 0;
    dtNavMeshParams mesh_params{};
    dtTileCacheParams cache_params{};
};

/// TileCache 每个 tile 的头部，指示压缩数据大小与引用。
struct TileCacheTileHeader {
    dtCompressedTileRef tile_ref = 0;
    int data_size = 0;
};

}  // namespace slg::navigation
