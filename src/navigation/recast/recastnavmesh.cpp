// Recast navmesh builder implementation adapted from legacy Lua bindings.
#include "navigation/recast/recastnavmesh.h"

#include <DetourCommon.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourTileCache.h>
#include <DetourTileCacheBuilder.h>
#include <Recast.h>

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "navigation/recast/BuildContext.h"
#include "navigation/recast/ChunkyTriMesh.h"
#include "navigation/recast/MeshLoaderObj.h"
#include "navigation/recast_nav_mesh_builder.h"
#include "fastlz.h"

namespace slg::navigation::detail {

struct LegacyRecastState;
struct LinearAllocator;
struct FastLZCompressor;
struct MeshProcess;

namespace {

thread_local LegacyRecastState* g_active_recast_state = nullptr;

class ScopedRecastState {
public:
    explicit ScopedRecastState(LegacyRecastState& state) : previous_(g_active_recast_state) {
        g_active_recast_state = &state;
    }
    ~ScopedRecastState() {
        g_active_recast_state = previous_;
    }
private:
    LegacyRecastState* previous_;
};

LegacyRecastState& RecastState() {
    assert(g_active_recast_state && "RecastBuilderContext scope is not active");
    return *g_active_recast_state;
}

}  // namespace

static const int MAX_CONVEXVOL_PTS = 12;

enum SamplePartitionType
{
	SAMPLE_PARTITION_WATERSHED,
	SAMPLE_PARTITION_MONOTONE,
	SAMPLE_PARTITION_LAYERS,
};

enum SamplePolyAreas
{
	SAMPLE_POLYAREA_GROUND,
	SAMPLE_POLYAREA_WATER,
	SAMPLE_POLYAREA_ROAD,
	SAMPLE_POLYAREA_DOOR,
	SAMPLE_POLYAREA_GRASS,
	SAMPLE_POLYAREA_JUMP,
};
enum SamplePolyFlags
{
	SAMPLE_POLYFLAGS_WALK		= 0x01,
	SAMPLE_POLYFLAGS_SWIM		= 0x02,
	SAMPLE_POLYFLAGS_DOOR		= 0x04,
	SAMPLE_POLYFLAGS_JUMP		= 0x08,
	SAMPLE_POLYFLAGS_DISABLED	= 0x10,
	SAMPLE_POLYFLAGS_ALL		= 0xffff
};

static const int MAX_OFFMESH_CONNECTIONS = 256;

struct LegacyRecastState {
    MeshLoaderObj* mesh_loader = nullptr;
    dtNavMesh* nav_mesh = nullptr;
    BuildContext* build_context = nullptr;

    float mesh_bounds_min[3]{};
    float mesh_bounds_max[3]{};
    float cell_size = 1.0f;
    float cell_height = 1.0f;
    float agent_max_slope = 45.0f;
    float tile_size = 256.0f;
    float max_tiles = 1024.0f;
    float max_polys_per_tile = 4096.0f;
    float last_built_tile_bounds_min[3]{};
    float last_built_tile_bounds_max[3]{};
    float agent_height = 0.5f;
    float agent_radius = 0.5f;
    float agent_max_climb = 0.5f;
    float verts_per_poly = 3.0f;
    float edge_max_len = 12.0f;
    float edge_max_error = 1.3f;
    float region_min_size = 8.0f;
    float region_merge_size = 20.0f;
    float detail_sample_dist = 6.0f;
    float detail_sample_max_error = 1.0f;
    int max_obstacles = 200000;

    rcHeightfield* solid_heightfield = nullptr;
    rcCompactHeightfield* compact_heightfield = nullptr;
    rcContourSet* contour_set = nullptr;
    rcPolyMesh* poly_mesh = nullptr;
    rcPolyMeshDetail* poly_mesh_detail = nullptr;
    rcConfig recast_config{};
    ChunkyTriMesh* chunky_mesh = nullptr;
    unsigned char* triangle_areas = nullptr;

    bool keep_intermediate_results = false;
    bool filter_low_hanging_obstacles = true;
    bool filter_ledge_spans = true;
    bool filter_walkable_low_height_spans = true;
    int partition_type = SAMPLE_PARTITION_MONOTONE;

    float off_mesh_connection_verts[MAX_OFFMESH_CONNECTIONS * 3 * 2]{};
    float off_mesh_connection_radii[MAX_OFFMESH_CONNECTIONS]{};
    unsigned char off_mesh_connection_dirs[MAX_OFFMESH_CONNECTIONS]{};
    unsigned char off_mesh_connection_areas[MAX_OFFMESH_CONNECTIONS]{};
    unsigned short off_mesh_connection_flags[MAX_OFFMESH_CONNECTIONS]{};
    unsigned int off_mesh_connection_ids[MAX_OFFMESH_CONNECTIONS]{};
    int off_mesh_connection_count = 0;

    dtTileCache* tile_cache = nullptr;
    int cache_compressed_size = 0;
    int cache_raw_size = 0;
    int cache_layer_count = 0;

    LinearAllocator* tile_cache_allocator = nullptr;
    FastLZCompressor* tile_cache_compressor = nullptr;
    MeshProcess* tile_cache_mesh_process = nullptr;
};

// 每个 tile 期望的层数（即 navmesh 楼层数量）。
static const int EXPECTED_LAYERS_PER_TILE = 4;
struct ConvexVolume
{
	float verts[MAX_CONVEXVOL_PTS*3];
	float hmin, hmax;
	int nverts;
	int area;
};
/// @name Off-Mesh connections.
///@{
///@}

struct NavMeshSetHeader
{
	int magic;
	int version;
	int numTiles;
	dtNavMeshParams params;
};

static const int NAVMESHSET_MAGIC = 'M'<<24 | 'S'<<16 | 'E'<<8 | 'T'; //'MSET';
static const int NAVMESHSET_VERSION = 1;

static const int TILECACHESET_MAGIC = 'T'<<24 | 'S'<<16 | 'E'<<8 | 'T'; //'TSET';
static const int TILECACHESET_VERSION = 1;

struct NavMeshTileHeader
{
	dtTileRef tileRef;
	int dataSize;
};

static const int MAX_LAYERS = 32;
struct TileCacheData
{
	unsigned char* data;
	int dataSize;
};

struct TileCacheSetHeader
{
	int magic;
	int version;
	int numTiles;
	dtNavMeshParams meshParams;
	dtTileCacheParams cacheParams;
};

struct TileCacheTileHeader
{
	dtCompressedTileRef tileRef;
	int dataSize;
};

static const int ALLOC_CAPATICY = 12800000;

struct LinearAllocator : public dtTileCacheAlloc
{
	unsigned char* buffer;
	size_t capacity;
	size_t top;
	size_t high;
	
	LinearAllocator(const size_t cap) : buffer(0), capacity(0), top(0), high(0)
	{
		resize(cap);
	}
	
	~LinearAllocator()
	{
		dtFree(buffer);
	}

	void resize(const size_t cap)
	{
		if (buffer) dtFree(buffer);
		buffer = (unsigned char*)dtAlloc(cap, DT_ALLOC_PERM);
		capacity = cap;
	}
	
	virtual void reset()
	{
		high = dtMax(high, top);
		top = 0;
	}
	
	virtual void* alloc(const size_t size)
	{
		if (!buffer)
			return 0;
		if (top+size > capacity)
			return 0;
		unsigned char* mem = &buffer[top];
		top += size;
		return mem;
	}
	
