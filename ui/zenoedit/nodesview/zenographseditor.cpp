#include "zenographseditor.h"
#include "zenosubnetlistview.h"
#include <comctrl/ztoolbutton.h>
#include "zenoapplication.h"
#include "../nodesys/zenosubgraphscene.h"
#include "zenowelcomepage.h"
#include <zenomodel/include/graphsmanagment.h>
#include <zenomodel/include/modelrole.h>
#include <zenomodel/include/api.h>
#include <comctrl/zenocheckbutton.h>
#include <comctrl/ziconbutton.h>
#include <zenoui/style/zenostyle.h>
#include "zenomainwindow.h"
#include "nodesys/zenosubgraphview.h"
#include "ui_zenographseditor.h"
#include "nodesview/zsubnetlistitemdelegate.h"
#include "searchitemdelegate.h"
#include <zenoui/util/cihou.h>
#include "startup/zstartup.h"
#include "util/log.h"
#include "settings/zsettings.h"


ZenoGraphsEditor::ZenoGraphsEditor(ZenoMainWindow* pMainWin)
	: QWidget(nullptr)
	, m_mainWin(pMainWin)
    , m_model(nullptr)
    , m_searchOpts(SEARCHALL)
{
    initUI();
    initModel();
    initSignals();
}

ZenoGraphsEditor::~ZenoGraphsEditor()
{
}

void ZenoGraphsEditor::initUI()
{
	m_ui = new Ui::GraphsEditor;
	m_ui->setupUi(this);

    m_ui->subnetBtn->setIcons(QIcon(":/icons/ic_sidemenu_subnet.svg"), QIcon(":/icons/ic_sidemenu_subnet_on.svg"));
    m_ui->treeviewBtn->setIcons(QIcon(":/icons/ic_sidemenu_tree.svg"), QIcon(":/icons/ic_sidemenu_tree_on.svg"));
    m_ui->searchBtn->setIcons(QIcon(":/icons/ic_sidemenu_search.svg"), QIcon(":/icons/ic_sidemenu_search_on.svg"));

    int _margin = ZenoStyle::dpiScaled(10);
    QMargins margins(_margin, _margin, _margin, _margin);
    QSize szIcons = ZenoStyle::dpiScaledSize(QSize(20, 20));

    m_ui->moreBtn->setIcons(szIcons, ":/icons/more.svg", ":/icons/more_on.svg");
    m_ui->btnSearchOpt->setIcons(szIcons, ":/icons/more.svg", ":/icons/more_on.svg");

    m_ui->subnetBtn->setSize(szIcons, margins);
    m_ui->treeviewBtn->setSize(szIcons, margins);
    m_ui->searchBtn->setSize(szIcons, margins);

    m_ui->stackedWidget->hide();
    m_ui->splitter->setStretchFactor(1, 5);

    m_ui->mainStackedWidget->setCurrentWidget(m_ui->welcomePage);

    m_ui->graphsViewTab->setFont(QFont("HarmonyOS Sans", 12));  //bug in qss font setting.
    m_ui->searchEdit->setProperty("cssClass", "searchEditor");

    initRecentFiles();
}

void ZenoGraphsEditor::initModel()
{
    m_sideBarModel = new QStandardItemModel;

    QStandardItem* pItem = new QStandardItem;
	pItem->setData(Side_Subnet);
    m_sideBarModel->appendRow(pItem);

    pItem = new QStandardItem;
    pItem->setData(Side_Tree);
    m_sideBarModel->appendRow(pItem);

    pItem = new QStandardItem;
    pItem->setData(Side_Search);
    m_sideBarModel->appendRow(pItem);

    m_selection = new QItemSelectionModel(m_sideBarModel);
}

void ZenoGraphsEditor::initSignals()
{
	auto graphsMgr = zenoApp->graphsManagment();
	connect(&*graphsMgr, SIGNAL(modelInited(IGraphsModel*)), this, SLOT(resetModel(IGraphsModel*)));
    connect(graphsMgr->logModel(), &QStandardItemModel::rowsInserted, this, &ZenoGraphsEditor::onLogInserted);

    connect(m_ui->subnetBtn, &ZenoCheckButton::toggled, this, &ZenoGraphsEditor::sideButtonToggled);
    connect(m_ui->treeviewBtn, &ZenoCheckButton::toggled, this, &ZenoGraphsEditor::sideButtonToggled);
    connect(m_ui->searchBtn, &ZenoCheckButton::toggled, this, &ZenoGraphsEditor::sideButtonToggled);

    connect(m_selection, &QItemSelectionModel::selectionChanged, this, &ZenoGraphsEditor::onSideBtnToggleChanged);
    connect(m_selection, &QItemSelectionModel::currentChanged, this, &ZenoGraphsEditor::onCurrentChanged);

    connect(m_ui->subnetList, SIGNAL(clicked(const QModelIndex&)), this, SLOT(onListItemActivated(const QModelIndex&)));
    connect(m_ui->subnetTree, SIGNAL(clicked(const QModelIndex&)), this, SLOT(onTreeItemActivated(const QModelIndex&)));

	connect(m_ui->welcomePage, SIGNAL(newRequest()), m_mainWin, SLOT(onNewFile()));
	connect(m_ui->welcomePage, SIGNAL(openRequest()), m_mainWin, SLOT(openFileDialog()));

    connect(m_ui->moreBtn, SIGNAL(clicked()), this, SLOT(onSubnetOptionClicked()));
    connect(m_ui->btnSearchOpt, SIGNAL(clicked()), this, SLOT(onSearchOptionClicked()));
    connect(m_ui->graphsViewTab, &QTabWidget::tabCloseRequested, this, [=](int index) {
        m_ui->graphsViewTab->removeTab(index);
    });
    connect(m_ui->searchEdit, SIGNAL(textChanged(const QString&)), this, SLOT(onSearchEdited(const QString&)));
    connect(m_ui->searchResView, SIGNAL(clicked(const QModelIndex&)), this, SLOT(onSearchItemClicked(const QModelIndex&)));

    //m_selection->setCurrentIndex(m_sideBarModel->index(0, 0), QItemSelectionModel::SelectCurrent);
}

