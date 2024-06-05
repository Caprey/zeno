#include "GraphModel.h"
#include "uicommon.h"
#include "Descriptors.h"
#include "zassert.h"
#include "variantptr.h"
#include "model/GraphsTreeModel.h"
#include "model/graphsmanager.h"
#include "zenoapplication.h"
#include <zeno/extra/SubnetNode.h>
#include <zeno/core/Assets.h>
#include <zeno/core/data.h>
#include <zeno/core/CoreParam.h>
#include "util/uihelper.h"
#include "util/jsonhelper.h"


NodeItem::NodeItem(QObject* parent) : QObject(parent)
{
}

NodeItem::~NodeItem()
{
    unregister();
}

void NodeItem::unregister()
{
    if (std::shared_ptr<zeno::INode> spNode = m_wpNode.lock())
    {
        bool ret = spNode->unregister_set_pos(m_cbSetPos);
        ZASSERT_EXIT(ret);
        ret = spNode->unregister_set_view(m_cbSetView);
        ZASSERT_EXIT(ret);
    }
    m_cbSetPos = "";
    m_cbSetView = "";
}

void NodeItem::init(GraphModel* pGraphM, std::shared_ptr<zeno::INode> spNode)
{
    this->m_wpNode = spNode;

    m_cbSetPos = spNode->register_set_pos([=](std::pair<float, float> pos) {
        this->pos = { pos.first, pos.second };  //update the cache
        QModelIndex idx = pGraphM->indexFromName(this->name);
        emit pGraphM->dataChanged(idx, idx, QVector<int>{ ROLE_OBJPOS });
    });

    m_cbSetView = spNode->register_set_view([=](bool bView) {
        this->bView = bView;
        QModelIndex idx = pGraphM->indexFromName(this->name);
        emit pGraphM->dataChanged(idx, idx, QVector<int>{ ROLE_NODE_ISVIEW });
    });

    this->params = new ParamsModel(spNode, this);
    this->name = QString::fromStdString(spNode->get_name());
    this->cls = QString::fromStdString(spNode->get_nodecls());
    this->dispName = QString::fromStdString(spNode->get_show_name());
    this->dispIcon = QString::fromStdString(spNode->get_show_icon());
    this->bView = spNode->is_view();
    this->runState.bDirty = spNode->is_dirty();
    this->runState.runstatus = spNode->get_run_status();
    auto pair = spNode->get_pos();
    this->pos = QPointF(pair.first, pair.second);
    this->uuidPath = spNode->get_uuid_path();
    if (std::shared_ptr<zeno::SubnetNode> subnetnode = std::dynamic_pointer_cast<zeno::SubnetNode>(spNode))
    {
        GraphModel* parentM = qobject_cast<GraphModel*>(this->parent());
        auto pModel = new GraphModel(subnetnode->subgraph, parentM->treeModel(), this);
        bool bAssets = subnetnode->subgraph->isAssets();
        if (bAssets) {
            if (!subnetnode->in_asset_file())
                pModel->setLocked(true);
        }
        this->optSubgraph = pModel;
    }
}


GraphModel::GraphModel(std::shared_ptr<zeno::Graph> spGraph, GraphsTreeModel* pTree, QObject* parent)
    : QAbstractListModel(parent)
    , m_wpCoreGraph(spGraph)
    , m_pTree(pTree)
{
    m_graphName = QString::fromStdString(spGraph->getName());
    m_undoRedoStack = m_graphName == "main" || zeno::getSession().assets->isAssetGraph(spGraph) ? new QUndoStack(this) : nullptr;
    m_linkModel = new LinkModel(this);
    registerCoreNotify();

    for (auto& [name, node] : spGraph->getNodes()) {
        _appendNode(node);
    }

    _initLink();

    if (m_pTree) {
        connect(this, SIGNAL(rowsInserted(const QModelIndex&, int, int)),
            m_pTree, SLOT(onGraphRowsInserted(const QModelIndex&, int, int)));
        connect(this, SIGNAL(rowsAboutToBeRemoved(const QModelIndex&, int, int)),
            m_pTree, SLOT(onGraphRowsAboutToBeRemoved(const QModelIndex&, int, int)));
        connect(this, SIGNAL(rowsRemoved(const QModelIndex&, int, int)),
            m_pTree, SLOT(onGraphRowsRemoved(const QModelIndex&, int, int)));
        connect(this, SIGNAL(nameUpdated(const QModelIndex&, const QString&)),
            m_pTree, SLOT(onNameUpdated(const QModelIndex&, const QString&)));
    }
}

void GraphModel::registerCoreNotify()
{
    std::shared_ptr<zeno::Graph> coreGraph = m_wpCoreGraph.lock();
    ZASSERT_EXIT(coreGraph);

    m_cbCreateNode = coreGraph->register_createNode([&](const std::string& name, std::weak_ptr<zeno::INode> spNode) {
        auto coreNode = spNode.lock();
        _appendNode(coreNode);
    });

    m_cbRemoveNode = coreGraph->register_removeNode([&](const std::string& name) {
        QString qName = QString::fromStdString(name);
        ZASSERT_EXIT(m_name2uuid.find(qName) != m_name2uuid.end(), false);
        QString uuid = m_name2uuid[qName];
        ZASSERT_EXIT(m_uuid2Row.find(uuid) != m_uuid2Row.end(), false);
        int row = m_uuid2Row[uuid];
        removeRow(row);
        GraphsManager::instance().currentModel()->markDirty(true);
    });

    m_cbAddLink = coreGraph->register_addLink([&](zeno::EdgeInfo edge) -> bool {
        _addLink(edge);
        return true;
    });

    m_cbRenameNode = coreGraph->register_updateNodeName([&](std::string oldname, std::string newname) {
        const QString& oldName = QString::fromStdString(oldname);
        const QString& newName = QString::fromStdString(newname);
        _updateName(oldName, newName);
    });

    m_cbRemoveLink = coreGraph->register_removeLink([&](zeno::EdgeInfo edge) -> bool {
        return _removeLink(edge);
    });

    m_cbClearGraph = coreGraph->register_clear([&]() {
        _clear();
    });
}

