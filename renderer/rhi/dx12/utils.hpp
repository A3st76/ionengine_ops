// Copyright © 2020-2024 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(Result) if(FAILED(Result)) throw ionengine::core::Exception(ionengine::renderer::rhi::hresult_to_string(Result));
#endif // THROW_IF_FAILED

namespace ionengine {

namespace renderer {

namespace rhi {

inline auto hresult_to_string(HRESULT const result) -> std::string {

	switch(result) {
		case E_FAIL: return "Attempted to create a device with the debug layer enabled and the layer is not installed";
		case E_INVALIDARG: return "An invalid parameter was passed to the returning function";
		case E_OUTOFMEMORY: return "Direct3D could not allocate sufficient memory to complete the call";
		case E_NOTIMPL: return "The method call isn't implemented with the passed parameter combination";
		case S_FALSE: return "Alternate success value, indicating a successful but nonstandard completion";
		case S_OK: return "No error occurred";
		case D3D12_ERROR_ADAPTER_NOT_FOUND: return "The specified cached PSO was created on a different adapter and cannot be reused on the current adapter";
		case D3D12_ERROR_DRIVER_VERSION_MISMATCH: return "The specified cached PSO was created on a different driver version and cannot be reused on the current adapter";
		case DXGI_ERROR_INVALID_CALL: return "The method call is invalid. For example, a method's parameter may not be a valid pointer";
		case DXGI_ERROR_WAS_STILL_DRAWING: return "The previous blit operation that is transferring information to or from this surface is incomplete";
		default: return "An unknown error has occurred";
	}
}

}

}

}