void ZenoGraphsEditor::initRecentFiles()
{
    m_ui->welcomePage->initRecentFiles();
}

void ZenoGraphsEditor::resetModel(IGraphsModel* pModel)
{
    if (!pModel)
    {
        onModelCleared();
        return;
    }

    auto mgr = zenoApp->graphsManagment();
    m_model = pModel;
    ZASSERT_EXIT(m_model);

    m_ui->subnetTree->setModel(mgr->treeModel());
    m_ui->subnetList->setModel(pModel);

    m_ui->subnetList->setItemDelegate(new ZSubnetListItemDelegate(m_model, this));

    m_ui->mainStackedWidget->setCurrentWidget(m_ui->mainEditor);
    m_ui->graphsViewTab->clear();

    connect(pModel, &IGraphsModel::modelClear, this, &ZenoGraphsEditor::onModelCleared);
	connect(pModel, SIGNAL(rowsAboutToBeRemoved(const QModelIndex&, int, int)), this, SLOT(onSubGraphsToRemove(const QModelIndex&, int, int)));
	connect(pModel, SIGNAL(modelReset()), this, SLOT(onModelReset()));
	connect(pModel, SIGNAL(graphRenamed(const QString&, const QString&)), this, SLOT(onSubGraphRename(const QString&, const QString&)));

    activateTab("main");
}

void ZenoGraphsEditor::onModelCleared()
{
    m_ui->mainStackedWidget->setCurrentWidget(m_ui->welcomePage);
}

void ZenoGraphsEditor::onSubGraphsToRemove(const QModelIndex& parent, int first, int last)
{
	for (int r = first; r <= last; r++)
	{
		QModelIndex subgIdx = m_model->index(r, 0);
		const QString& name = subgIdx.data(ROLE_OBJNAME).toString();
		int idx = tabIndexOfName(name);
		m_ui->graphsViewTab->removeTab(idx);
	}
}

void ZenoGraphsEditor::onModelReset()
{
	m_ui->graphsViewTab->clear();
    m_model = nullptr;
}

void ZenoGraphsEditor::onSubGraphRename(const QString& oldName, const QString& newName)
{
	int idx = tabIndexOfName(oldName);
	if (idx != -1)
	{
		QTabBar* pTabBar = m_ui->graphsViewTab->tabBar();
		pTabBar->setTabText(idx, newName);
	}
}

void ZenoGraphsEditor::onSearchOptionClicked()
{
	QMenu* pOptionsMenu = new QMenu;

	QAction* pNode = new QAction(tr("Node"));
    pNode->setCheckable(true);
    pNode->setChecked(m_searchOpts & SEARCH_NODECLS);

	QAction* pSubnet = new QAction(tr("Subnet"));
    pSubnet->setCheckable(true);
    pSubnet->setChecked(m_searchOpts & SEARCH_SUBNET);

	QAction* pAnnotation = new QAction(tr("Annotation"));
    pAnnotation->setCheckable(true);
    pAnnotation->setEnabled(false);

	QAction* pWrangle = new QAction(tr("Parameter"));
    pWrangle->setCheckable(true);
    pWrangle->setChecked(m_searchOpts & SEARCH_ARGS);

	pOptionsMenu->addAction(pNode);
	pOptionsMenu->addAction(pSubnet);
	pOptionsMenu->addAction(pAnnotation);
	pOptionsMenu->addAction(pWrangle);

	connect(pNode, &QAction::triggered, this, [=](bool bChecked) {
        if (bChecked)
            m_searchOpts |= SEARCH_NODECLS;
        else
            m_searchOpts &= (~(int)SEARCH_NODECLS);
	});

	connect(pSubnet, &QAction::triggered, this, [=](bool bChecked) {
		if (bChecked)
			m_searchOpts |= SEARCH_SUBNET;
		else
			m_searchOpts &= (~(int)SEARCH_SUBNET);
		});

	connect(pAnnotation, &QAction::triggered, this, [=](bool bChecked) {
		if (bChecked)
			m_searchOpts |= SEARCH_ANNO;
		else
			m_searchOpts &= (~(int)SEARCH_ANNO);
		});

	connect(pWrangle, &QAction::triggered, this, [=](bool bChecked) {
		if (bChecked)
			m_searchOpts |= SEARCH_ARGS;
		else
			m_searchOpts &= (~(int)SEARCH_ARGS);
		});

	pOptionsMenu->exec(QCursor::pos());
	pOptionsMenu->deleteLater();
}