	virtual void free(void* /*ptr*/)
	{
		// Empty
	}
};

struct FastLZCompressor : public dtTileCacheCompressor
{
	virtual int maxCompressedSize(const int bufferSize)
	{
		return (int)(bufferSize* 1.05f);
	}
	
	virtual dtStatus compress(const unsigned char* buffer, const int bufferSize,
							  unsigned char* compressed, const int /*maxCompressedSize*/, int* compressedSize)
	{
		*compressedSize = fastlz_compress((const void *const)buffer, bufferSize, compressed);
		return DT_SUCCESS;
	}
	
	virtual dtStatus decompress(const unsigned char* compressed, const int compressedSize,
								unsigned char* buffer, const int maxBufferSize, int* bufferSize)
	{
		*bufferSize = fastlz_decompress(compressed, compressedSize, buffer, maxBufferSize);
		return *bufferSize < 0 ? DT_FAILURE : DT_SUCCESS;
	}
};


struct MeshProcess : public dtTileCacheMeshProcess
{
	virtual void process(struct dtNavMeshCreateParams* params,
						 unsigned char* polyAreas, unsigned short* polyFlags)
	{
		// Update poly flags from areas.
		for (int i = 0; i < params->polyCount; ++i)
		{
			if (polyAreas[i] == DT_TILECACHE_WALKABLE_AREA)
				polyAreas[i] = SAMPLE_POLYAREA_GROUND;

			if (polyAreas[i] == SAMPLE_POLYAREA_GROUND ||
				polyAreas[i] == SAMPLE_POLYAREA_GRASS ||
				polyAreas[i] == SAMPLE_POLYAREA_ROAD)
			{
				polyFlags[i] = SAMPLE_POLYFLAGS_WALK;
			}
			else if (polyAreas[i] == SAMPLE_POLYAREA_WATER)
			{
				polyFlags[i] = SAMPLE_POLYFLAGS_SWIM;
			}
			else if (polyAreas[i] == SAMPLE_POLYAREA_DOOR)
			{
				polyFlags[i] = SAMPLE_POLYFLAGS_WALK | SAMPLE_POLYFLAGS_DOOR;
			}
		}

		// Pass in off-mesh connections.
		params->offMeshConVerts = RecastState().off_mesh_connection_verts;
		params->offMeshConRad = RecastState().off_mesh_connection_radii;
		params->offMeshConDir = RecastState().off_mesh_connection_dirs;
		params->offMeshConAreas = RecastState().off_mesh_connection_areas;
		params->offMeshConFlags = RecastState().off_mesh_connection_flags;
		params->offMeshConUserID = RecastState().off_mesh_connection_ids;
		params->offMeshConCount = RecastState().off_mesh_connection_count;	
	}
};

// 加载obj
/// 加载 OBJ 文件并生成用于 Recast 的三角网格。
bool LoadMeshObj( const char* path )
{
	const std::string filepath(path);
    RecastState().mesh_loader = new MeshLoaderObj;
	if (!RecastState().mesh_loader)
	{
		printf("loadMesh: Out of memory 'RecastState().mesh_loader'.\n");
		return false;
	}
	if (!RecastState().mesh_loader->load(filepath))
	{
		printf("buildTiledNavigation: Could not load '%s'\n", filepath.c_str());
		return false;
	}

	rcCalcBounds(RecastState().mesh_loader->getVerts(), RecastState().mesh_loader->getVertCount(), RecastState().mesh_bounds_min, RecastState().mesh_bounds_max);

	RecastState().chunky_mesh = new ChunkyTriMesh;
	if (!RecastState().chunky_mesh)
	{
		printf("buildTiledNavigation: Out of memory 'RecastState().chunky_mesh'.\n");
		return false;
	}
	if (!CreateChunkyTriMesh(RecastState().mesh_loader->getVerts(), RecastState().mesh_loader->getTris(), RecastState().mesh_loader->getTriCount(), 256, RecastState().chunky_mesh))
	{
		printf("buildTiledNavigation: Failed to build chunky mesh.\n");
		return false;
	}

	printf("Verts:%f, VertCount:%d, Tris:%d, TriCount:%d\n", *RecastState().mesh_loader->getVerts(), RecastState().mesh_loader->getVertCount(), *RecastState().mesh_loader->getTris(), RecastState().mesh_loader->getTriCount());

	return true;
}


unsigned char* buildTileMesh(const int tx, const int ty, const float* bmin, const float* bmax, int& dataSize)
{
	if (!RecastState().mesh_loader)
	{
		printf("buildNavigation: Input mesh is not specified.\n");
		return 0;
	}
	
	const float* verts = RecastState().mesh_loader->getVerts();
	const int nverts = RecastState().mesh_loader->getVertCount();
	const int ntris = RecastState().mesh_loader->getTriCount();
	const ChunkyTriMesh* chunkyMesh = RecastState().chunky_mesh;
		
	// Init build configuration
	memset(&RecastState().recast_config, 0, sizeof(RecastState().recast_config));
	RecastState().recast_config.cs = RecastState().cell_size;
	RecastState().recast_config.ch = RecastState().cell_height;
	RecastState().recast_config.walkableSlopeAngle = RecastState().agent_max_slope;
	RecastState().recast_config.walkableHeight = (int)ceilf(RecastState().agent_height / RecastState().recast_config.ch);
	RecastState().recast_config.walkableClimb = (int)floorf(RecastState().agent_max_climb / RecastState().recast_config.ch);
	RecastState().recast_config.walkableRadius = (int)ceilf(RecastState().agent_radius / RecastState().recast_config.cs);
	RecastState().recast_config.maxEdgeLen = (int)(RecastState().edge_max_len / RecastState().cell_size);
	RecastState().recast_config.maxSimplificationError = RecastState().edge_max_error;
	RecastState().recast_config.minRegionArea = (int)rcSqr(RecastState().region_min_size);		// Note: area = size*size
	RecastState().recast_config.mergeRegionArea = (int)rcSqr(RecastState().region_merge_size);	// Note: area = size*size
	RecastState().recast_config.maxVertsPerPoly = (int)RecastState().verts_per_poly;
	RecastState().recast_config.tileSize = (int)RecastState().tile_size;
	RecastState().recast_config.borderSize = RecastState().recast_config.walkableRadius; // Reserve enough padding.
	RecastState().recast_config.width = RecastState().recast_config.tileSize + RecastState().recast_config.borderSize*2;
	RecastState().recast_config.height = RecastState().recast_config.tileSize + RecastState().recast_config.borderSize*2;
	RecastState().recast_config.detailSampleDist = RecastState().detail_sample_dist < 0.9f ? 0 : RecastState().cell_size * RecastState().detail_sample_dist;
	RecastState().recast_config.detailSampleMaxError = RecastState().cell_height * RecastState().detail_sample_max_error;
	
	// Expand the heighfield bounding box by border size to find the extents of geometry we need to build this tile.
	//
	// This is done in order to make sure that the navmesh tiles connect correctly at the borders,
	// and the obstacles close to the border work correctly with the dilation process.
	// No polygons (or contours) will be created on the border area.
	//
	// IMPORTANT!
	//
	//   :''''''''':
	//   : +-----+ :
	//   : |     | :
	//   : |     |<--- tile to build
	//   : |     | :  
	//   : +-----+ :<-- geometry needed
	//   :.........:
	//
	// You should use this bounding box to query your input geometry.
	//
	// For example if you build a navmesh for terrain, and want the navmesh tiles to match the terrain tile size
	// you will need to pass in data from neighbour terrain tiles too! In a simple case, just pass in all the 8 neighbours,
	// or use the bounding box below to only pass in a sliver of each of the 8 neighbours.
	rcVcopy(RecastState().recast_config.bmin, bmin);
	rcVcopy(RecastState().recast_config.bmax, bmax);
	RecastState().recast_config.bmin[0] -= RecastState().recast_config.borderSize*RecastState().recast_config.cs;
	RecastState().recast_config.bmin[2] -= RecastState().recast_config.borderSize*RecastState().recast_config.cs;
	RecastState().recast_config.bmax[0] += RecastState().recast_config.borderSize*RecastState().recast_config.cs;
	RecastState().recast_config.bmax[2] += RecastState().recast_config.borderSize*RecastState().recast_config.cs;
	
	
	printf("Building navigation: x-%d y-%d\n", tx, ty);
	printf(" - %d x %d cells\n", RecastState().recast_config.width, RecastState().recast_config.height);
	printf(" - %.1fK verts, %.1fK tris\n", nverts/1000.0f, ntris/1000.0f);
	
	// Allocate voxel heightfield where we rasterize our input data to.
	RecastState().solid_heightfield = rcAllocHeightfield();
	if (!RecastState().solid_heightfield)
	{
		printf("buildNavigation: Out of memory 'solid'.\n");
		return 0;
	}
	if (!rcCreateHeightfield(RecastState().build_context, *RecastState().solid_heightfield, RecastState().recast_config.width, RecastState().recast_config.height, RecastState().recast_config.bmin, RecastState().recast_config.bmax, RecastState().recast_config.cs, RecastState().recast_config.ch))
	{
		printf("buildNavigation: Could not create solid heightfield.\n");
		return 0;
	}
	
	// Allocate array that can hold triangle flags.
	// If you have multiple meshes you need to process, allocate
	// and array which can hold the max number of triangles you need to process.
	RecastState().triangle_areas = new unsigned char[chunkyMesh->maxTrisPerChunk];
	if (!RecastState().triangle_areas)
	{
		printf("buildNavigation: Out of memory 'RecastState().triangle_areas' (%d).\n", chunkyMesh->maxTrisPerChunk);
		return 0;
	}
	
	float tbmin[2], tbmax[2];
	tbmin[0] = RecastState().recast_config.bmin[0];
	tbmin[1] = RecastState().recast_config.bmin[2];
	tbmax[0] = RecastState().recast_config.bmax[0];
	tbmax[1] = RecastState().recast_config.bmax[2];
	int cid[512];
	const int ncid = GetChunksOverlappingRect(chunkyMesh, tbmin, tbmax, cid, 512);

	if (!ncid)
	{
		printf("buildNavigation: ncid (%d).\n", ncid);
		return 0;
	}
	
	float m_tileTriCount = 0;
	
	for (int i = 0; i < ncid; ++i)
	{
		const ChunkyTriMeshNode& node = chunkyMesh->nodes[cid[i]];
		const int* ctris = &chunkyMesh->tris[node.i*3];
		const int nctris = node.n;
		
		m_tileTriCount += nctris;
		
		memset(RecastState().triangle_areas, 0, nctris*sizeof(unsigned char));
		rcMarkWalkableTriangles(RecastState().build_context, RecastState().recast_config.walkableSlopeAngle,
								verts, nverts, ctris, nctris, RecastState().triangle_areas);
		
		if (!rcRasterizeTriangles(RecastState().build_context, verts, nverts, ctris, RecastState().triangle_areas, nctris, *RecastState().solid_heightfield, RecastState().recast_config.walkableClimb))
		{
			printf("buildNavigation: rcRasterizeTriangles error.\n");
			return 0;
		}
	}
	
	if (!RecastState().keep_intermediate_results)
	{
		delete [] RecastState().triangle_areas;
		RecastState().triangle_areas = 0;
	}
	
	// Once all geometry is rasterized, we do initial pass of filtering to
	// remove unwanted overhangs caused by the conservative rasterization
	// as well as filter spans where the character cannot possibly stand.
	if (RecastState().filter_low_hanging_obstacles)
		rcFilterLowHangingWalkableObstacles(RecastState().build_context, RecastState().recast_config.walkableClimb, *RecastState().solid_heightfield);
	if (RecastState().filter_ledge_spans)
		rcFilterLedgeSpans(RecastState().build_context, RecastState().recast_config.walkableHeight, RecastState().recast_config.walkableClimb, *RecastState().solid_heightfield);
	if (RecastState().filter_walkable_low_height_spans)
		rcFilterWalkableLowHeightSpans(RecastState().build_context, RecastState().recast_config.walkableHeight, *RecastState().solid_heightfield);
	
	// Compact the heightfield so that it is faster to handle from now on.
	// This will result more cache coherent data as well as the neighbours
	// between walkable cells will be calculated.
	RecastState().compact_heightfield = rcAllocCompactHeightfield();
	if (!RecastState().compact_heightfield)
	{
		printf("buildNavigation: Out of memory 'chf'.\n");
		return 0;
	}
	if (!rcBuildCompactHeightfield(RecastState().build_context, RecastState().recast_config.walkableHeight, RecastState().recast_config.walkableClimb, *RecastState().solid_heightfield, *RecastState().compact_heightfield))
	{
		printf("buildNavigation: Could not build compact data.\n");
		return 0;
	}
	
	if (!RecastState().keep_intermediate_results)
	{
		rcFreeHeightField(RecastState().solid_heightfield);
		RecastState().solid_heightfield = 0;
	}

	// Erode the walkable area by agent radius.
	if (!rcErodeWalkableArea(RecastState().build_context, RecastState().recast_config.walkableRadius, *RecastState().compact_heightfield))
	{
		printf("buildNavigation: Could not erode.\n");
		return 0;
	}
	
	// Partition the heightfield so that we can use simple algorithm later to triangulate the walkable areas.
	// There are 3 martitioning methods, each with some pros and cons:
	// 1) Watershed partitioning
	//   - the classic Recast partitioning
	//   - creates the nicest tessellation
	//   - usually slowest
	//   - partitions the heightfield into nice regions without holes or overlaps
	//   - the are some corner cases where this method creates produces holes and overlaps
	//      - holes may appear when a small obstacles is close to large open area (triangulation can handle this)
	//      - overlaps may occur if you have narrow spiral corridors (i.e stairs), this make triangulation to fail
	//   * generally the best choice if you precompute the nacmesh, use this if you have large open areas
	// 2) Monotone partioning
	//   - fastest
	//   - partitions the heightfield into regions without holes and overlaps (guaranteed)
	//   - creates long thin polygons, which sometimes causes paths with detours
	//   * use this if you want fast navmesh generation
	// 3) Layer partitoining
	//   - quite fast
	//   - partitions the heighfield into non-overlapping regions
	//   - relies on the triangulation code to cope with holes (thus slower than monotone partitioning)
	//   - produces better triangles than monotone partitioning
	//   - does not have the corner cases of watershed partitioning
	//   - can be slow and create a bit ugly tessellation (still better than monotone)
	//     if you have large open areas with small obstacles (not a problem if you use tiles)
	//   * good choice to use for tiled navmesh with medium and small sized tiles
	
	if (RecastState().partition_type == SAMPLE_PARTITION_WATERSHED)
	{
		// Prepare for region partitioning, by calculating distance field along the walkable surface.
		if (!rcBuildDistanceField(RecastState().build_context, *RecastState().compact_heightfield))
		{
			printf("buildNavigation: Could not build distance field.\n");
			return 0;
		}
		
		// Partition the walkable surface into simple regions without holes.
		if (!rcBuildRegions(RecastState().build_context, *RecastState().compact_heightfield, RecastState().recast_config.borderSize, RecastState().recast_config.minRegionArea, RecastState().recast_config.mergeRegionArea))
		{
			printf("buildNavigation: Could not build watershed regions.\n");
			return 0;
		}
	}
	else if (RecastState().partition_type == SAMPLE_PARTITION_MONOTONE)
	{
		// Partition the walkable surface into simple regions without holes.
		// Monotone partitioning does not need distancefield.
		if (!rcBuildRegionsMonotone(RecastState().build_context, *RecastState().compact_heightfield, RecastState().recast_config.borderSize, RecastState().recast_config.minRegionArea, RecastState().recast_config.mergeRegionArea))
		{
			printf("buildNavigation: Could not build monotone regions.\n");
			return 0;
		}
	}
	else // SAMPLE_PARTITION_LAYERS
	{
		// Partition the walkable surface into simple regions without holes.
		if (!rcBuildLayerRegions(RecastState().build_context, *RecastState().compact_heightfield, RecastState().recast_config.borderSize, RecastState().recast_config.minRegionArea))
		{
			printf("buildNavigation: Could not build layer regions.\n");
			return 0;
		}
	}
	 	
	// Create contours.
	RecastState().contour_set = rcAllocContourSet();
	if (!RecastState().contour_set)
	{
		printf("buildNavigation: Out of memory 'cset'.\n");
		return 0;
	}
	if (!rcBuildContours(RecastState().build_context, *RecastState().compact_heightfield, RecastState().recast_config.maxSimplificationError, RecastState().recast_config.maxEdgeLen, *RecastState().contour_set))
	{
		printf("buildNavigation: Could not create contours.\n");
		return 0;
	}
	
	if (RecastState().contour_set->nconts == 0)
	{
		printf("buildNavigation: RecastState().contour_set->nconts == 0.\n");
		return 0;
	}
	
	// Build polygon navmesh from the contours.
	RecastState().poly_mesh = rcAllocPolyMesh();
	if (!RecastState().poly_mesh)
	{
		printf("buildNavigation: Out of memory 'pmesh'.\n");
		return 0;
	}
	if (!rcBuildPolyMesh(RecastState().build_context, *RecastState().contour_set, RecastState().recast_config.maxVertsPerPoly, *RecastState().poly_mesh))
	{
		printf("buildNavigation: Could not triangulate contours.\n");
		return 0;
	}
	
	// Build detail mesh.
	RecastState().poly_mesh_detail = rcAllocPolyMeshDetail();
	if (!RecastState().poly_mesh_detail)
	{
		printf("buildNavigation: Out of memory 'dmesh'.\n");
		return 0;
	}
	
	if (!rcBuildPolyMeshDetail(RecastState().build_context, *RecastState().poly_mesh, *RecastState().compact_heightfield,
							   RecastState().recast_config.detailSampleDist, RecastState().recast_config.detailSampleMaxError,
							   *RecastState().poly_mesh_detail))
	{
		printf("buildNavigation: Could build polymesh detail.\n");
		return 0;
	}
	
	if (!RecastState().keep_intermediate_results)
	{
		rcFreeCompactHeightfield(RecastState().compact_heightfield);
		RecastState().compact_heightfield = 0;
		rcFreeContourSet(RecastState().contour_set);
		RecastState().contour_set = 0;
	}
	
	unsigned char* navData = 0;
	int navDataSize = 0;

	if (RecastState().recast_config.maxVertsPerPoly <= DT_VERTS_PER_POLYGON)
	{
		if (RecastState().poly_mesh->nverts >= 0xffff)
		{
			// The vertex indices are ushorts, and cannot point to more than 0xffff vertices.
			printf("Too many vertices per tile %d (max: %d).\n", RecastState().poly_mesh->nverts, 0xffff);
			return 0;
		}
		
		// Update poly flags from areas.
		for (int i = 0; i < RecastState().poly_mesh->npolys; ++i)
		{
			if (RecastState().poly_mesh->areas[i] == RC_WALKABLE_AREA)
				RecastState().poly_mesh->areas[i] = SAMPLE_POLYAREA_GROUND;
			
			if (RecastState().poly_mesh->areas[i] == SAMPLE_POLYAREA_GROUND ||
				RecastState().poly_mesh->areas[i] == SAMPLE_POLYAREA_GRASS ||
				RecastState().poly_mesh->areas[i] == SAMPLE_POLYAREA_ROAD)
			{
				RecastState().poly_mesh->flags[i] = SAMPLE_POLYFLAGS_WALK;
			}
			else if (RecastState().poly_mesh->areas[i] == SAMPLE_POLYAREA_WATER)
			{
				RecastState().poly_mesh->flags[i] = SAMPLE_POLYFLAGS_SWIM;
			}
			else if (RecastState().poly_mesh->areas[i] == SAMPLE_POLYAREA_DOOR)
			{
				RecastState().poly_mesh->flags[i] = SAMPLE_POLYFLAGS_WALK | SAMPLE_POLYFLAGS_DOOR;
			}
		}
		
		dtNavMeshCreateParams params;
		memset(&params, 0, sizeof(params));
		params.verts = RecastState().poly_mesh->verts;
		params.vertCount = RecastState().poly_mesh->nverts;
		params.polys = RecastState().poly_mesh->polys;
		params.polyAreas = RecastState().poly_mesh->areas;
		params.polyFlags = RecastState().poly_mesh->flags;
		params.polyCount = RecastState().poly_mesh->npolys;
		params.nvp = RecastState().poly_mesh->nvp;
		params.detailMeshes = RecastState().poly_mesh_detail->meshes;
		params.detailVerts = RecastState().poly_mesh_detail->verts;
		params.detailVertsCount = RecastState().poly_mesh_detail->nverts;
		params.detailTris = RecastState().poly_mesh_detail->tris;
		params.detailTriCount = RecastState().poly_mesh_detail->ntris;
		params.offMeshConVerts = RecastState().off_mesh_connection_verts;
		params.offMeshConRad = RecastState().off_mesh_connection_radii;
		params.offMeshConDir = RecastState().off_mesh_connection_dirs;
		params.offMeshConAreas = RecastState().off_mesh_connection_areas;
		params.offMeshConFlags = RecastState().off_mesh_connection_flags;
		params.offMeshConUserID = RecastState().off_mesh_connection_ids;
		params.offMeshConCount = RecastState().off_mesh_connection_count;
		params.walkableHeight = RecastState().agent_height;
		params.walkableRadius = RecastState().agent_radius;
		params.walkableClimb = RecastState().agent_max_climb;
		params.tileX = tx;
		params.tileY = ty;
		params.tileLayer = 0;
		rcVcopy(params.bmin, RecastState().poly_mesh->bmin);
		rcVcopy(params.bmax, RecastState().poly_mesh->bmax);
		params.cs = RecastState().recast_config.cs;
		params.ch = RecastState().recast_config.ch;
		params.buildBvTree = true;

		if (!dtCreateNavMeshData(&params, &navData, &navDataSize))
		{
			printf("Could not build Detour navmesh.\n");
			return 0;
		}
	}

	dataSize = navDataSize;
	return navData;
}

void buildAllTiles()
{
	if (!RecastState().mesh_loader) return;
	if (!RecastState().nav_mesh) return;
	
	const float* bmin = RecastState().mesh_bounds_min;
	const float* bmax = RecastState().mesh_bounds_max;
	int gw = 0, gh = 0;
	rcCalcGridSize(bmin, bmax, RecastState().cell_size, &gw, &gh);

	const int ts = (int)RecastState().tile_size;
	const int tw = (gw + ts-1) / ts;
	const int th = (gh + ts-1) / ts;
	const float tcs = RecastState().tile_size * RecastState().cell_size;

	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
		{
			RecastState().last_built_tile_bounds_min[0] = bmin[0] + x*tcs;
			RecastState().last_built_tile_bounds_min[1] = bmin[1];
			RecastState().last_built_tile_bounds_min[2] = bmin[2] + y*tcs;
			
			RecastState().last_built_tile_bounds_max[0] = bmin[0] + (x+1)*tcs;
			RecastState().last_built_tile_bounds_max[1] = bmax[1];
			RecastState().last_built_tile_bounds_max[2] = bmin[2] + (y+1)*tcs;
			
			int dataSize = 0;
			unsigned char* data = buildTileMesh(x, y, RecastState().last_built_tile_bounds_min, RecastState().last_built_tile_bounds_max, dataSize);
			if (data)
			{
				// Remove any previous data (navmesh owns and deletes the data).
				RecastState().nav_mesh->removeTile(RecastState().nav_mesh->getTileRefAt(x,y,0),0,0);
				// Let the navmesh own the data.
				dtStatus status = RecastState().nav_mesh->addTile(data,dataSize,DT_TILE_FREE_DATA,0,0);
				if (dtStatusFailed(status))
					dtFree(data);
			}
		}
	}
}

