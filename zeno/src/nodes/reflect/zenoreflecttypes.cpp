#pragma once

#include <zeno/core/common.h>
#include <zeno/types/ObjectDef.h>
#include <zeno/core/reflectdef.h>
#include "reflect/metadata.hpp"
#include "reflect/registry.hpp"
#include "reflect/container/object_proxy"
#include "reflect/container/any"
#include "reflect/container/arraylist"
#include <vector>
#include <memory>

/*
这里是直接将已定义的类型或者内置类型，直接注册反射类型，如果需要定义颜色，可以到ui\zenqt\style\colormanager.cpp上登记

还有一种定义自定义primitive类型的方式是在一个单独的文件里定义反射类，参考HeatmapObject.h里的HeatmapData2，
但这种方式的缺点是不会生成枚举值的hashcode，比如gParamType_Heatmap2这种，好处是可以统一定义颜色等属性。
*/

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

REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::ReflectCustomUI, ReflectCustomUI)

//由于对象类型必须用shared_ptr包着，所以只能用这种做法定义（继承shared_ptr<T>的情况没考虑，而且反射起来可能有坑）
REFLECT_REGISTER_OBJECT(zeno::PrimitiveObject, Primitive)
REFLECT_REGISTER_OBJECT(zeno::CameraObject, Camera)
REFLECT_REGISTER_OBJECT(zeno::LightObject, Light)
REFLECT_REGISTER_OBJECT(zeno::IObject, IObject)
REFLECT_REGISTER_OBJECT(zeno::ListObject, List)
REFLECT_REGISTER_OBJECT(zeno::DictObject, Dict)