void ZenoGraphsEditor::onSubnetOptionClicked()
{
    QMenu* pOptionsMenu = new QMenu;

	QAction* pCreate = new QAction(tr("create subnet"));
	QAction* pSubnetMap = new QAction(tr("subnet map"));
	QAction* pImpFromFile = new QAction(tr("import from local file"));
	QAction* pImpFromSys = new QAction(tr("import system subnet"));

    pOptionsMenu->addAction(pCreate);
    pOptionsMenu->addAction(pSubnetMap);
    pOptionsMenu->addSeparator();
    pOptionsMenu->addAction(pImpFromFile);
    pOptionsMenu->addAction(pImpFromSys);

    connect(pCreate, &QAction::triggered, this, [=]() {
        bool bOk = false;
        QString newSubgName = QInputDialog::getText(this, tr("create subnet"), tr("new subgraph name:")
            , QLineEdit::Normal, "SubgraphName", &bOk);

        if (newSubgName.compare("main", Qt::CaseInsensitive) == 0)
        {
            QMessageBox msg(QMessageBox::Warning, tr("Zeno"), tr("main graph is not allowed to be created"));
            msg.exec();
            return;
        }

        if (bOk) {
            m_model->newSubgraph(newSubgName);
        }
	});
	connect(pSubnetMap, &QAction::triggered, this, [=]() {

		});
	connect(pImpFromFile, &QAction::triggered, this, [=]() {
        m_mainWin->importGraph();
    });
	connect(pImpFromSys, &QAction::triggered, this, [=]() {

	});

    pOptionsMenu->exec(QCursor::pos());
    pOptionsMenu->deleteLater();
}

void ZenoGraphsEditor::sideButtonToggled(bool bToggled)
{
    QObject* pBtn = sender();

    QModelIndex idx;
    if (pBtn == m_ui->subnetBtn)
    {
        idx = m_sideBarModel->match(m_sideBarModel->index(0, 0), Qt::UserRole + 1, Side_Subnet)[0];
    }
    else if (pBtn == m_ui->treeviewBtn)
    {
        idx = m_sideBarModel->match(m_sideBarModel->index(0, 0), Qt::UserRole + 1, Side_Tree)[0];
    }
    else if (pBtn == m_ui->searchBtn)
    {
        idx = m_sideBarModel->match(m_sideBarModel->index(0, 0), Qt::UserRole + 1, Side_Search)[0];
    }

    if (bToggled)
        m_selection->setCurrentIndex(idx, QItemSelectionModel::SelectCurrent);
    else
        m_selection->clearCurrentIndex();
}

void ZenoGraphsEditor::onSideBtnToggleChanged(const QItemSelection& selected, const QItemSelection& deselected)
{
}

void ZenoGraphsEditor::onCurrentChanged(const QModelIndex& current, const QModelIndex& previous)
{
	if (previous.isValid())
	{
        int sideBar = current.data(Qt::UserRole + 1).toInt();
		switch (previous.data(Qt::UserRole + 1).toInt())
		{
		case Side_Subnet: m_ui->subnetBtn->setChecked(false); break;
		case Side_Tree: m_ui->treeviewBtn->setChecked(false); break;
		case Side_Search: m_ui->searchBtn->setChecked(false); break;
		}
	}

	if (current.isValid())
	{
        m_ui->stackedWidget->show();
		int sideBar = current.data(Qt::UserRole + 1).toInt();
        switch (sideBar)
        {
            case Side_Subnet:
            {
                m_ui->subnetBtn->setChecked(true);
                m_ui->stackedWidget->setCurrentWidget(m_ui->subnetPage);
                break;
            }
            case Side_Tree:
            {
                m_ui->treeviewBtn->setChecked(true);
                m_ui->stackedWidget->setCurrentWidget(m_ui->treePage);
                break;
            }
            case Side_Search:
            {
                m_ui->searchBtn->setChecked(true);
                m_ui->stackedWidget->setCurrentWidget(m_ui->searchPage);
                break;
            }
        }
	}
    else
    {
        m_ui->stackedWidget->hide();
    }
}

int ZenoGraphsEditor::tabIndexOfName(const QString& subGraphName)
{
	for (int i = 0; i < m_ui->graphsViewTab->count(); i++)
	{
		if (m_ui->graphsViewTab->tabText(i) == subGraphName)
		{
			return i;
		}
	}
	return -1;
}

void ZenoGraphsEditor::onListItemActivated(const QModelIndex& index)
{
	const QString& subgraphName = index.data().toString();
    activateTab(subgraphName);
}

