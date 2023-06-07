#include "zenonewmenu.h"
#include <zenomodel/include/graphsmanagment.h>
#include "zenoapplication.h"
#include <zenomodel/include/nodesmgr.h>
#include "fuzzy_search.h"
#include <zenoui/comctrl/gv/zenoparamwidget.h>
#include <zenomodel/include/uihelper.h>


ZenoNewnodeMenu::ZenoNewnodeMenu(const QModelIndex& subgIdx, const NODE_CATES& cates, const QPointF& scenePos, QWidget* parent)
    : QMenu(parent)
    , m_cates(cates)
    , m_subgIdx(subgIdx)
    , m_scenePos(scenePos)
    , m_searchEdit(nullptr)
    , m_pWAction(nullptr)
{
    QVBoxLayout* pLayout = new QVBoxLayout;

    m_pWAction = new QWidgetAction(this);
    m_searchEdit = new ZenoGvLineEdit;
    m_searchEdit->setAutoFillBackground(false);
    m_searchEdit->setTextMargins(QMargins(8, 0, 0, 0));
    m_searchEdit->installEventFilter(this);

    QPalette palette;
    palette.setColor(QPalette::Base, QColor(25, 29, 33));
    QColor clr = QColor(255, 255, 255);
    palette.setColor(QPalette::Text, clr);

    m_searchEdit->setPalette(palette);
    QFont font = zenoApp->font();
    m_searchEdit->setFont(font);
    m_pWAction->setDefaultWidget(m_searchEdit);
    addAction(m_pWAction);

    IGraphsModel* pModel = UiHelper::getGraphsBySubg(m_subgIdx);
    ZASSERT_EXIT(pModel);
    QList<QAction*> actions = getCategoryActions(pModel, m_subgIdx, "", m_scenePos);
    addActions(actions);

	if (!m_cates.isEmpty())
	{
		for (auto i : m_cates["deprecated"].nodes)
		{
			deprecatedNodes.insert(i);
		}
	}

    connect(m_searchEdit, SIGNAL(textChanged(const QString&)), this, SLOT(onTextChanged(const QString&)));
}

ZenoNewnodeMenu::~ZenoNewnodeMenu()
{
}

void ZenoNewnodeMenu::setEditorFocus()
{
    m_searchEdit->setFocus();
}

bool ZenoNewnodeMenu::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress)
    {
        QKeyEvent *pKeyEvent = static_cast<QKeyEvent *>(event);
        if (QMenu* pMenu = qobject_cast<QMenu*>(watched))
        {
            int ch = pKeyEvent->key();
            QChar c(ch);
            QString text = m_searchEdit->text();
            text.append(c);
            m_searchEdit->setText(text);
            pMenu->hide();
            return true;
        }
        else if (watched == m_searchEdit && pKeyEvent->key() == Qt::Key_Down) 
		{
            focusNextPrevChild(true);
        }
    }
    else if (watched == m_searchEdit && event->type() == QEvent::Show) 
	{
        m_searchEdit->activateWindow();
    }
    return QMenu::eventFilter(watched, event);
}

void ZenoNewnodeMenu::onTextChanged(const QString& text)
{
    QList<QAction*> acts = actions();

    for (int i = 0; i < acts.size(); i++) {
        if (acts[i] == m_pWAction) continue;
        removeAction(acts[i]);
        if (acts[i]->parent() == this)
            delete acts[i];
    }

    IGraphsModel* pModel = UiHelper::getGraphsBySubg(m_subgIdx);
    ZASSERT_EXIT(pModel);
    QList<QAction*> actions = getCategoryActions(pModel, m_subgIdx, text, m_scenePos);
    addActions(actions);
    setEditorFocus();
}

QList<QAction*> ZenoNewnodeMenu::getCategoryActions(IGraphsModel* pModel, QModelIndex subgIdx, const QString& filter, QPointF scenePos)
{
    Q_ASSERT(pModel);
    if (!pModel)
        return QList<QAction*>();

    auto &mgr = GraphsManagment::instance();
    NODE_CATES cates = mgr.getCates();
    QList<QAction*> acts;
    int nodesNum = 0;
    if (cates.isEmpty())
    {
        QAction* pAction = new QAction("ERROR: no descriptors loaded!");
        pAction->setEnabled(false);
        acts.push_back(pAction);
        return acts;
    }

    if (!filter.isEmpty())
    {
        QList<QString> condidates;
        for (const NODE_CATE& cate : cates) {
            for (const QString& name : cate.nodes) {
                condidates.push_back(name);
            }
        }
        for(const QString& name: fuzzy_search(filter, condidates)) {
            QAction* pAction = new QAction();
            connect(pAction, &QAction::triggered, [=]() {
                NodesMgr::createNewNode(pModel, subgIdx, name, scenePos);
            });
            if (deprecatedNodes.contains(name))
            {
                pAction->setText(name + " (deprecated)");
				acts.push_back(pAction);
            }
            else {
				pAction->setText(name);
				acts.insert(nodesNum, pAction);
				nodesNum++;
            }
        }
        return acts;
    }
    else
    {
        for (const NODE_CATE& cate : cates)
        {
            QAction* pAction = new QAction(cate.name);
            QMenu* pChildMenu = new QMenu;
            pChildMenu->setToolTipsVisible(true);
            for (const QString& name : cate.nodes)
            {
                QAction* pChildAction = pChildMenu->addAction(name);
                //todo: tooltip
                connect(pChildAction, &QAction::triggered, [=]() {
                    NodesMgr::createNewNode(pModel, subgIdx, name, scenePos);
                });
            }
            pAction->setMenu(pChildMenu);
            pChildMenu->installEventFilter(this);
            acts.push_back(pAction);
        }
    }
    return acts;
}