inline unsigned int ilog2(unsigned int v)
{
	unsigned int r;
	unsigned int shift;
	r = (v > 0xffff) << 4; v >>= r;
	shift = (v > 0xff) << 3; v >>= shift; r |= shift;
	shift = (v > 0xf) << 2; v >>= shift; r |= shift;
	shift = (v > 0x3) << 1; v >>= shift; r |= shift;
	r |= (v >> 1);
	return r;
}

inline unsigned int nextPow2(unsigned int v)
{
	v--;
	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	v++;
	return v;
}

// obj -> navmesh
/// 构建静态 navmesh（无 TileCache），返回 dtNavMesh*。
void* BuildMeshObj()
{
	if (!RecastState().mesh_loader || RecastState().mesh_loader->getVertCount() <= 0 || RecastState().mesh_loader->getTriCount() <= 0)
	{
		printf("buildTiledNavigation: No vertices and triangles.\n");
		return nullptr;
	}
	
    if(RecastState().nav_mesh != nullptr)
	    dtFreeNavMesh(RecastState().nav_mesh);
	
	RecastState().nav_mesh = dtAllocNavMesh();
	if (!RecastState().nav_mesh)
	{
		printf("buildTiledNavigation: Could not allocate navmesh.\n");
		return nullptr;
	}

	RecastState().build_context = new BuildContext;

	int gw = 0, gh = 0;
	const float* bmin = RecastState().mesh_bounds_min;
	const float* bmax = RecastState().mesh_bounds_max;
	rcCalcGridSize(bmin, bmax, RecastState().cell_size, &gw, &gh);
	const int ts = (int)RecastState().tile_size;
	const int tw = (gw + ts-1) / ts;
	const int th = (gh + ts-1) / ts;

	int tileBits = rcMin((int)ilog2(nextPow2(tw*th)), 14);
	if (tileBits > 14) tileBits = 14;
	int polyBits = 22 - tileBits;
	RecastState().max_tiles = 1 << tileBits;
	RecastState().max_polys_per_tile = 1 << polyBits;

	dtNavMeshParams params;
	rcVcopy(params.orig, RecastState().mesh_bounds_min);
	params.tileWidth = RecastState().tile_size* RecastState().cell_size;
	params.tileHeight = RecastState().tile_size* RecastState().cell_size;
	params.maxTiles = RecastState().max_tiles;
	params.maxPolys = RecastState().max_polys_per_tile;
	
	dtStatus status;
	status = RecastState().nav_mesh->init(&params);
	if (dtStatusFailed(status))
	{
		printf("buildTiledNavigation: Could not init navmesh.\n");
		return nullptr;
	}

	RecastState().partition_type = SAMPLE_PARTITION_MONOTONE;

	buildAllTiles();

	return RecastState().nav_mesh;
}

