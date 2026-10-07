#pragma once
#include "../core/model.hpp"
#include <functional>
#include <span>

namespace hive {
struct OcrImage {
    int width{},height{};
    std::span<const std::uint8_t> rgba;
};
using OcrReader=std::function<std::vector<TextLine>(const OcrImage&)>;
std::vector<TextLine> split_ocr_columns(const TextLine& line,std::span<const TextLine> words);
std::vector<TextLine> recognize_tiled(const OcrImage& image,int maximum,const OcrReader& reader,const std::function<bool()>& cancelled={});
}
