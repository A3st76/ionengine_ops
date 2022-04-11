// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#include <precompiled.h>
#include <scene/mesh_node.h>

using namespace ionengine::scene;

void MeshNode::mesh(Handle<asset::Asset> const& asset) {

    _mesh = asset;
}