/// 将当前构建好的静态 navmesh 序列化到文件。
bool SaveNavMesh(const char* filepath)
{
	if (!RecastState().nav_mesh)
		return false;

	FILE* fp = fopen(filepath, "wb");
	if (!fp)
		return false;

	// Store header.
	NavMeshSetHeader header;
	header.magic = NAVMESHSET_MAGIC;
	header.version = NAVMESHSET_VERSION;
	header.numTiles = 0;
	for (int i = 0; i < RecastState().nav_mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = ((const dtNavMesh*)RecastState().nav_mesh)->getTile(i);
		if (!tile || !tile->header || !tile->dataSize)
			continue;
		header.numTiles++;
	}
	memcpy(&header.params, RecastState().nav_mesh->getParams(), sizeof(dtNavMeshParams));
	fwrite(&header, sizeof(NavMeshSetHeader), 1, fp);

	// Store tiles.
	for (int i = 0; i < RecastState().nav_mesh->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = ((const dtNavMesh*)RecastState().nav_mesh)->getTile(i);
		if (!tile || !tile->header || !tile->dataSize)
			continue;

		NavMeshTileHeader tileHeader;
		tileHeader.tileRef = RecastState().nav_mesh->getTileRef(tile);
		tileHeader.dataSize = tile->dataSize;
		fwrite(&tileHeader, sizeof(tileHeader), 1, fp);
		fwrite(tile->data, tile->dataSize, 1, fp);
	}

	fclose(fp);

	return true;
}


