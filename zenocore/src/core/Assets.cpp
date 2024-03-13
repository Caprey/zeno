#include <zeno/core/Assets.h>
#include <zeno/extra/SubnetNode.h>
#include <zeno/core/IParam.h>
#include <filesystem>
#include <zeno/io/zdareader.h>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif


namespace zeno {

ZENO_API AssetsMgr::AssetsMgr() {
    initAssetsInfo();
}

ZENO_API AssetsMgr::~AssetsMgr() {

}

void AssetsMgr::initAssetsInfo() {
#ifdef _WIN32
    WCHAR documents[MAX_PATH];
    SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, documents);

    std::filesystem::path docPath(documents);

    std::filesystem::path zenoDir = std::filesystem::u8path(docPath.string() + "/Zeno");
    if (!std::filesystem::is_directory(zenoDir)) {
        std::filesystem::create_directories(zenoDir);
    }
    std::filesystem::path assetsDir = std::filesystem::u8path(zenoDir.string() + "/assets");
    if (!std::filesystem::is_directory(assetsDir)) {
        std::filesystem::create_directories(assetsDir);
    }

    for (auto const& dir_entry : std::filesystem::directory_iterator(assetsDir))
    {
        std::filesystem::path itemPath = dir_entry.path();
        if (itemPath.extension() == ".zda") {
            std::string zdaPath = itemPath.string();
            zenoio::ZdaReader reader;
            reader.setDelayReadGraph(true);
            zeno::scope_exit sp([&] {reader.setDelayReadGraph(false); });

            zenoio::ZSG_PARSE_RESULT result = reader.openFile(zdaPath);
            if (result.bSucceed) {
                zeno::ZenoAsset zasset = reader.getParsedAsset();
                zasset.info.path = zdaPath;
                createAsset(zasset);
            }
        }
    }
#endif
}

ZENO_API std::shared_ptr<Graph> AssetsMgr::getAssetGraph(const std::string& name, bool bLoadIfNotExist) {
    if (m_assets.find(name) != m_assets.end()) {
        if (!m_assets[name].sharedGraph) {
            zenoio::ZdaReader reader;
            reader.setDelayReadGraph(false);
            const AssetInfo& info = m_assets[name].m_info;
            std::string zdaPath = info.path;
            zenoio::ZSG_PARSE_RESULT result = reader.openFile(zdaPath);
            if (result.bSucceed) {
                zeno::ZenoAsset zasset = reader.getParsedAsset();
                assert(zasset.optGraph.has_value());
                std::shared_ptr<Graph> spGraph = std::make_shared<Graph>(info.name, true);
                spGraph->setName(info.name);
                spGraph->init(zasset.optGraph.value());
                m_assets[name].sharedGraph = spGraph;
            }
        }
        return m_assets[name].sharedGraph;
    }
    return nullptr;
}

ZENO_API void AssetsMgr::createAsset(const zeno::ZenoAsset asset) {
    Asset newAsst;

    newAsst.m_info = asset.info;
    if (asset.optGraph.has_value())
    {
        std::shared_ptr<Graph> spGraph = std::make_shared<Graph>(asset.info.name, true);
        spGraph->setName(asset.info.name);
        spGraph->init(asset.optGraph.value());
        newAsst.sharedGraph = spGraph;
    }
    newAsst.inputs = asset.inputs;
    newAsst.outputs = asset.outputs;

    if (m_assets.find(asset.info.name) != m_assets.end()) {
        m_assets[asset.info.name] = newAsst;
    }
    else {
        m_assets.insert(std::make_pair(asset.info.name, newAsst));
    }

    CALLBACK_NOTIFY(createAsset, asset.info)
}

ZENO_API void AssetsMgr::removeAsset(const std::string& name) {
    m_assets.erase(name);
    CALLBACK_NOTIFY(removeAsset, name)
}

ZENO_API void AssetsMgr::renameAsset(const std::string& old_name, const std::string& new_name) {
    //TODO
    CALLBACK_NOTIFY(renameAsset, old_name, new_name)
}

ZENO_API Asset AssetsMgr::getAsset(const std::string& name) const {
    if (m_assets.find(name) != m_assets.end()) {
        return m_assets.at(name);
    }
    return Asset();
}

ZENO_API std::vector<Asset> AssetsMgr::getAssets() const {
    std::vector<Asset> assets;
    for (auto& [name, asset] : m_assets) {
        assets.push_back(asset);
    }
    return assets;
}

