#pragma once

//! The one place the Direct3D 12 and DXGI headers are included.
//!
//! `os/windows/win32.h` brings `<windows.h>` between the unity guards; the D3D12 and DXGI headers go inside their own
//! pair here, so their macros do not leak into the next file of a unity batch (CLAUDE.md, "Unity builds"). The backend
//! uses the OS's own `d3d12.dll` and `dxgi.dll` (no Agility SDK, no D3D12MA, no DirectX-Headers package, no WRL: design
//! spec decision 10).

#include "os/windows/win32.h"

#include "unity/windows_enable.h"
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include "unity/windows_disable.h"
