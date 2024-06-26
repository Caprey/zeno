#include <zeno/core/INode.h>
#include <zeno/core/Graph.h>
#include <zeno/core/Descriptor.h>
#include <zeno/core/Session.h>
#include <zeno/core/Assets.h>
#include <zeno/core/ObjectManager.h>
#include <zeno/types/DummyObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/StringObject.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/extra/DirtyChecker.h>
#include <zeno/extra/TempNode.h>
#include <zeno/utils/Error.h>
#include <zeno/utils/string.h>
#include <zeno/funcs/ParseObjectFromUi.h>
#ifdef ZENO_BENCHMARKING
#include <zeno/utils/Timer.h>
#endif
#include <zeno/utils/safe_at.h>
#include <zeno/utils/logger.h>
#include <zeno/utils/uuid.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/core/CoreParam.h>
#include <zeno/DictObject.h>
#include <zeno/ListObject.h>
#include <zeno/utils/helper.h>
#include <zeno/utils/uuid.h>
#include <zeno/extra/SubnetNode.h>
#include <zeno/extra/GraphException.h>
#include <zeno/formula/formula.h>
#include <zeno/core/ReferManager.h>


namespace zeno {

ZENO_API INode::INode() {}

void INode::initUuid(std::shared_ptr<Graph> pGraph, const std::string nodecls) {
    m_nodecls = nodecls;
    this->graph = pGraph;

    m_uuid = generateUUID(nodecls);
    ObjPath path;
    path += m_uuid;
    while (pGraph) {
        const std::string name = pGraph->getName();
        if (name == "main") {
            break;
        }
        else {
            if (!pGraph->optParentSubgNode.has_value())
                break;
            auto pSubnetNode = pGraph->optParentSubgNode.value();
            assert(pSubnetNode);
            path = (pSubnetNode->m_uuid) + "/" + path;
            pGraph = pSubnetNode->graph.lock();
        }
    }
    m_uuidPath = path;
}

ZENO_API INode::~INode() = default;

ZENO_API std::shared_ptr<Graph> INode::getThisGraph() const {
    return graph.lock();
}

ZENO_API Session *INode::getThisSession() const {
    return &getSession();
}

ZENO_API GlobalState *INode::getGlobalState() const {
    return getSession().globalState.get();
}

ZENO_API void INode::doComplete() {
    set_output("DST", std::make_shared<DummyObject>());
    complete();
}

ZENO_API std::string INode::get_nodecls() const
{
    return m_nodecls;
}

ZENO_API std::string INode::get_ident() const
{
    return m_name;
}

ZENO_API std::string INode::get_show_name() const {
    if (nodeClass) {
        std::string dispName = nodeClass->m_customui.nickname;
        if (!dispName.empty())
            return dispName;
    }
    return m_nodecls;
}

ZENO_API std::string INode::get_show_icon() const {
    if (nodeClass) {
        return nodeClass->m_customui.iconResPath;
    }
    else {
        return "";
    }
}

ZENO_API CustomUI INode::get_customui() const {
    if (nodeClass) {
        return nodeClass->m_customui;
    }
    else {
        return CustomUI();
    }
}

ZENO_API ObjPath INode::get_path() const {
    ObjPath path;
    path = m_name;

    std::shared_ptr<Graph> pGraph = graph.lock();

    while (pGraph) {
        const std::string name = pGraph->getName();
        if (name == "main") {
            path = "/main" + path;
            break;
        }
        else {
            if (!pGraph->optParentSubgNode.has_value())
                break;
            auto pSubnetNode = pGraph->optParentSubgNode.value();
            assert(pSubnetNode);
            path = pSubnetNode->m_name + "/" + path;
            pGraph = pSubnetNode->graph.lock();
        }
    }
    return path;
}

std::string INode::get_uuid() const
{
    return m_uuid;
}

ZENO_API std::string INode::get_name() const
{
    return m_name;
}

ZENO_API void INode::set_name(const std::string& customname)
{
    m_name = customname;
}

ZENO_API void INode::set_view(bool bOn)
{
    CORE_API_BATCH

    m_bView = bOn;
    CALLBACK_NOTIFY(set_view, m_bView)

    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    spGraph->viewNodeUpdated(m_name, bOn);
}

ZENO_API bool INode::is_view() const
{
    return m_bView;
}

void INode::reportStatus(bool bDirty, NodeRunStatus status) {
    m_status = status;
    m_dirty = bDirty;
    zeno::getSession().reportNodeStatus(m_uuidPath, bDirty, status);
}

void INode::mark_previous_ref_dirty() {
    mark_dirty(true);
    //����Ҫ������࣬���ǰ��Ľڵ��������õķ�ʽ���ӣ�˵��ǰ��Ľڵ㶼���ܱ���Ⱦ�ˣ����ж�Ҫ���ࡣ
    //TODO: �ɶ˿ڶ����Ǳ߿��ơ�
    /*
    for (const auto& [name, param] : m_inputs) {
        for (const auto& link : param->links) {
            if (link->lnkProp == Link_Ref) {
                auto spOutParam = link->fromparam.lock();
                auto spPreviusNode = spOutParam->m_wpNode.lock();
                spPreviusNode->mark_previous_ref_dirty();
            }
        }
    }
    */
}

void INode::onInterrupted() {
    mark_dirty(true);
    mark_previous_ref_dirty();
}

ZENO_API void INode::mark_dirty(bool bOn, bool bWholeSubnet, bool bRecursively)
{
    scope_exit sp([&] {
        m_status = Node_DirtyReadyToRun;  //�޸������ݣ����࣬����Ϊ��״̬���������ڼ�������в������޸����ݣ�����markDirty��������ǰ��������
        reportStatus(m_dirty, m_status);
    });

    if (m_dirty == bOn)
        return;

    m_dirty = bOn;

    if (!bRecursively)
        return;

    if (m_dirty) {
        for (auto& [name, param] : m_outputObjs) {
            for (auto link : param->links) {
                auto inParam = link->toparam;
                assert(inParam);
                if (inParam) {
                    auto inNode = inParam->m_wpNode.lock();
                    assert(inNode);
                    inNode->mark_dirty(m_dirty);
                }
            }
        }
        for (auto& [name, param] : m_outputPrims) {
            for (auto link : param->links) {
                auto inParam = link->toparam;
                assert(inParam);
                if (inParam) {
                    auto inNode = inParam->m_wpNode.lock();
                    assert(inNode);
                    inNode->mark_dirty(m_dirty);
                }
            }
        }
    }

    if (SubnetNode* pSubnetNode = dynamic_cast<SubnetNode*>(this))
    {
        if (bWholeSubnet)
            pSubnetNode->mark_subnetdirty(bOn);
    }

    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    if (spGraph->optParentSubgNode.has_value())
    {
        spGraph->optParentSubgNode.value()->mark_dirty(true, false);
    }
}

void INode::mark_dirty_objs()
{
    for (auto const& [name, param] : m_outputObjs)
    {
        if (param->spObject) {
            if (param->spObject->key().empty()) {
                continue;
            }
            getSession().objsMan->collect_removing_objs(param->spObject->key());
        }
    }
}

ZENO_API void INode::complete() {}

ZENO_API void INode::preApply() {
    if (!m_dirty)
        return;

    reportStatus(true, Node_Pending);

    //TODO: the param order should be arranged by the descriptors.
    for (const auto& [name, param] : m_inputObjs) {
        bool ret = requireInput(name);
        if (!ret)
            zeno::log_warn("the param {} may not be initialized", name);
    }
    for (const auto& [name, param] : m_inputPrims) {
        bool ret = requireInput(name);
        if (!ret)
            zeno::log_warn("the param {} may not be initialized", name);
    }
}

ZENO_API void INode::registerObjToManager()
{
    for (auto const& [name, param] : m_outputObjs)
    {
        if (param->spObject)
        {
            if (std::dynamic_pointer_cast<NumericObject>(param->spObject) ||
                std::dynamic_pointer_cast<StringObject>(param->spObject)) {
                return;
            }

            if (param->spObject->key().empty())
            {
                //�����ǰ�ڵ�������ǰ�̽ڵ������obj����obj.key��Ϊ�գ���ʱ�ͱ�������֮ǰ��id��
                //�Ա�ʾ�����á�����������½�id��objָ�������ͬһ��������manager������ҡ�
                param->spObject->update_key(m_uuid);
            }

            const std::string& key = param->spObject->key();
            assert(!key.empty());
            param->spObject->nodeId = m_name;

            auto& objsMan = getSession().objsMan;
            std::shared_ptr<INode> spNode = shared_from_this();
            objsMan->collectingObject(param->spObject, spNode, m_bView);
        }
    }
}

std::shared_ptr<DictObject> INode::processDict(ObjectParam* in_param) {
    std::shared_ptr<DictObject> spDict;
    //���ӵ�Ԫ����list����list of list�Ĺ��򣬲���Graph::addLink��ע�͡�
    bool bDirecyLink = false;
    const auto& inLinks = in_param->links;
    if (inLinks.size() == 1)
    {
        std::shared_ptr<ObjectLink> spLink = inLinks.front();
        auto out_param = spLink->fromparam;
        std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

        if (out_param->type == in_param->type && !spLink->tokey.empty())
        {
            bDirecyLink = true;
            GraphException::translated([&] {
                outNode->doApply();
                }, outNode.get());
            zany outResult = outNode->get_output_obj(out_param->name);
            spDict = std::dynamic_pointer_cast<DictObject>(outResult);
        }
    }
    if (!bDirecyLink)
    {
        spDict = std::make_shared<DictObject>();
        for (const auto& spLink : in_param->links)
        {
            const std::string& keyName = spLink->tokey;
            auto out_param = spLink->fromparam;
            std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

            GraphException::translated([&] {
                outNode->doApply();
                }, outNode.get());

            zany outResult = outNode->get_output_obj(out_param->name);
            spDict->lut[keyName] = outResult;
        }
    }
    return spDict;
}

std::shared_ptr<ListObject> INode::processList(ObjectParam* in_param) {
    std::shared_ptr<ListObject> spList;
    bool bDirectLink = false;
    if (in_param->links.size() == 1)
    {
        std::shared_ptr<ObjectLink> spLink = in_param->links.front();
        auto out_param = spLink->fromparam;
        std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

        if (out_param->type == in_param->type && !spLink->tokey.empty()) {
            bDirectLink = true;

            GraphException::translated([&] {
                outNode->doApply();
                }, outNode.get());

            zany outResult = outNode->get_output_obj(out_param->name);
            spList = std::dynamic_pointer_cast<ListObject>(outResult);
        }
    }
    if (!bDirectLink)
    {
        spList = std::make_shared<ListObject>();
        int indx = 0;
        for (const auto& spLink : in_param->links)
        {
            //list������£�keyName�ǲ���û���壬˳����ôά�֣�
            auto out_param = spLink->fromparam;
            std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();
            if (outNode->is_dirty()) {  //list�е�Ԫ����dirty�ģ����¼��㲢����list
                GraphException::translated([&] {
                    outNode->doApply();
                    }, outNode.get());

                zany outResult = outNode->get_output_obj(out_param->name);
                spList->push_back(outResult);
                //spList->dirtyIndice.insert(indx);
            }
            else {
                zany outResult = outNode->get_output_obj(out_param->name);
                spList->push_back(outResult);
            }
        }
    }
    return spList;
}

zvariant INode::processPrimitive(PrimitiveParam* in_param)
{
    if (!in_param) {
        return nullptr;
    }

    int frame = getGlobalState()->getFrameId();
    //zany result;

    const ParamType type = in_param->type;
    const zvariant defl = in_param->defl;
    zvariant result;

    switch (type) {
    case Param_Int:
    case Param_Float:
    case Param_Bool:
    {
        //�Ȳ�����int float�Ļ���,ֱ�Ӱ�variant��ֵ����
        zvariant resolve_value;
        if (std::holds_alternative<std::string>(defl))
        {
            std::string str = std::get<std::string>(defl);
            float fVal = resolve(str, type);
            result = fVal;
        }
        else if (std::holds_alternative<int>(defl))
        {
            result = defl;
        }
        else if (std::holds_alternative<float>(defl))
        {
            result = defl;
        }
        else
        {
            //error, throw expection.
        }
        break;
    }
    case Param_String:
    {
        if (std::holds_alternative<std::string>(defl))
        {
            result = defl;
        }
        else {
            //error, throw expection.
        }
        break;
    }
    case Param_Vec2f:   result = resolveVec<vec2f, vec2s>(defl, type);  break;
    case Param_Vec2i:   result = resolveVec<vec2i, vec2s>(defl, type);  break;
    case Param_Vec3f:   result = resolveVec<vec3f, vec3s>(defl, type);  break;
    case Param_Vec3i:   result = resolveVec<vec3i, vec3s>(defl, type);  break;
    case Param_Vec4f:   result = resolveVec<vec4f, vec4s>(defl, type);  break;
    case Param_Vec4i:   result = resolveVec<vec4i, vec4s>(defl, type);  break;
    case Param_Heatmap:
    {
        //TODO: heatmap�Ľṹ��Ҫ���ϵ�zvariant.
        //if (std::holds_alternative<std::string>(defl))
        //    result = zeno::parseHeatmapObj(std::get<std::string>(defl));
        break;
    }
    //����ָ���ǻ������͵�List/Dict.
    case Param_List:
    {
        //TODO: List���ڻ�û��ui֧�֣�����List�Ƿ������������ڷ�Literalֵ�����趨Ĭ��ֵ��
        break;
    }
    case Param_Dict:
    {
        break;
    }
    }
    return result;
}

bool INode::receiveOutputObj(ObjectParam* in_param, zany outputObj) {
    //�۲�˿�����
    //TODO
    if (in_param->socketType == Socket_Clone) {
        in_param->spObject = outputObj->clone();
        //TODO: list/dict case.
    }
    else if (in_param->socketType == Socket_Owning) {
        in_param->spObject = outputObj->move_clone();
    }
    else if (in_param->socketType == Socket_ReadOnly) {
        in_param->spObject = outputObj;
        //TODO: readonly property on object.
    }
    return true;
}

ZENO_API bool INode::requireInput(std::string const& ds) {
    // Ŀǰ������������������ֵ����������������ʵ�֣��Ͻڵ�ֱ�Ӹģ���
    auto iter = m_inputObjs.find(ds);
    if (iter != m_inputObjs.end()) {
        ObjectParam* in_param = iter->second.get();
        if (in_param->links.empty()) {
            //�ڵ���������˶��󣬵�û�б�����ȥ���Ƿ�Ҫ���ڵ�apply��δ���
        }
        else {
            switch (in_param->type)
            {
                case Param_Dict:
                {
                    std::shared_ptr<DictObject> outDict = processDict(in_param);
                    receiveOutputObj(in_param, outDict);
                    break;
                }
                case Param_List:
                {
                    std::shared_ptr<ListObject> outList = processList(in_param);
                    receiveOutputObj(in_param, outList);
                    break;
                }
                case Param_Curve:
                {
                    //CurveҪ����Object����Ϊ���ϵ�variant̫�鷳��ֻҪ������ԭʼ��MakeCurve�ڵ㣬���ַ���������json����Ϊ�������ͼ��ɡ�
                }
                default:
                {
                    if (in_param->links.size() == 1)
                    {
                        std::shared_ptr<ObjectLink> spLink = *in_param->links.begin();
                        ObjectParam* out_param = spLink->fromparam;
                        std::shared_ptr<INode> outNode = out_param->m_wpNode.lock();

                        GraphException::translated([&] {
                            outNode->doApply();
                        }, outNode.get());

                        receiveOutputObj(in_param, out_param->spObject);
                    }
                }
            }
        }
    }
    else {
        auto iter2 = m_inputPrims.find(ds);
        if (iter2 != m_inputPrims.end()) {
            PrimitiveParam* in_param = iter2->second.get();
            if (in_param->links.empty()) {
                in_param->result = processPrimitive(in_param);
                //�ɰ汾��requireInputָ�����Ƿ������ߣ��������ݾɰ汾��������Է���false����ʹ�������࣬���Ծ��޸����Ķ��塣
            }
            else {
                if (in_param->links.size() == 1) {
                    std::shared_ptr<PrimitiveLink> spLink = *in_param->links.begin();
                    std::shared_ptr<INode> outNode = spLink->fromparam->m_wpNode.lock();

                    GraphException::translated([&] {
                        outNode->doApply();
                    }, outNode.get());
                    //��ֵ�������ͣ�ֱ�Ӹ��ơ�
                    in_param->result = spLink->fromparam->result;
                }
            }
        } else {
            return false;
        }
    }
    return true;
}

ZENO_API void INode::doOnlyApply() {
    apply();
}

ZENO_API void INode::doApply() {

    if (!m_dirty) {
        registerObjToManager();//���ֻ�Ǵ�view��Ҳ����Ҫ�ӵ�manager�ġ�
        return;
    }

    /*
    zeno::scope_exit spe([&] {//applyʱ���������IParam���Ϊmodified���˳�ʱ������IParam���Ϊδmodified
        for (auto const& [name, param] : m_outputs)
            param->m_idModify = false;
        });
    */

    preApply();

    if (zeno::getSession().is_interrupted()) {
        throw makeError<InterruputError>(m_uuidPath);
    }

    log_debug("==> enter {}", m_name);
    {
#ifdef ZENO_BENCHMARKING
        Timer _(m_name);
#endif
        reportStatus(true, Node_Running);
        apply();
    }
    log_debug("==> leave {}", m_name);

    registerObjToManager();
    reportStatus(false, Node_RunSucceed);
}

ZENO_API ObjectParams INode::get_input_object_params() const
{
    ObjectParams params;
    for (auto& [name, spObjParam] : m_inputObjs)
    {
        ParamObject obj;
        for (auto linkInfo : spObjParam->links) {
            obj.links.push_back(getEdgeInfo(linkInfo));
        }
        obj.name = name;
        obj.type = spObjParam->type;
        obj.bInput = true;
        obj.socketType = spObjParam->socketType;
        obj.wildCardGroup = spObjParam->wildCardGroup;
        //obj.prop = ?
        params.push_back(obj);
    }
    return params;
}

ZENO_API ObjectParams INode::get_output_object_params() const
{
    ObjectParams params;
    for (auto& [name, spObjParam] : m_outputObjs)
    {
        ParamObject obj;
        for (auto linkInfo : spObjParam->links) {
            obj.links.push_back(getEdgeInfo(linkInfo));
        }
        obj.name = name;
        obj.type = spObjParam->type;
        obj.bInput = false;
        obj.socketType = spObjParam->socketType;
        obj.wildCardGroup = spObjParam->wildCardGroup;
        //obj.prop = ?
        params.push_back(obj);
    }
    return params;
}

ZENO_API PrimitiveParams INode::get_input_primitive_params() const {
    //TODO: deprecated node.
    PrimitiveParams params;
    for (auto& [name, spParamObj] : m_inputPrims) {
        ParamPrimitive param;
        param.bInput = true;
        param.name = name;
        param.type = spParamObj->type;
        param.control = spParamObj->control;
        param.ctrlProps = spParamObj->optCtrlprops;
        param.defl = spParamObj->defl;
        param.bVisible = spParamObj->bVisible;
        for (auto spLink : spParamObj->links) {
            param.links.push_back(getEdgeInfo(spLink));
        }
        param.socketType = spParamObj->socketType;
        param.wildCardGroup = spParamObj->wildCardGroup;
        params.push_back(param);
    }
    return params;
}

ZENO_API PrimitiveParams INode::get_output_primitive_params() const {
    PrimitiveParams params;
    for (auto& [name, spParamObj] : m_outputPrims) {
        ParamPrimitive param;
        param.bInput = false;
        param.name = name;
        param.type = spParamObj->type;
        param.control = NullControl;
        param.ctrlProps = std::nullopt;
        param.defl = spParamObj->defl;
        for (auto spLink : spParamObj->links) {
            param.links.push_back(getEdgeInfo(spLink));
        }
        param.socketType = spParamObj->socketType;
        param.wildCardGroup = spParamObj->wildCardGroup;
        params.push_back(param);
    }
    return params;
}

ZENO_API ParamPrimitive INode::get_input_prim_param(std::string const& name, bool* pExist) const {
    ParamPrimitive param;
    auto iter = m_inputPrims.find(name);
    if (iter != m_inputPrims.end()) {
        auto& paramPrim = iter->second;
        param = paramPrim->exportParam();
        if (pExist)
            *pExist = true;
    }
    else {
        if (pExist)
            *pExist = false;
    }
    return param;
}

ZENO_API ParamObject INode::get_input_obj_param(std::string const& name, bool* pExist) const {
    ParamObject param;
    auto iter = m_inputObjs.find(name);
    if (iter != m_inputObjs.end()) {
        auto& paramObj = iter->second;
        param = paramObj->exportParam();
        if (pExist)
            *pExist = true;
    }
    else {
        if (pExist)
            *pExist = false;
    }
    return param;
}

ZENO_API ParamPrimitive INode::get_output_prim_param(std::string const& name, bool* pExist) const {
    ParamPrimitive param;
    auto iter = m_outputPrims.find(name);
    if (iter != m_outputPrims.end()) {
        auto& paramPrim = iter->second;
        param = paramPrim->exportParam();
        if (pExist)
            *pExist = true;
    }
    else {
        if (pExist)
            *pExist = false;
    }
    return param;
}

ZENO_API ParamObject INode::get_output_obj_param(std::string const& name, bool* pExist) const {
    ParamObject param;
    auto iter = m_outputObjs.find(name);
    if (iter != m_outputObjs.end()) {
        auto& paramObj = iter->second;
        param = paramObj->exportParam();
        if (pExist)
            *pExist = true;
    }
    else {
        if (pExist)
            *pExist = false;
    }
    return param;
}

bool INode::add_input_prim_param(ParamPrimitive param) {
    if (m_inputPrims.find(param.name) != m_inputPrims.end()) {
        return false;
    }
    std::unique_ptr<PrimitiveParam> sparam = std::make_unique<PrimitiveParam>();
    sparam->bInput = true;
    sparam->control = param.control;
    sparam->defl = param.defl;
    sparam->m_wpNode = shared_from_this();
    sparam->name = param.name;
    sparam->socketType = param.socketType;
    sparam->type = param.type;
    sparam->optCtrlprops = param.ctrlProps;
    sparam->bVisible = param.bVisible;
    sparam->wildCardGroup = param.wildCardGroup;
    m_inputPrims.insert(std::make_pair(param.name, std::move(sparam)));
    return true;
}

bool INode::add_input_obj_param(ParamObject param) {
    if (m_inputObjs.find(param.name) != m_inputObjs.end()) {
        return false;
    }
    std::unique_ptr<ObjectParam> sparam = std::make_unique<ObjectParam>();
    sparam->bInput = true;
    sparam->name = param.name;
    sparam->type = param.type;
    sparam->socketType = param.socketType;
    sparam->m_wpNode = shared_from_this();
    sparam->wildCardGroup = param.wildCardGroup;
    m_inputObjs.insert(std::make_pair(param.name, std::move(sparam)));
    return true;
}

bool INode::add_output_prim_param(ParamPrimitive param) {
    if (m_outputPrims.find(param.name) != m_outputPrims.end()) {
        return false;
    }
    std::unique_ptr<PrimitiveParam> sparam = std::make_unique<PrimitiveParam>();
    sparam->bInput = false;
    sparam->control = param.control;
    sparam->defl = param.defl;
    sparam->m_wpNode = shared_from_this();
    sparam->name = param.name;
    sparam->socketType = param.socketType;
    sparam->type = param.type;
    sparam->optCtrlprops = param.ctrlProps;
    sparam->wildCardGroup = param.wildCardGroup;
    m_outputPrims.insert(std::make_pair(param.name, std::move(sparam)));
    return true;
}

bool INode::add_output_obj_param(ParamObject param) {
    if (m_outputObjs.find(param.name) != m_outputObjs.end()) {
        return false;
    }
    std::unique_ptr<ObjectParam> sparam = std::make_unique<ObjectParam>();
    sparam->bInput = true;
    sparam->name = param.name;
    sparam->type = param.type;
    sparam->socketType = param.socketType;
    sparam->m_wpNode = shared_from_this();
    sparam->wildCardGroup = param.wildCardGroup;
    m_outputObjs.insert(std::make_pair(param.name, std::move(sparam)));
    return true;
}

ZENO_API void INode::set_result(bool bInput, const std::string& name, zany spObj) {
    if (bInput) {
        auto& param = safe_at(m_inputObjs, name, "");
        param->spObject = spObj;
    }
    else {
        auto& param = safe_at(m_outputObjs, name, "");
        param->spObject = spObj;
    }
}

void INode::init_object_link(bool bInput, const std::string& paramname, std::shared_ptr<ObjectLink> spLink) {
    auto iter = bInput ? m_inputObjs.find(paramname) : m_outputObjs.find(paramname);
    if (bInput)
        spLink->toparam = iter->second.get();
    else
        spLink->fromparam = iter->second.get();
    iter->second->links.emplace_back(spLink);
}

void INode::init_primitive_link(bool bInput, const std::string& paramname, std::shared_ptr<PrimitiveLink> spLink) {
    auto iter = bInput ? m_inputPrims.find(paramname) : m_outputPrims.find(paramname);
    if (bInput)
        spLink->toparam = iter->second.get();
    else
        spLink->fromparam = iter->second.get();
    iter->second->links.emplace_back(spLink);
}

bool INode::isPrimitiveType(bool bInput, const std::string& param_name, bool& bExist) {
    if (bInput) {
        if (m_inputObjs.find(param_name) != m_inputObjs.end()) {
            bExist = true;
            return false;
        }
        else if (m_inputPrims.find(param_name) != m_inputPrims.end()) {
            bExist = true;
            return true;
        }
        bExist = false;
        return false;
    }
    else {
        if (m_outputObjs.find(param_name) != m_outputObjs.end()) {
            bExist = true;
            return false;
        }
        else if (m_outputPrims.find(param_name) != m_outputPrims.end()) {
            bExist = true;
            return true;
        }
        bExist = false;
        return false;
    }
}

std::vector<EdgeInfo> INode::getLinks() const {
    std::vector<EdgeInfo> remLinks;
    for (const auto& [_, spParam] : m_inputObjs) {
        for (std::shared_ptr<ObjectLink> spLink : spParam->links) {
            remLinks.push_back(getEdgeInfo(spLink));
        }
    }
    for (const auto& [_, spParam] : m_inputPrims) {
        for (std::shared_ptr<PrimitiveLink> spLink : spParam->links) {
            remLinks.push_back(getEdgeInfo(spLink));
        }
    }
    for (const auto& [_, spParam] : m_outputObjs) {
        for (std::shared_ptr<ObjectLink> spLink : spParam->links) {
            remLinks.push_back(getEdgeInfo(spLink));
        }
    }
    for (const auto& [_, spParam] : m_outputPrims) {
        for (std::shared_ptr<PrimitiveLink> spLink : spParam->links) {
            remLinks.push_back(getEdgeInfo(spLink));
        }
    }
    return remLinks;
}

std::vector<EdgeInfo> INode::getLinksByParam(bool bInput, const std::string& param_name) const {
    std::vector<EdgeInfo> links;

    auto& objects = bInput ? m_inputObjs : m_outputObjs;
    auto& primtives = bInput ? m_inputPrims : m_outputPrims;

    auto iter = objects.find(param_name);
    if (iter != objects.end()) {
        for (auto spLink : iter->second->links) {
            links.push_back(getEdgeInfo(spLink));
        }
    }
    else {
        auto iter2 = primtives.find(param_name);
        if (iter2 != primtives.end()) {
            for (auto spLink : iter2->second->links) {
                links.push_back(getEdgeInfo(spLink));
            }
        }
    }
    return links;
}

bool INode::updateLinkKey(bool bInput, const std::string& param_name, const std::string& oldkey, const std::string& newkey)
{
    auto& objects = bInput ? m_inputObjs : m_outputObjs;
    auto iter = objects.find(param_name);
    if (iter != objects.end()) {
        for (auto spLink : iter->second->links) {
            if (spLink->tokey == oldkey) {
                spLink->tokey = newkey;
                return true;
            }
        }
    }
    return false;
}

bool INode::moveUpLinkKey(bool bInput, const std::string& param_name, const std::string& key)
{
    auto& objects = bInput ? m_inputObjs : m_outputObjs;
    auto iter = objects.find(param_name);
    if (iter != objects.end()) {
        for (auto it = iter->second->links.begin(); it != iter->second->links.end(); it++) {
            if ((*it)->tokey == key) {
                auto it_ = std::prev(it);
                std::swap(*it, *it_);
                return true;
            }
        }
    }
    return false;
}

bool INode::removeLink(bool bInput, const EdgeInfo& edge) {
    if (bInput) {
        if (edge.bObjLink) {
            auto iter = m_inputObjs.find(edge.inParam);
            if (iter == m_inputObjs.end())
                return false;
            for (auto spLink : iter->second->links) {
                if (spLink->fromparam->name == edge.outParam && spLink->fromkey == edge.outKey) {
                    iter->second->links.remove(spLink);
                    return true;
                }
            }
        }
        else {
            auto iter = m_inputPrims.find(edge.inParam);
            if (iter == m_inputPrims.end())
                return false;
            for (auto spLink : iter->second->links) {
                if (spLink->fromparam->name == edge.outParam) {
                    iter->second->links.remove(spLink);
                    return true;
                }
            }
        }
    }
    else {
        if (edge.bObjLink) {
            auto iter = m_outputObjs.find(edge.outParam);
            if (iter == m_outputObjs.end())
                return false;
            for (auto spLink : iter->second->links) {
                if (spLink->toparam->name == edge.inParam && spLink->tokey == edge.inKey) {
                    iter->second->links.remove(spLink);
                    return true;
                }
            }
        }
        else {
            auto iter = m_outputPrims.find(edge.outParam);
            if (iter == m_outputPrims.end())
                return false;
            for (auto spLink : iter->second->links) {
                if (spLink->toparam->name == edge.inParam) {
                    iter->second->links.remove(spLink);
                    return true;
                }
            }
        }
    }
    return false;
}

ZENO_API std::string INode::get_viewobject_output_param() const {
    //������ʱ��û��ʲô��ʶ������ָ���ĸ�������Ƕ�Ӧ���view obj��
    //һ�㶼��Ĭ�ϵ�һ�����obj����ʱ��ô�涨�����������ñ�ʶ����
    if (m_outputObjs.empty())
        return "";
    return m_outputObjs.begin()->second->name;
}

ZENO_API NodeData INode::exportInfo() const
{
    NodeData node;
    node.cls = m_nodecls;
    node.name = m_name;
    node.bView = m_bView;
    node.uipos = m_pos;
    //TODO: node type
    if (node.subgraph.has_value())
        node.type = Node_SubgraphNode;
    else
        node.type = Node_Normal;

    node.customUi = get_customui();
    node.customUi.inputObjs.clear();
    for (auto& [name, paramObj] : m_inputObjs)
    {
        node.customUi.inputObjs.push_back(paramObj->exportParam());
    }
    for (auto &tab : node.customUi.inputPrims.tabs)
    {
        for (auto &group : tab.groups)
        {
            for (auto& param : group.params)
            {
                auto iter = m_inputPrims.find(param.name);
                if (iter != m_inputPrims.end())
                {
                    param = iter->second->exportParam();
                }
            }
        }
    }

    node.customUi.outputPrims.clear();
    for (auto& [name, paramObj] : m_outputPrims)
    {
        node.customUi.outputPrims.push_back(paramObj->exportParam());
    }
    node.customUi.outputObjs.clear();
    for (auto& [name, paramObj] : m_outputObjs)
    {
        node.customUi.outputObjs.push_back(paramObj->exportParam());
    }
    return node;
}

ZENO_API bool INode::update_param(const std::string& param, const zvariant& new_value) {
    CORE_API_BATCH
    auto& spParam = safe_at(m_inputPrims, param, "miss input param `" + param + "` on node `" + m_name + "`");
    if (!zeno::isEqual(spParam->defl, new_value, spParam->type))
    {
        zvariant old_value = spParam->defl;
        spParam->defl = new_value;

        std::shared_ptr<Graph> spGraph = graph.lock();
        assert(spGraph);

        spGraph->onNodeParamUpdated(spParam.get(), old_value, new_value);
        CALLBACK_NOTIFY(update_param, param, old_value, new_value)
        mark_dirty(true);
        getSession().referManager->checkReference(m_uuidPath, spParam->name);
        return true;
    }
    return false;
}

ZENO_API bool zeno::INode::update_param_socket_type(const std::string& param, SocketType type)
{
    CORE_API_BATCH
    auto& spParam = safe_at(m_inputObjs, param, "miss input param `" + param + "` on node `" + m_name + "`");
    if (type != spParam->socketType)
    {
        spParam->socketType = type;
        if (type == Socket_Owning)
        {
            auto spGraph = graph.lock();
            spGraph->removeLinks(m_name, true, param);
        }
        mark_dirty(true);
        CALLBACK_NOTIFY(update_param_socket_type, param, type)
        return true;
    }
    return false;
}

ZENO_API bool zeno::INode::update_param_type(const std::string& param, bool bPrim, ParamType type)
{
    CORE_API_BATCH
        if (bPrim)
        {
            bool bInput = m_inputPrims.find(param) != m_inputPrims.end();
            const auto& prims = bInput ? m_inputPrims : m_outputPrims;
            auto& prim = prims.find(param);
            if (prim != prims.end())
            {
                auto& spParam = prim->second;
                if (type != spParam->type)
                {
                    spParam->type = type;
                    CALLBACK_NOTIFY(update_param_type, param, type) 
                        return true;
                }
            }
        }
        else 
        {
            bool bInput = m_inputObjs.find(param) != m_inputObjs.end();
            const auto& objects = bInput ? m_inputObjs : m_outputObjs;
            auto& object = objects.find(param);
            if (object != objects.end())
            {
                auto& spParam = object->second;
                if (type != spParam->type)
                {
                    spParam->type = type;
                    CALLBACK_NOTIFY(update_param_type, param, type)
                        return true;
                }
            }
        }
    return false;
}

ZENO_API bool zeno::INode::update_param_control(const std::string& param, ParamControl control)
{
    CORE_API_BATCH
    auto& spParam = safe_at(m_inputPrims, param, "miss input param `" + param + "` on node `" + m_name + "`");
    if (control != spParam->control)
    {
        spParam->control = control;
        CALLBACK_NOTIFY(update_param_control, param, control)
        return true;
    }
    return false;
}

ZENO_API bool zeno::INode::update_param_control_prop(const std::string& param, ControlProperty props)
{
    CORE_API_BATCH
    auto& spParam = safe_at(m_inputPrims, param, "miss input param `" + param + "` on node `" + m_name + "`");

    if (props.items.has_value() || props.items.has_value())
    {
        spParam->optCtrlprops = props;
        CALLBACK_NOTIFY(update_param_control_prop, param, props)
        return true;
    }
    return false;
}

ZENO_API bool zeno::INode::update_param_visible(const std::string& param, bool bVisible)
{
    CORE_API_BATCH
    auto& spParam = safe_at(m_inputPrims, param, "miss input param `" + param + "` on node `" + m_name + "`");

    if (spParam->bVisible != bVisible)
    {
        spParam->bVisible = bVisible;
        CALLBACK_NOTIFY(update_param_visible, param, bVisible)
            return true;
    }
    return false;
}

ZENO_API params_change_info INode::update_editparams(const ParamsUpdateInfo& params)
{
    params_change_info ret;
    return ret;
}

ZENO_API void INode::init(const NodeData& dat)
{
    //IO init
    if (!dat.name.empty())
        m_name = dat.name;

    m_pos = dat.uipos;
    m_bView = dat.bView;
    if (m_bView) {
        std::shared_ptr<Graph> spGraph = graph.lock();
        assert(spGraph);
        spGraph->viewNodeUpdated(m_name, m_bView);
    }
    if (SubnetNode* pSubnetNode = dynamic_cast<SubnetNode*>(this))
    {
        pSubnetNode->setCustomUi(dat.customUi);
    }
    initParams(dat);
    m_dirty = true;
}

ZENO_API void INode::initParams(const NodeData& dat)
{
    for (const ParamObject& paramObj : dat.customUi.inputObjs)
    {
        auto iter = m_inputObjs.find(paramObj.name);
        if (iter == m_inputObjs.end()) {
            add_input_obj_param(paramObj);
            continue;
        }
        auto& sparam = iter->second;
        sparam->socketType = paramObj.socketType;
    }
    for (auto tab : dat.customUi.inputPrims.tabs)
    {
        for (auto group : tab.groups)
        {
            for (auto param : group.params)
            {
                auto iter = m_inputPrims.find(param.name);
                if (iter == m_inputPrims.end()) {
                    add_input_prim_param(param);
                    continue;
                }
                auto& sparam = iter->second;
                sparam->defl = param.defl;
                sparam->control = param.control;
                sparam->optCtrlprops = param.ctrlProps;
                sparam->bVisible = param.bVisible;
            }
        }
    }
    for (const ParamPrimitive& param : dat.customUi.outputPrims)
    {
        add_output_prim_param(param);
    }
    for (const ParamObject& paramObj : dat.customUi.outputObjs)
    {
        add_output_obj_param(paramObj);
    }
}

ZENO_API bool INode::has_input(std::string const &id) const {
    //���has_input�ھɵ�������������input obj�������һЩ��û�����ϣ���ô��һЩ����ֵ����Ĭ��ֵ��δ�ػ������input�ģ�
    //����һ����������Ƕ���ֵ�Ƿ�����������
    //�������Ҫ���ɰ汾��ô����
    //�����°汾���ԣ�������ֵ�����룬û�����ϱ߽���Ĭ��ֵ���Ͳ���has_input���е���֣��������ֱ���жϲ����Ƿ���ڡ�
    auto iter = m_inputObjs.find(id);
    if (iter != m_inputObjs.end()) {
        return !iter->second->links.empty();
    }
    else {
        return m_inputPrims.find(id) != m_inputPrims.end();
    }
}

ZENO_API zany INode::get_input(std::string const &id) const {
    auto iter = m_inputPrims.find(id);
    if (iter != m_inputPrims.end()) {
        auto& val = iter->second->result;
        switch (iter->second->type) {
            case Param_Int:
            case Param_Float:
            case Param_Bool:
            case Param_Vec2f:
            case Param_Vec2i:
            case Param_Vec3f:
            case Param_Vec3i:
            case Param_Vec4f:
            case Param_Vec4i:
            {
                //��Ȼ�кܶ�ڵ�����NumericObject��Ϊ�˼��ݣ���Ҫ��һ��NumericObject��ȥ��
                std::shared_ptr<NumericObject> spNum = std::make_shared<NumericObject>();
                zvariant value;
                if (std::holds_alternative<int>(val))
                {
                    spNum->set<int>(std::get<int>(val));
                }
                else if (std::holds_alternative<float>(val))
                {
                    spNum->set<float>(std::get<float>(val));
                }
                else if (std::holds_alternative<vec2i>(val))
                {
                    spNum->set<vec2i>(std::get<vec2i>(val));
                }
                else if (std::holds_alternative<vec2f>(val))
                {
                    spNum->set<vec2f>(std::get<vec2f>(val));
                }
                else if (std::holds_alternative<vec3i>(val))
                {
                    spNum->set<vec3i>(std::get<vec3i>(val));
                }
                else if (std::holds_alternative<vec3f>(val))
                {
                    spNum->set<vec3f>(std::get<vec3f>(val));
                }
                else if (std::holds_alternative<vec4i>(val))
                {
                    spNum->set<vec4i>(std::get<vec4i>(val));
                }
                else if (std::holds_alternative<vec4f>(val))
                {
                    spNum->set<vec4f>(std::get<vec4f>(val));
                }
                else
                {
                    //throw makeError<TypeError>(typeid(T));
                    //error, throw expection.
                }
                return spNum;
            }
            case Param_String:
            {
                if (std::holds_alternative<std::string>(val))
                {
                    std::shared_ptr<StringObject> stringobj = std::make_shared<StringObject>();
                    return stringobj;
                }
                else {
                    //error, throw expection.
                }
                break;
            }
            return nullptr;
        }
    }
    else {
        auto iter2 = m_inputObjs.find(id);
        if (iter2 != m_inputObjs.end()) {
            return iter2->second->spObject;
        }
        else {
            return nullptr;
        }
    }
}

zvariant INode::resolveInput(std::string const& id) {
    if (requireInput(id)) {
        auto iter = m_inputPrims.find(id);
        return iter->second->result;
    }
    else {
        return nullptr;
    }
}

ZENO_API void INode::set_pos(std::pair<float, float> pos) {
    m_pos = pos;
    CALLBACK_NOTIFY(set_pos, m_pos)
}

ZENO_API std::pair<float, float> INode::get_pos() const {
    return m_pos;
}

ZENO_API bool INode::in_asset_file() const {
    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    return getSession().assets->isAssetGraph(spGraph);
}

bool INode::set_primitive_input(std::string const& id, const zvariant& val) {
    auto iter = m_inputPrims.find(id);
    if (iter == m_inputPrims.end())
        return false;
    iter->second->result = val;
}

bool INode::set_primitive_output(std::string const& id, const zvariant& val) {
    auto iter = m_outputPrims.find(id);
    if (iter == m_outputPrims.end())
        return false;
    iter->second->result = val;
}

ZENO_API bool INode::set_output(std::string const& param, zany obj) {
    auto iter = m_outputObjs.find(param);
    if (iter != m_outputObjs.end()) {
        iter->second->spObject = obj;
        return true;
    }
    else {
        auto iter2 = m_outputPrims.find(param);
        if (iter2 != m_outputPrims.end()) {
            //������ǰNumericObject�����
            if (auto numObject = std::dynamic_pointer_cast<NumericObject>(obj)) {
                const auto& val = numObject->value;
                if (std::holds_alternative<int>(val))
                {
                    iter2->second->result = std::get<int>(val);
                }
                else if (std::holds_alternative<float>(val))
                {
                    iter2->second->result = std::get<float>(val);
                }
                else if (std::holds_alternative<vec2i>(val))
                {
                    iter2->second->result = std::get<vec2i>(val);
                }
                else if (std::holds_alternative<vec2f>(val))
                {
                    iter2->second->result = std::get<vec2f>(val);
                }
                else if (std::holds_alternative<vec3i>(val))
                {
                    iter2->second->result = std::get<vec3i>(val);
                }
                else if (std::holds_alternative<vec3f>(val))
                {
                    iter2->second->result = std::get<vec3f>(val);
                }
                else if (std::holds_alternative<vec4i>(val))
                {
                    iter2->second->result = std::get<vec4i>(val);
                }
                else if (std::holds_alternative<vec4f>(val))
                {
                    iter2->second->result = std::get<vec4f>(val);
                }
                else
                {
                    //throw makeError<TypeError>(typeid(T));
                    //error, throw expection.
                }
            }
            else if (auto strObject = std::dynamic_pointer_cast<StringObject>(obj)) {
                const auto& val = strObject->value;
                iter2->second->result = val;
            }
            return true;
        }
    }
    return false;
}

ZENO_API zany INode::get_output_obj(std::string const& param) {
    auto& spParam = safe_at(m_outputObjs, param, "miss output param `" + param + "` on node `" + m_name + "`");
    return spParam->spObject;
}

ZENO_API TempNodeCaller INode::temp_node(std::string const &id) {
    //TODO: deprecated
    std::shared_ptr<Graph> spGraph = graph.lock();
    assert(spGraph);
    return TempNodeCaller(spGraph.get(), id);
}

float INode::resolve(const std::string& formulaOrKFrame, const ParamType type)
{
    int frame = getGlobalState()->getFrameId();
    if (zeno::starts_with(formulaOrKFrame, "=")) {
        std::string code = formulaOrKFrame.substr(1);
        std::set<std::string>paths = zeno::getReferPath(code);
        std::string currPath = zeno::objPathToStr(get_path());
        currPath = currPath.substr(0, currPath.find_last_of("/"));
        for (auto& path : paths)
        {
            auto absolutePath = zeno::absolutePath(currPath, path);
            if (absolutePath != path)
            {
                code.replace(code.find(path), path.size(), absolutePath);
            }
        }
        Formula fmla(code);
        int ret = fmla.parse();
        float res = fmla.getResult();
        return res;
    }
    else if (zany curve = zeno::parseCurveObj(formulaOrKFrame)) {
        std::shared_ptr<zeno::CurveObject> curves = std::dynamic_pointer_cast<zeno::CurveObject>(curve);
        assert(curves && curves->keys.size() == 1);
        float fVal = curves->keys.begin()->second.eval(frame);
        return fVal;
    }
    else {
        if (Param_Float == type)
        {
            float fVal = std::stof(formulaOrKFrame);
            return fVal;
        }
        else {
            float fVal = std::stoi(formulaOrKFrame);
            return fVal;
        }
    }
}

std::vector<std::string> zeno::INode::getWildCardParams(const std::string& param_name, bool bPrim)
{
    std::vector<std::string> params;
    if (bPrim)
    {
        std::string wildCardGroup;
        if (m_inputPrims.find(param_name) != m_inputPrims.end())
        {
            wildCardGroup = m_inputPrims.find(param_name)->second->wildCardGroup;
        }
        else if (m_outputPrims.find(param_name) != m_outputPrims.end())
        {
            wildCardGroup = m_outputPrims.find(param_name)->second->wildCardGroup;
        }
        for (const auto&[name, spParam] : m_inputPrims)
        {
            if (spParam->wildCardGroup == wildCardGroup)
            {
                if (!spParam->links.empty())
                    return std::vector<std::string>();
                params.push_back(name);
            }
        }
        for (const auto& [name, spParam] : m_outputPrims)
        {
            if (spParam->wildCardGroup == wildCardGroup)
            {
                if (!spParam->links.empty())
                    return std::vector<std::string>();
                params.push_back(name);
            }
        }
    } 
    else
    {
        std::string wildCardGroup;
        if (m_inputObjs.find(param_name) != m_inputObjs.end())
        {
            wildCardGroup = m_inputObjs.find(param_name)->second->wildCardGroup;
        }
        else if (m_outputObjs.find(param_name) != m_outputObjs.end())
        {
            wildCardGroup = m_outputObjs.find(param_name)->second->wildCardGroup;
        }
        for (const auto& [name, spParam] : m_inputObjs)
        {
            if (spParam->wildCardGroup == wildCardGroup)
            {
                if (!spParam->links.empty())
                    return std::vector<std::string>();
                params.push_back(name);
            }
        }
        for (const auto& [name, spParam] : m_outputObjs)
        {
            if (spParam->wildCardGroup == wildCardGroup)
            {
                if (!spParam->links.empty())
                    return std::vector<std::string>();
                params.push_back(name);
            }
        }
    }
    return params;
}

template<class T, class E> T INode::resolveVec(const zvariant& defl, const ParamType type)
{
    if (std::holds_alternative<T>(defl)) {
        return std::get<T>(defl);
    }
    else if (std::holds_alternative<E>(defl)) {
        E vec = std::get<E>(defl);
        T vecnum;
        for (int i = 0; i < vec.size(); i++) {
            float fVal = resolve(vec[i], type);
            vecnum[i] = fVal;
        }
        return vecnum;
    }
    else {
        //error, throw expection.
        return T();
        //throw makeError<TypeError>(typeid(T));
    }
}

}