struct RasterizationContext
{
	RasterizationContext() :
		solid(0),
		triareas(0),
		lset(0),
		chf(0),
		ntiles(0)
	{
		memset(tiles, 0, sizeof(TileCacheData)*MAX_LAYERS);
	}
	
	~RasterizationContext()
	{
		rcFreeHeightField(solid);
		delete [] triareas;
		rcFreeHeightfieldLayerSet(lset);
		rcFreeCompactHeightfield(chf);
		for (int i = 0; i < MAX_LAYERS; ++i)
		{
			dtFree(tiles[i].data);
			tiles[i].data = 0;
		}
	}
	
	rcHeightfield* solid;
	unsigned char* triareas;
	rcHeightfieldLayerSet* lset;
	rcCompactHeightfield* chf;
	TileCacheData tiles[MAX_LAYERS];
	int ntiles;
};


int rasterizeTileLayers( const int tx, const int ty,
						const rcConfig& cfg,
						TileCacheData* tiles,
						const int maxTiles )
{
	if (!RecastState().mesh_loader)
	{
		printf("buildTile: Input mesh is not specified.");
		return 0;
	}
	
	FastLZCompressor comp;
	RasterizationContext rc;
	
	const float* verts = RecastState().mesh_loader->getVerts();
	const int nverts = RecastState().mesh_loader->getVertCount();
	const ChunkyTriMesh* chunkyMesh = RecastState().chunky_mesh;
	
	// Tile bounds.
	const float tcs = cfg.tileSize * cfg.cs;
	
	rcConfig tcfg;
	memcpy(&tcfg, &cfg, sizeof(tcfg));

	tcfg.bmin[0] = cfg.bmin[0] + tx*tcs;
	tcfg.bmin[1] = cfg.bmin[1];
	tcfg.bmin[2] = cfg.bmin[2] + ty*tcs;
	tcfg.bmax[0] = cfg.bmin[0] + (tx+1)*tcs;
	tcfg.bmax[1] = cfg.bmax[1];
	tcfg.bmax[2] = cfg.bmin[2] + (ty+1)*tcs;
	tcfg.bmin[0] -= tcfg.borderSize*tcfg.cs;
	tcfg.bmin[2] -= tcfg.borderSize*tcfg.cs;
	tcfg.bmax[0] += tcfg.borderSize*tcfg.cs;
	tcfg.bmax[2] += tcfg.borderSize*tcfg.cs;
	
	// Allocate voxel heightfield where we rasterize our input data to.
	rc.solid = rcAllocHeightfield();
	if (!rc.solid)
	{
		printf("buildNavigation: Out of memory 'solid'.");
		return 0;
	}
	if (!rcCreateHeightfield(RecastState().build_context, *rc.solid, tcfg.width, tcfg.height, tcfg.bmin, tcfg.bmax, tcfg.cs, tcfg.ch))
	{
		printf("buildNavigation: Could not create solid heightfield.");
		return 0;
	}
	
	// Allocate array that can hold triangle flags.
	// If you have multiple meshes you need to process, allocate
	// and array which can hold the max number of triangles you need to process.
	rc.triareas = new unsigned char[chunkyMesh->maxTrisPerChunk];
	if (!rc.triareas)
	{
		printf("buildNavigation: Out of memory 'RecastState().triangle_areas' (%d).", chunkyMesh->maxTrisPerChunk);
		return 0;
	}
	
	float tbmin[2], tbmax[2];
	tbmin[0] = tcfg.bmin[0];
	tbmin[1] = tcfg.bmin[2];
	tbmax[0] = tcfg.bmax[0];
	tbmax[1] = tcfg.bmax[2];
	int cid[512];// TODO: Make grow when returning too many items.
	const int ncid = GetChunksOverlappingRect(chunkyMesh, tbmin, tbmax, cid, 512);
	if (!ncid)
	{
		return 0; // empty
	}
	
	for (int i = 0; i < ncid; ++i)
	{
		const ChunkyTriMeshNode& node = chunkyMesh->nodes[cid[i]];
		const int* tris = &chunkyMesh->tris[node.i*3];
		const int ntris = node.n;
		
		memset(rc.triareas, 0, ntris*sizeof(unsigned char));
		rcMarkWalkableTriangles(RecastState().build_context, tcfg.walkableSlopeAngle,
								verts, nverts, tris, ntris, rc.triareas);
		
		if (!rcRasterizeTriangles(RecastState().build_context, verts, nverts, tris, rc.triareas, ntris, *rc.solid, tcfg.walkableClimb))
			return 0;
	}
	
	// Once all geometry is rasterized, we do initial pass of filtering to
	// remove unwanted overhangs caused by the conservative rasterization
	// as well as filter spans where the character cannot possibly stand.
	if (RecastState().filter_low_hanging_obstacles)
		rcFilterLowHangingWalkableObstacles(RecastState().build_context, tcfg.walkableClimb, *rc.solid);
	if (RecastState().filter_ledge_spans)
		rcFilterLedgeSpans(RecastState().build_context, tcfg.walkableHeight, tcfg.walkableClimb, *rc.solid);
	if (RecastState().filter_walkable_low_height_spans)
		rcFilterWalkableLowHeightSpans(RecastState().build_context, tcfg.walkableHeight, *rc.solid);
	
	
	rc.chf = rcAllocCompactHeightfield();
	if (!rc.chf)
	{
		printf("buildNavigation: Out of memory 'chf'.");
		return 0;
	}
	if (!rcBuildCompactHeightfield(RecastState().build_context, tcfg.walkableHeight, tcfg.walkableClimb, *rc.solid, *rc.chf))
	{
		printf("buildNavigation: Could not build compact data.");
		return 0;
	}
	
	// Erode the walkable area by agent radius.
	if (!rcErodeWalkableArea(RecastState().build_context, tcfg.walkableRadius, *rc.chf))
	{
		printf("buildNavigation: Could not erode.");
		return 0;
	}

	/*
	// (Optional) Mark areas.
	const ConvexVolume* vols = m_geom->getConvexVolumes();
	for (int i  = 0; i < m_geom->getConvexVolumeCount(); ++i)
	{
		rcMarkConvexPolyArea(RecastState().build_context, vols[i].verts, vols[i].nverts,
							 vols[i].hmin, vols[i].hmax,
							 (unsigned char)vols[i].area, *rc.chf);
	}
	*/
	
	rc.lset = rcAllocHeightfieldLayerSet();
	if (!rc.lset)
	{
		printf("buildNavigation: Out of memory 'lset'.");
		return 0;
	}
	if (!rcBuildHeightfieldLayers(RecastState().build_context, *rc.chf, tcfg.borderSize, tcfg.walkableHeight, *rc.lset))
	{
		printf("buildNavigation: Could not build heighfield layers.");
		return 0;
	}
	
	rc.ntiles = 0;
	for (int i = 0; i < rcMin(rc.lset->nlayers, MAX_LAYERS); ++i)
	{
		TileCacheData* tile = &rc.tiles[rc.ntiles++];
		const rcHeightfieldLayer* layer = &rc.lset->layers[i];
		
		// Store header
		dtTileCacheLayerHeader header;
		header.magic = DT_TILECACHE_MAGIC;
		header.version = DT_TILECACHE_VERSION;
		
		// Tile layer location in the navmesh.
		header.tx = tx;
		header.ty = ty;
		header.tlayer = i;
		dtVcopy(header.bmin, layer->bmin);
		dtVcopy(header.bmax, layer->bmax);
		
		// Tile info.
		header.width = (unsigned char)layer->width;
		header.height = (unsigned char)layer->height;
		header.minx = (unsigned char)layer->minx;
		header.maxx = (unsigned char)layer->maxx;
		header.miny = (unsigned char)layer->miny;
		header.maxy = (unsigned char)layer->maxy;
		header.hmin = (unsigned short)layer->hmin;
		header.hmax = (unsigned short)layer->hmax;

		dtStatus status = dtBuildTileCacheLayer(&comp, &header, layer->heights, layer->areas, layer->cons,
												&tile->data, &tile->dataSize);
		if (dtStatusFailed(status))
		{
			return 0;
		}
	}

	// Transfer ownsership of tile data from build context to the caller.
	int n = 0;
	for (int i = 0; i < rcMin(rc.ntiles, maxTiles); ++i)
	{
		tiles[n++] = rc.tiles[i];
		rc.tiles[i].data = 0;
		rc.tiles[i].dataSize = 0;
	}
	
	return n;
}

