#pragma once
#include "perception.hpp"
#include "detection.hpp"
namespace hive::game {
struct HoneyDetection final : Detection { Reading<std::uint64_t> amount; };
std::shared_ptr<const Detector> honey_detector();
}