void ZenoGraphsEditor::activateTab(const QString& subGraphName, const QString& path, const QString& objId, bool isError)
{
	auto graphsMgm = zenoApp->graphsManagment();
	IGraphsModel* pModel = graphsMgm->currentModel();

    if (!pModel->index(subGraphName).isValid())
        return;

	int idx = tabIndexOfName(subGraphName);
	if (idx == -1)
	{
		const QModelIndex& subgIdx = pModel->index(subGraphName);

        ZenoSubGraphScene* pScene = qobject_cast<ZenoSubGraphScene*>(graphsMgm->gvScene(subgIdx));
        if (!pScene)
        {
            pScene = new ZenoSubGraphScene(graphsMgm);
            graphsMgm->addScene(subgIdx, pScene);
            pScene->initModel(subgIdx);
        }

        ZenoSubGraphView* pView = new ZenoSubGraphView;
        pView->initScene(pScene);

        idx = m_ui->graphsViewTab->addTab(pView, subGraphName);

        connect(pView, &ZenoSubGraphView::zoomed, pScene, &ZenoSubGraphScene::onZoomed);

        connect(pView, &ZenoSubGraphView::pathUpdated, this, [=](QString newPath) {
            QStringList L = newPath.split("/", QtSkipEmptyParts);
            QString subgName = L.last();
            activateTab(subgName, newPath);
        });
	}
	m_ui->graphsViewTab->setCurrentIndex(idx);

    ZenoSubGraphView* pView = qobject_cast<ZenoSubGraphView*>(m_ui->graphsViewTab->currentWidget());
    ZASSERT_EXIT(pView);
    pView->resetPath(path, subGraphName, objId, isError);
}

void ZenoGraphsEditor::onTreeItemActivated(const QModelIndex& index)
{
	QModelIndex idx = index;

	const QString& objId = idx.data(ROLE_OBJID).toString();
	QString path, subgName;
	if (!idx.parent().isValid())
	{
        subgName = idx.data(ROLE_OBJNAME).toString();
		path = "/" + subgName;
	}
	else
	{
		idx = idx.parent();
        subgName = idx.data(ROLE_OBJNAME).toString();

		while (idx.isValid())
		{
			QString objName = idx.data(ROLE_OBJNAME).toString();
			path = "/" + objName + path;
			idx = idx.parent();
		}
	}

    activateTab(subgName, path, objId);
}

void ZenoGraphsEditor::onPageActivated(const QPersistentModelIndex& subgIdx, const QPersistentModelIndex& nodeIdx)
{
    const QString& subgName = nodeIdx.data(ROLE_OBJNAME).toString();
    activateTab(subgName);
}

void ZenoGraphsEditor::onLogInserted(const QModelIndex& parent, int first, int last)
{
    QStandardItemModel* logModel = qobject_cast<QStandardItemModel*>(sender());
    const QModelIndex& idx = logModel->index(first, 0, parent);
    if (idx.isValid())
    {
        const QString& objId = idx.data(ROLE_NODE_IDENT).toString();
        const QString& msg = idx.data(Qt::DisplayRole).toString();
        QtMsgType type = (QtMsgType)idx.data(ROLE_LOGTYPE).toInt();
        if (!objId.isEmpty() && type == QtFatalMsg)
        {
            QList<SEARCH_RESULT> results = m_model->search(objId, SEARCH_NODEID);
            for (int i = 0; i < results.length(); i++)
            {
                const SEARCH_RESULT& res = results[i];
                const QString &subgName = res.subgIdx.data(ROLE_OBJNAME).toString();
                const QString &objId = res.targetIdx.data(ROLE_OBJID).toString();

                static bool bFocusOnError = false;
                if (bFocusOnError)
                {
                    activateTab(subgName, "", objId, true);
                    if (i == results.length() - 1)
                        break;

                    QMessageBox msgbox(QMessageBox::Question, "", tr("next one?"), QMessageBox::Yes | QMessageBox::No);
                    int ret = msgbox.exec();
                    if (ret & QMessageBox::Yes) {
                    }
                    else {
                        break;
                    }
                }
                else
                {
                    const QModelIndex& subgIdx = m_model->index(subgName);
                    auto graphsMgm = zenoApp->graphsManagment();
                    ZenoSubGraphScene* pScene = qobject_cast<ZenoSubGraphScene*>(graphsMgm->gvScene(subgIdx));
                    if (!pScene) {
                        pScene = new ZenoSubGraphScene(graphsMgm);
                        graphsMgm->addScene(subgIdx, pScene);
                        pScene->initModel(subgIdx);
                    }
                    pScene->markError(objId);
                }
            }
        }
    }
}

