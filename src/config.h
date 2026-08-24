// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#pragma once

#include <string>

namespace pinballfx_ht
{
    struct Config
    {
        int udp_port = 4242;
        bool enable_on_startup = true;
        // true = yaw about the world up-axis (horizon-locked); false = yaw about
        // the camera's own up-axis, which leans on pitched turns.
        bool world_space_yaw = true;
        int yaw_mode_key = 0x22;  // Page Down

        float yaw_sensitivity = 1.0f;
        float pitch_sensitivity = 1.0f;
        float roll_sensitivity = 1.0f;
        bool invert_yaw = false;
        bool invert_pitch = false;
        bool invert_roll = false;

        // Two smoothing parameters, picked per connection from the packet source
        // address. Both cover rotation and position.
        float local_smoothing = 0.0f;
        float remote_smoothing = 0.15f;

        // Camera field of view, in degrees. The game exposes no FOV control, so
        // these are the only way to change it. fov_override replaces whatever
        // the table's view asks for; fov_offset (used when override is 0) adds
        // to it and so keeps the relative differences between the views. 0 and 0
        // leave the game's FOV exactly as it is.
        float fov_override = 0.0f;
        float fov_offset = 0.0f;

        // A fixed shift of the render camera, in UE units (cm), applied while
        // gameplay owns the camera. Forward runs along the line of sight, so a
        // negative value pulls back; up and right are perpendicular to it and
        // ignore the engine's display roll. Mainly for the cabinet/portrait
        // views, where the stock camera is far back behind a ~15 degree lens.
        float camera_offset_forward = 0.0f;
        float camera_offset_up = 0.0f;
        float camera_offset_right = 0.0f;

        // Where head tracking is allowed to touch the camera. Pinball FX renders
        // a live table behind its menus and plays scripted camera moves at the
        // start of a table and around table events; tracking in either fights a
        // shot the game composed for you.
        bool gameplay_only = true;
        bool suppress_during_camera_sequences = true;

        // No lean limits, and no INI keys for them: how far a player leans is
        // their call, so nothing here clamps the tracked offset.
        bool position_enabled = true;
        float position_sensitivity_x = 1.0f;
        float position_sensitivity_y = 1.0f;
        float position_sensitivity_z = 1.0f;
    };

    // Both take the directory holding the game EXE; the INI sits beside it.
    // Keys absent from the file keep the defaults already in @p out, so a
    // partial INI is valid. LoadConfig validates every value it reads: a key
    // that is out of range or not a number keeps its default and the
    // substitution is logged, so nothing here is ever NaN, infinite, or outside
    // the range its consumer can take.
    void LoadConfig(const std::string& exeDir, Config& out);
    void WriteDefaultConfigIfMissing(const std::string& exeDir);
}
