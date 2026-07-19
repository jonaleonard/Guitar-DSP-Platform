#pragma once

#include <vector>

namespace dsp {

// Short synthetic guitar-cab-like IR for demos/tests (no external files required).
std::vector<float> makeSyntheticCabIr(int length, unsigned int sampleRate);

} // namespace dsp