ZENO_API void AssetsMgr::updateAssets(const std::string name, ParamsUpdateInfo info) {
    if (m_assets.find(name) == m_assets.end()) {
        return;
    }
    auto& assets = m_assets[name];
    std::set<std::string> inputs_old, outputs_old;

    std::set<std::string> input_names;
    std::set<std::string> output_names;
    for (auto param : assets.inputs) {
        input_names.insert(param.name);
    }
    for (auto param : assets.outputs) {
        output_names.insert(param.name);
    }

    for (const auto& param_name : input_names) {
        inputs_old.insert(param_name);
    }
    for (const auto& param_name : output_names) {
        outputs_old.insert(param_name);
    }

    params_change_info changes;

    for (auto _pair : info) {
        const ParamInfo& param = _pair.param;
        const std::string oldname = _pair.oldName;
        const std::string newname = param.name;

        auto& in_outputs = param.bInput ? input_names : output_names;
        auto& new_params = param.bInput ? changes.new_inputs : changes.new_outputs;
        auto& remove_params = param.bInput ? changes.remove_inputs : changes.remove_outputs;
        auto& rename_params = param.bInput ? changes.rename_inputs : changes.rename_outputs;

        if (oldname.empty()) {
            //new added name.
            if (in_outputs.find(newname) != in_outputs.end()) {
                // the new name happen to have the same name with the old name, but they are not the same param.
                in_outputs.erase(newname);
                if (param.bInput)
                    inputs_old.erase(newname);
                else
                    outputs_old.erase(newname);

                remove_params.insert(newname);
            }
            new_params.insert(newname);
        }
        else if (in_outputs.find(oldname) != in_outputs.end()) {
            if (oldname != newname) {
                //exist name changed.
                in_outputs.insert(newname);
                in_outputs.erase(oldname);

                rename_params.insert({ oldname, newname });
            }
            else {
                //name stays.
            }

            if (param.bInput)
                inputs_old.erase(oldname);
            else
                outputs_old.erase(oldname);
        }
        else {
            throw makeError<KeyError>(oldname, "the name does not exist on the node");
        }
    }

    //the left names are the names of params which will be removed.
    for (auto rem_name : inputs_old) {
        changes.remove_inputs.insert(rem_name);
    }
    //update the names.
    input_names.clear();
    for (const auto& [param, _] : info) {
        if (param.bInput)
            input_names.insert(param.name);
    }

    for (auto rem_name : outputs_old) {
        changes.remove_outputs.insert(rem_name);
    }
    output_names.clear();
    for (const auto& [param, _] : info) {
        if (!param.bInput)
            output_names.insert(param.name);
    }

    //update subnetnode.
    for (auto name : changes.new_inputs) {
        assets.sharedGraph->createNode("SubInput", name);
    }
    for (const auto& [old_name, new_name] : changes.rename_inputs) {
        assets.sharedGraph->updateNodeName(old_name, new_name);
    }
    for (auto name : changes.remove_inputs) {
        assets.sharedGraph->removeNode(name);
    }

    for (auto name : changes.new_outputs) {
        assets.sharedGraph->createNode("SubOutput", name);
    }
    for (const auto& [old_name, new_name] : changes.rename_outputs) {
        assets.sharedGraph->updateNodeName(old_name, new_name);
    }
    for (auto name : changes.remove_outputs) {
        assets.sharedGraph->removeNode(name);
    }

    //update assets data
    assets.inputs.clear();
    assets.outputs.clear();
    for (auto pair : info) {
        if (pair.param.bInput)
            assets.inputs.push_back(pair.param);
        else
            assets.outputs.push_back(pair.param);
    }
}