static int calcLayerBufferSize(const int gridWidth, const int gridHeight)
{
	const int headerSize = dtAlign4(sizeof(dtTileCacheLayerHeader));
	const int gridSize = gridWidth * gridHeight;
	return headerSize + gridSize*4;
}

/// 构建带 TileCache 的导航数据，返回 dtNavMesh*。
void* BuildObstaclesNavMesh()
{
	dtStatus status;

	if (!RecastState().mesh_loader || RecastState().mesh_loader->getVertCount() <= 0 || RecastState().mesh_loader->getTriCount() <= 0)
	{
		printf("buildTiledNavigation: No vertices and triangles.\n");
		return nullptr;
	}
	
	// Init cache
	const float* bmin = RecastState().mesh_bounds_min;
	const float* bmax = RecastState().mesh_bounds_max;
	int gw = 0, gh = 0;
	rcCalcGridSize(bmin, bmax, RecastState().cell_size, &gw, &gh);

	RecastState().tile_size = 64.0f;

	const int ts = (int)RecastState().tile_size;
	const int tw = (gw + ts-1) / ts;
	const int th = (gh + ts-1) / ts;

	int tileBits = rcMin((int)dtIlog2(dtNextPow2(tw*th*EXPECTED_LAYERS_PER_TILE)), 14);
	if (tileBits > 14)
		tileBits = 14;
	int polyBits = 22 - tileBits;
	RecastState().max_tiles = 1 << tileBits;
	RecastState().max_polys_per_tile = 1 << polyBits;

	// Generation params.
	rcConfig cfg;
	memset(&cfg, 0, sizeof(cfg));
	cfg.cs = RecastState().cell_size;
	cfg.ch = RecastState().cell_height;
	cfg.walkableSlopeAngle = RecastState().agent_max_slope;
	cfg.walkableHeight = (int)ceilf(RecastState().agent_height / cfg.ch);
	cfg.walkableClimb = (int)floorf(RecastState().agent_max_climb / cfg.ch);
	cfg.walkableRadius = (int)ceilf(RecastState().agent_radius / cfg.cs);
	cfg.maxEdgeLen = (int)(RecastState().edge_max_len / RecastState().cell_size);
	cfg.maxSimplificationError = RecastState().edge_max_error;
	cfg.minRegionArea = (int)rcSqr(RecastState().region_min_size);		// Note: area = size*size
	cfg.mergeRegionArea = (int)rcSqr(RecastState().region_merge_size);	// Note: area = size*size
	cfg.maxVertsPerPoly = (int)RecastState().verts_per_poly;
	cfg.tileSize = (int)RecastState().tile_size;
	cfg.borderSize = cfg.walkableRadius + 3; // Reserve enough padding.
	cfg.width = cfg.tileSize + cfg.borderSize*2;
	cfg.height = cfg.tileSize + cfg.borderSize*2;
	cfg.detailSampleDist = RecastState().detail_sample_dist < 0.9f ? 0 : RecastState().cell_size * RecastState().detail_sample_dist;
	cfg.detailSampleMaxError = RecastState().cell_height * RecastState().detail_sample_max_error;
	rcVcopy(cfg.bmin, bmin);
	rcVcopy(cfg.bmax, bmax);
	
	// Tile cache params.
	dtTileCacheParams tcparams;
	memset(&tcparams, 0, sizeof(tcparams));
	rcVcopy(tcparams.orig, bmin);
	tcparams.cs = RecastState().cell_size;
	tcparams.ch = RecastState().cell_height;
	tcparams.width = (int)RecastState().tile_size;
	tcparams.height = (int)RecastState().tile_size;
	tcparams.walkableHeight = RecastState().agent_height;
	tcparams.walkableRadius = RecastState().agent_radius;
	tcparams.walkableClimb = RecastState().agent_max_climb;
	tcparams.maxSimplificationError = RecastState().edge_max_error;
	tcparams.maxTiles = tw*th*EXPECTED_LAYERS_PER_TILE;
	tcparams.maxObstacles = RecastState().max_obstacles;

	dtFreeTileCache(RecastState().tile_cache);
	
	RecastState().tile_cache = dtAllocTileCache();
	if (!RecastState().tile_cache)
	{
		printf("buildTiledNavigation: Could not allocate tile cache.");
		return nullptr;
	}

	RecastState().tile_cache_allocator = new LinearAllocator(ALLOC_CAPATICY);
	RecastState().tile_cache_compressor = new FastLZCompressor;
	RecastState().tile_cache_mesh_process = new MeshProcess;

	status = RecastState().tile_cache->init(&tcparams, RecastState().tile_cache_allocator, RecastState().tile_cache_compressor, RecastState().tile_cache_mesh_process);
	if (dtStatusFailed(status))
	{
		printf("buildTiledNavigation: Could not init tile cache.");
		return nullptr;
	}
	
	dtFreeNavMesh(RecastState().nav_mesh);
	RecastState().nav_mesh = dtAllocNavMesh();
	if (!RecastState().nav_mesh)
	{
		printf("buildTiledNavigation: Could not allocate navmesh.");
		return nullptr;
	}

	RecastState().build_context = new BuildContext;

	dtNavMeshParams params;
	memset(&params, 0, sizeof(params));
	rcVcopy(params.orig, bmin);
	params.tileWidth = RecastState().tile_size*RecastState().cell_size;
	params.tileHeight = RecastState().tile_size*RecastState().cell_size;
	params.maxTiles = RecastState().max_tiles;
	params.maxPolys = RecastState().max_polys_per_tile;
	
	status = RecastState().nav_mesh->init(&params);
	if (dtStatusFailed(status))
	{
		printf("buildTiledNavigation: Could not init navmesh.");
		return nullptr;
	}
	
	RecastState().cache_layer_count = 0;
	RecastState().cache_compressed_size = 0;
	RecastState().cache_raw_size = 0;
	
	printf("th->%d, tw->%d\n", th, tw);

	for (int y = 0; y < th; ++y)
	{
		for (int x = 0; x < tw; ++x)
		{
			TileCacheData tiles[MAX_LAYERS];
			memset(tiles, 0, sizeof(tiles));
			int ntiles = rasterizeTileLayers(x, y, cfg, tiles, MAX_LAYERS);
			for (int i = 0; i < ntiles; ++i)
			{
				TileCacheData* tile = &tiles[i];
				status = RecastState().tile_cache->addTile(tile->data, tile->dataSize, DT_COMPRESSEDTILE_FREE_DATA, 0);
				if (dtStatusFailed(status))
				{
					dtFree(tile->data);
					tile->data = 0;
					continue;
				}
				
				RecastState().cache_layer_count++;
				RecastState().cache_compressed_size += tile->dataSize;
				RecastState().cache_raw_size += calcLayerBufferSize(tcparams.width, tcparams.height);
			}
		}
	}

	// Build initial meshes
	for (int y = 0; y < th; ++y)
		for (int x = 0; x < tw; ++x)
			RecastState().tile_cache->buildNavMeshTilesAt(x,y, RecastState().nav_mesh);

	const dtNavMesh* nav = RecastState().nav_mesh;
	int navmeshMemUsage = 0;
	for (int i = 0; i < nav->getMaxTiles(); ++i)
	{
		const dtMeshTile* tile = nav->getTile(i);
		if (tile->header)
			navmeshMemUsage += tile->dataSize;
	}
	printf("maxTiles:%d, navmeshMemUsage = %.1f kB\n", nav->getMaxTiles(), navmeshMemUsage/1024.0f);

	return RecastState().nav_mesh;
}

