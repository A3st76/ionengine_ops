// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

namespace ionengine::renderer {

class BaseRenderer {
public:

    virtual ~BaseRenderer() = default;
    virtual void tick() = 0;
};

}