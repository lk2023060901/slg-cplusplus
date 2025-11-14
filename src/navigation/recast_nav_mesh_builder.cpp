#include "navigation/recast_nav_mesh_builder.h"

#include "navigation/recast/recastnavmesh.h"

namespace slg::navigation {

RecastNavMeshBuilder::RecastNavMeshBuilder()
    : context_(std::make_unique<detail::RecastBuilderContext>()) {}

RecastNavMeshBuilder::~RecastNavMeshBuilder() = default;

bool RecastNavMeshBuilder::LoadMeshFromObj(const std::string& obj_path) {
    return context_->LoadMesh(obj_path);
}

bool RecastNavMeshBuilder::BuildStaticNavMesh(const RecastBuildSettings& settings,
                                              const std::string& output_bin_path) {
    return context_->BuildStaticNavMesh(settings, output_bin_path);
}

bool RecastNavMeshBuilder::BuildTileCacheNavMesh(const RecastBuildSettings& settings,
                                                 const std::string& output_bin_path) {
    return context_->BuildTileCacheNavMesh(settings, output_bin_path);
}

}  // namespace slg::navigation