/// 序列化 TileCache + NavMesh 数据到文件，供运行时加载。
bool SaveObstaclesNavMesh(const char* filapath)
{
	if (!RecastState().tile_cache)
		return false;
	
	FILE* fp = fopen(filapath, "wb");
	if (!fp)
		return false;
	
	// Store header.
	TileCacheSetHeader header;
	header.magic = TILECACHESET_MAGIC;
	header.version = TILECACHESET_VERSION;
	header.numTiles = 0;
	for (int i = 0; i < RecastState().tile_cache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = RecastState().tile_cache->getTile(i);
		if (!tile || !tile->header || !tile->dataSize) continue;
		header.numTiles++;
	}
	memcpy(&header.cacheParams, RecastState().tile_cache->getParams(), sizeof(dtTileCacheParams));
	memcpy(&header.meshParams, RecastState().nav_mesh->getParams(), sizeof(dtNavMeshParams));
	fwrite(&header, sizeof(TileCacheSetHeader), 1, fp);

	// Store tiles.
	for (int i = 0; i < RecastState().tile_cache->getTileCount(); ++i)
	{
		const dtCompressedTile* tile = RecastState().tile_cache->getTile(i);
		if (!tile || !tile->header || !tile->dataSize) continue;

		TileCacheTileHeader tileHeader;
		tileHeader.tileRef = RecastState().tile_cache->getTileRef(tile);
		tileHeader.dataSize = tile->dataSize;
		fwrite(&tileHeader, sizeof(tileHeader), 1, fp);

		fwrite(tile->data, tile->dataSize, 1, fp);
	}

	fclose(fp);

	return true;
}