void GraphModel::unRegisterCoreNotify()
{
    if (std::shared_ptr<zeno::Graph> coreGraph = m_wpCoreGraph.lock())
    {
        bool ret = coreGraph->unregister_createNode(m_cbCreateNode);
        ZASSERT_EXIT(ret);
        ret = coreGraph->unregister_removeNode(m_cbRemoveNode);
        ZASSERT_EXIT(ret);
        ret = coreGraph->unregister_addLink(m_cbAddLink);
        ZASSERT_EXIT(ret);
        ret = coreGraph->unregister_removeLink(m_cbRemoveLink);
        ZASSERT_EXIT(ret);
        ret = coreGraph->unregister_updateNodeName(m_cbRenameNode);
        ZASSERT_EXIT(ret);
        ret = coreGraph->unregister_clear(m_cbClearGraph);
        ZASSERT_EXIT(ret);
    }
    m_cbCreateNode = "";
    m_cbCreateNode = "";
    m_cbAddLink = "";
    m_cbRemoveLink = "";
    m_cbRenameNode = "";
    m_cbClearGraph = "";
}

GraphModel::~GraphModel()
{
    unRegisterCoreNotify();
}

void GraphModel::clear()
{
    std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
    ZASSERT_EXIT(spGraph);
    spGraph->clear();
}

int GraphModel::indexFromId(const QString& name) const
{
    if (m_name2uuid.find(name) == m_name2uuid.end())
        return -1;

    QString uuid = m_name2uuid[name];
    if (m_uuid2Row.find(uuid) == m_uuid2Row.end())
        return -1;
    return m_uuid2Row[uuid];
}

QModelIndex GraphModel::indexFromName(const QString& name) const {
    int row = indexFromId(name);
    if (row == -1) {
        return QModelIndex();
    }
    return createIndex(row, 0);
}

void GraphModel::addLink(const QString& fromNodeStr, const QString& fromParamStr,
    const QString& toNodeStr, const QString& toParamStr)
{
    zeno::EdgeInfo link;
    link.inNode = toNodeStr.toStdString();
    link.inParam = toParamStr.toStdString();
    link.outNode = fromNodeStr.toStdString();
    link.outParam = fromParamStr.toStdString();
    _addLink(link);
    //_addLinkImpl(link, true);
}

void GraphModel::addLink(const zeno::EdgeInfo& link)
{
    _addLinkImpl(link, true);
}

QString GraphModel::name() const
{
    return m_graphName;
}

QString GraphModel::owner() const
{
    if (auto pItem = qobject_cast<NodeItem*>(parent()))
    {
        auto spNode = pItem->m_wpNode.lock();
        return spNode ? QString::fromStdString(spNode->get_name()) : "";
    }
    else {
        return "main";
    }
}

int GraphModel::rowCount(const QModelIndex& parent) const
{
    return m_nodes.size();
}

QVariant GraphModel::data(const QModelIndex& index, int role) const
{
    NodeItem* item = m_nodes[m_row2uuid[index.row()]];

    switch (role) {
        case Qt::DisplayRole:
        case ROLE_NODE_NAME: {
            return item->name;
        }
        case ROLE_NODE_DISPLAY_NAME: {
            return item->dispName;
        }
        case ROLE_NODE_DISPLAY_ICON: {
            return item->dispIcon;
        }
        case ROLE_NODE_UUID_PATH: {
            return QVariant::fromValue(item->uuidPath);
        }
        case ROLE_CLASS_NAME: {
            return item->cls;
        }
        case ROLE_OBJPOS: {
            return item->pos;   //qpoint supported by qml?
            //return QVariantList({ item->pos.x(), item->pos.y() });
        }
        case ROLE_PARAMS:
        {
            return QVariantPtr<ParamsModel>::asVariant(item->params);
        }
        case ROLE_SUBGRAPH:
        {
            if (item->optSubgraph.has_value())
                return QVariant::fromValue(item->optSubgraph.value());
            else
                return QVariant();  
        }
        case ROLE_GRAPH:
        {
            return QVariantPtr<GraphModel>::asVariant(const_cast<GraphModel*>(this));
        }
        case ROLE_INPUTS:
        {
            if (item->params)
                return QVariant::fromValue(item->params->getInputs());
            return QVariant();
        }
        case ROLE_OUTPUTS:
        {
            if (item->params)
                return QVariant::fromValue(item->params->getOutputs());
            return QVariant();
        }
        case ROLE_NODEDATA:
        {
            zeno::NodeData data;
            auto spNode = item->m_wpNode.lock();
            ZASSERT_EXIT(spNode, QVariant());
            return QVariant::fromValue(spNode->exportInfo());
        }
        case ROLE_NODE_STATUS:
        {
            int options = zeno::None;
            if (item->bView)
                options |= zeno::View;
            //if (item->bMute)
            return QVariant(options);
        }
        case ROLE_NODE_ISVIEW:
        {
            return item->bView;
        }
        case ROLE_NODE_RUN_STATE:
        {
            return QVariant::fromValue(item->runState);
        }
        case ROLE_NODE_DIRTY:
        {
            return item->runState.bDirty;
        }
        case ROLE_NODETYPE:
        {
            std::shared_ptr<zeno::INode> spNode = item->m_wpNode.lock();
            auto spSubnetNode = std::dynamic_pointer_cast<zeno::SubnetNode>(spNode);
            if (spSubnetNode) {
                bool bAssets = spSubnetNode->subgraph->isAssets();
                if (bAssets) {
                    if (spSubnetNode->in_asset_file())
                        return zeno::Node_AssetReference;
                    else
                        return zeno::Node_AssetInstance;
                }
                return zeno::Node_SubgraphNode;
            }
            if (spNode && spNode->get_nodecls() == "Group")
                return zeno::Node_Group;
            return zeno::Node_Normal;
        }
        case ROLE_NODE_CATEGORY:
        {
            std::shared_ptr<zeno::INode> spNode = item->m_wpNode.lock();
            if (spNode->nodeClass)
            {
                return QString::fromStdString(spNode->nodeClass->m_customui.category);
            }
            else
            {
                return "";
            }
        }
        case ROLE_OBJPATH:
        {
            QStringList path = currentPath();
            path.append(item->name);
            return path;
        }
        case ROLE_COLLASPED:
        {
            return item->bCollasped;
        }
        case ROLE_KEYFRAMES: 
        {
           QVector<int> keys;
            for (const zeno::ParamPrimitive& info : item->params->getInputs()) {
                QVariant value = UiHelper::zvarToQVar(info.defl);
                auto curves = JsonHelper::parseCurves(value);
                for (CURVE_DATA& curve : curves)
                {
                    keys << curve.pointBases();
                }
            }
            keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
            return QVariant::fromValue(keys);
        }
        default:
            return QVariant();
    }
}

