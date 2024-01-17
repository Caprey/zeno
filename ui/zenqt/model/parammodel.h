#ifndef __PARAMS_MODEL_H__
#define __PARAMS_MODEL_H__

#include <QObject>
#include <QStandardItemModel>
#include <QQuickItem>
#include "uicommon.h"
#include <zeno/core/data.h>
#include <zeno/core/INode.h>


struct ParamItem
{
    //BEGIN: temp cache on ui model, the actual value has been stored in m_wpParam.
    QString name;
    zeno::ParamType type = zeno::Param_Null;
    QVariant value;
    //END
    std::weak_ptr<zeno::IParam> m_wpParam;

    bool bInput = true;
    zeno::ParamControl control = zeno::NullControl;
    QList<QPersistentModelIndex> links;
};

class ParamsModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    ParamsModel(std::shared_ptr<zeno::INode> spNode, QObject* parent = nullptr);

    Q_INVOKABLE int indexFromName(const QString& name, bool bInput) const;
    Q_INVOKABLE QVariant getIndexList(bool bInput) const;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role = Qt::EditRole) override;
    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    bool removeRows(int row, int count, const QModelIndex& parent = QModelIndex()) override;

    //api:
    void setNodeIdx(const QModelIndex& nodeIdx);
    QModelIndex paramIdx(const QString& name, bool bInput) const;
    void addLink(const QModelIndex& paramIdx, const QPersistentModelIndex& linkIdx);
    int removeLink(const QModelIndex& paramIdx);
    void addParam(const ParamItem& param);

    int getParamlinkCount(const QModelIndex& paramIdx);

private:
    QPersistentModelIndex m_nodeIdx;
    QVector<ParamItem> m_items;

    std::weak_ptr<zeno::INode> m_wpNode;
    std::string cbUpdateParam;
};


#endif