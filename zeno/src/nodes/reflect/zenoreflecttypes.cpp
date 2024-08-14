#pragma once

#include <zeno/core/common.h>
#include <zeno/types/ObjectDef.h>
#include "reflect/core.hpp"
#include "reflect/type.hpp"
#include "reflect/metadata.hpp"
#include "reflect/registry.hpp"
#include "reflect/container/object_proxy"
#include "reflect/container/any"
#include "reflect/container/arraylist"
#include <vector>
#include <memory>
//#include "reflect/reflection.generated.hpp"


REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(bool, Bool)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(int, Int)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(float, Float)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(double, Double)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::string, String)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec2i, Vec2i)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec2f, Vec2f)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec2s, Vec2s)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec3i, Vec3i)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec3f, Vec3f)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec3s, Vec3s)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec4i, Vec4i)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec4f, Vec4f)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::vec4s, Vec4s)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::vector<std::string>, StringList)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::vector<int>, IntList)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::vector<float>, FloatList)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::CurvesData, Curve)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::BCurveObject, BCurve)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::HeatmapData, Heatmap)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::shared_ptr<zeno::PrimitiveObject>, Primitive)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::shared_ptr<zeno::CameraObject>, Camera)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::shared_ptr<zeno::LightObject>, Light)

REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::shared_ptr<zeno::IObject>, sharedIObject)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::shared_ptr<const zeno::IObject>, constIObject)

REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::shared_ptr<zeno::ListObject>, List)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::shared_ptr<zeno::DictObject>, Dict)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::unique_ptr<zeno::IObject>, uniqueIObject)
