#ifndef __PARAMS_MODEL_H__
#define __PARAMS_MODEL_H__

#include <QObject>
#include <QStandardItemModel>
#include <QQuickItem>
#include "uicommon.h"
#include <zeno/core/data.h>
#include <zeno/core/INode.h>
#include <zeno/extra/SubnetNode.h>

class GraphModel;

struct ParamItem
{
    //BEGIN: temp cache on ui model, the actual value has been stored in m_wpParam.
    QString name;
    zeno::ParamType type = zeno::Param_Null;
    QVariant value;
    //END
    //std::weak_ptr<zeno::CoreParam> m_wpParam;

    bool bInput = true;
    zeno::ParamControl control = zeno::NullControl;
    zeno::SocketType connectProp = zeno::NoSocket;
    zeno::NodeDataGroup group;
    std::optional<zeno::ControlProperty> optCtrlprops;
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
    void getDegrees(int& inDegrees, int& outDegrees);

    //api:
    void setNodeIdx(const QModelIndex& nodeIdx);
    QModelIndex paramIdx(const QString& name, bool bInput) const;
    void addLink(const QModelIndex& paramIdx, const QPersistentModelIndex& linkIdx);
    int removeLink(const QModelIndex& paramIdx);
    QModelIndex removeOneLink(const QModelIndex& paramIdx, const zeno::EdgeInfo& link);
    bool removeSpecificLink(const QModelIndex& paramIdx, const QModelIndex& linkIdx);
    void addParam(const ParamItem& param);
    GraphModel* getGraph() const;

    PARAMS_INFO getInputs();
    PARAMS_INFO getOutputs();

    //temp:
    QStandardItemModel* customParamModel();
    void batchModifyParams(const zeno::ParamsUpdateInfo& params);
    void resetCustomUi(const zeno::CustomUI& customui);

    int getParamlinkCount(const QModelIndex& paramIdx);
    int numOfInputParams() const;

signals:
    void linkAboutToBeInserted(const zeno::EdgeInfo& link);
    void linkAboutToBeRemoved(const zeno::EdgeInfo& link);

private slots:
    void onCustomModelDataChanged(const QModelIndex& topLeft, const QModelIndex& bottomRight, const QVector<int>& roles = QVector<int>());

private:
    void initParamItems();
    void initCustomUI(const zeno::CustomUI& customui);
    GraphModel* parentGraph() const;
    void test_customparamsmodel() const;
    void updateParamData(const QString& name, const QVariant& val, int role);

    QPersistentModelIndex m_nodeIdx;
    QVector<ParamItem> m_items;

    QStandardItemModel* m_customParamsM;

    std::weak_ptr<zeno::INode> m_wpNode;
    std::string cbUpdateParam;
    mutable bool m_bReentry = false;
};


#endif