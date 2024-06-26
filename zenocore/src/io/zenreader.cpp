#include <zeno/io/zenreader.h>
#include <zeno/io/iohelper.h>
#include <zeno/utils/helper.h>
#include <zeno/io/iotags.h>


namespace zenoio
{
    ZENO_API ZenReader::ZenReader()
    {
    }

    bool ZenReader::importNodes(const std::string& fn, zeno::NodesData& nodes, zeno::LinksData& links)
    {
        rapidjson::Document doc;
        doc.Parse(fn.c_str());

        if (!doc.IsObject() || !doc.HasMember("nodes"))
            return false;

        const rapidjson::Value& val = doc["nodes"];
        if (val.IsNull())
            return false;

        for (const auto& node : val.GetObject())
        {
            const std::string& nodeid = node.name.GetString();
            zeno::AssetsData assets;
            const zeno::NodeData& nodeData = _parseNode("", nodeid, node.value, assets, links);
            nodes.insert(std::make_pair(nodeid, nodeData));
        }
        return true;
    }

    bool ZenReader::_parseMainGraph(const rapidjson::Document& doc, zeno::GraphData& ret)
    {
        zeno::AssetsData assets;        //todo
        if (doc.HasMember("main"))
        {
            const rapidjson::Value& mainGraph = doc["main"];
            if (_parseGraph(mainGraph, assets, ret))
            {
                ret.name = "main";
                ret.type = zeno::Subnet_Main;
                return true;
            }
        }
        return false;
    }

    bool ZenReader::_parseGraph(const rapidjson::Value& graph, const zeno::AssetsData& assets, zeno::GraphData& ret)
    {
        if (!graph.IsObject() || !graph.HasMember("nodes"))
            return false;

        const auto& nodes = graph["nodes"];
        if (nodes.IsNull())
            return false;

        for (const auto& node : nodes.GetObject())
        {
            const std::string& nodeid = node.name.GetString();
            const zeno::NodeData& nodeData = _parseNode("", nodeid, node.value, assets, ret.links);
            ret.nodes.insert(std::make_pair(nodeid, nodeData));
        }
        return true;
    }

