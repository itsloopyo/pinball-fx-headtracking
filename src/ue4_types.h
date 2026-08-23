// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

namespace pinballfx_ht
{
    // UE 4.27 ABI: Large World Coordinates arrived with UE5, so the engine's
    // FVector / FRotator here are 3-FLOAT POD (12 bytes each), NOT the 3-double
    // FVector3d / FRotator3d an LWC build uses. These are the exact types
    // APlayerController::GetPlayerViewPoint(self, &OutLocation, &OutRotation)
    // writes through; declaring them as doubles would have the engine overflow
    // 12 bytes of our stack on every call. Quaternion math stays in core's
    // double types and is converted only at this ABI boundary.
    struct FVector4f  { float X, Y, Z; };
    struct FRotator4f { float Pitch, Yaw, Roll; };

    using GetPlayerViewPoint_t =
        void(__fastcall*)(void* self, FVector4f* outLocation, FRotator4f* outRotation);
}