std::shared_ptr<Graph> AssetsMgr::forkAssetGraph(std::shared_ptr<Graph> assetGraph, std::shared_ptr<SubnetNode> subNode)
{
    std::shared_ptr<Graph> newGraph = std::make_shared<Graph>(assetGraph->getName(), true);
    newGraph->optParentSubgNode = subNode.get();
    for (const auto& [uuid, spNode] : assetGraph->getNodes())
    {
        zeno::NodeData nodeDat;
        const std::string& name = spNode->get_name();
        const std::string& cls = spNode->get_nodecls();

        if (auto spSubnetNode = std::dynamic_pointer_cast<SubnetNode>(spNode))
        {
            if (m_assets.find(cls) != m_assets.end()) {
                //asset node
                auto spNewSubnetNode = newGraph->createNode(cls, name, "assets", spNode->get_pos());
            }
            else {
                std::shared_ptr<INode> spNewNode = newGraph->createNode(cls, name);
                nodeDat = spSubnetNode->exportInfo();
                spNewNode->init(nodeDat);   //should clone graph.
            }
        }
        else {
            std::shared_ptr<INode> spNewNode = newGraph->createNode(cls, name);
            nodeDat = spNode->exportInfo();
            spNewNode->init(nodeDat);
        }
    }

    LinksData oldLinks = assetGraph->exportLinks();
    for (zeno::EdgeInfo oldLink : oldLinks) {
        newGraph->addLink(oldLink);
    }
    return newGraph;
}

ZENO_API bool AssetsMgr::isAssetGraph(std::shared_ptr<Graph> spGraph) const
{
    for (auto& [name, asset] : m_assets) {
        if (asset.sharedGraph == spGraph)
            return true;
    }
    return false;
}

ZENO_API std::shared_ptr<INode> AssetsMgr::newInstance(Graph* pGraph, const std::string& assetName, const std::string& nodeName, bool createInAsset) {
    if (m_assets.find(assetName) == m_assets.end()) {
        return nullptr;
    }

    Asset& assets = m_assets[assetName];
    if (!assets.sharedGraph) {
        getAssetGraph(assetName, true);
    }
    assert(assets.sharedGraph);

    std::shared_ptr<SubnetNode> spNode = std::make_shared<SubnetNode>();
    spNode->graph = pGraph;
    spNode->initUuid(pGraph, assetName);
    std::shared_ptr<Graph> assetGraph;
    if (!createInAsset) {
        //should expand the asset graph into a tree.
        assetGraph = forkAssetGraph(assets.sharedGraph, spNode);
    }
    else {
        assetGraph = assets.sharedGraph;
    }

    spNode->subgraph = assetGraph;
    spNode->m_nodecls = assetName;
    spNode->m_name = nodeName;

    for (const ParamInfo& param : assets.inputs)
    {
        std::shared_ptr<IParam> sparam = std::make_shared<IParam>();
        sparam->defl = param.defl;
        sparam->name = param.name;
        sparam->type = param.type;
        sparam->control = param.control;
        sparam->socketType = param.socketType;
        sparam->m_wpNode = spNode;
        spNode->add_input_param(sparam);
        spNode->m_input_names.push_back(param.name);
    }

    for (const ParamInfo& param : assets.outputs)
    {
        std::shared_ptr<IParam> sparam = std::make_shared<IParam>();
        sparam->defl = param.defl;
        sparam->name = param.name;
        sparam->type = param.type;
        sparam->m_wpNode = spNode;
        sparam->socketType = PrimarySocket;
        spNode->add_output_param(sparam);
        spNode->m_output_names.push_back(param.name);
    }

    return std::dynamic_pointer_cast<INode>(spNode);
}

ZENO_API void zeno::AssetsMgr::updateAssetInstance(const std::string& assetName, std::shared_ptr<SubnetNode>& spNode)
{
    if(m_assets.find(assetName) == m_assets.end()) {
        return;
    }

    Asset& assets = m_assets[assetName];
    if (!assets.sharedGraph) {
        getAssetGraph(assetName, true);
    }
    assert(assets.sharedGraph);
    std::shared_ptr<Graph> assetGraph = forkAssetGraph(assets.sharedGraph, spNode);

    spNode->subgraph = assetGraph;

    for (const ParamInfo& param : assets.inputs)
    {
        std::shared_ptr<IParam> sparam = std::make_shared<IParam>();
        sparam->defl = param.defl;
        sparam->name = param.name;
        sparam->type = param.type;
        sparam->control = param.control;
        sparam->socketType = param.socketType;
        sparam->m_wpNode = spNode;
        spNode->add_input_param(sparam);
        spNode->m_input_names.push_back(param.name);
    }

    for (const ParamInfo& param : assets.outputs)
    {
        std::shared_ptr<IParam> sparam = std::make_shared<IParam>();
        sparam->defl = param.defl;
        sparam->name = param.name;
        sparam->type = param.type;
        sparam->m_wpNode = spNode;
        sparam->socketType = PrimarySocket;
        spNode->add_output_param(sparam);
        spNode->m_output_names.push_back(param.name);
    }
}

}