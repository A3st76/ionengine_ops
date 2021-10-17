// Copyright © 2020-2021 Dmitriy Lukovenko. All rights reserved.

#pragma once

#include <d3d12.h>
#include <dxgi1_4.h>

#pragma comment (lib, "d3d12.lib")
#pragma comment (lib, "dxgi.lib")
#pragma comment (lib, "dxguid.lib")

extern std::string HResultToString(const HRESULT result);

#ifndef THROW_IF_FAILED
#define THROW_IF_FAILED(Result) if(FAILED(Result)) std::runtime_error(HResultToString(Result));
#endif