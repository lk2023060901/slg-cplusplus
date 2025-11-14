/*
 * Recast/Detour builder helpers adapted from legacy Lua binding implementation.
 */

#pragma once

#include <memory>
#include <string>
#include <stdbool.h>

namespace slg::navigation {

struct RecastBuildSettings;

namespace detail {

class RecastBuilderContext {
public:
    RecastBuilderContext();
    ~RecastBuilderContext();

    bool LoadMesh(const std::string& obj_path);
    bool BuildStaticNavMesh(const RecastBuildSettings& settings, const std::string& output_bin_path);
    bool BuildTileCacheNavMesh(const RecastBuildSettings& settings, const std::string& output_bin_path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace detail

}  // namespace slg::navigation
