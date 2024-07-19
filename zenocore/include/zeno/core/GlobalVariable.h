#pragma once

#include <zeno/core/common.h>
#include <zeno/utils/api.h>
#include <memory>
#include <map>
#include <set>
#include <stack>
#include <string>
#include "reflect/container/any"


namespace zeno {

struct INode;

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
    void propagateDirty(std::weak_ptr<INode> wpCurrNode, GVariable globalvar);
    void getUpstreamNodes(std::shared_ptr<INode> spCurrNode, std::set<ObjPath>& depNodes, std::set<ObjPath>& upstreams, std::string outParamName = "");
    void mark_dirty_by_dependNodes(std::shared_ptr<INode> spCurrNode, bool bOn, std::set<ObjPath> nodesRange, std::string inParamName = "");
    //nodepath�Ľڵ㲻������ĳ��ȫ�ֱ���
    ZENO_API void removeDependGlobalVaraible(const ObjPath& nodepath, std::string name);
    //���nodepath�Ľڵ�����ĳ��ȫ�ֱ���
    void addDependGlobalVaraible(const ObjPath& nodepath, std::string name, zeno::reflect::RTTITypeInfo type);

    ZENO_API bool updateVariable(const GVariable& newvar);
    ZENO_API bool overrideVariable(const GVariable& var);
    void cancelOverride(std::string varname, GVariable& cancelVar);
    ZENO_API zeno::reflect::Any getVariable(std::string varname);

    GlobalVariableStack globalVariableStack;

    std::map<ObjPath, std::map<std::string, zeno::reflect::RTTITypeInfo>> globalVariablesNameTypeMap;  //�洢�ڵ�������Щȫ�ֱ���<�ڵ�path<�������ƣ���������>>
};

struct GlobalVariableOverride {
    std::weak_ptr<INode> currNode;
    GVariable gvar;
    bool overrideSuccess;

    ZENO_API GlobalVariableOverride(std::weak_ptr<INode> currNode, std::string gvarName, zeno::reflect::Any gvar);;
    ZENO_API ~GlobalVariableOverride();;
    ZENO_API bool updateGlobalVariable(GVariable globalVariable);
};

}
