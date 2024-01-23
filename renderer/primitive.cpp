// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#include "precompiled.h"
#include "primitive.hpp"
#include "core/exception.hpp"

using namespace ionengine;
using namespace ionengine::renderer;
/*
Primitive::Primitive(
    Context& context, 
    BufferAllocator<LinearAllocator>& allocator,
    PrimitiveData const& data
) : 
    context(&context), 
    allocator(&allocator)
{
    vertex_buffer = allocator.allocate(data.vertices.size());
    if(!vertex_buffer.buffer) {
        throw core::Exception("Not enough memory to allocate a new buffer");
    }

    index_buffer = allocator.allocate(data.indices.size());
    if(!index_buffer.buffer) {
        throw core::Exception("Not enough memory to allocate a new buffer");
    }

    context.get_queue().writeBuffer(
        vertex_buffer.buffer->get_buffer(),
        vertex_buffer.offset,
        data.vertices.data(),
        data.vertices.size()
    );

    context.get_queue().writeBuffer(
        index_buffer.buffer->get_buffer(),
        index_buffer.offset,
        data.indices.data(),
        data.indices.size()
    );
}*/