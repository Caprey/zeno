#include <zeno/core/Session.h>
#include <zeno/core/IObject.h>
#include <zeno/core/Assets.h>
#include <zeno/core/ObjectManager.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/extra/GlobalComm.h>
#include <zeno/extra/GlobalError.h>
#include <zeno/extra/EventCallbacks.h>
#include <zeno/types/UserData.h>
#include <zeno/core/Graph.h>
#include <zeno/core/INode.h>
#include <zeno/core/CoreParam.h>
#include <zeno/utils/safe_at.h>
#include <zeno/utils/logger.h>
#include <zeno/utils/string.h>
#include <zeno/utils/helper.h>
#include "rapidjson/document.h"
#include "rapidjson/writer.h"
#include "rapidjson/stringbuffer.h"
#include <zeno/extra/SubnetNode.h>
#include <zeno/extra/GraphException.h>
#include <zeno/core/ReferManager.h>
#include <zeno/core/GlobalVariable.h>

#include <reflect/core.hpp>
#include <reflect/type.hpp>
#include <reflect/metadata.hpp>
#include <reflect/registry.hpp>
#include <reflect/container/object_proxy>
#include <reflect/container/any>
#include <reflect/container/arraylist>

#include "reflect/reflection.generated.hpp"

using namespace zeno::reflect;

extern std::map<size_t, std::string> g_clrMapping;


namespace zeno {

namespace {

struct ImplNodeClass : INodeClass {
    std::shared_ptr<INode>(*ctor)();

    ImplNodeClass(std::shared_ptr<INode>(*ctor)(), CustomUI const& customui, std::string const& name)
        : INodeClass(customui, name), ctor(ctor) {}

    virtual std::shared_ptr<INode> new_instance(std::shared_ptr<Graph> pGraph, std::string const &name) override {
        std::shared_ptr<INode> spNode = ctor();
        spNode->initUuid(pGraph, classname);
        spNode->set_name(name);

        //init all params, and set defl value
        for (const ParamObject& param : m_customui.inputObjs)
        {
            spNode->add_input_obj_param(param);
        }
        for (const ParamPrimitive& param : customUiToParams(m_customui.inputPrims))
        {
            spNode->add_input_prim_param(param);
        }
        for (const ParamPrimitive& param : m_customui.outputPrims)
        {
            spNode->add_output_prim_param(param);
        }
        for (const ParamObject& param : m_customui.outputObjs)
        {
            spNode->add_output_obj_param(param);
        }
        return spNode;
    }
};

struct ReflectNodeClass : INodeClass {
    std::function<std::shared_ptr<INode>()> ctor;
    zeno::reflect::TypeBase* typebase;

    ReflectNodeClass(std::function<std::shared_ptr<INode>()> ctor, std::string const& nodecls, zeno::reflect::TypeBase* pTypeBase)
        : INodeClass(CustomUI(), nodecls)
        , ctor(ctor)
        , typebase(pTypeBase)
    {
    }