bool GraphModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    NodeItem* item = m_nodes[m_row2uuid[index.row()]];

    switch (role) {
        case ROLE_CLASS_NAME: {
            //TODO: rename by core graph
            emit dataChanged(index, index, QVector<int>{role});
            return true;
        }
        case ROLE_OBJPOS:
        {
            auto spNode = item->m_wpNode.lock();
            if (value.type() == QVariant::PointF) {
                QPointF pos = value.toPoint();
                spNode->set_pos({ pos.x(), pos.y() });
            }
            else {
                QVariantList lst = value.toList();
                if (lst.size() != 2)
                    return false;
                spNode->set_pos({ lst[0].toFloat(), lst[1].toFloat() });
            }
            return true;
        }
        case ROLE_COLLASPED:
        {
            item->bCollasped = value.toBool();
            emit dataChanged(index, index, QVector<int>{role});
            return true;
        }
        case ROLE_NODE_RUN_STATE:
        {
            item->runState = value.value<NodeState>();
            emit dataChanged(index, index, QVector<int>{role});
            return true;
        }
        case ROLE_NODE_STATUS:
        {
            setView(index, value.toInt() & zeno::View);
            // setMute();
            break;
        }
        case ROLE_NODE_ISVIEW:
        {
            break;
        }
        case ROLE_INPUTS:
        {
            PARAMS_INFO paramsInfo = value.value<PARAMS_INFO>();
            for (auto&[key, param] : paramsInfo)
            {
                QModelIndex idx = item->params->paramIdx(key, true);
                item->params->setData(idx, UiHelper::zvarToQVar(param.defl) , ROLE_PARAM_VALUE);
            }
            return true;
        }
    }
    return false;
}

QModelIndexList GraphModel::match(const QModelIndex& start, int role,
    const QVariant& value, int hits,
    Qt::MatchFlags flags) const
{
    QModelIndexList result;
    if (role == ROLE_CLASS_NAME) {
        std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
        ZASSERT_EXIT(spGraph, result);
        std::string content = value.toString().toStdString();
        auto results = spGraph->searchByClass(content);
        for (std::string node : results) {
            QModelIndex nodeIdx = indexFromName(QString::fromStdString(node));
            result.append(nodeIdx);
        }
    }
    return result;
}

QList<SEARCH_RESULT> GraphModel::search(const QString& content, SearchType searchType, SearchOpt searchOpts)
{
    QList<SEARCH_RESULT> results;
    if (content.isEmpty())
        return results;

    if (searchType & SEARCH_NODEID) {
        QModelIndexList lst;
        if (searchOpts == SEARCH_MATCH_EXACTLY) {
            QModelIndex idx = indexFromName(content);
            if (idx.isValid())
                lst.append(idx);
        }
        else {
            lst = _base::match(this->index(0, 0), ROLE_NODE_NAME, content, -1, Qt::MatchContains);
        }
        if (!lst.isEmpty()) {
            for (const QModelIndex& nodeIdx : lst) {
                SEARCH_RESULT result;
                result.targetIdx = nodeIdx;
                result.subGraph = this;
                result.type = SEARCH_NODEID;
                results.append(result);
            }
        }
        for (auto& subnode : m_subgNodes)
        {
            if (m_name2uuid.find(subnode) == m_name2uuid.end())
                continue;
            NodeItem* pItem = m_nodes[m_name2uuid[subnode]];
            if (!pItem->optSubgraph.has_value())
                continue;
            QList<SEARCH_RESULT>& subnodeRes = pItem->optSubgraph.value()->search(content, searchType, searchOpts);
            for (auto& res: subnodeRes)
                results.push_back(res);
        }
    }

    //TODO

    return results;
}

QList<SEARCH_RESULT> GraphModel::searchByUuidPath(const zeno::ObjPath& uuidPath)
{
    QList<SEARCH_RESULT> results;
    if (uuidPath.empty())
        return results;

    SEARCH_RESULT result;
    result.targetIdx = indexFromUuidPath(uuidPath);
    result.subGraph = getGraphByPath(uuidPath2ObjPath(uuidPath));
    result.type = SEARCH_NODEID;
    results.append(result);
    return results;
}

QStringList GraphModel::uuidPath2ObjPath(const zeno::ObjPath& uuidPath)
{
    QStringList res;
    zeno::ObjPath tmp = uuidPath;
    if (tmp.empty())
        return res;

    int idx = tmp.find("/");
    auto uuid = tmp.substr(0, idx);
    auto it = m_nodes.find(QString::fromStdString(uuid));
    if (it == m_nodes.end()) {
        NodeItem* pItem = it.value();
        res.append(pItem->getName());

        if (idx >= 0)
            tmp = tmp.substr(idx+1, tmp.size() - idx);

        if (pItem->optSubgraph.has_value())
            res.append(pItem->optSubgraph.value()->uuidPath2ObjPath(tmp));
    }

    return res;
}

QModelIndex GraphModel::indexFromUuidPath(const zeno::ObjPath& uuidPath)
{
    if (uuidPath.empty())
        return QModelIndex();

    int idx = uuidPath.find("/");
    const QString& uuid = QString::fromStdString(uuidPath.substr(0, idx));
    if (m_nodes.find(uuid) != m_nodes.end()) {
        NodeItem* pItem = m_nodes[uuid];
        zeno::ObjPath _path = uuidPath;
        if (idx < 0) {
            return createIndex(m_uuid2Row[uuid], 0, nullptr);
        }
        else if (pItem->optSubgraph.has_value()) {
            _path = uuidPath.substr(idx + 1, uuidPath.size() - idx);
            return pItem->optSubgraph.value()->indexFromUuidPath(_path);
        }
    }
    return QModelIndex();
}

