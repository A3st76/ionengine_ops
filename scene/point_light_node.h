// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include <scene/transform_node.h>
#include <asset/asset_ptr.h>
#include <asset/texture.h>
#include <lib/math/color.h>

namespace ionengine::scene {

class PointLightNode : public TransformNode {
public:

    PointLightNode();

    virtual void accept(SceneVisitor& visitor);

    void color(lib::math::Color const& color);

    lib::math::Color const& color() const;

    float range() const;

    void range(float const range);

    void editor_icon(asset::AssetPtr<asset::Texture> texture);

    asset::AssetPtr<asset::Texture>& editor_icon();

private:

    lib::math::Color _color;
    float _range;

    asset::AssetPtr<asset::Texture> _editor_icon;
};

}