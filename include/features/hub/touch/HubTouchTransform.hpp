#pragma once

#include <utility>
#include <stdint.h>

namespace HubTouchTransform {

struct Mapping {
  bool swapXY = false;
  bool mirrorX = false;
  bool mirrorY = false;
};

inline void apply(int16_t& x, int16_t& y, uint16_t width, uint16_t height,
                  const Mapping& mapping) {
  if (mapping.swapXY) {
    std::swap(x, y);
  }
  if (mapping.mirrorX) {
    x = static_cast<int16_t>(static_cast<int32_t>(width) - 1 - x);
  }
  if (mapping.mirrorY) {
    y = static_cast<int16_t>(static_cast<int32_t>(height) - 1 - y);
  }
}

inline void apply(float& x, float& y, const Mapping& mapping) {
  if (mapping.swapXY) {
    std::swap(x, y);
  }
  if (mapping.mirrorX) {
    x = -x;
  }
  if (mapping.mirrorY) {
    y = -y;
  }
}

}  // namespace HubTouchTransform