GraphModel* GraphModel::getGraphByPath(const QStringList& objPath)
{
    QStringList items = objPath;
    if (items.empty())
        return this;

    QString item = items[0];

    if (m_name2uuid.find(item) == m_name2uuid.end()) {
        return nullptr;
    }

    QString uuid = m_name2uuid[item];
    auto it = m_nodes.find(uuid);
    if (it == m_nodes.end()) {
        return nullptr;
    }

    NodeItem* pItem = it.value();
    items.removeAt(0);

    if (items.isEmpty())
    {
        if (pItem->optSubgraph.has_value())
        {
            return pItem->optSubgraph.value();
        }
        else
        {
            return this;
        }
    }
    ZASSERT_EXIT(pItem->optSubgraph.has_value(), nullptr);
    return pItem->optSubgraph.value()->getGraphByPath(items);
}

QStringList GraphModel::currentPath() const
{
    QStringList path;
    NodeItem* pNode = qobject_cast<NodeItem*>(this->parent());
    if (!pNode)
        return { this->m_graphName };

    GraphModel* pGraphM = nullptr;
    while (pNode) {
        path.push_front(pNode->name);
        pGraphM = qobject_cast<GraphModel*>(pNode->parent());
        ZASSERT_EXIT(pGraphM, {});
        pNode = qobject_cast<NodeItem*>(pGraphM->parent());
    }
    path.push_front(pGraphM->name());
    return path;
}

void GraphModel::undo()
{
    zeno::getSession().beginApiCall();
    zeno::scope_exit scope([=]() { zeno::getSession().endApiCall(); });
    if (m_undoRedoStack.has_value() && m_undoRedoStack.value())
        m_undoRedoStack.value()->undo();
}

void GraphModel::redo()
{
    zeno::getSession().beginApiCall();
    zeno::scope_exit scope([=]() { zeno::getSession().endApiCall(); });
    if (m_undoRedoStack.has_value() && m_undoRedoStack.value())
        m_undoRedoStack.value()->redo();
}

void GraphModel::pushToplevelStack(QUndoCommand* cmd)
{
    if (m_undoRedoStack.has_value() && m_undoRedoStack.value())
        m_undoRedoStack.value()->push(cmd);
}

void GraphModel::beginMacro(const QString& name)
{
    auto curpath = currentPath();
    if (curpath.size() > 1)   //不是顶层graph，则调用顶层graph
    {
        if (GraphModel* topLevelGraph = getTopLevelGraph(curpath))
            topLevelGraph->beginMacro(name);
    }
    else {
        if (m_undoRedoStack.has_value() && m_undoRedoStack.value())
            m_undoRedoStack.value()->beginMacro(name);
        zeno::getSession().beginApiCall();
    }
}

void GraphModel::endMacro()
{
    auto curpath = currentPath();
    if (curpath.size() > 1)   //不是顶层graph，则调用顶层graph
    {
        if (GraphModel* topLevelGraph = getTopLevelGraph(curpath))
            topLevelGraph->endMacro();
    }
    else {
        if (m_undoRedoStack.has_value() && m_undoRedoStack.value())
            m_undoRedoStack.value()->endMacro();
        zeno::getSession().endApiCall();
    }
}

void GraphModel::_initLink()
{
    for (auto item : m_nodes)
    {
        auto spNode = item->m_wpNode.lock();
        ZASSERT_EXIT(spNode);
        zeno::NodeData nodedata = spNode->exportInfo();
        //objects links init
        for (auto param : nodedata.customUi.inputObjs) {
            for (auto link : param.links) {
                _addLink(link);
            }
        }
        //primitives links init
        for (auto tab : nodedata.customUi.inputPrims.tabs) {
            for (auto group : tab.groups) {
                for (auto param : group.params) {
                    for (auto link : param.links) {
                        _addLink(link);
                    }
                }
            }
        }
    }
}

void GraphModel::_addLink(const zeno::EdgeInfo link)
{
    QModelIndex from, to;

    QString outNode = QString::fromStdString(link.outNode);
    QString outParam = QString::fromStdString(link.outParam);
    QString outKey = QString::fromStdString(link.outKey);
    QString inNode = QString::fromStdString(link.inNode);
    QString inParam = QString::fromStdString(link.inParam);
    QString inKey = QString::fromStdString(link.inKey);

    if (m_name2uuid.find(outNode) == m_name2uuid.end() ||
        m_name2uuid.find(inNode) == m_name2uuid.end())
        return;

    ParamsModel* fromParams = m_nodes[m_name2uuid[outNode]]->params;
    ParamsModel* toParams = m_nodes[m_name2uuid[inNode]]->params;

    from = fromParams->paramIdx(outParam, false);
    to = toParams->paramIdx(inParam, true);
    
    if (from.isValid() && to.isValid())
    {
        //notify ui to create dict key slot.
        if (!link.inKey.empty())
            emit toParams->linkAboutToBeInserted(link);
        if (!link.outKey.empty()) {
            if (zenoApp->graphsManager()->isInitializing())
                emit fromParams->linkAboutToBeInserted(link);
        }

        QModelIndex linkIdx = m_linkModel->addLink(from, outKey, to, inKey, link.bObjLink);
        fromParams->addLink(from, linkIdx);
        toParams->addLink(to, linkIdx);
    }

    GraphsManager::instance().currentModel()->markDirty(true);
}

