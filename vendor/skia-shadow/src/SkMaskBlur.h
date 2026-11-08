/*
 * Copyright 2017 Google LLC
 * Use of this source code is governed by a BSD-style license in ../LICENSE.
 */
#pragma once

#include <cstdint>
#include <vector>

// A8-only scalar adaptation of SkMaskBlurFilter and SkGaussFilter. Coordinates of the result
// move left/up by Border(sigma); transparent samples outside the input are included.
namespace skia_mask {
    int Border(double sigma);
    std::vector<std::uint8_t> Blur(const std::vector<std::uint8_t>& src, int width, int height, double sigma);
}