/// 将外部配置映射到 Recast 全局参数。
void ConfigureRecastSettings(const RecastBuildSettings& settings)
{
	RecastState().cell_size = settings.cell_size;
	RecastState().cell_height = settings.cell_height;
	RecastState().agent_height = settings.agent_height;
	RecastState().agent_radius = settings.agent_radius;
	RecastState().agent_max_climb = settings.agent_max_climb;
	RecastState().agent_max_slope = settings.agent_max_slope;
	RecastState().region_min_size = settings.region_min_size;
	RecastState().region_merge_size = settings.region_merge_size;
	RecastState().edge_max_len = settings.edge_max_len;
	RecastState().edge_max_error = settings.edge_max_error;
	RecastState().detail_sample_dist = settings.detail_sample_dist;
	RecastState().detail_sample_max_error = settings.detail_sample_max_error;
	RecastState().verts_per_poly = settings.verts_per_poly;
	RecastState().tile_size = static_cast<float>(settings.tile_size);
	RecastState().max_tiles = settings.max_tiles;
	RecastState().max_polys_per_tile = settings.max_polys_per_tile;
	RecastState().keep_intermediate_results = settings.keep_intermediate;
	RecastState().filter_low_hanging_obstacles = settings.filter_low_hanging_obstacles;
	RecastState().filter_ledge_spans = settings.filter_ledge_spans;
	RecastState().filter_walkable_low_height_spans = settings.filter_walkable_low_height;
	RecastState().max_obstacles = settings.max_obstacles;

	switch (settings.partition_type)
	{
	case RecastPartitionType::Monotone: RecastState().partition_type = SAMPLE_PARTITION_MONOTONE; break;
	case RecastPartitionType::Layers: RecastState().partition_type = SAMPLE_PARTITION_LAYERS; break;
	default: RecastState().partition_type = SAMPLE_PARTITION_WATERSHED; break;
	}
}

static void ClearRuntimeResources()
{
    auto& state = RecastState();
    if (state.triangle_areas)
    {
        delete [] state.triangle_areas;
        state.triangle_areas = nullptr;
    }
    if (state.solid_heightfield)
    {
        rcFreeHeightField(state.solid_heightfield);
        state.solid_heightfield = nullptr;
    }
    if (state.compact_heightfield)
    {
        rcFreeCompactHeightfield(state.compact_heightfield);
        state.compact_heightfield = nullptr;
    }
    if (state.contour_set)
    {
        rcFreeContourSet(state.contour_set);
        state.contour_set = nullptr;
    }
    if (state.poly_mesh)
    {
        rcFreePolyMesh(state.poly_mesh);
        state.poly_mesh = nullptr;
    }
    if (state.poly_mesh_detail)
    {
        rcFreePolyMeshDetail(state.poly_mesh_detail);
        state.poly_mesh_detail = nullptr;
    }
    if (state.build_context)
    {
        delete state.build_context;
        state.build_context = nullptr;
    }
    if (state.nav_mesh)
    {
        dtFreeNavMesh(state.nav_mesh);
        state.nav_mesh = nullptr;
    }
    if (state.tile_cache)
    {
        dtFreeTileCache(state.tile_cache);
        state.tile_cache = nullptr;
    }
    if (state.tile_cache_allocator)
    {
        delete state.tile_cache_allocator;
        state.tile_cache_allocator = nullptr;
    }
    if (state.tile_cache_compressor)
    {
        delete state.tile_cache_compressor;
        state.tile_cache_compressor = nullptr;
    }
    if (state.tile_cache_mesh_process)
    {
        delete state.tile_cache_mesh_process;
        state.tile_cache_mesh_process = nullptr;
    }
}

static void ResetAllResources()
{
    auto& state = RecastState();
    ClearRuntimeResources();
    if (state.mesh_loader)
    {
        delete state.mesh_loader;
        state.mesh_loader = nullptr;
    }
    if (state.chunky_mesh)
    {
        delete state.chunky_mesh;
        state.chunky_mesh = nullptr;
    }
}

struct RecastBuilderContext::Impl {
    LegacyRecastState state;
};

RecastBuilderContext::RecastBuilderContext()
    : impl_(std::make_unique<Impl>()) {}

RecastBuilderContext::~RecastBuilderContext() {
    ScopedRecastState scope(impl_->state);
    ResetAllResources();
}

bool RecastBuilderContext::LoadMesh(const std::string& obj_path) {
    ScopedRecastState scope(impl_->state);
    ResetAllResources();
    return LoadMeshObj(obj_path.c_str());
}

bool RecastBuilderContext::BuildStaticNavMesh(const RecastBuildSettings& settings,
                                              const std::string& output_bin_path) {
    ScopedRecastState scope(impl_->state);
    ConfigureRecastSettings(settings);
    if (!BuildMeshObj()) {
        return false;
    }
    return SaveNavMesh(output_bin_path.c_str());
}

bool RecastBuilderContext::BuildTileCacheNavMesh(const RecastBuildSettings& settings,
                                                 const std::string& output_bin_path) {
    ScopedRecastState scope(impl_->state);
    ConfigureRecastSettings(settings);
    if (!BuildObstaclesNavMesh()) {
        return false;
    }
    return SaveObstaclesNavMesh(output_bin_path.c_str());
}

}  // namespace slg::navigation::detail