QVariant GraphModel::removeLink(const QString& nodeName, const QString& paramName, bool bInput)
{
    if (bInput)
    {
        ZASSERT_EXIT(m_name2uuid.find(nodeName) != m_name2uuid.end(), QVariant());
        ParamsModel* toParamM = m_nodes[m_name2uuid[nodeName]]->params;
        QModelIndex toIndex = toParamM->paramIdx(paramName, bInput);
        int nRow = toParamM->removeLink(toIndex);

        if (nRow != -1)
        {
            QModelIndex linkIndex = m_linkModel->index(nRow);
            QVariant var = m_linkModel->data(linkIndex, ROLE_LINK_FROMPARAM_INFO);
            QVariantList varList = var.toList();

            QString fromNodeName = varList.isEmpty() ? "" : varList[0].toString();
            ZASSERT_EXIT(m_name2uuid.find(fromNodeName) != m_name2uuid.end(), QVariant());

            ParamsModel* fromParamM = m_nodes[m_name2uuid[fromNodeName]]->params;
            QModelIndex fromIndex = fromParamM->paramIdx(varList[1].toString(), varList[2].toBool());
            fromParamM->removeLink(fromIndex);

            m_linkModel->removeRows(nRow, 1);
            return var;
        }
    }
    return QVariant();
}

void GraphModel::removeLink(const QModelIndex& linkIdx)
{
    zeno::EdgeInfo edge = linkIdx.data(ROLE_LINK_INFO).value<zeno::EdgeInfo>();
    removeLink(edge);
}

void GraphModel::removeLink(const zeno::EdgeInfo& link)
{
    _removeLinkImpl(link, true);
}

bool GraphModel::updateLink(const QModelIndex& linkIdx, bool bInput, const QString& oldkey, const QString& newkey)
{
    std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
    ZASSERT_EXIT(spGraph, false);
    zeno::EdgeInfo edge = linkIdx.data(ROLE_LINK_INFO).value<zeno::EdgeInfo>();
    bool ret = spGraph->updateLink(edge, bInput, oldkey.toStdString(), newkey.toStdString());
    if (!ret)
        return ret;

    QAbstractItemModel* pModel = const_cast<QAbstractItemModel*>(linkIdx.model());
    LinkModel* linksM = qobject_cast<LinkModel*>(pModel);
    linksM->setData(linkIdx, newkey, ROLE_LINK_INKEY);
}

void GraphModel::moveUpLinkKey(const QModelIndex& linkIdx, bool bInput, const std::string& keyName)
{
    std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
    ZASSERT_EXIT(spGraph);
    zeno::EdgeInfo edge = linkIdx.data(ROLE_LINK_INFO).value<zeno::EdgeInfo>();
    spGraph->moveUpLinkKey(edge, bInput, keyName);
}

bool GraphModel::_removeLink(const zeno::EdgeInfo& edge)
{
    QString outNode = QString::fromStdString(edge.outNode);
    QString inNode = QString::fromStdString(edge.inNode);
    QString outParam = QString::fromStdString(edge.outParam);
    QString inParam = QString::fromStdString(edge.inParam);

    ZASSERT_EXIT(m_name2uuid.find(outNode) != m_name2uuid.end(), false);
    ZASSERT_EXIT(m_name2uuid.find(inNode) != m_name2uuid.end(), false);

    QModelIndex from, to;

    ParamsModel* fromParams = m_nodes[m_name2uuid[outNode]]->params;
    ParamsModel* toParams = m_nodes[m_name2uuid[inNode]]->params;

    from = fromParams->paramIdx(outParam, false);
    to = toParams->paramIdx(inParam, true);
    if (from.isValid() && to.isValid())
    {
        emit toParams->linkAboutToBeRemoved(edge);
        QModelIndex linkIdx = fromParams->removeOneLink(from, edge);
        QModelIndex linkIdx2 = toParams->removeOneLink(to, edge);
        ZASSERT_EXIT(linkIdx == linkIdx2, false);
        m_linkModel->removeRow(linkIdx.row());
    }
    GraphsManager::instance().currentModel()->markDirty(true);
    return true;
}

void GraphModel::_updateName(const QString& oldName, const QString& newName)
{
    ZASSERT_EXIT(oldName != newName);

    m_name2uuid[newName] = m_name2uuid[oldName];

    QString uuid = m_name2uuid[newName];
    auto& item = m_nodes[uuid];
    item->name = newName;   //update cache.

    if (m_subgNodes.find(oldName) != m_subgNodes.end()) {
        m_subgNodes.remove(oldName);
        m_subgNodes.insert(newName);
    }

    int row = m_uuid2Row[uuid];
    QModelIndex idx = createIndex(row, 0);
    emit dataChanged(idx, idx, QVector<int>{ ROLE_NODE_NAME });
    emit nameUpdated(idx, oldName);
    GraphsManager::instance().currentModel()->markDirty(true);
}

zeno::NodeData GraphModel::createNode(const QString& nodeCls, const QString& cate, const QPointF& pos)
{
    zeno::NodeData nodedata;
    nodedata.cls = nodeCls.toStdString();
    nodedata.uipos = { pos.x(), pos.y() };
    return _createNodeImpl(cate, nodedata, true);
}

void GraphModel::_appendNode(std::shared_ptr<zeno::INode> spNode)
{
    ZASSERT_EXIT(spNode);

    int nRows = m_nodes.size();

    beginInsertRows(QModelIndex(), nRows, nRows);

    NodeItem* pItem = new NodeItem(this);
    pItem->init(this, spNode);

    const QString& name = QString::fromStdString(spNode->get_name());
    const QString& uuid = QString::fromStdString(spNode->get_uuid());

    if (pItem->optSubgraph.has_value())
        m_subgNodes.insert(name);

    m_row2uuid[nRows] = uuid;
    m_uuid2Row[uuid] = nRows;
    m_nodes.insert(uuid, pItem);
    m_name2uuid.insert(name, uuid);

    endInsertRows();

    pItem->params->setNodeIdx(createIndex(nRows, 0));

    GraphsManager::instance().currentModel()->markDirty(true);
}

