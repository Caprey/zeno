#pragma once

#include <zeno/core/common.h>

#include <zeno/utils/api.h>
#include <memory>
#include <map>
#include <set>
#include <stack>
#include <string>

#include "reflect/core.hpp"
#include "reflect/type.hpp"
#include "reflect/metadata.hpp"
#include "reflect/registry.hpp"
#include "reflect/container/object_proxy"
#include "reflect/container/any"
#include "reflect/container/arraylist"
#include "reflect/reflection.generated.hpp"


namespace zeno {

    class INode;

struct GVariable {
    std::string name;
    zeno::reflect::Any gvar;

    GVariable() { name = ""; }
    GVariable(std::string globalvarName, zeno::reflect::Any globalvar) :name(globalvarName), gvar(globalvar) {};
    bool operator==(const GVariable& var1) {
        return var1.name == name && var1.gvar.type() == gvar.type();
    }
};

struct OverrdeVector {
    std::stack<GVariable> stack;
    zeno::reflect::RTTITypeInfo variableType;
    OverrdeVector(std::stack<GVariable> v, zeno::reflect::RTTITypeInfo vtype) : stack(v), variableType(vtype) {}
};

struct GlobalVariableStack {
    std::map<std::string, OverrdeVector> GlobalVariables;

    bool updateVariable(const GVariable& newvar);
    bool overrideVariable(const GVariable& var);
    void cancelOverride(std::string varname, GVariable& cancelVar);
    zeno::reflect::Any getVariable(std::string varname);
};

struct GlobalVariableManager
{
public:
    //��ѯ����dependType���ͽڵ㲢����dirty
    void propagateDirty(INode* wpCurrNode, GVariable globalvar);
    void getUpstreamNodes(INode* spCurrNode, std::set<ObjPath>& depNodes, std::set<ObjPath>& upstreams, std::string outParamName = "");
    void mark_dirty_by_dependNodes(INode* spCurrNode, bool bOn, std::set<ObjPath> nodesRange, std::string inParamName = "");
    //nodepath�Ľڵ㲻������ĳ��ȫ�ֱ���
    void removeDependGlobalVaraible(const ObjPath& nodepath, std::string name);
    //���nodepath�Ľڵ�����ĳ��ȫ�ֱ���
    void addDependGlobalVaraible(const ObjPath& nodepath, std::string name, zeno::reflect::RTTITypeInfo type);

    bool updateVariable(const GVariable& newvar);
    bool overrideVariable(const GVariable& var);
    void cancelOverride(std::string varname, GVariable& cancelVar);
    zeno::reflect::Any getVariable(std::string varname);

    GlobalVariableStack globalVariableStack;

    std::map<ObjPath, std::map<std::string, zeno::reflect::RTTITypeInfo>> globalVariablesNameTypeMap;  //�洢�ڵ�������Щȫ�ֱ���<�ڵ�path<�������ƣ���������>>
};

}
//ȫ�ֱ�������
REFLECT_REGISTER_RTTI_TYPE_MANUAL(int)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(float)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(double)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(std::string)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec2i)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec2f)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec2s)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec3i)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec3f)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec3s)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec4i)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec4f)
REFLECT_REGISTER_RTTI_TYPE_MANUAL(zeno::vec4s)