    zeno::NodeData ZenReader::_parseNode(
        const std::string& subgPath,    //Ҳ�������ˣ���Ϊ����Ϣ������path�ķ�ʽ���棨�����鷳�����ȱ�����
        const std::string& nodeid,
        const rapidjson::Value& nodeObj,
        const zeno::AssetsData& assets,
        zeno::LinksData& links)
    {
        zeno::NodeData retNode;

        const auto& objValue = nodeObj;
        const rapidjson::Value& nameValue = objValue["name"];
        const std::string& cls = objValue["class"].GetString();

        retNode.name = nodeid;
        retNode.cls = cls;
        retNode.type = zeno::Node_Normal;

        //Ҫ��parse customui�Ի�������������ṹ��
        if (objValue.HasMember("subnet-customUi")) {
            retNode.customUi = _parseCustomUI(objValue["subnet-customUi"]);
        }
        if (objValue.HasMember(iotags::params::node_inputs_objs)) {
            _parseInputs(true, nodeid, cls, objValue[iotags::params::node_inputs_objs], retNode, links);
        }
        if (objValue.HasMember(iotags::params::node_inputs_primitive) && !objValue.HasMember("subnet-customUi")) {
            _parseInputs(false, nodeid, cls, objValue[iotags::params::node_inputs_primitive], retNode, links);
        }
        if (objValue.HasMember(iotags::params::node_outputs_primitive)) {
            _parseOutputs(false, nodeid, cls, objValue[iotags::params::node_outputs_primitive], retNode, links);
        }
        if (objValue.HasMember(iotags::params::node_outputs_objs)) {
            _parseOutputs(true, nodeid, cls, objValue[iotags::params::node_outputs_objs], retNode, links);
        }

        if (objValue.HasMember("uipos"))
        {
            auto uipos = objValue["uipos"].GetArray();
            retNode.uipos = { uipos[0].GetFloat(), uipos[1].GetFloat() };
        }
        if (objValue.HasMember("status"))
        {
            auto optionsArr = objValue["status"].GetArray();
            zeno::NodeStatus opts = zeno::None;
            for (int i = 0; i < optionsArr.Size(); i++)
            {
                assert(optionsArr[i].IsString());

                const std::string& optName = optionsArr[i].GetString();
                if (optName == "View")
                {
                    retNode.bView = true;
                }
                else if (optName == "MUTE")
                {
                }
            }
        }
        if (objValue.HasMember("collasped"))
        {
            bool bCollasped = objValue["collasped"].GetBool();
            retNode.bCollasped = bCollasped;
        }

        if (cls == "Blackboard")
        {
            zeno::GroupInfo blackboard;
            //use subkey "blackboard" for zeno2 io, but still compatible with zeno1
            const rapidjson::Value& blackBoardValue = objValue.HasMember("blackboard") ? objValue["blackboard"] : objValue;

            if (blackBoardValue.HasMember("special")) {
                blackboard.special = blackBoardValue["special"].GetBool();
            }

            blackboard.title = blackBoardValue.HasMember("title") ? blackBoardValue["title"].GetString() : "";
            blackboard.content = blackBoardValue.HasMember("content") ? blackBoardValue["content"].GetString() : "";

            if (blackBoardValue.HasMember("width") && blackBoardValue.HasMember("height")) {
                float w = blackBoardValue["width"].GetFloat();
                float h = blackBoardValue["height"].GetFloat();
                blackboard.sz = { w,h };
            }
            if (blackBoardValue.HasMember("params")) {
                //todo
            }
            //TODO: import blackboard.
        }
        /*else if (cls == "Group")
        {
            zeno::GroupInfo group;
            const rapidjson::Value& blackBoardValue = objValue.HasMember("blackboard") ? objValue["blackboard"] : objValue;

            group.title = blackBoardValue.HasMember("title") ? blackBoardValue["title"].GetString() : "";
            group.background = blackBoardValue.HasMember("background") ? blackBoardValue["background"].GetString() : "#3C4645";

            if (blackBoardValue.HasMember("width") && blackBoardValue.HasMember("height")) {
                float w = blackBoardValue["width"].GetFloat();
                float h = blackBoardValue["height"].GetFloat();
                group.sz = { w,h };
            }
            if (blackBoardValue.HasMember("items")) {
                auto item_keys = blackBoardValue["items"].GetArray();
                for (int i = 0; i < item_keys.Size(); i++) {
                    std::string key = item_keys[i].GetString();
                    group.items.push_back(key);
                }
            }*/
            //TODO: import group.
        //}

        if (objValue.HasMember("subnet")) {
            zeno::GraphData subgraph;
            _parseGraph(objValue["subnet"], assets, subgraph);
            retNode.subgraph = subgraph;
            retNode.type = zeno::Node_SubgraphNode;
        }

        if (objValue.HasMember("asset")) {
            zeno::AssetInfo info;
            auto& assetObj = objValue["asset"];
            if (assetObj.HasMember("name") && assetObj.HasMember("version"))
            {
                info.name = assetObj["name"].GetString();
                std::string verStr = assetObj["version"].GetString();
                std::vector<std::string> vec = zeno::split_str(verStr.c_str(), '.');
                if (vec.size() == 1)
                {
                    info.majorVer = std::stoi(vec[0]);
                }
                else if (vec.size() == 2)
                {
                    info.majorVer = std::stoi(vec[0]);
                    info.minorVer = std::stoi(vec[1]);
                }
            }
            retNode.type = zeno::Node_AssetInstance;
            retNode.asset = info;
        }

        return retNode;
    }

    void ZenReader::_parseOutputs(
        const bool bObjectParam,
        const std::string& id,
        const std::string& nodeName,
        const rapidjson::Value& outputs,
        zeno::NodeData& ret,
        zeno::LinksData& links)
    {
        for (const auto& outParamObj : outputs.GetObject())
        {
            const std::string& outParam = outParamObj.name.GetString();
            if (outParam == "DST")
                continue;

            const auto& outObj = outParamObj.value;
            if (outObj.IsNull())
            {
                zeno::ParamObject param;
                param.name = outParam;
                param.socketType = zeno::Socket_Output;
                ret.customUi.outputObjs.push_back(param);
            }
            else if (outObj.IsObject())
            {
                _parseSocket(false, false, bObjectParam, id, nodeName, outParam, outObj, ret, links);
            }
            else
            {
                zeno::log_error("unknown format");
            }
        }
    }

