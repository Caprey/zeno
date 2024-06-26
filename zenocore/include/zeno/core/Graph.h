#pragma once

#include <zeno/utils/api.h>
#include <zeno/core/IObject.h>
#include <zeno/core/INode.h>
#include <zeno/utils/safe_dynamic_cast.h>
#include <zeno/types/UserData.h>
#include <functional>
#include <variant>
#include <memory>
#include <unordered_map>
#include <string>
#include <set>
#include <any>
#include <map>
#include <zeno/core/data.h>
#include <zeno/utils/uuid.h>

namespace zeno {

struct Session;
struct SubgraphNode;
struct SubnetNode;
struct DirtyChecker;
struct INode;

struct Context {
    std::set<std::string> visited;

    inline void mergeVisited(Context const &other) {
        visited.insert(other.visited.begin(), other.visited.end());
    }

    ZENO_API Context();
    ZENO_API Context(Context const &other);
    ZENO_API ~Context();
};

struct Graph : std::enable_shared_from_this<Graph> {
    Session* session = nullptr;

    std::map<std::string, std::shared_ptr<INode>> m_nodes;  //based on uuid.
    std::set<std::string> nodesToExec;
    int beginFrameNumber = 0, endFrameNumber = 0;  // only use by runnermain.cpp

    std::map<std::string, std::string> portalIns;   //todo: deprecated, but need to keep compatible with old zsg.
    std::map<std::string, zany> portals;

    std::unique_ptr<Context> ctx;
    std::unique_ptr<DirtyChecker> dirtyChecker;

    std::optional<SubnetNode*> optParentSubgNode;

    ZENO_API Graph(const std::string& name, bool bAssets = false);
    ZENO_API ~Graph();

    Graph(Graph const &) = delete;
    Graph &operator=(Graph const &) = delete;
    Graph(Graph &&) = delete;
    Graph &operator=(Graph &&) = delete;

    //BEGIN NEW STANDARD API
    ZENO_API void init(const GraphData& graph);

    ZENO_API std::shared_ptr<INode> createNode(std::string const& cls, const std::string& orgin_name = "", bool bAssets = false, std::pair<float, float> pos = {});
    CALLBACK_REGIST(createNode, void, const std::string&, std::weak_ptr<zeno::INode>)

    ZENO_API bool removeNode(std::string const& name);
    CALLBACK_REGIST(removeNode, void, const std::string&)

    ZENO_API bool addLink(const EdgeInfo& edge);
    CALLBACK_REGIST(addLink, bool, EdgeInfo)

    ZENO_API bool removeLink(const EdgeInfo& edge);
    CALLBACK_REGIST(removeLink, bool, EdgeInfo)

    ZENO_API bool removeLinks(const std::string nodename, bool bInput, const std::string paramname);
    CALLBACK_REGIST(removeLinks, bool, std::string, bool, std::string)

    ZENO_API bool updateLink(const EdgeInfo& edge, bool bInput, const std::string oldkey, const std::string newkey);
    ZENO_API bool moveUpLinkKey(const EdgeInfo& edge, bool bInput, const std::string keyName);

    ZENO_API std::shared_ptr<INode> getNode(std::string const& name);
    ZENO_API std::shared_ptr<INode> getNodeByUuidPath(ObjPath path);
    ZENO_API std::shared_ptr<INode> Graph::getNodeByPath(std::string path);
    ZENO_API std::map<std::string, std::shared_ptr<INode>> getNodes() const;

    ZENO_API GraphData exportGraph() const;

    ZENO_API LinksData exportLinks() const;

    ZENO_API std::string getName() const;
    ZENO_API void setName(const std::string& name);

    ZENO_API std::string updateNodeName(const std::string oldName, const std::string newName = "");
    CALLBACK_REGIST(updateNodeName, void, std::string, std::string)

    ZENO_API void clear();
    CALLBACK_REGIST(clear, void)

    ZENO_API bool isAssets() const;
    ZENO_API std::set<std::string> searchByClass(const std::string& name) const;

    //END

    ZENO_API DirtyChecker &getDirtyChecker();
    ZENO_API void clearNodes();
    ZENO_API void runGraph();
    ZENO_API void applyNodes(std::set<std::string> const &ids);
    ZENO_API void addNode(std::string const &cls, std::string const &id);
    ZENO_API Graph *addSubnetNode(std::string const &id);
    ZENO_API Graph *getSubnetGraph(std::string const &id) const;
    ZENO_API bool applyNode(std::string const &id);
    ZENO_API void completeNode(std::string const &id);
    ZENO_API void bindNodeInput(std::string const &dn, std::string const &ds,
        std::string const &sn, std::string const &ss);

    //���������壺���input��defl value������ʵ�ʵĶ��󣿰�ԭ��zeno�����壬��ָdefl value
    ZENO_API void setNodeInput(std::string const &id, std::string const &par, zany const &val);

    ZENO_API void setKeyFrame(std::string const &id, std::string const &par, zany const &val);
    ZENO_API void setFormula(std::string const &id, std::string const &par, zany const &val);
    ZENO_API void addNodeOutput(std::string const &id, std::string const &par);
    ZENO_API zany getNodeInput(std::string const &sn, std::string const &ss) const;
    ZENO_API void loadGraph(const char *json);
    ZENO_API void setNodeParam(std::string const &id, std::string const &par,
        std::variant<int, float, std::string, zany> const &val);  /* to be deprecated */
    ZENO_API std::map<std::string, zany> callSubnetNode(std::string const &id,
            std::map<std::string, zany> inputs) const;
    ZENO_API std::map<std::string, zany> callTempNode(std::string const &id,
            std::map<std::string, zany> inputs);

    std::set<std::string> getSubInputs();
    std::set<std::string> getSubOutputs();
    void viewNodeUpdated(const std::string node, bool bView);
    void markDirtyWhenFrameChanged();
    void markDirtyAll();
    void onNodeParamUpdated(PrimitiveParam* spParam, zvariant old_value, zvariant new_value);

private:
    std::string generateNewName(const std::string& node_cls, const std::string& origin_name = "", bool bAssets = false);
    bool isLinkVaild(const EdgeInfo& edge);

    std::map<std::string, std::string> subInputNodes;
    std::map<std::string, std::string> subOutputNodes;

    std::map<std::string, std::string> m_name2uuid;

    std::map<std::string, std::set<std::string>> node_set;
    std::set<std::string> frame_nodes;      //record all nodes depended on frame num.
    std::set<std::string> subnet_nodes;
    std::set<std::string> asset_nodes;
    std::set<std::string> subinput_nodes;
    std::set<std::string> suboutput_nodes;

    std::set<std::string> m_viewnodes;
    std::string m_name;
    const bool m_bAssets;
};

}