void ZenoGraphsEditor::onSearchEdited(const QString& content)
{
    QList<SEARCH_RESULT> results = m_model->search(content, m_searchOpts);

    QStandardItemModel* pModel = new QStandardItemModel(this);

    for (SEARCH_RESULT res : results)
    {
        if (res.type == SEARCH_SUBNET)
        {
            QString subgName = res.targetIdx.data(ROLE_OBJNAME).toString();
            QModelIndexList lst = pModel->match(pModel->index(0, 0), ROLE_OBJNAME, subgName, 1, Qt::MatchExactly);
            if (lst.size() == 0)
            {
                //add subnet
                QStandardItem* pItem = new QStandardItem(subgName + " (Subnet)");
                pItem->setData(subgName, ROLE_OBJNAME);
                pItem->setData(res.targetIdx.data(ROLE_OBJID).toString(), ROLE_OBJID);
                pModel->appendRow(pItem);
            }
        }
        else if (res.type == SEARCH_NODECLS || res.type == SEARCH_NODEID || res.type == SEARCH_ARGS)
        {
            QString subgName = res.subgIdx.data(ROLE_OBJNAME).toString();
            QModelIndexList lst = pModel->match(pModel->index(0, 0), ROLE_OBJNAME, subgName, 1, Qt::MatchExactly);

            QStandardItem* parentItem = nullptr;
            if (lst.size() == 0)
            {
                //add subnet
                parentItem = new QStandardItem(subgName + " (Subnet)");
                parentItem->setData(subgName, ROLE_OBJNAME);
                pModel->appendRow(parentItem);
            }
            else
            {
                ZASSERT_EXIT(lst.size() == 1);
                parentItem = pModel->itemFromIndex(lst[0]);
            }

            QString nodeName = res.targetIdx.data(ROLE_OBJNAME).toString();
            QString nodeIdent = res.targetIdx.data(ROLE_OBJID).toString();
            QStandardItem* pItem = new QStandardItem(nodeIdent);
            pItem->setData(nodeName, ROLE_OBJNAME);
            pItem->setData(res.targetIdx.data(ROLE_OBJID).toString(), ROLE_OBJID);
            parentItem->appendRow(pItem);
        }
    }

    m_ui->searchResView->setModel(pModel);
    m_ui->searchResView->setItemDelegate(new SearchItemDelegate(content));
    m_ui->searchResView->expandAll();
}

void ZenoGraphsEditor::onSearchItemClicked(const QModelIndex& index)
{
    QString objId = index.data(ROLE_OBJID).toString();
    if (index.parent().isValid())
    {
        QString parentId = index.parent().data(ROLE_OBJID).toString();
        QString subgName = index.parent().data(ROLE_OBJNAME).toString();
        activateTab(subgName, "", objId);
    }
    else
    {
        QString subgName = index.data(ROLE_OBJNAME).toString();
        activateTab(subgName);
    }
}

void ZenoGraphsEditor::toggleViewForSelected(bool bOn)
{
    ZenoSubGraphView* pView = qobject_cast<ZenoSubGraphView*>(m_ui->graphsViewTab->currentWidget());
    if (pView)
    {
        ZenoSubGraphScene* pScene = pView->scene();
        QModelIndexList nodes = pScene->selectNodesIndice();
        const QModelIndex& subgIdx = pScene->subGraphIndex();
        for (QModelIndex idx : nodes)
        {
            STATUS_UPDATE_INFO info;
            int options = idx.data(ROLE_OPTIONS).toInt();
            info.oldValue = options;
            if (bOn) {
                options |= OPT_VIEW;
            }
            else {
                options ^= OPT_VIEW;
            }
            info.role = ROLE_OPTIONS;
            info.newValue = options;
            m_model->updateNodeStatus(idx.data(ROLE_OBJID).toString(), info, subgIdx);
        }
    }
}

void ZenoGraphsEditor::onMenuActionTriggered(QAction* pAction)
{
    const QString& text = pAction->text();
    if (text == tr("Collaspe"))
    {
        ZenoSubGraphView* pView = qobject_cast<ZenoSubGraphView*>(m_ui->graphsViewTab->currentWidget());
        ZASSERT_EXIT(pView);
        QModelIndex subgIdx = pView->scene()->subGraphIndex();
        m_model->collaspe(subgIdx);
    }
    else if (text == tr("Expand"))
	{
		ZenoSubGraphView* pView = qobject_cast<ZenoSubGraphView*>(m_ui->graphsViewTab->currentWidget());
        ZASSERT_EXIT(pView);
		QModelIndex subgIdx = pView->scene()->subGraphIndex();
		m_model->expand(subgIdx);
    }
    else if (text == tr("Open View"))
    {
        toggleViewForSelected(false);
    }
    else if (text == tr("Clear View"))
    {
        toggleViewForSelected(false);
    }
    else if (text == tr("Easy Subgraph"))
    {
        ZenoSubGraphView* pView = qobject_cast<ZenoSubGraphView*>(m_ui->graphsViewTab->currentWidget());
        if (pView)
        {
            ZenoSubGraphScene* pScene = pView->scene();
            QModelIndexList nodes = pScene->selectNodesIndice();
            bool bOk = false;
            QString newSubgName = QInputDialog::getText(this, tr("create subnet"), tr("new subgraph name:") , QLineEdit::Normal, "subgraph name", &bOk);
            if (bOk)
            {
                QModelIndex fromSubgIdx = pView->scene()->subGraphIndex();
                QModelIndex toSubgIdx = m_model->extractSubGraph(nodes, fromSubgIdx, newSubgName, true);
                if (toSubgIdx.isValid())
                {
                    activateTab(toSubgIdx.data(ROLE_OBJNAME).toString());
                }
            }
            else
            {
                //todo: msg to feedback.
            }
        }
    }
    else if (text == tr("Set NASLOC"))
    {
        QSettings settings(zsCompanyName, zsEditor);
        QString v = settings.value("nas_loc").toString();

        bool ok;
        QString text = QInputDialog::getText(this, tr("Set NASLOC"),
                                             tr("NASLOC"), QLineEdit::Normal,
                                             v, &ok);
        if (ok) {
            text.replace('\\', '/');
            settings.setValue("nas_loc", text);
            // refresh settings (zeno::setConfigVariable), only needed in single-process mode
            startUp();
        }
    }
    else if (text == tr("Set ZENCACHE"))
    {
        QSettings settings(zsCompanyName, zsEditor);
        QString v = settings.value("zencachedir").toString();
        QString v2 = settings.value("zencachenum").toString();

        bool ok;
        QString text = QInputDialog::getText(this, tr("Set ZENCACHE directory"),
                                             tr("ZENCACHEDIR"), QLineEdit::Normal,
                                             v, &ok);
        QString text2 = QInputDialog::getText(this, tr("Set ZENCACHE count"),
                                             tr("ZENCACHENUM"), QLineEdit::Normal,
                                             v2, &ok);
        if (ok) {
            text.replace('\\', '/');
            settings.setValue("zencachedir", text);
            settings.setValue("zencachenum", text2);
        }
    }
    else if (text == tr("MaterialX")) {
        importMaterialX();
    }
}


