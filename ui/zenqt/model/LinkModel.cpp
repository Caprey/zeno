#include "linkmodel.h"
#include "uicommon.h"



LinkModel::LinkModel(QObject* parent)
    : _base(parent)
{

}

LinkModel::~LinkModel()
{

}

int LinkModel::rowCount(const QModelIndex& parent) const
{
    return m_items.length();
}

QVariant LinkModel::data(const QModelIndex& index, int role) const
{
    switch (role) {
        case ROLE_LINK_FROMPARAM_INFO:
        {
            const auto& info = m_items[index.row()];
            QModelIndex nodeIdx = info.fromParam.data(ROLE_NODEIDX).toModelIndex();
            const QString& nodeName = nodeIdx.data(ROLE_NODE_NAME).toString();
            const QString& paramName = info.fromParam.data(ROLE_PARAM_NAME).toString();
            return QVariantList{ nodeName, paramName, false};
        }
        case ROLE_LINK_TOPARAM_INFO:
        {
            const auto& info = m_items[index.row()];
            QModelIndex nodeIdx = info.toParam.data(ROLE_NODEIDX).toModelIndex();
            const QString& nodeName = nodeIdx.data(ROLE_NODE_NAME).toString();
            const QString& paramName = info.toParam.data(ROLE_PARAM_NAME).toString();
            return QVariantList{ nodeName, paramName, true };
        }
        case ROLE_INSOCK_IDX:
        {
            const auto& info = m_items[index.row()];
            return info.toParam;
        }
        case ROLE_OUTSOCK_IDX:
        {
            const auto& info = m_items[index.row()];
            return info.fromParam;
        }
        case ROLE_LINK_INFO:
        {
            const auto& info = m_items[index.row()];
            const QString& outNode = info.fromParam.data(ROLE_NODE_NAME).toString();
            const QString& outParam = info.fromParam.data(ROLE_PARAM_NAME).toString();
            const QString& inNode = info.toParam.data(ROLE_NODE_NAME).toString();
            const QString& inParam = info.toParam.data(ROLE_PARAM_NAME).toString();
            zeno::EdgeInfo edge = { outNode.toStdString(), outParam.toStdString(), "", inNode.toStdString(), inParam.toStdString(), ""};
            return QVariant::fromValue(edge);
        }
        case ROLE_LINKID:
        {
            const auto& info = m_items[index.row()];
            return info.uuid;
        }
    }
    return QVariant();
}

QHash<int, QByteArray> LinkModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ROLE_LINK_FROMPARAM_INFO] = "fromParam";
    roles[ROLE_LINK_TOPARAM_INFO] = "toParam";
    return roles;
}

bool LinkModel::removeRows(int row, int count, const QModelIndex& parent)
{
    beginRemoveRows(parent, row, row);
    m_items.removeAt(row);
    endRemoveRows();
    return true;
}

QModelIndex LinkModel::addLink(const QModelIndex& fromParam, const QModelIndex& toParam)
{
    int row = m_items.size();
    beginInsertRows(QModelIndex(), row, row);

    _linkItem item;
    item.fromParam = fromParam;
    item.toParam = toParam;
    item.uuid = QUuid::createUuid();

    m_items.append(item);

    endInsertRows();
    return index(row, 0);
}