zeno::NodeData GraphModel::_createNodeImpl(const QString& cate, zeno::NodeData& nodedata, bool endTransaction)
{
    bool bEnableIoProc = GraphsManager::instance().isInitializing() || GraphsManager::instance().isImporting();
    if (bEnableIoProc)
        endTransaction = false;

    if (endTransaction)
    {
        auto currtPath = currentPath();
        AddNodeCommand* pCmd = new AddNodeCommand(cate, nodedata, currtPath);
        if (auto topLevelGraph = getTopLevelGraph(currtPath))
        {
            topLevelGraph->pushToplevelStack(pCmd);
            return pCmd->getNodeData();
        }
        return zeno::NodeData();
    }
    else {
        auto updateInputs = [](zeno::NodeData& nodedata, std::shared_ptr<zeno::INode> spNode) {
            for (auto& tab : nodedata.customUi.inputPrims.tabs)
            {
                for (auto& group : tab.groups)
                {
                    for (auto& param : group.params)
                    {
                        spNode->update_param(param.name, param.defl);
                    }
                }
            }
        };

        std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
        if (!spGraph)
            return zeno::NodeData();

        std::shared_ptr<zeno::INode> spNode = spGraph->createNode(nodedata.cls, nodedata.name, cate == "assets", nodedata.uipos);
        if (!spNode)
            return zeno::NodeData();

        zeno::NodeData node;

        if (nodedata.cls == "Subnet" || cate == "assets") {
            QString nodeName = QString::fromStdString(spNode->get_name());
            QString uuid = m_name2uuid[nodeName];
            ZASSERT_EXIT(m_nodes.find(uuid) != m_nodes.end(), zeno::NodeData());
            auto paramsM = m_nodes[uuid]->params;

            if (std::shared_ptr<zeno::SubnetNode> subnetNode = std::dynamic_pointer_cast<zeno::SubnetNode>(spNode)) {
                //create input/output in subnet
                if (cate == "assets")
                {
                    const auto asset = zeno::getSession().assets->getAsset(nodedata.cls);
                    nodedata.customUi = asset.m_customui;
                }
                zeno::ParamsUpdateInfo updateInfo;
                UiHelper::parseUpdateInfo(nodedata.customUi, updateInfo);
                paramsM->resetCustomUi(nodedata.customUi);
                paramsM->batchModifyParams(updateInfo);

                if (nodedata.subgraph.has_value())
                {
                    for (auto& [name, nodedata] : nodedata.subgraph.value().nodes)
                    {
                        if (nodedata.cls == "Subnet") {   //if is subnet, create recursively
                            QStringList cur = currentPath();
                            cur.append(QString::fromStdString(spNode->get_name()));
                            GraphModel* model = GraphsManager::instance().getGraph(cur);
                            if (model)
                                model->_createNodeImpl(cate, nodedata, false);
                        }
                        else if (nodedata.cls == "SubInput" || nodedata.cls == "SubOutput") {   //dont create, just update subinput/output pos
                            auto ioNode = subnetNode->subgraph->getNode(name);
                            if (ioNode)
                                ioNode->set_pos(nodedata.uipos);
                        }
                        else if (nodedata.asset.has_value()) {  //if is asset
                            std::shared_ptr<zeno::INode> spNode = subnetNode->subgraph->createNode(nodedata.cls, name, true, {nodedata.uipos.first, nodedata.uipos.second});
                            if (spNode)
                                updateInputs(nodedata, spNode);
                        }
                        else {
                            std::shared_ptr<zeno::INode> spNode = subnetNode->subgraph->createNode(nodedata.cls, name, false, {nodedata.uipos.first, nodedata.uipos.second});
                            if (spNode)
                                updateInputs(nodedata, spNode);
                        }
                    }
                    for (zeno::EdgeInfo oldLink : nodedata.subgraph.value().links) {
                        subnetNode->subgraph->addLink(oldLink);
                    }
                }
                updateInputs(nodedata, spNode);
                node = spNode->exportInfo();
            }
        }
        else {
            updateInputs(nodedata, spNode);
            node = spNode->exportInfo();
        }

        return node;
    }
}

bool GraphModel::_removeNodeImpl(const QString& name, bool endTransaction)
{
    bool bEnableIoProc = GraphsManager::instance().isInitializing() || GraphsManager::instance().isImporting();
    if (bEnableIoProc)
        endTransaction = false;

    if (endTransaction)
    {
        if (m_name2uuid.find(name) != m_name2uuid.end() && m_nodes.find(m_name2uuid[name]) != m_nodes.end())
        {
            auto spNode = m_nodes[m_name2uuid[name]]->m_wpNode.lock();
            if (spNode)
            {
                auto nodedata = spNode->exportInfo();
                auto currtPath = currentPath();
                RemoveNodeCommand* pCmd = new RemoveNodeCommand(nodedata, currtPath);
                if (auto topLevelGraph = getTopLevelGraph(currtPath))
                {
                    topLevelGraph->pushToplevelStack(pCmd);
                    return true;
                }
                //m_undoRedoStack->push(pCmd);
            }
        }
        return false;
    }
    else {
        //remove all related links
        NodeItem* item = m_nodes[m_name2uuid[name]];
        if (item)
        {
            PARAMS_INFO ioParams = item->params->getInputs();
            ioParams.insert(item->params->getOutputs());
            for (zeno::ParamPrimitive& paramInfo : ioParams)
            {
                for (zeno::EdgeInfo& edge: paramInfo.links)
                {
                    auto currtPath = currentPath();
                    LinkCommand* pCmd = new LinkCommand(false, edge, currentPath());
                    if (auto topLevelGraph = getTopLevelGraph(currtPath))
                        topLevelGraph->pushToplevelStack(pCmd);
                }
            }
        }

        auto spCoreGraph = m_wpCoreGraph.lock();
        ZASSERT_EXIT(spCoreGraph, false);
        if (spCoreGraph)
            return spCoreGraph->removeNode(name.toStdString());
        return false;
    }
}

void GraphModel::_addLinkImpl(const zeno::EdgeInfo& link, bool endTransaction)
{
    bool bEnableIoProc = GraphsManager::instance().isInitializing() || GraphsManager::instance().isImporting();
    if (bEnableIoProc)
        endTransaction = false;

    if (endTransaction)
    {
        LinkCommand* pCmd = new LinkCommand(true, link, currentPath());
        auto currtPath = currentPath();
        if (auto topLevelGraph = getTopLevelGraph(currtPath))
        {
            topLevelGraph->pushToplevelStack(pCmd);
        }
    }
    else {
        std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
        ZASSERT_EXIT(spGraph);
        if (spGraph)
            spGraph->addLink(link);
    }
}

