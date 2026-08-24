// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "config.h"

#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include <windows.h>

#include <cameraunlock/config/ini_reader.h>
#include <cameraunlock/math/finite_utils.h>
#include <cameraunlock/protocol/port_utils.h>

#include "logging.h"
#include "view_injection.h"

namespace pinballfx_ht
{
    namespace
    {
        constexpr const char* kIniName = "HeadTracking.ini";

        // Bounds for the INI numbers. Deliberately far wider than anything a
        // user would choose - they exist to stop a typo reaching the maths, not
        // to second-guess a setting. A negative sensitivity inverts the axis,
        // which is a legitimate thing to want, so those ranges stay symmetric.
        constexpr float kMaxSensitivity = 10.0f;

        // GetAsyncKeyState reports nothing outside this range, so a key code
        // outside it would leave the toggle it binds silently dead.
        constexpr int kMinVirtualKey = 0x01;
        constexpr int kMaxVirtualKey = 0xFE;

        std::string IniPath(const std::string& exeDir)
        {
            return exeDir + "\\" + kIniName;
        }

        // Nothing downstream of the INI rejects a bad float. strtod accepts
        // "nan" and "inf" and overflows a literal like 1e400 to +inf; a NaN
        // sensitivity then poisons the smoothing state for the rest of the
        // session and presents as the view simply being gone, so the
        // substitution is logged with the key that caused it instead of being
        // applied quietly.
        float ReadFloatChecked(const cameraunlock::IniReader& reader, const char* section,
                               const char* key, float fallback, float lo, float hi)
        {
            const float raw = reader.ReadFloat(section, key, fallback);
            const float value = cameraunlock::math::SanitizeFinite(raw, fallback, lo, hi);
            if (value != raw)
                Log::Line("WARNING: config [%s] %s = %g is not a number in [%g, %g] - using %g.",
                    section, key, static_cast<double>(raw), static_cast<double>(lo),
                    static_cast<double>(hi), static_cast<double>(value));
            return value;
        }

        // ReadInt yields 0 for a present-but-non-numeric value rather than the
        // default (see ini_reader.h rule 4), and a raw cast to uint16_t turns
        // 70000 into port 4464. Either one binds a socket the tracker never
        // reaches, and both look exactly like "head tracking just doesn't work"
        // from the game.
        int ReadPortChecked(const cameraunlock::IniReader& reader, int fallback)
        {
            const int raw = reader.ReadInt("Network", "UdpPort", fallback);
            bool valid = false;
            const std::uint16_t port = cameraunlock::NormalizeUdpPort(
                raw, static_cast<std::uint16_t>(fallback), valid);
            if (!valid)
                Log::Line("WARNING: config [Network] UdpPort = %d is not in 1024-65535 (a "
                          "non-numeric value reads as 0) - using %u.", raw, port);
            return port;
        }

        int ReadVirtualKeyChecked(const cameraunlock::IniReader& reader, const char* section,
                                  const char* key, int fallback)
        {
            const int raw = reader.ReadHex(section, key, fallback);
            if (raw >= kMinVirtualKey && raw <= kMaxVirtualKey) return raw;
            Log::Line("WARNING: config [%s] %s = 0x%X is not a virtual-key "
                      "code (0x%02X-0x%02X) - using 0x%02X.",
                section, key, raw, kMinVirtualKey, kMaxVirtualKey, fallback);
            return fallback;
        }

        // Warned once per process rather than once per load: config is
        // reloadable, and repeating this on every reload buries it.
        //
        // The old value is deliberately NOT migrated into the new keys. The
        // single Smoothing value carried a hidden 0.15 floor, so the number in
        // an existing config does not mean what it used to: copying it across
        // would hand a local user smoothing they never chose under the new
        // semantics, and copying it into only one of the two keys would be a
        // guess about which connection they were on.
        void WarnRetiredSmoothingKey(const cameraunlock::IniReader& reader,
                                     const char* section, const char* key)
        {
            static bool warned = false;
            if (warned) return;
            if (reader.ReadString(section, key, "").empty()) return;
            warned = true;
            Log::Line(
                "WARNING: Config key [%s] %s has been retired and is IGNORED. Smoothing is "
                "now two keys: LocalSmoothing (default 0, applies to a tracker on this "
                "machine) and RemoteSmoothing (default 0.15, applies to a tracker on the "
                "network). The old value is not migrated because the semantics changed - it "
                "carried a hidden 0.15 floor that no longer exists. Set the two new keys.",
                section, key);
        }

        // The four position limits are gone, not renamed, so an INI carrying
        // them from an older build now describes nothing. Saying so beats a
        // player setting LimitZBack to hold the camera in the cabinet and
        // watching it lean straight out anyway.
        void WarnRetiredLimitKeys(const cameraunlock::IniReader& reader)
        {
            static bool warned = false;
            if (warned) return;
            for (const char* key : {"LimitX", "LimitY", "LimitZ", "LimitZBack"}) {
                if (reader.ReadString("Position", key, "").empty()) continue;
                warned = true;
                Log::Line(
                    "WARNING: Config key [Position] %s has been retired and is IGNORED. The "
                    "mod no longer limits how far the camera leans - it follows your head "
                    "wherever you take it. Delete the Limit keys from the INI.", key);
                return;
            }
        }
    }

