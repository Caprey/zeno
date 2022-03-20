#ifndef __ZENO_GRAPHS_EDITOR_H__
#define __ZENO_GRAPHS_EDITOR_H__

#include <QtWidgets>

class ZToolButton;
class ZenoWelcomePage;
class ZenoMainWindow;
class IGraphsModel;

namespace Ui
{
    class GraphsEditor;
}

class ZenoGraphsEditor : public QWidget
{
    Q_OBJECT

    enum SideBarItem
    {
        Side_Subnet,
        Side_Tree,
        Side_Search
    };

public:
    ZenoGraphsEditor(ZenoMainWindow* pMainWin);
    ~ZenoGraphsEditor();

public slots:
	void resetModel(IGraphsModel* pModel);
    void sideButtonToggled(bool bToggled);
    void onSideBtnToggleChanged(const QItemSelection& selected, const QItemSelection& deselected);
    void onCurrentChanged(const QModelIndex& current, const QModelIndex& previous);
    void onListItemActivated(const QModelIndex& index);
    void onTreeItemActivated(const QModelIndex& index);
    void onSubnetOptionClicked();
    void onPageActivated(const QPersistentModelIndex& subgIdx, const QPersistentModelIndex& nodeIdx);

private slots:
	void onSubGraphsToRemove(const QModelIndex&, int, int);
	void onModelReset();
	void onSubGraphRename(const QString& oldName, const QString& newName);

private:
    void initUI();
    void initSignals();
    void initModel();
    int tabIndexOfName(const QString& subGraphName);
    void activateTab(const QString& subGraphName, const QString& path = "", const QString& objId = "");

    ZenoMainWindow* m_mainWin;
    Ui::GraphsEditor* m_ui;
    IGraphsModel* m_model;
    QItemSelectionModel* m_selection;
    QStandardItemModel* m_sideBarModel;
};


#endif