    std::shared_ptr<INode> new_instance(std::shared_ptr<Graph> pGraph, std::string const& name) override {

        std::shared_ptr<INode> spNode = ctor();
        spNode->initUuid(pGraph, classname);
        spNode->set_name(name);

        if (m_customui.inputPrims.tabs.empty())
        {
            zeno::ParamTab tab;
            tab.name = "Tab1";
            zeno::ParamGroup group;
            group.name = "Group1";
            tab.groups.emplace_back(group);
            m_customui.inputPrims.tabs.emplace_back(tab);
        }

        if (!m_customui.inputPrims.tabs.empty() && !m_customui.inputPrims.tabs[0].groups.empty()) {
            m_customui.inputPrims.tabs[0].groups[0].params.clear();
        }
        m_customui.inputObjs.clear();
        m_customui.outputPrims.clear();
        m_customui.outputObjs.clear();

        std::set<std::string> reg_inputobjs, reg_inputprims, reg_outputobjs, reg_outputprims;

        //�ȱ������г�Ա���ռ����в�����Ŀǰ�ٶ����г�Ա��������Ϊ�ڵ�Ĳ������ڣ��������������ָ��
        for (IMemberField* field : typebase->get_member_fields()) {
            // �ҵ�����Ҫ��
            std::string field_name(field->get_name().c_str());
            std::string param_name;
            if (const zeno::reflect::IRawMetadata* metadata = field->get_metadata()) {

                //name:
                if (const zeno::reflect::IMetadataValue* value = metadata->get_value("DisplayName")) {
                    param_name = value->as_string();
                }
                else {
                    param_name = field_name;
                }
                //TODO: ���ƺϷ����ж�

                //���������ж�һ����object����primitive
                zeno::reflect::TypeHandle fieldType = field->get_field_type();

                auto iter2 = g_clrMapping.find(zeno::reflect::get_type<int>().type_hash());
                if (iter2 != g_clrMapping.end()) {
                    int j;
                    j = 0;
                }

                ParamType type = fieldType.type_hash();
                //role:
                NodeDataGroup role = Role_InputObject;
                if (const zeno::reflect::IMetadataValue* value = metadata->get_value("Role")) {
                    int _role = value->as_int();
                    if (_role < Role_InputObject || _role > Role_OutputPrimitive) {
                        throw makeError<UnimplError>("parsing error when parsing reflected node.");
                    }
                    role = static_cast<NodeDataGroup>(_role);
                }
                else {
                    //û��ָ��role��һ�ɶ��ǰ�input�����Ƿ�Ϊobj�����������ж�
                    role = (type == Param_Object) ? Role_InputObject : Role_InputPrimitive;
                }

                if (role == Role_InputObject)
                {
                    if (reg_inputobjs.find(param_name) != reg_inputobjs.end()) {
                        //��Ϊ�Ƕ�����PROPERTY�ϣ����������Ͽ����ظ�д
                        throw makeError<UnimplError>("repeated name on input objs");
                    }

                    //�۲����޶���socket����
                    SocketType socketProp = Socket_Owning;
                    if (const zeno::reflect::IMetadataValue* value = metadata->get_value("Socket")) {
                        int _role = value->as_int();
                        if (_role < NoSocket || _role > Socket_WildCard) {
                            throw makeError<UnimplError>("parsing error when parsing reflected node.");
                        }
                        socketProp = (SocketType)_role;
                    }

                    //TODO: wilecard

                    ParamObject inputObj;
                    inputObj.name = param_name;
                    inputObj.type = type;
                    inputObj.socketType = socketProp;
                    m_customui.inputObjs.emplace_back(inputObj);
                    reg_inputobjs.insert(param_name);
                }
                else if (role == Role_OutputObject)
                {
                    if (reg_outputobjs.find(param_name) != reg_outputobjs.end()) {
                        //��Ϊ�Ƕ�����PROPERTY�ϣ����������Ͽ����ظ�д
                        throw makeError<UnimplError>("repeated name on input objs");
                    }
                    ParamObject outputObj;
                    outputObj.name = param_name;
                    outputObj.type = type;
                    outputObj.socketType = Socket_Output;
                    m_customui.outputObjs.emplace_back(outputObj);
                    reg_outputobjs.insert(param_name);
                }
                else if (role == Role_InputPrimitive)
                {
                    //defl value
                    zeno::reflect::Any defl = field->get_field_value(spNode.get());
                    zeno::reflect::Any controlProps;
                    ParamPrimitive prim;

                    ParamControl ctrl = getDefaultControl(type);
                    //control:
                    if (const zeno::reflect::IMetadataValue* value = metadata->get_value("Control")) {
                        ctrl = (ParamControl)value->as_int();
                        if (ctrl == Slider || ctrl == SpinBox || ctrl == SpinBoxSlider || ctrl == DoubleSpinBox)
                        {
                            if (const zeno::reflect::IMetadataValue* value = metadata->get_value("range")) {
                                if (value->is_list() && value->list_length() > 0)
                                {
                                    if (value->list_get_item(0)->is_int()) {
                                        std::vector<int> vec;
                                        for (int i = 0; i < value->list_length(); i++) {
                                            auto pItem = value->list_get_item(i);
                                            assert(pItem->is_int());
                                            vec.push_back(pItem->as_int());
                                        }
                                        if (vec.size() == 2) {
                                            controlProps = vec;
                                        }
                                    }
                                    else if (value->list_get_item(0)->is_float()) {
                                        std::vector<float> vec;
                                        for (int i = 0; i < value->list_length(); i++) {
                                            auto pItem = value->list_get_item(i);
                                            assert(pItem->is_float());
                                            vec.push_back(pItem->as_float());
                                        }
                                        if (vec.size() == 2) {
                                            controlProps = vec;
                                        }
                                    }
                                }
                            }
                        }
                        else if (ctrl == Combobox)
                        {
                            //comobox items:
                            if (const zeno::reflect::IMetadataValue* value = metadata->get_value("ComboBoxItems")) {
                                assert(value->is_list());
                                std::vector<std::string> items;
                                for (int i = 0; i < value->list_length(); i++) {
                                    items.push_back(value->list_get_item(i)->as_string());
                                }
                                controlProps = items;
                            }
                        }
                    }
                    prim.name = param_name;
                    prim.type = type;
                    prim.bInput = true;
                    prim.bVisible = true;
                    prim.control = ctrl;
                    prim.ctrlProps = controlProps;
                    prim.defl = defl;
                    prim.socketType = Socket_Primitve;
                    prim.tooltip;
                    prim.wildCardGroup;

                    //���ڻ�û��֧�ֲ㼶���ֻ�ܷ���Ĭ�ϵĲ㼶
                    m_customui.inputPrims.tabs[0].groups[0].params.emplace_back(prim);
                }
                else if (role == Role_OutputPrimitive)
                {
                    if (reg_outputprims.find(param_name) != reg_outputprims.end()) {
                        //��Ϊ�Ƕ�����PROPERTY�ϣ����������Ͽ����ظ�д
                        throw makeError<UnimplError>("repeated name on output prims");
                    }

                    ParamPrimitive prim;
                    prim.name = param_name;
                    prim.bInput = false;
                    prim.bVisible = true;
                    prim.control = NullControl;
                    prim.socketType = Socket_Primitve;
                    prim.tooltip;
                    prim.wildCardGroup;
                    m_customui.outputPrims.emplace_back(prim);
                    reg_outputprims.insert(param_name);
                }
            }
        }

        //ͨ��Ѱ��apply�����ϵĲ����ͷ���ֵ��Ϊ�ڵ���Ӳ���������ZenoReflect��û֧�ֲ������Ƶķ��䣬ֻ��������Ϣ
        for (IMemberFunction* func : typebase->get_member_functions())
        {
            const auto& funcname = func->get_name();
            if (funcname != "apply") {
                continue;
            }
            const RTTITypeInfo& ret_type = func->get_return_rtti();
            ParamType type = ret_type.get_decayed_hash() == 0 ? ret_type.hash_code() : ret_type.get_decayed_hash();
            if (type != Param_Null)
            {
                //���ڷ������ͣ�˵�����������Ҫ����һ���������
                int idx = 1;
                std::string param_name = "result";
                if (type == Param_Object) {
                    while (reg_outputobjs.find(param_name) != reg_outputobjs.end()) {
                        param_name = "result" + std::to_string(idx++);
                    }
                    ParamObject outputObj;
                    outputObj.name = param_name;
                    outputObj.bInput = false;
                    outputObj.socketType = Socket_Output;
                    outputObj.type = type;
                    m_customui.outputObjs.emplace_back(outputObj);
                    reg_outputobjs.insert(param_name);
                }
                else {
                    while (reg_outputprims.find(param_name) != reg_outputprims.end()) {
                        param_name = "result" + std::to_string(idx++);
                    }
                    ParamPrimitive outPrim;
                    outPrim.name = param_name;
                    outPrim.bInput = false;
                    outPrim.socketType = Socket_Primitve;
                    outPrim.type = type;
                    outPrim.wildCardGroup;
                    m_customui.outputPrims.emplace_back(outPrim);
                    reg_outputprims.insert(param_name);
                }
            }

            const ArrayList<RTTITypeInfo>& params = func->get_params();
            const auto& param_names = func->get_params_name();
            assert(params.size() == param_names.size());
            for (int i = 0; i < params.size(); i++)
            {
                const RTTITypeInfo& param_type = params[i];

                size_t param_code = param_type.hash_code();
                std::string const& param_name(param_names[i].c_str());
                if (param_name.empty()) {
                    //�հײ���������
                    continue;
                }
                if (!param_type.has_flags(TF_IsConst) && param_type.has_flags(TF_IsLValueRef)) {
                    //���÷��ص������������
                    if (param_type.get_decayed_hash() == zeno::reflect::type_info<std::shared_ptr<zeno::IObject>>().hash_code() ||
                        param_type.get_decayed_hash() == zeno::reflect::type_info<std::unique_ptr<zeno::IObject>>().hash_code())
                    {
                        type = Param_Object;
                        if (reg_outputobjs.find(param_name) == reg_outputobjs.end()) {
                            ParamObject outputObj;
                            outputObj.name = param_name;
                            outputObj.bInput = false;
                            outputObj.socketType = Socket_Output;
                            outputObj.type = type;
                            m_customui.outputObjs.emplace_back(outputObj);
                            reg_outputobjs.insert(param_name);
                        }
                    }
                    else {
                        type = param_type.get_decayed_hash() == 0 ? param_type.hash_code() : param_type.get_decayed_hash();
                        if (reg_outputprims.find(param_name) == reg_outputprims.end()) {
                            ParamPrimitive prim;
                            prim.name = param_name;
                            prim.bInput = false;
                            prim.bVisible = true;
                            prim.control = NullControl;
                            prim.socketType = Socket_Primitve;
                            prim.type = type;
                            prim.defl = initAnyDeflValue(type);
                            prim.tooltip;
                            prim.wildCardGroup;
                            m_customui.outputPrims.emplace_back(prim);
                            reg_outputprims.insert(param_name);
                        }
                    }
                }
                else {
                    //�۲��Ƿ�Ϊshared_ptr<IObject>
                    if (param_code == get_type<std::shared_ptr<zeno::IObject>>().type_hash() ||
                        param_code == get_type<std::unique_ptr<zeno::IObject>>().type_hash())
                    {
                        type = Param_Object;
                        if (reg_inputobjs.find(param_name) != reg_inputobjs.end()) {
                            //ͬ�������˵����Ա����������һ����ͬ���ֵĲ������ܺ�����������ֱ������
                        }
                        else {
                            ParamObject inObj;
                            inObj.name = param_name;
                            inObj.bInput = true;
                            inObj.socketType = Socket_Owning;   //TODO: Ҳ�������������ͻ���const�����Ƿ�owning.
                            inObj.type = type;
                            m_customui.inputObjs.emplace_back(inObj);
                            reg_inputobjs.insert(param_name);
                        }
                    }
                    else {
                        type = param_type.get_decayed_hash() == 0 ? param_type.hash_code() : param_type.get_decayed_hash();
                        if (reg_inputprims.find(param_name) == reg_inputprims.end()) {
                            ParamPrimitive inPrim;
                            inPrim.name = param_name;
                            inPrim.bInput = true;
                            inPrim.socketType = Socket_Primitve;
                            inPrim.type = type;
                            inPrim.defl = initAnyDeflValue(type);
                            inPrim.control = getDefaultControl(type);
                            inPrim.wildCardGroup;
                            m_customui.inputPrims.tabs[0].groups[0].params.emplace_back(inPrim);
                            reg_inputprims.insert(param_name);
                        }
                    }
                }
            }
        }

        //init all params, and set defl value
        for (const ParamObject& param : m_customui.inputObjs)
        {
            spNode->add_input_obj_param(param);
        }
        for (const ParamPrimitive& param : customUiToParams(m_customui.inputPrims))
        {
            spNode->add_input_prim_param(param);
        }
        for (const ParamPrimitive& param : m_customui.outputPrims)
        {
            spNode->add_output_prim_param(param);
        }
        for (const ParamObject& param : m_customui.outputObjs)
        {
            spNode->add_output_obj_param(param);
        }
        return spNode;
    }
};

}

ZENO_API Session::Session()
    : globalState(std::make_unique<GlobalState>())
    , globalComm(std::make_unique<GlobalComm>())
    , globalError(std::make_unique<GlobalError>())
    , eventCallbacks(std::make_unique<EventCallbacks>())
    , m_userData(std::make_unique<UserData>())
    , mainGraph(std::make_shared<Graph>("main"))
    , assets(std::make_shared<AssetsMgr>())
    , objsMan(std::make_unique<ObjectManager>())
    , referManager(std::make_unique<ReferManager>())
    , globalVariableManager(std::make_unique<GlobalVariableManager>())
{
    initReflectNodes();
    //initNodeCates();  //should init after all static initialization finished.
}

ZENO_API Session::~Session() = default;


static CustomUI descToCustomui(const Descriptor& desc) {
    //������ǰд�ĸ���ZENDEFINE
    CustomUI ui;

    ui.nickname = desc.displayName;
    ui.iconResPath = desc.iconResPath;
    ui.doc = desc.doc;
    if (!desc.categories.empty())
        ui.category = desc.categories[0];   //�ܶ�cate��ֻ��һ��

    ParamGroup default;
    for (const SocketDescriptor& param_desc : desc.inputs) {
        ParamType type = zeno::convertToType(param_desc.type, param_desc.name);
        if (isPrimitiveType(type)) {
            //�������ֵ���ͣ�����ӵ�����
            ParamPrimitive param;
            param.name = param_desc.name;
            param.type = type;
            param.defl = zeno::str2any(param_desc.defl, param.type);
            if (param_desc.socketType != zeno::NoSocket)
                param.socketType = param_desc.socketType;
            if (param_desc.control != NullControl)
                param.control = param_desc.control;
            if (starts_with(param_desc.type, "enum ")) {
                //compatible with old case of combobox items.
                param.type = zeno::types::gParamType_String;
                param.control = Combobox;
                std::vector<std::string> items = split_str(param_desc.type, ' ');
                if (!items.empty()) {
                    items.erase(items.begin());
                    param.ctrlProps = items;
                }
            }
            if (param.type != Param_Null && param.control == NullControl)
                param.control = getDefaultControl(param.type);
            param.tooltip = param_desc.doc;
            param.prop = Socket_Normal;
            param.wildCardGroup = param_desc.wildCard;
            param.bVisible = false;
            default.params.emplace_back(std::move(param));
        }
        else
        {
            //����һ����Ϊ�Ƕ���ZenoĿǰ�����͹���ǳ����ң���Щ����ֵ�ǿ��ַ���������������Ƕ�������
            ParamObject param;
            param.name = param_desc.name;
            param.type = type;
            if (param_desc.socketType != zeno::NoSocket)
                param.socketType = param_desc.socketType;
            param.bInput = true;
            param.wildCardGroup = param_desc.wildCard;
            ui.inputObjs.emplace_back(std::move(param));
        }
    }
    for (const ParamDescriptor& param_desc : desc.params) {
        ParamPrimitive param;
        param.name = param_desc.name;
        param.type = zeno::convertToType(param_desc.type, param.name);
        param.defl = zeno::str2any(param_desc.defl, param.type);
        param.socketType = NoSocket;
        //�����ؼ������Ǹ��������ƶϵġ�
        if (starts_with(param_desc.type, "enum ")) {
            //compatible with old case of combobox items.
            param.type = zeno::types::gParamType_String;
            param.control = Combobox;
            std::vector<std::string> items = split_str(param_desc.type, ' ');
            if (!items.empty()) {
                items.erase(items.begin());
                param.ctrlProps = items;
            }
        }
        if (param.type != Param_Null && param.control == NullControl)
            param.control = getDefaultControl(param.type);
        param.tooltip = param_desc.doc;
        param.bVisible = false;
        default.params.emplace_back(std::move(param));
    }
    for (const SocketDescriptor& param_desc : desc.outputs) {
        ParamType type = zeno::convertToType(param_desc.type, param_desc.name);
        if (isPrimitiveType(type)) {
            //�������ֵ���ͣ�����ӵ�����
            ParamPrimitive param;
            param.name = param_desc.name;
            param.type = type;
            param.defl = zeno::str2any(param_desc.defl, param.type);
            if (param_desc.socketType != zeno::NoSocket)
                param.socketType = param_desc.socketType;
            param.control = NullControl;
            param.tooltip = param_desc.doc;
            param.prop = Socket_Normal;
            param.wildCardGroup = param_desc.wildCard;
            param.bVisible = false;
            ui.outputPrims.emplace_back(std::move(param));
        }
        else
        {
            //����һ����Ϊ�Ƕ���ZenoĿǰ�����͹���ǳ����ң���Щ����ֵ�ǿ��ַ���������������Ƕ�������
            ParamObject param;
            param.name = param_desc.name;
            param.type = type;
            if (param_desc.socketType != zeno::NoSocket)
                param.socketType = param_desc.socketType;
            param.socketType = Socket_Output;
            param.bInput = false;
            param.prop = Socket_Normal;
            param.wildCardGroup = param_desc.wildCard;
            ui.outputObjs.emplace_back(std::move(param));
        }
    }
    ParamTab tab;
    tab.groups.emplace_back(std::move(default));
    ui.inputPrims.tabs.emplace_back(std::move(tab));
    return ui;
}

ZENO_API void Session::defNodeClass(std::shared_ptr<INode>(*ctor)(), std::string const &clsname, Descriptor const &desc) {
    if (nodeClasses.find(clsname) != nodeClasses.end()) {
        log_error("node class redefined: `{}`\n", clsname);
    }

    if (clsname == "PrimitiveTransform") {
        int j;
        j = 0;
    }

    CustomUI ui = descToCustomui(desc);
    auto cls = std::make_unique<ImplNodeClass>(ctor, ui, clsname);
    nodeClasses.emplace(clsname, std::move(cls));
}

ZENO_API void Session::defNodeClass2(std::shared_ptr<INode>(*ctor)(), std::string const& nodecls, CustomUI const& customui) {
    if (nodeClasses.find(nodecls) != nodeClasses.end()) {
        log_error("node class redefined: `{}`\n", nodecls);
    }
    CustomUI ui = customui;
    initControlsByType(ui);
    auto cls = std::make_unique<ImplNodeClass>(ctor, ui, nodecls);
    nodeClasses.emplace(nodecls, std::move(cls));
}

ZENO_API void Session::defNodeReflectClass(std::function<std::shared_ptr<INode>()> ctor, zeno::reflect::TypeBase* pTypeBase)
{
    assert(pTypeBase);
    const zeno::reflect::ReflectedTypeInfo& info = pTypeBase->get_info();
    auto& nodecls = std::string(info.qualified_name.c_str());
    //��Щname����������ܴ��������ռ����zeno::XXX
    int idx = nodecls.find_last_of(':');
    if (idx != std::string::npos) {
        nodecls = nodecls.substr(idx + 1);
    }

    if (nodeClasses.find(nodecls) != nodeClasses.end()) {
        log_error("node class redefined: `{}`\n", nodecls);
    }
    auto cls = std::make_unique<ReflectNodeClass>(ctor, nodecls, pTypeBase);
    //TODO: From metadata
    cls->m_customui.category = "reflect";
    nodeClasses.emplace(nodecls, std::move(cls));
}

ZENO_API INodeClass::INodeClass(CustomUI const &customui, std::string const& classname)
    : m_customui(customui)
    , classname(classname)
{
}

ZENO_API INodeClass::~INodeClass() = default;

ZENO_API std::shared_ptr<Graph> Session::createGraph(const std::string& name) {
    auto graph = std::make_shared<Graph>(name);
    return graph;
}

ZENO_API void Session::resetMainGraph() {
    mainGraph.reset();
    mainGraph = std::make_shared<Graph>("main");
    referManager.reset();
    referManager = std::make_unique<ReferManager>();
    globalVariableManager.reset();
    globalVariableManager = std::make_unique<GlobalVariableManager>();
}

ZENO_API void Session::setApiLevelEnable(bool bEnable)
{
    m_bApiLevelEnable = bEnable;
}

ZENO_API void Session::beginApiCall()
{
    if (!m_bApiLevelEnable || m_bDisableRunning) return;
    m_apiLevel++;
}

ZENO_API void Session::endApiCall()
{
    if (!m_bApiLevelEnable || m_bDisableRunning) return;
    m_apiLevel--;
    if (m_apiLevel == 0) {
        if (m_bAutoRun) {
            if (m_callbackRunTrigger) {
                m_callbackRunTrigger();
            }
            else {
                run();
            }
        }
    }
}

ZENO_API void Session::setDisableRunning(bool bOn)
{
    m_bDisableRunning = bOn;
}

ZENO_API void Session::registerRunTrigger(std::function<void()> func)
{
    m_callbackRunTrigger = func;
}

ZENO_API void Session::registerNodeCallback(F_NodeStatus func)
{
    m_funcNodeStatus = func;
}

ZENO_API std::string Session::getColorByRtti(const ParamType type)
{
    auto iter = g_clrMapping.find(type);
    if (iter == g_clrMapping.end()) return "#FFFFFF";
    return iter->second;
}

void Session::reportNodeStatus(const ObjPath& path, bool bDirty, NodeRunStatus status)
{
    if (m_funcNodeStatus) {
        m_funcNodeStatus(path, bDirty, status);
    }
}

ZENO_API int Session::registerObjId(const std::string& objprefix)
{
    int objid = objsMan->registerObjId(objprefix);
    return objid;
}

ZENO_API void Session::switchToFrame(int frameid)
{
    CORE_API_BATCH
    mainGraph->markDirtyWhenFrameChanged();
    globalState->updateFrameId(frameid);
}

ZENO_API void Session::interrupt() {
    m_bInterrupted = true;
}

ZENO_API bool Session::is_interrupted() const {
    return m_bInterrupted;
}

ZENO_API bool Session::run() {
    if (m_bDisableRunning)
        return false;

    m_bInterrupted = false;
    globalState->set_working(true);

    zeno::log_info("Session::run()");

    objsMan->beforeRun();
    zeno::scope_exit sp([&]() { objsMan->afterRun(); });

    globalError->clearState();

    //�����������m_objects����һ������ʱ������Ƴ���obj���������������Ϊ�Ӵ�export_loading_objsʱ����Ҫ��m_objects�в��ұ�ɾ����obj
    objsMan->clearLastUnregisterObjs();
    //��֮ǰɾ���ڵ�ʱ��¼��obj����Ӧ���������������ڵ㣬������
    objsMan->remove_attach_node_by_removing_objs();

    zeno::GraphException::catched([&] {
        mainGraph->runGraph();
        }, *globalError);
    if (globalError->failed()) {
        zeno::log_error("");
    }

    return true;
}

ZENO_API void Session::set_auto_run(bool bOn) {
    m_bAutoRun = bOn;
}

ZENO_API bool Session::is_auto_run() const {
    return m_bAutoRun;
}

ZENO_API void Session::set_Rerun()
{
    mainGraph->markDirtyAll();
    objsMan->clear();
}

void Session::initNodeCates() {
    for (auto const& [key, cls] : nodeClasses) {
        if (!key.empty() && key.front() == '^')
            continue;
        std::string cate = cls->m_customui.category;
        if (m_cates.find(cate) == m_cates.end())
            m_cates.insert(std::make_pair(cate, std::vector<std::string>()));
        m_cates[cate].push_back(key);
    }
}

void Session::initReflectNodes() {
    auto& registry = zeno::reflect::ReflectionRegistry::get();
    for (zeno::reflect::TypeBase* type : registry->all()) {
        //TODO: �ж�type�Ļ����ǲ��ǻ���INode
        const zeno::reflect::ReflectedTypeInfo& info = type->get_info();
        zeno::reflect::ITypeConstructor& ctor = type->get_constructor_checked({});

        defNodeReflectClass([&]()->std::shared_ptr<INode> {
            INode* pNewNode = static_cast<INode*>(ctor.new_instance());
            std::shared_ptr<INode> spNode(pNewNode);
            return spNode;
        }, type);
    }
}

ZENO_API zeno::NodeCates Session::dumpCoreCates() {
    if (m_cates.empty()) {
        initNodeCates();
    }
    return m_cates;
}

namespace {
std::string dumpDescriptorToJson(const std::string &key, const Descriptor& descriptor) {
    using namespace rapidjson;
    Document doc;
    doc.SetArray();

    // Inputs array
    Value inputs(kArrayType);
    for (const auto& input : descriptor.inputs) {
        Value inputArray(kArrayType);
        inputArray.PushBack(Value().SetString(input.type.c_str(), doc.GetAllocator()), doc.GetAllocator());
        inputArray.PushBack(Value().SetString(input.name.c_str(), doc.GetAllocator()), doc.GetAllocator());
        inputArray.PushBack(Value().SetString(input.defl.c_str(), doc.GetAllocator()), doc.GetAllocator());
        inputArray.PushBack(Value().SetString(input.doc.c_str(), doc.GetAllocator()), doc.GetAllocator());
        inputs.PushBack(inputArray, doc.GetAllocator());
    }

    // Outputs array
    Value outputs(kArrayType);
    for (const auto& output : descriptor.outputs) {
        Value outputArray(kArrayType);
        outputArray.PushBack(Value().SetString(output.type.c_str(), doc.GetAllocator()), doc.GetAllocator());
        outputArray.PushBack(Value().SetString(output.name.c_str(), doc.GetAllocator()), doc.GetAllocator());
        outputArray.PushBack(Value().SetString(output.defl.c_str(), doc.GetAllocator()), doc.GetAllocator());
        outputArray.PushBack(Value().SetString(output.doc.c_str(), doc.GetAllocator()), doc.GetAllocator());
        outputs.PushBack(outputArray, doc.GetAllocator());
    }

    // Params array
    Value params(kArrayType);
    for (const auto& param : descriptor.params) {
        Value paramArray(kArrayType);
        paramArray.PushBack(Value().SetString(param.type.c_str(), doc.GetAllocator()), doc.GetAllocator());
        paramArray.PushBack(Value().SetString(param.name.c_str(), doc.GetAllocator()), doc.GetAllocator());
        paramArray.PushBack(Value().SetString(param.defl.c_str(), doc.GetAllocator()), doc.GetAllocator());
        paramArray.PushBack(Value().SetString(param.doc.c_str(), doc.GetAllocator()), doc.GetAllocator());
        params.PushBack(paramArray, doc.GetAllocator());
    }

    // Categories array
    Value categories(kArrayType);
    for (const auto& category : descriptor.categories) {
        categories.PushBack(Value().SetString(category.c_str(), doc.GetAllocator()), doc.GetAllocator());
    }

    // Push values into the main document
    doc.PushBack(Value().SetString(key.c_str(), doc.GetAllocator()), doc.GetAllocator());
    doc.PushBack(inputs, doc.GetAllocator());
    doc.PushBack(outputs, doc.GetAllocator());
    doc.PushBack(params, doc.GetAllocator());
    doc.PushBack(categories, doc.GetAllocator());
    doc.PushBack(Value().SetString(descriptor.doc.c_str(), doc.GetAllocator()), doc.GetAllocator());

    // Write the JSON string to stdout
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);
    return buffer.GetString();
}
}

ZENO_API std::string Session::dumpDescriptorsJSON() const {
    //deprecated.
    return "";
}

ZENO_API UserData &Session::userData() const {
    return *m_userData;
}

ZENO_API Session &getSession() {
#if 0
    static std::unique_ptr<Session> ptr;
    if (!ptr) {
        ptr = std::make_unique<Session>();
    }
#else
    static std::unique_ptr<Session> ptr = std::make_unique<Session>();
#endif
    return *ptr;
}

}