std::unordered_map<std::string, std::string> parse_input(rapidxml::xml_node<>* cchild) {
    std::unordered_map<std::string, std::string> m;
    for (xml_attribute<> *attr = cchild->first_attribute(); attr; attr = attr->next_attribute()) {
        std::string attr_name = attr->name();
        std::string attr_value = attr->value();
        m[attr_name] = attr_value;
    }
    return m;
}

std::unordered_map<std::string, std::unordered_map<std::string, std::string>>
parse_inputs(rapidxml::xml_node<>* child) {
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> ms;
    for (auto cchild = child->first_node(); cchild != nullptr; cchild = cchild->next_sibling()) {
        auto m = parse_input(cchild);
        ms[m["name"]] = m;
    }
    return ms;
}
void ZenoGraphsEditor::importMaterialX() {
    QString filename = "E:/mtlx/Dimgrey_Decorative_Granite.mtlx";
    auto dir_path = QFileInfo(filename).absoluteDir().absolutePath().toStdString();
    QFile file(filename);
    bool ret = file.open(QIODevice::ReadOnly | QIODevice::Text);
    if (!ret) {
        throw std::runtime_error("ztf file: [" + filename.toStdString() + "] not found");
    }
    QByteArray arr = file.readAll();
    rapidxml::xml_document<> doc;
    doc.parse<0>(arr.data());
    std::cerr << doc;
    auto root = doc.first_node();
    if (root == nullptr) {
        zeno::log_error("not found root node in mtlx");
        return;
    }

    std::unordered_map<std::string, ZENO_HANDLE> node_id_mapping;
    std::unordered_map<std::string, ZENO_HANDLE> extract_out_socket;
    std::unordered_map<std::string, ZENO_HANDLE> image_index;
    std::unordered_map<std::string, ZENO_HANDLE> image_node;
    std::unordered_map<std::string, ZVARIANT> constants;
    std::unordered_set<std::string> ss_nodes;
    std::vector<std::tuple<std::string, std::string, std::string>> edges;
    std::vector<std::tuple<std::string, std::string, std::string>> ss_edges;

    auto hGraph = Zeno_GetGraph("main");
    std::map<std::string, std::string> name_map {
            {"metalness", "metallic"},
            {"specular", "specular"},
            {"specular_roughness", "roughness"},
            {"subsurface", "subsurface"},
            {"subsurface_radius", "sssParam"},
            {"subsurface_color", "sssColor"},
            {"specular_color", "specularTint"},
            {"specular_anisotropy", "anisotropic"},
            {"sheen", "sheen"},
            {"sheen_color", "sheenTint"},
            {"coat", "clearcoat"},
            {"specular_IOR", "ior"},
            {"thin_walled", "thin"},
            {"normal", "normal"},
            {"opacity", "opacity"},
            {"base_color", "basecolor"},
    };
    /////////////////////////////////////////////////////////////////////////////////////////
    auto standard_surface = root->first_node("standard_surface");
    if (standard_surface == nullptr) {
        zeno::log_error("not found standard_surface in mtlx");
        return;
    }
    while (standard_surface) {
        auto std_surf_name = parse_input(standard_surface)["name"];
        ss_nodes.insert(std_surf_name);
        auto hNode = Zeno_AddNode(hGraph, "ShaderFinalize");
        node_id_mapping[std_surf_name] = hNode;
        Zeno_SetView(hNode, true);

        Zeno_SetInputDefl(hNode, "mtlid", std_surf_name);

        for (auto child = standard_surface->first_node(); child != nullptr; child = child->next_sibling()) {
            auto m = parse_input(child);

            std::string name = m["name"];
            std::string type = m.count("type")? m["type"]: "";
            std::string link_node = m.count("output")? m["output"]: "";
            std::string node_graph = m.count("nodegraph")? m["nodegraph"] + ":": "";
            QString value = QString::fromStdString(m.count("value")? m["value"]: "");
    //        zeno::log_info("{}:{}:{}", name, type, value.toStdString());
            if (name_map.count(name) == 0) {
                zeno::log_info("unsupported std_surf socket: {}", name);
                continue;
            }
            if (value.isEmpty()) {
                ss_edges.emplace_back(std_surf_name, name_map[name], node_graph + link_node);
                continue;
            }
            std::string socket_name = name_map[name];
            if (type == "float") {
                float v = value.toFloat();
                Zeno_SetInputDefl(hNode, socket_name, v);
            }
            else if (type == "boolean") {
                float v = value == "true";
                Zeno_SetInputDefl(hNode, socket_name, v);
            }
            else if (type == "color3") {
                auto items = value.split(',');
                zeno::vec3f v = zeno::vec3f(
                    items[0].toFloat(),
                    items[1].toFloat(),
                    items[2].toFloat()
                );
                Zeno_SetInputDefl(hNode, socket_name, v);
            }
        }
        standard_surface = standard_surface->next_sibling("standard_surface");
    }

    /////////////////////////////////////////////////////////////////
    std::map<std::string, std::string> unary {
            {"absval", "abs"},
            {"sign", "sign"},
            {"floor", "floor"},
            {"ceil", "ceil"},
            {"round", "round"},
            {"sin", "sin"},
            {"cos", "cos"},
            {"tan", "tan"},
            {"asin", "asin"},
            {"acos", "acos"},
            {"sqrt", "sqrt"},
            {"ln", "log"},
            {"exp", "exp"},
            {"normalize", "normalize"},
            {"magnitude", "length"},
    };
    std::map<std::string, std::string> binary {
            {"add", "add"},
            {"subtract", "sub"},
            {"multiply", "mul"},
            {"divide", "div"},
            {"module", "mod"},
            {"power", "pow"},
            {"atan2", "atan2"},
            {"min", "min"},
            {"max", "max"},
            {"dotproduct", "dot"},
            {"crossproduct", "cross"},
    };

    auto nodegraph = root->first_node("nodegraph");
    while (nodegraph) {
        auto mNodeGraph = parse_input(nodegraph);
        auto nameNodeGraph = mNodeGraph["name"] + ":";
        for (auto child = nodegraph->first_node(); child != nullptr; child = child->next_sibling()) {
            std::string start_name = child->name();
            auto m = parse_input(child);
            std::string name = nameNodeGraph + m["name"];
            std::string type = m["type"];
            std::string nodename = nameNodeGraph + m["nodename"];

            auto ms = parse_inputs(child);
            if (start_name == "constant") {
                if (type == "float") {
                    constants[name] = std::stof(ms["value"]["value"]);
                }
                else {
                    zeno::log_error("err unsupported constant {} type {}", name, type);
                }
            }
            else if (unary.count(start_name)) {
                auto hNode = Zeno_AddNode(hGraph, "ShaderUnaryMath");
                node_id_mapping[name] = hNode;
                Zeno_SetInputDefl(hNode, "op", binary[start_name]);
                edges.emplace_back(name, "in1",  nameNodeGraph + ms["in"]["nodename"]);
            }
            else if (binary.count(start_name)) {
                auto hNode = Zeno_AddNode(hGraph, "ShaderBinaryMath");
                node_id_mapping[name] = hNode;
                Zeno_SetInputDefl(hNode, "op", binary[start_name]);
                edges.emplace_back(name, "in1", nameNodeGraph + ms["in1"]["nodename"]);
                edges.emplace_back(name, "in2", nameNodeGraph + ms["in2"]["nodename"]);
            }
            else if (start_name == "output") {
                auto hNode = Zeno_AddNode(hGraph, "ShaderUnaryMath");
                node_id_mapping[name] = hNode;
                Zeno_SetInputDefl(hNode, "op", std::string("copy"));
                edges.emplace_back(name, "in1", nodename);
            }
            else if (start_name == "normalmap") {
                auto hNode = Zeno_AddNode(hGraph, "ShaderUnaryMath");
                node_id_mapping[name] = hNode;
                Zeno_SetInputDefl(hNode, "op", std::string("copy"));
                edges.emplace_back(name, "in1",  nameNodeGraph + ms["in"]["nodename"]);
            }
            else if (start_name == "clamp") {
                auto hNode = Zeno_AddNode(hGraph, "ShaderTernaryMath");
                node_id_mapping[name] = hNode;
                Zeno_SetInputDefl(hNode, "op", std::string("clamp"));
                edges.emplace_back(name, "in1", nameNodeGraph + ms["in"]["nodename"]);
                edges.emplace_back(name, "in2", nameNodeGraph + ms["low"]["nodename"]);
                edges.emplace_back(name, "in3", nameNodeGraph + ms["high"]["nodename"]);
            }
            else if (start_name == "smoothstep") {
                auto hNode = Zeno_AddNode(hGraph, "ShaderTernaryMath");
                node_id_mapping[name] = hNode;
                Zeno_SetInputDefl(hNode, "op", std::string("smoothstep"));
                edges.emplace_back(name, "in1", nameNodeGraph + ms["in"]["nodename"]);
                edges.emplace_back(name, "in2", nameNodeGraph + ms["low"]["nodename"]);
                edges.emplace_back(name, "in3", nameNodeGraph + ms["high"]["nodename"]);
            }
            else if (start_name == "mix") {
                auto hNode = Zeno_AddNode(hGraph, "ShaderTernaryMath");
                node_id_mapping[name] = hNode;
                Zeno_SetInputDefl(hNode, "op", std::string("mix"));
                edges.emplace_back(name, "in1", nameNodeGraph + ms["fg"]["nodename"]);
                edges.emplace_back(name, "in2", nameNodeGraph + ms["bg"]["nodename"]);
                edges.emplace_back(name, "in3", nameNodeGraph + ms["mix"]["nodename"]);
            }
            else if (start_name == "extract") {
                auto hNode = Zeno_AddNode(hGraph, "ShaderExtractVec");
                node_id_mapping[name] = hNode;
                extract_out_socket[name] = std::stoul(ms["index"]["value"]);
                edges.emplace_back(name, "vec", nameNodeGraph + ms["in"]["nodename"]);
            }
            else if (start_name == "normal" || start_name == "tangent") {
                // ignore
            }
            else if (start_name == "texcoord") {
                auto hNode = Zeno_AddNode(hGraph, "ShaderInputAttr");
                Zeno_SetInputDefl(hNode, "attr", std::string("uv"));
                node_id_mapping[name] = hNode;
            }
            else if (start_name == "image") {
                auto hNode = Zeno_AddNode(hGraph, "MakeTexture2D");
                node_id_mapping[name] = hNode;
                image_index[name] = image_index.size();
                std::string file_path = zeno::format("{}/{}", dir_path, ms["file"]["value"]);
                Zeno_SetInputDefl(hNode, "path", file_path);
                edges.emplace_back(name, "coord", nameNodeGraph + ms["texcoord"]["nodename"]);
            }
            else {
                zeno::log_info("unsupported node: {}", start_name);
            }
        }
        nodegraph = nodegraph->next_sibling("nodegraph");
    }

    // specially deal with image node
    {
        auto nMakeSmallList = Zeno_AddNode(hGraph, "MakeSmallList");
        auto nStandardSurface = node_id_mapping[*ss_nodes.begin()];
        Zeno_AddLink(nMakeSmallList, "list", nStandardSurface, "tex2dList");
        for (const auto& [name, index]: image_index) {
            ZENO_ERROR err = Zeno_AddLink(node_id_mapping[name], "tex", nMakeSmallList, zeno::format("obj{}", index));
            if (err) {
                zeno::log_error("Image {}, {}", name, index);
            }

            auto nShaderTexture2D = Zeno_AddNode(hGraph, "ShaderTexture2D");
            Zeno_SetInputDefl(nShaderTexture2D, "texId", (int)index);

            image_node[name] = nShaderTexture2D;
        }
    }

    int index = 0;
    for (auto [i_node, i_socket, o_node] : edges) {
        std::string out_socket = "out";
        if (extract_out_socket.count(o_node)) {
            auto i = extract_out_socket[o_node];
            out_socket.clear();
            out_socket.push_back("xyz"[i]);
        }

        ZENO_HANDLE hOutNode = node_id_mapping[o_node];
        ZENO_HANDLE hInNode = node_id_mapping[i_node];
        if (image_node.count(o_node)) {
            hOutNode = image_node[o_node];
        }
        if (image_node.count(i_node)) {
            hInNode = image_node[i_node];
        }
        if (constants.count(o_node)) {
            Zeno_SetInputDefl(hInNode, i_socket, constants[o_node]);
            continue;
        }
        ZENO_ERROR err = Zeno_AddLink(hOutNode, out_socket, hInNode, i_socket);
        if (err) {
            zeno::log_info("{}, err {}, edge {} {} {}", index, err, i_node, i_socket, o_node);
        }
        index++;
    }
    int ss_index = 0;
    for (auto [i_node, i_socket, o_node] : ss_edges) {
        std::string out_socket = "out";

        ZENO_HANDLE hOutNode = node_id_mapping[o_node];
        ZENO_HANDLE hInNode = node_id_mapping[i_node];

        ZENO_ERROR err;
        if (i_socket == "normal") {
            auto hMul = Zeno_AddNode(hGraph, "ShaderBinaryMath");
            Zeno_SetInputDefl(hMul, "op", std::string("mul"));
            Zeno_SetInputDefl(hMul, "in2", std::string("2"));
            auto hAdd = Zeno_AddNode(hGraph, "ShaderBinaryMath");
            Zeno_SetInputDefl(hAdd, "op", std::string("add"));
            Zeno_SetInputDefl(hAdd, "in2", std::string("-1"));

            err = Zeno_AddLink(hOutNode, out_socket, hMul, "in1");
            Zeno_AddLink(hMul, "out", hAdd, "in1");
            Zeno_AddLink(hAdd, "out", hInNode, i_socket);
        }
        else {
            err = Zeno_AddLink(hOutNode, out_socket, hInNode, i_socket);
        }

        if (err) {
            zeno::log_info("ss {}, err {}, edge {} {} {}", ss_index, err, i_node, i_socket, o_node);
        }
        ss_index++;
    }
}
