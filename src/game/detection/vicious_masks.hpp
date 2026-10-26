#pragma once
#include "mask_match.hpp"
namespace hive::game::detail {
// Fixed UI words extracted from the reported Cactus captures. No player names,
// health values, field scenery, or exact RGB colors are part of these masks.
inline const cv::Mat& vicious_name() {
    static constexpr std::uint64_t bits[]{0x1001800ULL,0x0ULL,0x3001c00ULL,0x1ULL,0xe000000000000983ULL,0xfULL,0xe0000000000000c3ULL,0x1cULL,0x60000000000008c3ULL,0x18ULL,0xe07e6187c31e18c6ULL,0x7c3e18ULL,0xe47e618fe31f9846ULL,0x7e7f0fULL,0xe606618c73039866ULL,0xee771fULL,0xe01e619c73019866ULL,0xff7f98ULL,0x6070719c7301986cULL,0x707b8ULL,0x6660738c7303983cULL,0x70338ULL,0xe47e7f8fe31f983cULL,0x7e3f1fULL,0xe03e6f07c31e1838ULL,0x7c3c0fULL,0x0ULL,0x0ULL,0x0ULL,0x40000ULL};
    static const cv::Mat mask=reference(88,15,bits); return mask;
}
inline const cv::Mat& vicious_attack() {
    static constexpr std::uint64_t bits[]{0xf000000022ULL,0x880ULL,0x40005f810080c33ULL,0x581fff4ULL,0xf879ff87cdfdef2ULL,0x3ffbffffffeULL,0xf87fff83efbd7feULL,0x3fdf7effffbULL,0xe87fffc7ffbc3deULL,0x3fcf3f7fffbULL,0x1ec3dffc7fdf7fceULL,0x3fcf7ffffdfULL,0xe038cfc3bcf4cc4ULL,0x1fcf0ffffdfULL,0x0ULL,0xf000000000ULL};
    static const cv::Mat mask=reference(106,8,bits); return mask;
}
inline const cv::Mat& vicious_toast() {
    static constexpr std::uint64_t bits[]{0x3e000000200443ULL,0x0ULL,0x3200000001800000ULL,0x66000000000063ULL,0x6000003000ULL,0x3200020000801800ULL,0x104603000c018063ULL,0x50c181e02040b018ULL,0x33c1821810c21e00ULL,0x784607911e33c666ULL,0xf9e3c3e078f1f03cULL,0x33e3c73c79c79f01ULL,0x44660191b3306636ULL,0x9b32666009833026ULL,0x332267604cc89981ULL,0x6c7e0399b3306636ULL,0x1bf7e46039c3307eULL,0x3237e2786ccd9881ULL,0x7c460611b330263cULL,0x1bf7e46061f3307eULL,0x3237e27c7ccf9881ULL,0x4c60411b330661cULL,0x1830666041933006ULL,0x230626604c09881ULL,0x6c66059b3f33e61cULL,0x1963666059933036ULL,0x363e6766ccd9d81ULL,0x387e079f1e33c618ULL,0x19e3c3e079f3303cULL,0x3c3c63c38c70f01ULL};
    static const cv::Mat mask=reference(190,10,bits); return mask;
}
}