void GraphModel::_removeLinkImpl(const zeno::EdgeInfo& link, bool endTransaction)
{
    bool bEnableIoProc = GraphsManager::instance().isInitializing() || GraphsManager::instance().isImporting();
    if (bEnableIoProc)
        endTransaction = false;

    if (endTransaction)
    {
        LinkCommand* pCmd = new LinkCommand(false, link, currentPath());
        auto currtPath = currentPath();
        if (auto topLevelGraph = getTopLevelGraph(currtPath))
        {
            topLevelGraph->pushToplevelStack(pCmd);
        }
    }
    else {
        //emit to core data.
        std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
        ZASSERT_EXIT(spGraph);
        if (spGraph)
            spGraph->removeLink(link);
    }
}

bool GraphModel::setModelData(const QModelIndex& index, const QVariant& newValue, int role)
{
    zeno::getSession().beginApiCall();
    zeno::scope_exit scope([=]() { zeno::getSession().endApiCall(); });

    const auto& oldVal = index.data(role);
    ModelDataCommand* pcmd = new ModelDataCommand(index, oldVal, newValue, role, currentPath());
    if (auto topLevelGraph = getTopLevelGraph(currentPath()))
    {
        topLevelGraph->pushToplevelStack(pcmd);
    }
    return true;
}

void GraphModel::_setViewImpl(const QModelIndex& idx, bool bOn, bool endTransaction)
{
    bool bEnableIoProc = GraphsManager::instance().isInitializing() || GraphsManager::instance().isImporting();
    if (bEnableIoProc)
        endTransaction = false;

    if (endTransaction)
    {
        auto currtPath = currentPath();
        NodeStatusCommand* pCmd = new NodeStatusCommand(true, idx.data(ROLE_NODE_NAME).toString(), bOn, currtPath);
        if (auto topLevelGraph = getTopLevelGraph(currtPath))
            topLevelGraph->pushToplevelStack(pCmd);
    }
    else {
        auto spCoreGraph = m_wpCoreGraph.lock();
        ZASSERT_EXIT(spCoreGraph);
        NodeItem* item = m_nodes[m_row2uuid[idx.row()]];
        auto spCoreNode = item->m_wpNode.lock();
        ZASSERT_EXIT(spCoreNode);
        spCoreNode->set_view(bOn);
    }
}

std::weak_ptr<zeno::INode> GraphModel::getWpNode(QString& nodename)
{
    auto it = m_name2uuid.find(nodename);
    if (it != m_name2uuid.end() && m_nodes.find(it.value()) != m_nodes.end())
    {
        if (NodeItem* item = m_nodes[it.value()])
        {
            return item->m_wpNode;
        }
    }
}

void GraphModel::appendSubgraphNode(QString name, QString cls, NODE_DESCRIPTOR desc, GraphModel* subgraph, const QPointF& pos)
{
    //TODO:
#if 0
    int nRows = m_nodes.size();
    beginInsertRows(QModelIndex(), nRows, nRows);

    NodeItem* pItem = new NodeItem(this);
    pItem->setParent(this);
    pItem->name = name;
    pItem->name = cls;
    pItem->pos = pos;
    pItem->params = new ParamsModel(desc, pItem);
    pItem->pSubgraph = subgraph;
    subgraph->setParent(pItem);

    m_row2name[nRows] = name;
    m_name2Row[name] = nRows;
    m_nodes.insert(name, pItem);

    endInsertRows();
    pItem->params->setNodeIdx(createIndex(nRows, 0));
#endif
}

bool GraphModel::removeNode(const QString& name)
{
    if (m_name2uuid.find(name) != m_name2uuid.end() && m_nodes.find(m_name2uuid[name]) != m_nodes.end()) {
        return _removeNodeImpl(name, true);
    }
    return false;
}

void GraphModel::setView(const QModelIndex& idx, bool bOn)
{
    _setViewImpl(idx, bOn, true);
}

void GraphModel::setMute(const QModelIndex& idx, bool bOn)
{
    //TODO
}

QString GraphModel::updateNodeName(const QModelIndex& idx, QString newName)
{
    auto spCoreGraph = m_wpCoreGraph.lock();
    ZASSERT_EXIT(spCoreGraph, false);

    std::string oldName = idx.data(ROLE_NODE_NAME).toString().toStdString();
    newName = QString::fromStdString(spCoreGraph->updateNodeName(oldName, newName.toStdString()));
    return newName;
}

void GraphModel::updateSocketValue(const QModelIndex& nodeidx, const QString socketName, const QVariant newValue)
{
    if (ParamsModel* paramModel = params(nodeidx))
    {
        QModelIndex& socketIdx = paramModel->paramIdx(socketName, true);
        paramModel->setData(socketIdx, newValue, ROLE_PARAM_VALUE);
    }
}

QHash<int, QByteArray> GraphModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ROLE_CLASS_NAME] = "classname";
    roles[ROLE_NODE_NAME] = "name";
    roles[ROLE_PARAMS] = "params";
    roles[ROLE_LINKS] = "linkModel";
    roles[ROLE_OBJPOS] = "pos";
    roles[ROLE_SUBGRAPH] = "subgraph";
    return roles;
}

void GraphModel::_clear()
{
    while (rowCount() > 0) {
        //only delete ui model element itself, and then unregister from core.
        removeRows(0, 1);
    }
}

bool GraphModel::removeRows(int row, int count, const QModelIndex& parent)
{
    //this is a private impl method, called by callback function.
    beginRemoveRows(parent, row, row);

    QString id = m_row2uuid[row];
    NodeItem* pItem = m_nodes[id];
    const QString& name = pItem->getName();

    for (int r = row + 1; r < rowCount(); r++)
    {
        const QString& id_ = m_row2uuid[r];
        m_row2uuid[r - 1] = id_;
        m_uuid2Row[id_] = r - 1;
    }

    m_row2uuid.remove(rowCount() - 1);
    m_uuid2Row.remove(id);
    m_nodes.remove(id);
    m_name2uuid.remove(name);

    if (m_subgNodes.find(id) != m_subgNodes.end())
        m_subgNodes.remove(id);

    delete pItem;

    endRemoveRows();

    emit nodeRemoved(name);
    return true;
}