    void LoadConfig(const std::string& exeDir, Config& out)
    {
        cameraunlock::IniReader ini;
        const std::string path = IniPath(exeDir);
        // WriteDefaultConfigIfMissing runs first, so reaching this branch means
        // the file could not be created or could not be read back. Saying so is
        // the difference between "my settings are being ignored" being
        // answerable from the log and not: every value below silently keeps its
        // built-in default.
        if (!ini.Open(path)) {
            Log::Line("config: no readable %s - running on built-in defaults.", path.c_str());
            return;
        }

        out.udp_port           = ReadPortChecked(ini, out.udp_port);

        out.enable_on_startup  = ini.ReadBool("General", "EnableOnStartup", out.enable_on_startup);
        out.world_space_yaw    = ini.ReadBool("General", "WorldSpaceYaw",   out.world_space_yaw);

        out.yaw_mode_key       = ReadVirtualKeyChecked(ini, "Hotkeys", "YawModeKey",
                                                       out.yaw_mode_key);

        out.yaw_sensitivity    = ReadFloatChecked(ini, "Rotation", "YawSensitivity",
                                                  out.yaw_sensitivity, -kMaxSensitivity, kMaxSensitivity);
        out.pitch_sensitivity  = ReadFloatChecked(ini, "Rotation", "PitchSensitivity",
                                                  out.pitch_sensitivity, -kMaxSensitivity, kMaxSensitivity);
        out.roll_sensitivity   = ReadFloatChecked(ini, "Rotation", "RollSensitivity",
                                                  out.roll_sensitivity, -kMaxSensitivity, kMaxSensitivity);
        out.invert_yaw         = ini.ReadBool("Rotation", "InvertYaw",   out.invert_yaw);
        out.invert_pitch       = ini.ReadBool("Rotation", "InvertPitch", out.invert_pitch);
        out.invert_roll        = ini.ReadBool("Rotation", "InvertRoll",  out.invert_roll);

        out.local_smoothing    = ReadFloatChecked(ini, "Rotation", "LocalSmoothing",
                                                  out.local_smoothing, 0.0f, 1.0f);
        out.remote_smoothing   = ReadFloatChecked(ini, "Rotation", "RemoteSmoothing",
                                                  out.remote_smoothing, 0.0f, 1.0f);
        WarnRetiredSmoothingKey(ini, "Rotation", "Smoothing");

        // Clamped to the same bounds EffectiveFov enforces, so the INI and the
        // hook agree on what is acceptable rather than the hook quietly
        // repairing a value the INI let through. A negative or zero override
        // means "off", which is why the override floor is 0 and not
        // kMinConfiguredFov.
        out.fov_override       = ReadFloatChecked(ini, "Camera", "FovOverride",
                                                  out.fov_override, 0.0f, kMaxConfiguredFov);
        out.fov_offset         = ReadFloatChecked(ini, "Camera", "FovOffset",
                                                  out.fov_offset, -kMaxConfiguredFov, kMaxConfiguredFov);

        out.camera_offset_forward = ReadFloatChecked(ini, "Camera", "OffsetForward",
                                                    out.camera_offset_forward,
                                                    -kMaxCameraOffset, kMaxCameraOffset);
        out.camera_offset_up      = ReadFloatChecked(ini, "Camera", "OffsetUp",
                                                    out.camera_offset_up,
                                                    -kMaxCameraOffset, kMaxCameraOffset);
        out.camera_offset_right   = ReadFloatChecked(ini, "Camera", "OffsetRight",
                                                    out.camera_offset_right,
                                                    -kMaxCameraOffset, kMaxCameraOffset);

        out.gameplay_only      = ini.ReadBool("GameState", "GameplayOnly", out.gameplay_only);
        out.suppress_during_camera_sequences =
            ini.ReadBool("GameState", "SuppressDuringCameraSequences",
                         out.suppress_during_camera_sequences);

        out.position_enabled   = ini.ReadBool("Position", "Enabled", out.position_enabled);
        out.position_sensitivity_x = ReadFloatChecked(ini, "Position", "SensitivityX",
                                                      out.position_sensitivity_x,
                                                      -kMaxSensitivity, kMaxSensitivity);
        out.position_sensitivity_y = ReadFloatChecked(ini, "Position", "SensitivityY",
                                                      out.position_sensitivity_y,
                                                      -kMaxSensitivity, kMaxSensitivity);
        out.position_sensitivity_z = ReadFloatChecked(ini, "Position", "SensitivityZ",
                                                      out.position_sensitivity_z,
                                                      -kMaxSensitivity, kMaxSensitivity);
        WarnRetiredSmoothingKey(ini, "Position", "Smoothing");
        WarnRetiredLimitKeys(ini);
    }