    void ZenReader::_parseInputs(
        const bool bObjectParam,
        const std::string& id,
        const std::string& nodeName,
        const rapidjson::Value& inputs,
        zeno::NodeData& ret,
        zeno::LinksData& links)
    {
        for (const auto& inObj : inputs.GetObject())
        {
            const std::string& inSock = inObj.name.GetString();
            const auto& inputObj = inObj.value;

            if (inputObj.IsNull())
            {
                zeno::ParamObject param;
                param.name = inSock;
                ret.customUi.inputObjs.emplace_back(param);
            }
            else if (inputObj.IsObject())
            {
                bool bSubnet = ret.cls == "Subnet";
                _parseSocket(true, bSubnet, bObjectParam, id, nodeName, inSock, inputObj, ret, links);
            }
            else
            {
                zeno::log_error("unknown format");
            }
        }
    }

    void ZenReader::_parseSocket(
        const bool bInput,
        const bool bSubnetNode,
        const bool bObjectParam,
        const std::string& id,
        const std::string& nodeCls,
        const std::string& sockName,
        const rapidjson::Value& sockObj,
        zeno::NodeData& ret,
        zeno::LinksData& links)
    {
        std::string sockProp;
        if (sockObj.HasMember("property"))
        {
            //ZASSERT_EXIT(sockObj["property"].IsString());
            sockProp = sockObj["property"].GetString();
        }

        zeno::ParamControl ctrl = zeno::NullControl;
        zeno::ParamType paramType = zeno::Param_Null;
        zeno::SocketType socketType = zeno::NoSocket;
        zeno::zvariant defl;
        zeno::LinksData paramLinks;
        std::optional<zeno::ControlProperty> ctrlProps;
        std::string tooltip;

        zeno::SocketProperty prop = zeno::SocketProperty::Socket_Normal;
        if (sockProp == "dict-panel")
            prop = zeno::SocketProperty::Socket_Normal;    //deprecated
        else if (sockProp == "editable")
            prop = zeno::SocketProperty::Socket_Editable;
        else if (sockProp == "group-line")
            prop = zeno::SocketProperty::Socket_Normal;    //deprecated

        if (m_bDiskReading &&
            (prop == zeno::SocketProperty::Socket_Editable ||
                nodeCls == "MakeList" || nodeCls == "MakeDict" || nodeCls == "ExtractDict"))
        {
            if (prop == zeno::SocketProperty::Socket_Editable) {
                //like extract dict.
                paramType = zeno::Param_String;
            }
            else {
                paramType = zeno::Param_Null;
            }
        }

        if (sockObj.HasMember("type")) {
            paramType = zeno::convertToType(sockObj["type"].GetString());
        }

        bool bPrimitiveType = !bObjectParam;

        if (sockObj.HasMember("default-value")) {
            defl = zenoio::jsonValueToZVar(sockObj["default-value"], paramType);
        }

        if (sockObj.HasMember("socket-type") && sockObj["socket-type"].IsString()) {
            const std::string& sockType = sockObj["socket-type"].GetString();
            socketType = getSocketTypeByDesc(sockType);
        }

        std::string wildCardGroup;
        if (sockObj.HasMember("wild_card_group"))
        {
            wildCardGroup = sockObj["wild_card_group"].GetString();
        }
        //link:
        if (bInput && sockObj.HasMember("links") && sockObj["links"].IsArray())
        {
            auto& arr = sockObj["links"].GetArray();
            for (int i = 0; i < arr.Size(); i++) {
                auto& linkObj = arr[i];
                const std::string& outnode = linkObj["out-node"].GetString();
                const std::string& outsock = linkObj["out-socket"].GetString();
                const std::string& outkey = linkObj["out-key"].GetString();
                const std::string& innode = id;
                const std::string& insock = sockName;
                const std::string& inkey = linkObj["in-key"].GetString();
                std::string property = "copy";
                if (linkObj.HasMember("property")) {
                    property = linkObj["property"].GetString();
                }

                zeno::LinkFunction prop = property == "copy" ? zeno::Link_Copy : zeno::Link_Ref;
                zeno::EdgeInfo link = { outnode, outsock, outkey, innode, insock, inkey, prop };
                paramLinks.push_back(link);
                links.push_back(link);
            }
        }

        if (sockObj.HasMember("control"))
        {
            zeno::ControlProperty props;
            bool bret = zenoio::importControl(sockObj["control"], ctrl, props);
            if (bret) {
                if (ctrl == zeno::NullControl)
                    ctrl = zeno::getDefaultControl(paramType);
                if (props.items || props.ranges)
                    ctrlProps = props;
            }
        }

        if (sockObj.HasMember("tooltip"))
        {
            tooltip = sockObj["tooltip"].GetString();
        }

        bool bVisible = true;
        if (sockObj.HasMember("visible")) {
            bVisible = sockObj["visible"].GetBool();
        }

        if (bPrimitiveType) {
            zeno::ParamPrimitive param;
            param.bInput = bInput;
            param.control = ctrl;
            param.ctrlProps = ctrlProps;
            param.defl = defl;
            param.name = sockName;
            param.prop = prop;
            param.socketType = socketType;
            param.tooltip = tooltip;
            param.type = paramType;
            param.bVisible = bVisible;
            param.wildCardGroup = wildCardGroup;
            if (bInput) {
                //��zsgû�в㼶�ṹ��ֱ����Ĭ�Ͼ���
                if (ret.customUi.inputPrims.tabs.empty())
                {
                    zeno::ParamTab tab;
                    tab.name = "Tab1";
                    zeno::ParamGroup group;
                    group.name = "Group1";
                    tab.groups.emplace_back(group);
                    ret.customUi.inputPrims.tabs.emplace_back(tab);
                }
                auto& group = ret.customUi.inputPrims.tabs[0].groups[0];
                group.params.emplace_back(param);
            }
            else {
                ret.customUi.outputPrims.emplace_back(param);
            }
        }
        else {
            zeno::ParamObject param;
            param.bInput = bInput;
            param.name = sockName;
            param.prop = prop;
            param.socketType = socketType;
            param.tooltip = tooltip;
            param.type = paramType;
            param.wildCardGroup = wildCardGroup;
            if (bInput) {
                ret.customUi.inputObjs.emplace_back(param);
            }
            else {
                ret.customUi.outputObjs.emplace_back(param);
            }
        }
    }