void GraphModel::syncToAssetsInstance(const QString& assetsName, zeno::ParamsUpdateInfo info, const zeno::CustomUI& customui)
{
    QModelIndexList results = match(QModelIndex(), ROLE_CLASS_NAME, assetsName);
    for (QModelIndex res : results) {
        zeno::NodeType type = (zeno::NodeType)res.data(ROLE_NODETYPE).toInt();
        if (type == zeno::Node_AssetInstance || type == zeno::Node_AssetReference) {
            ParamsModel* paramsM = QVariantPtr<ParamsModel>::asPtr(res.data(ROLE_PARAMS));
            ZASSERT_EXIT(paramsM);
            paramsM->resetCustomUi(customui);
            paramsM->batchModifyParams(info);
        }
    }

    const QStringList& path = currentPath();
    if (!path.isEmpty() && path[0] != "main") {
        return;
    }

    syncToAssetsInstance(assetsName);

    for (QString subgnode : m_subgNodes) {
        ZASSERT_EXIT(m_name2uuid.find(subgnode) != m_name2uuid.end());
        QString uuid = m_name2uuid[subgnode];
        ZASSERT_EXIT(m_nodes.find(uuid) != m_nodes.end());
        GraphModel* pSubgM = m_nodes[uuid]->optSubgraph.value();
        ZASSERT_EXIT(pSubgM);
        pSubgM->syncToAssetsInstance(assetsName, info, customui);
    }
}

void GraphModel::syncToAssetsInstance(const QString& assetsName)
{
    for (const QString & name : m_subgNodes)
    {
        ZASSERT_EXIT(m_name2uuid.find(name) != m_name2uuid.end());
        QString uuid = m_name2uuid[name];
        ZASSERT_EXIT(m_nodes.find(uuid) != m_nodes.end());
        GraphModel* pSubgM = m_nodes[uuid]->optSubgraph.value();
        ZASSERT_EXIT(pSubgM);
        if (assetsName == m_nodes[uuid]->cls)
        {
            //TO DO: compare diff
            if (!pSubgM->isLocked())
                continue;
            while (pSubgM->rowCount() > 0)
            {
                const QString& nodeName = pSubgM->index(0, 0).data(ROLE_NODE_NAME).toString();
                pSubgM->removeNode(nodeName);
            }
            std::shared_ptr<zeno::INode> spNode = m_nodes[uuid]->m_wpNode.lock();
            auto spSubnetNode = std::dynamic_pointer_cast<zeno::SubnetNode>(spNode);
            if (spSubnetNode) {
                auto& assetsMgr = zeno::getSession().assets;
                assetsMgr->updateAssetInstance(assetsName.toStdString(), spSubnetNode);
                pSubgM->updateAssetInstance(spSubnetNode->subgraph);
                spNode->mark_dirty(true);
            }
        }
        else
        {
            pSubgM->syncToAssetsInstance(assetsName);
        }
    }
}

void GraphModel::updateAssetInstance(const std::shared_ptr<zeno::Graph> spGraph)
{
    m_wpCoreGraph = spGraph;
    registerCoreNotify();
    for (auto& [name, node] : spGraph->getNodes()) {
        _appendNode(node);
    }

    _initLink();
}

void GraphModel::updateParamName(QModelIndex nodeIdx, int row, QString newName)
{
    NodeItem* item = m_nodes[m_row2uuid[nodeIdx.row()]];
    QModelIndex paramIdx = item->params->index(row, 0);
    item->params->setData(paramIdx, newName, ROLE_PARAM_NAME);
}

void GraphModel::removeParam(QModelIndex nodeIdx, int row)
{
    NodeItem* item = m_nodes[m_row2uuid[nodeIdx.row()]];
    item->params->removeRow(row);
}

ParamsModel* GraphModel::params(QModelIndex nodeIdx)
{
    NodeItem* item = m_nodes[m_row2uuid[nodeIdx.row()]];
    return item->params;
}

GraphModel* GraphModel::subgraph(QModelIndex nodeIdx) {
    NodeItem* item = m_nodes[m_row2uuid[nodeIdx.row()]];
    if (item->optSubgraph.has_value()) {
        return item->optSubgraph.value();
    }
    return nullptr;
}

GraphsTreeModel* GraphModel::treeModel() const {
    return m_pTree;
}

void GraphModel::setLocked(bool bLocked)
{
    m_bLocked = bLocked;
    emit lockStatusChanged();
}

bool GraphModel::isLocked() const
{
    return m_bLocked;
}

void GraphModel::importNodes(const zeno::NodesData& nodes, const zeno::LinksData& links, const QPointF& pos)
{
    if (nodes.empty())
        return;
    std::shared_ptr<zeno::Graph> spGraph = m_wpCoreGraph.lock();
    if (!spGraph)
        return;
    std::map<std::string, std::string> old2new;
    QPointF offset = pos - QPointF(nodes.begin()->second.uipos.first, nodes.begin()->second.uipos.second);
    unRegisterCoreNotify();
    for (auto [name, node] : nodes) {
        bool bAsset = node.asset.has_value();
        std::shared_ptr<zeno::INode> spNode = spGraph->createNode(node.cls, "", bAsset);
        node.name = spNode->get_name();
        spNode->init(node);
        auto pos = spNode->get_pos();
        spNode->set_pos({ pos.first + offset.x(), pos.second + offset.y()});
        old2new[name] = node.name;
        _appendNode(spNode);
    }
    registerCoreNotify();
    //import edges
    for (auto link : links) {
        if (old2new.find(link.outNode) == old2new.end() || old2new.find(link.inNode) == old2new.end())
            continue;
        link.inNode = old2new[link.inNode];
        link.outNode = old2new[link.outNode];
        addLink(link);
    }
}

GraphModel* GraphModel::getTopLevelGraph(const QStringList& currentPath)
{
    return GraphsManager::instance().getGraph({ currentPath[0] });
}
