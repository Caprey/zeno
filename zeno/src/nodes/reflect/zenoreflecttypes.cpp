#pragma once

#include <zeno/core/common.h>
#include <zeno/types/ObjectDef.h>
#include <zeno/core/reflectdef.h>
#include <glm/glm.hpp>
#include "reflect/metadata.hpp"
#include "reflect/registry.hpp"
#include "reflect/container/object_proxy"
#include "reflect/container/any"
#include "reflect/container/arraylist"
#include <vector>
#include <memory>

/*
������ֱ�ӽ��Ѷ�������ͻ����������ͣ�ֱ��ע�ᷴ�����ͣ������Ҫ������ɫ�����Ե�ui\zenqt\style\colormanager.cpp�ϵǼ�

����һ�ֶ����Զ���primitive���͵ķ�ʽ����һ���������ļ��ﶨ�巴���࣬�ο�HeatmapObject.h���HeatmapData2��
�����ַ�ʽ��ȱ���ǲ�������ö��ֵ��hashcode������gParamType_Heatmap2���֣��ô��ǿ���ͳһ������ɫ�����ԡ�
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
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(glm::mat3, Matrix3)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(glm::mat4, Matrix4)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::vector<std::string>, StringList)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::vector<int>, IntList)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(std::vector<float>, FloatList)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::CurvesData, Curve)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::BCurveObject, BCurve)
REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::HeatmapData, Heatmap)

REFLECT_REGISTER_RTTI_TYPE_WITH_NAME(zeno::ReflectCustomUI, ReflectCustomUI)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::ParamControl)

//ֻ�ܶ������ָ��
REFLECT_REGISTER_RTTI_TYPE_MANUAL(std::shared_ptr<IObject>)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(std::shared_ptr<const IObject>)
//���ܶ�����ָ��ʵʩ���䣬��ΪAny��ת���Ĺ������޷�תΪ����ָ�룬ͬʱҲ��Ϊ������ģ���������͹��췴�䣬������
//REFLECT_REGISTER_OBJECT(zeno::PrimitiveObject, Primitive)
//REFLECT_REGISTER_OBJECT(zeno::CameraObject, Camera)
//REFLECT_REGISTER_OBJECT(zeno::LightObject, Light)
//REFLECT_REGISTER_OBJECT(zeno::IObject, IObject)
//REFLECT_REGISTER_OBJECT(zeno::ListObject, List)
//REFLECT_REGISTER_OBJECT(zeno::DictObject, Dict)
//REFLECT_REGISTER_OBJECT(zeno::MeshObject, Mesh)
//REFLECT_REGISTER_OBJECT(zeno::ParticlesObject, Particles)