    zeno::CustomUI ZenReader::_parseCustomUI(const rapidjson::Value& customuiObj)
    {
        auto readCustomUiParam = [](zeno::ParamPrimitive& paramInfo, const rapidjson::Value& param) {
            if (param.IsObject()) {
                auto paramValue = param.GetObject();
                if (paramValue.HasMember("type") && paramValue["type"].IsString())
                    paramInfo.type = zeno::convertToType(paramValue["type"].GetString());
                if (paramValue.HasMember("socket-type") && paramValue["socket-type"].IsString())
                    paramInfo.socketType = getSocketTypeByDesc(paramValue["socket-type"].GetString());

                paramInfo.defl = zenoio::jsonValueToZVar(paramValue["default-value"], paramInfo.type);
                if (paramValue.HasMember("control") && paramValue["control"].IsObject())
                {
                    zeno::ControlProperty props;
                    bool bret = zenoio::importControl(paramValue["control"], paramInfo.control, props);
                    if (bret) {
                        if (paramInfo.control == zeno::NullControl)
                            paramInfo.control = zeno::getDefaultControl(paramInfo.type);
                        if (props.items || props.ranges)
                            paramInfo.ctrlProps = props;
                    }
                }
                if (paramValue.HasMember("tooltip"))
                    paramInfo.tooltip = paramValue["tooltip"].GetString();
            }
        };

        zeno::CustomUI ui;
        if (!customuiObj.IsNull())
        {
            auto cusomui = customuiObj.GetObject();
            if (cusomui.HasMember("tabs") && !cusomui["tabs"].IsNull())
            {
                auto tabs = cusomui["tabs"].GetObject();
                for (const auto& tab : tabs)
                {
                    zeno::ParamTab paramTab;
                    paramTab.name = tab.name.GetString();
                    if (!tab.value.IsNull())
                    {
                        auto groups = tab.value.GetObject();
                        for (const auto& group : groups)
                        {
                            zeno::ParamGroup paramGroup;
                            paramGroup.name = group.name.GetString();
                            if (!group.value.IsNull())
                            {
                                auto params = group.value.GetObject();
                                for (const auto& param : params)
                                {
                                    zeno::ParamPrimitive paramInfo;
                                    paramInfo.name = param.name.GetString();
                                    readCustomUiParam(paramInfo, param.value);
                                    paramGroup.params.push_back(paramInfo);
                                }
                            }
                            paramTab.groups.push_back(paramGroup);
                        }
                   }
                   ui.inputPrims.tabs.push_back(paramTab);
                }
            }

            ui.nickname = cusomui["nickname"].GetString();
            ui.iconResPath = cusomui["iconResPath"].GetString();
            ui.category = cusomui["category"].GetString();
            ui.doc = cusomui["doc"].GetString();
        }
        return ui;
    }
}