//
// Minimal OBJ mesh loader used during Recast navmesh baking stage.
//

#pragma once

#include <string>

class MeshLoaderObj {
public:
    MeshLoaderObj();
    ~MeshLoaderObj();

    bool load(const std::string& filename);

    int getVertCount() const { return num_verts_; }
    const float* getVerts() const { return verts_; }
    int getTriCount() const { return num_tris_; }
    const int* getTris() const { return tris_; }
    const float* getNormals() const { return normals_; }

private:
    void addVertex(float x, float y, float z, int& cap);
    void addTriangle(int a, int b, int c, int& cap);

    int num_verts_;
    int verts_cap_;
    float* verts_;
    float* normals_;
    int num_tris_;
    int tris_cap_;
    int* tris_;

    MeshLoaderObj(const MeshLoaderObj&) = delete;
    MeshLoaderObj& operator=(const MeshLoaderObj&) = delete;
};
