#include "navigation/recast/MeshLoaderObj.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

MeshLoaderObj::MeshLoaderObj() : num_verts_(0), verts_cap_(0), verts_(nullptr), normals_(nullptr), num_tris_(0), tris_cap_(0), tris_(nullptr) {}

MeshLoaderObj::~MeshLoaderObj() {
    delete[] verts_;
    delete[] normals_;
    delete[] tris_;
}

void MeshLoaderObj::addVertex(float x, float y, float z, int& cap) {
    if (num_verts_ + 1 > cap) {
        cap = !cap ? 8 : cap * 2;
        float* new_verts = new float[cap * 3];
        if (num_verts_) {
            std::memcpy(new_verts, verts_, sizeof(float) * 3 * num_verts_);
        }
        delete[] verts_;
        verts_ = new_verts;

        float* new_normals = new float[cap * 3];
        if (num_verts_) {
            std::memcpy(new_normals, normals_, sizeof(float) * 3 * num_verts_);
        }
        delete[] normals_;
        normals_ = new_normals;
    }

    float* dst = &verts_[num_verts_ * 3];
    dst[0] = x;
    dst[1] = y;
    dst[2] = z;

    dst = &normals_[num_verts_ * 3];
    dst[0] = 0;
    dst[1] = 0;
    dst[2] = 0;

    num_verts_++;
}

void MeshLoaderObj::addTriangle(int a, int b, int c, int& cap) {
    if (num_tris_ + 1 > cap) {
        cap = !cap ? 8 : cap * 2;
        int* new_tris = new int[cap * 3];
        if (num_tris_) {
            std::memcpy(new_tris, tris_, sizeof(int) * 3 * num_tris_);
        }
        delete[] tris_;
        tris_ = new_tris;
    }

    int* dst = &tris_[num_tris_ * 3];
    dst[0] = a;
    dst[1] = b;
    dst[2] = c;

    float* vn0 = &normals_[a * 3];
    float* vn1 = &normals_[b * 3];
    float* vn2 = &normals_[c * 3];

    const float* v0 = &verts_[a * 3];
    const float* v1 = &verts_[b * 3];
    const float* v2 = &verts_[c * 3];

    float e0[3], e1[3];
    e0[0] = v1[0] - v0[0];
    e0[1] = v1[1] - v0[1];
    e0[2] = v1[2] - v0[2];
    e1[0] = v2[0] - v0[0];
    e1[1] = v2[1] - v0[1];
    e1[2] = v2[2] - v0[2];

    float n[3];
    n[0] = e0[1] * e1[2] - e0[2] * e1[1];
    n[1] = e0[2] * e1[0] - e0[0] * e1[2];
    n[2] = e0[0] * e1[1] - e0[1] * e1[0];

    vn0[0] += n[0];
    vn0[1] += n[1];
    vn0[2] += n[2];
    vn1[0] += n[0];
    vn1[1] += n[1];
    vn1[2] += n[2];
    vn2[0] += n[0];
    vn2[1] += n[1];
    vn2[2] += n[2];

    num_tris_++;
}

bool MeshLoaderObj::load(const std::string& filename) {
    FILE* fp = std::fopen(filename.c_str(), "rb");
    if (!fp) {
        return false;
    }

    char line[512];
    int verts_cap = 0;
    int tris_cap = 0;

    while (std::fgets(line, sizeof(line), fp)) {
        if (std::strncmp(line, "v ", 2) == 0) {
            float x, y, z;
            std::sscanf(line + 2, "%f %f %f", &x, &y, &z);
            addVertex(x, y, z, verts_cap);
        } else if (std::strncmp(line, "f ", 2) == 0) {
            int a, b, c;
            std::sscanf(line + 2, "%d %d %d", &a, &b, &c);
            addTriangle(a - 1, b - 1, c - 1, tris_cap);
        }
    }

    std::fclose(fp);
    return true;
}
