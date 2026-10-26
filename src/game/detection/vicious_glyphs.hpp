// RGB notification signatures from NatroTeam/NatroMacroDev's vb-rewrite branch.
// GPL-3.0; see the repository LICENSE.
// https://github.com/NatroTeam/NatroMacroDev/blob/b342e75d03776b5c9c8485c157e6b30647b08bc7/nm_image_assets/kill/bitmaps.ahk
#pragma once
#include <array>
#include <cstdint>
#include <span>
namespace hive::game::detail {
struct ViciousGlyph { int width,height; bool defeated; std::span<const std::uint32_t> rgb; };
inline constexpr std::array<std::uint32_t,27> vicious_found_2{
    0xfecb4d,0xd2a944,0xfdcb4d,
    0xc29c40,0x231f20,0xc59f41,
    0xba963f,0x231f20,0xba963f,
    0xba963f,0x231f20,0xba963f,
    0xba963f,0x231f20,0xba963f,
    0xbd983f,0x231f20,0xbc973f,
    0xf4c34b,0x997c38,0xf3c34b,
    0xe1b447,0x5e4d2c,0xe1b447,
    0xd3a944,0x3a3125,0xd3a944,
};
inline constexpr std::array<std::uint32_t,36> vicious_found_3{
    0xfecc4d,0x755f31,0x745f31,0xfecc4d,
    0xffcc4d,0x3e3425,0x3e3425,0xffcc4d,
    0xffcc4d,0x3e3425,0x3e3425,0xffcc4d,
    0xffcc4d,0x3e3425,0x3e3425,0xffcc4d,
    0xffcc4d,0x3e3425,0x3e3425,0xffcc4d,
    0xffcc4d,0x443927,0x463a27,0xffcc4d,
    0xffcc4d,0xddb246,0xdbb046,0xffcc4d,
    0xffcc4d,0x6d592f,0x6c592f,0xffcc4d,
    0xffcc4d,0x6b582f,0x6a572e,0xffcc4d,
};
inline constexpr std::array<std::uint32_t,15> vicious_dead_2{
    0xea6555,0xeb5e6d,0xe64c62,0xdd2e44,0xdd2e44,
    0xf38d5e,0xea5d65,0xde3046,0xdd2e44,0xdd2e44,
    0xe9566b,0xdf344a,0xdd2e44,0xdd2e44,0xdd2e44,
};
inline constexpr std::array<std::uint32_t,12> vicious_dead_3{
    0xe85166,0xea596e,0xe64b61,0xdd2e44,
    0xef7566,0xe85166,0xde3046,0xdd2e44,
    0xec6667,0xdf3349,0xdd2e44,0xdd2e44,
};
inline constexpr std::array<ViciousGlyph,4> vicious_glyphs{{
    {3,9,false,vicious_found_2},
    {4,9,false,vicious_found_3},
    {5,3,true,vicious_dead_2},
    {4,3,true,vicious_dead_3}
}};
}