    void WriteDefaultConfigIfMissing(const std::string& exeDir)
    {
        const std::string path = IniPath(exeDir);
        if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES) return;

        // "wx" creates exclusively: the existence test above and this open are
        // two separate operations, and truncating whatever turned up at that
        // path in between - a file another process created, or a link planted
        // at it - is not something writing a default config should ever do.
        FILE* file = nullptr;
        fopen_s(&file, path.c_str(), "wx");
        if (!file) {
            Log::Line("config: could not write %s - the mod runs on built-in defaults "
                      "and there is no file to edit.", path.c_str());
            return;
        }
        std::fprintf(file,
            "; Pinball FX Head Tracking - configuration\n"
            "; Edit values, restart the game to apply.\n\n"
            "[Network]\n"
            "UdpPort=4242\n\n"
            "[General]\n"
            "EnableOnStartup=1\n"
            "; Yaw mode: 1 = horizon-locked yaw about the world up-axis (default),\n"
            "; 0 = yaw about the camera's own up-axis. Toggle in-game with Page Down.\n"
            "WorldSpaceYaw=1\n\n"
            "[Hotkeys]\n"
            "; Virtual-key code for the yaw-mode toggle. 0x22 = Page Down.\n"
            "; The Ctrl+Shift+H chord always toggles it as well.\n"
            "YawModeKey=0x22\n\n"
            "[Rotation]\n"
            "YawSensitivity=1.0\n"
            "PitchSensitivity=1.0\n"
            "RollSensitivity=1.0\n"
            "InvertYaw=0\n"
            "InvertPitch=0\n"
            "InvertRoll=0\n"
            "; Smoothing applied when the tracker runs on this machine (loopback).\n"
            "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
            "LocalSmoothing=0.0\n"
            "; Smoothing applied when the tracker is a remote device on the network.\n"
            "; 0 = no smoothing, 1 = heavy. Covers rotation and position.\n"
            "RemoteSmoothing=0.15\n\n"
            "[Camera]\n"
            "; The game has no field-of-view control of its own, so this is it.\n"
            "; Both are degrees, and 0/0 leaves the FOV exactly as each table view\n"
            "; sets it. FovOverride replaces the angle outright (e.g. 90).\n"
            "; FovOffset adds to whatever each view asks for, which keeps the\n"
            "; differences between the views. FovOverride wins if you set both.\n"
            "FovOverride=0\n"
            "FovOffset=0\n"
            "; Move the camera the game placed, in centimetres, while a table is\n"
            "; in play. OffsetForward runs along the line of sight, so a negative\n"
            "; value pulls back and a positive one pushes in; OffsetUp and\n"
            "; OffsetRight shift it across the view. Paired with FovOffset this is\n"
            "; a dolly-zoom: widen the lens and dolly in to match, and the table\n"
            "; keeps its size in frame but stops looking flat. The\n"
            "; cabinet/portrait views are the ones that need it - they sit far\n"
            "; back behind a ~15 degree lens, which is what flattens them.\n"
            "; All four can be tuned in game, one chord pair each: Ctrl+Shift+Q/A\n"
            "; is FovOffset, W/S is OffsetForward, E/D is OffsetUp, R/F is\n"
            "; OffsetRight, and Ctrl+Shift+Z puts them back to what is written\n"
            "; here. Every change is written to HeadTracking.log as a block to\n"
            "; paste back in - nothing is saved automatically.\n"
            "OffsetForward=0\n"
            "OffsetUp=0\n"
            "OffsetRight=0\n\n"
            "[GameState]\n"
            "; Head tracking only while a table is actually being played. The menus\n"
            "; render a live table behind them and the camera there is a composed\n"
            "; shot, so tracking is held until the game starts, and again while it\n"
            "; is paused.\n"
            "GameplayOnly=1\n"
            "; Hold the camera still while a table plays a scripted camera move - the\n"
            "; fly-in at the start of a table, and the cut-ins around table events.\n"
            "; Those moves already own the camera.\n"
            "SuppressDuringCameraSequences=1\n\n"
            "[Position]\n"
            "; Leaning to see past a ramp is most of what head tracking buys you on\n"
            "; a pinball table, so this is on by default. Page Up cycles it.\n"
            "; The camera moves exactly as far as your tracker says your head did.\n"
            "; If the movement feels too small, raise the translation output in your\n"
            "; tracker - that is where pose shaping belongs, so one profile then\n"
            "; behaves the same in every game.\n"
            "Enabled=1\n"
            "SensitivityX=1.0\n"
            "SensitivityY=1.0\n"
            "SensitivityZ=1.0\n"
            "; There are no lean limits: the camera follows your head as far\n"
            "; as you take it, including back out of the cabinet.\n");
        std::fclose(file);
    }
}
