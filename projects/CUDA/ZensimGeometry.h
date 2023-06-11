#pragma once

#include "zensim/geometry/Structure.hpp"
#include "zensim/geometry/Structurefree.hpp"
#include "zensim/physics/ConstitutiveModel.hpp"
#include <zeno/zeno.h>

namespace zeno {

struct ZenoAffineMatrix : zeno::IObject {
  using mat4 = zs::vec<float, 4, 4>;
  mat4 affineMap;
};

} // namespace zeno