#pragma once

#include "navigation/navigation_export.h"

#include <DetourNavMesh.h>
#include <DetourNavMeshQuery.h>
#include <DetourTileCache.h>

#include <array>
#include <memory>
#include <string>
#include <vector>

namespace slg::navigation {

/**
 * 表示导航空间中的三维位置。
 * 用于 FindStraightPath 等接口传递坐标。
 */
struct NavPosition {
    float x = 0.0f;  // 世界坐标 X，单位米
    float y = 0.0f;  // 世界坐标 Y（高度）
    float z = 0.0f;  // 世界坐标 Z
};

/**
 * DetourNavMeshRuntime 封装了 dtNavMesh/dtNavMeshQuery/dtTileCache 的生命周期。
 * 典型用法：
 *   DetourNavMeshRuntime runtime;
 *   runtime.InitializeStaticMesh("map.bin");
 *   std::vector<NavPosition> path;
 *   runtime.FindStraightPath(start, end, path);
 * 在 InitializeWithTileCache 成功后，还可调用 AddObstacle/RemoveObstacle 并定期 Tick 更新。
 */
class SLG_NAVIGATION_API DetourNavMeshRuntime {
public:
    /** 默认构造，仅初始化内部指针，未加载任何导航数据。 */
    DetourNavMeshRuntime();
    /** 析构时释放 Detour 对象及 tile cache 资源。 */
    ~DetourNavMeshRuntime();

    DetourNavMeshRuntime(const DetourNavMeshRuntime&) = delete;
    DetourNavMeshRuntime& operator=(const DetourNavMeshRuntime&) = delete;

    /** 支持移动语义，便于在容器或管理器间转移所有权。 */
    DetourNavMeshRuntime(DetourNavMeshRuntime&&) noexcept;
    DetourNavMeshRuntime& operator=(DetourNavMeshRuntime&&) noexcept;

    /**
     * 初始化仅包含静态数据的寻路网格。
     * @return 加载是否成功
     */
    bool InitializeStaticMesh(const std::string& nav_mesh_bin_path);

    /**
     * 初始化启用了 TileCache 的动态寻路网格。
     * 完成后可使用 AddObstacle / RemoveObstacle / Tick 管理动态阻挡。
     */
    bool InitializeWithTileCache(const std::string& tile_cache_bin_path);

    /**
     * 计算起点到终点的直线路径（Detour findStraightPath）。
     * 结果写入 path，单位为米。返回 false 表示寻路失败。
     */
    bool FindStraightPath(const NavPosition& start,
                          const NavPosition& end,
                          std::vector<NavPosition>& path);

    /**
     * 检查一条射线是否被阻挡，常用于判断最终停留点是否可达。
     */
    bool CheckIdlePosition(const NavPosition& from, const NavPosition& to) const;

    /**
     * 新增圆形动态阻挡，delay_update=true 表示延迟刷新（可批量插入后再 Tick）。
     * reference 返回 Detour 的 obstacleRef，供 RemoveObstacle 使用。
     */
    bool AddObstacle(const NavPosition& position, float radius, unsigned int& reference, bool delay_update);

    /**
     * 移除 AddObstacle 得到的阻挡引用。
     */
    bool RemoveObstacle(unsigned int reference);

    /**
     * 驱动 TileCache 更新，需在动态阻挡变更后定期调用。
     */
    bool Tick();

    /**
     * 便于外部直接访问 dtNavMeshQuery（例如自定义查询）。
     */
    dtNavMeshQuery* GetNavMeshQuery();
    const dtNavMeshQuery* GetNavMeshQuery() const;

    /**
     * 返回底层 tile cache，只有 InitializeWithTileCache 成功后才非空。
     */
    dtTileCache* GetTileCache();
    const dtTileCache* GetTileCache() const;

private:
    static constexpr int kMaxPolygons = 256;  // findPath / raycast 使用的最大 polygon 数量
    static constexpr int kMaxStraightPathVertices = kMaxPolygons * 3;

    // 加载静态 navmesh（无 tile cache），max_nodes 为 Detour 查询节点上限。
    bool LoadStaticNavMesh(const std::string& nav_mesh_bin_path, int max_nodes);
    // 加载带 tile cache 的 navmesh，支持动态阻挡。
    bool LoadTileCacheNavMesh(const std::string& tile_cache_bin_path, int max_nodes);
    // 通过位置查找最近的 polygon，供寻路/射线测试使用。
    bool FindPolyByPosition(const float* position, dtPolyRef& reference) const;

    bool HasTileCache() const noexcept;  // 是否已加载 tile cache
    bool HasQuery() const noexcept;      // 是否已创建 navmesh query 实例

    struct DtNavMeshDeleter {
        void operator()(dtNavMesh* nav_mesh) const noexcept;
    };

    struct DtNavMeshQueryDeleter {
        void operator()(dtNavMeshQuery* query) const noexcept;
    };

    struct DtTileCacheDeleter {
        void operator()(dtTileCache* cache) const noexcept;
    };

    // Detour TileCache 的定制内存分配器 / 压缩器 / mesh 处理器。
    class TileCacheAllocator;
    class TileCacheCompressor;
    class TileCacheMeshProcess;

    // 基础 Detour 对象及其辅助组件。
    std::unique_ptr<dtNavMesh, DtNavMeshDeleter> nav_mesh_;  // 当前加载的 navmesh 数据
    std::unique_ptr<dtNavMeshQuery, DtNavMeshQueryDeleter> nav_mesh_query_;  // 查询实例（静态/动态共用）
    std::unique_ptr<dtTileCache, DtTileCacheDeleter> tile_cache_;  // 动态阻挡依赖的 tile cache
    std::unique_ptr<TileCacheAllocator> tile_cache_allocator_;  // tile cache 自定义分配器
    std::unique_ptr<TileCacheCompressor> tile_cache_compressor_;  // tile cache 压缩器（FastLZ）
    std::unique_ptr<TileCacheMeshProcess> tile_cache_mesh_process_;  // tile cache mesh 处理器

    std::unique_ptr<dtQueryFilter> filter_;  // 查询滤镜：控制可通行区域、权重。

    std::array<float, kMaxStraightPathVertices> straight_path_;  // 可复用的路径顶点坐标缓存
    std::array<unsigned char, kMaxPolygons> straight_path_flags_;  // 存储 dtNavMeshQuery 返回的 flag
    std::array<dtPolyRef, kMaxPolygons> straight_path_polys_;  // 存储 dtNavMeshQuery 返回的 polygon 引用
};

}  // namespace slg::navigation
