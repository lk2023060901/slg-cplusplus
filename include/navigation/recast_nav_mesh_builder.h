#pragma once

#include "navigation/navigation_export.h"

#include <memory>
#include <string>

namespace slg::navigation {

namespace detail {
class RecastBuilderContext;
}

enum class RecastPartitionType {
    Watershed,
    Monotone,
    Layers,
};

/**
 * 配置 Recast 构建参数。只有在 OBJ 资产和 Detour 运行时约束需调整时才修改。
 * 单位默认为米。
 */
struct RecastBuildSettings {
    float cell_size = 1.0f;                  ///< rcConfig::cs
    float cell_height = 1.0f;                ///< rcConfig::ch
    float agent_height = 0.5f;               ///< walkableHeight
    float agent_radius = 0.5f;               ///< walkableRadius
    float agent_max_climb = 0.5f;            ///< walkableClimb
    float agent_max_slope = 45.0f;           ///< walkableSlopeAngle
    float region_min_size = 8.0f;            ///< minRegionArea
    float region_merge_size = 20.0f;         ///< mergeRegionArea
    float edge_max_len = 12.0f;              ///< maxEdgeLen
    float edge_max_error = 1.3f;             ///< maxSimplificationError
    float detail_sample_dist = 6.0f;         ///< detailSampleDist
    float detail_sample_max_error = 1.0f;    ///< detailSampleMaxError
    float verts_per_poly = 3.0f;             ///< maxVertsPerPoly
    int tile_size = 256;                     ///< tile size in cells
    int max_tiles = 1024;                    ///< dtNavMesh::maxTiles
    int max_polys_per_tile = 4096;           ///< dtNavMesh::maxPolys
    int max_obstacles = 200000;              ///< dtTileCacheParams::maxObstacles
    bool keep_intermediate = false;          ///< 保留高度场/轮廓中间结果用于调试
    bool filter_low_hanging_obstacles = true;
    bool filter_ledge_spans = true;
    bool filter_walkable_low_height = true;
    RecastPartitionType partition_type = RecastPartitionType::Watershed;
};

/**
 * 负责将 OBJ 网格转换为 Detour 可加载的 navmesh/tile cache 二进制文件。
 * 使用流程：
 *   RecastNavMeshBuilder builder;
 *   builder.LoadMeshFromObj("map.obj");
 *   builder.BuildStaticNavMesh(settings, "map.bin");
 */
class SLG_NAVIGATION_API RecastNavMeshBuilder {
public:
    RecastNavMeshBuilder();
    ~RecastNavMeshBuilder();

    /// 读取 OBJ 文件，内部持有三角网格数据。
    bool LoadMeshFromObj(const std::string& obj_path);

    /// 根据 settings 构建静态 navmesh（二进制格式供 DetourNavMeshRuntime 使用）。
    bool BuildStaticNavMesh(const RecastBuildSettings& settings, const std::string& output_bin_path);
    /// 构建带有动态 tile cache 的 navmesh，生成的文件用于动态阻挡场景。
    bool BuildTileCacheNavMesh(const RecastBuildSettings& settings, const std::string& output_bin_path);

private:
    std::unique_ptr<detail::RecastBuilderContext> context_;
};

}  // namespace slg::navigation
