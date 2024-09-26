#pragma once

#include <memory>
#include "reflect/core.hpp"
#include "reflect/type"
#include "reflect/reflection_traits.hpp"


namespace zeno {

    struct _Param
    {
        std::string mapTo;
        std::string dispName;
        zeno::reflect::Any defl;
        std::string wildCardGroup;
        ParamControl ctrl;
        bool bInnerParam = false;
        zeno::reflect::Any ctrlProps;
    };

    struct _ObjectParam
    {
        std::string mapTo;
        std::string dispName;
        SocketType type;
        std::string wildCardGroup;
        std::string constrain;
    };

    struct _CommonParam
    {
        std::string mapTo;
        std::variant<ParamObject, ParamPrimitive> param;
    };

    struct _ParamGroup {
        std::string name = "Group1";
        std::vector<_Param> params;
    };

    using _Group = std::vector<_CommonParam>;

    struct _ParamTab {
        std::string name = "Tab1";
        std::vector<_ParamGroup> groups;
    };

    //ֻ������ǩ���ϵĲ�����ӳ����Ϣ���������Գ�Ա��������Ĳ��������
    struct ReflectCustomUI
    {
        _Group inputParams;
        _Group outputParams;
        CustomUIParams customUI;    //���������ֵ�Ͳ������Զ��岼��
    };

}