#include "calculationmgr.h"
#include <zeno/core/Session.h>
#include <zeno/core/Graph.h>
#include <zeno/extra/GraphException.h>
#include "viewport/displaywidget.h"
#include "zassert.h"
#include "util/uihelper.h"
#include "zenoapplication.h"
#include "zenomainwindow.h"
#include <zeno/core/common.h>
#include "model/graphsmanager.h"
#include "model/GraphsTreeModel.h"
#include "widgets/ztimeline.h"


CalcWorker::CalcWorker(QObject* parent) {
    auto& sess = zeno::getSession();
    if (m_bReportNodeStatus) {
        sess.registerNodeCallback([=](zeno::ObjPath nodePath, bool bDirty, zeno::NodeRunStatus status) {
            NodeState state;
            state.bDirty = bDirty;
            state.runstatus = status;
            emit nodeStatusChanged(nodePath, state);
        });
    }
}

void CalcWorker::run() {
    auto& sess = zeno::getSession();

    zeno::GraphException::catched([&] {
        sess.run();
    }, *sess.globalError);
    sess.globalState->set_working(false);

    if (sess.globalError->failed()) {
        QString errMsg = QString::fromStdString(sess.globalError->getErrorMsg());
        NodeState state;
        state.bDirty = true;
        state.runstatus = zeno::Node_RunError;
        zeno::ObjPath path = sess.globalError->getNode();
        emit nodeStatusChanged(path, state);
        emit calcFinished(false, path, errMsg);
    }
    else {
        emit calcFinished(true, {}, "");
    }
}


CalculationMgr::CalculationMgr(QObject* parent)
    : QObject(parent)
    , m_bMultiThread(false)
    , m_worker(nullptr)
    , m_playTimer(new QTimer(this))
{
    m_worker = new CalcWorker(this);
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::started, m_worker, &CalcWorker::run);
    connect(m_worker, &CalcWorker::calcFinished, this, &CalculationMgr::onCalcFinished);
    connect(m_worker, &CalcWorker::nodeStatusChanged, this, &CalculationMgr::onNodeStatusReported);
    connect(m_playTimer, SIGNAL(timeout()), this, SLOT(onPlayReady()));

    auto& sess = zeno::getSession();
    sess.registerRunTrigger([=]() {
        run();
    });
}

void CalculationMgr::onNodeStatusReported(zeno::ObjPath uuidPath, NodeState state)
{
    GraphsTreeModel* pMainTree = zenoApp->graphsManager()->currentModel();
    if (pMainTree) {
        const QModelIndex targetNode = pMainTree->getIndexByUuidPath(uuidPath);
        if (targetNode.isValid()) {
            UiHelper::qIndexSetData(targetNode, QVariant::fromValue(state), ROLE_NODE_RUN_STATE);
            if (!m_bMultiThread) {
                //TODO: �����ʱ������ܻ�����ı�ڵ�״̬�����ݵĲ��������绬��ʱ���ᣬ���Ա���Ҫ�����¼��ķ�Χ
                //zenoApp->processEvents();
            }
        }
    }
}

void CalculationMgr::onCalcFinished(bool bSucceed, zeno::ObjPath nodeUuidPath, QString msg)
{
    //ȷ����ʱ�����̲߳������߼���������ʱ�Ǵ�����Լ����Ҳ����CalcWorker::run()����ͷ��źš�
    if (m_bMultiThread)
    {
        m_thread.quit();
        m_thread.wait();
    }
    emit calcFinished(bSucceed, nodeUuidPath, msg);
}

void CalculationMgr::run()
{
    if (m_bMultiThread) {
        m_thread.start();
    }
    else {
        m_worker->run();
    }
}

void CalculationMgr::onPlayReady() {
    auto& sess = zeno::getSession();
    if (!sess.is_auto_run()) {
        run();
    }
    //�е���һ֡
    int frame = sess.globalState->getFrameId();
    sess.switchToFrame(frame + 1);

    //ui��Ҳͬ����һ֡
    if (auto mainWin = zenoApp->getMainWindow()) {
        if (auto timeline = mainWin->timeline()) {
            timeline->blockSignals(true);
            timeline->setSliderValue(frame + 1);
            timeline->blockSignals(false);
        }
    }

    m_playTimer->start();
}

void CalculationMgr::onPlayTriggered(bool bToggled) {
    if (m_playTimer) {
        if (bToggled) {
            m_playTimer->start();
        }
        else {
            m_playTimer->stop();
        }

        if (auto mainWin = zenoApp->getMainWindow()) {
            if (auto timeline = mainWin->timeline()) {
                bool block = timeline->signalsBlocked();
                timeline->blockSignals(true);
                timeline->setPlayButtonChecked(bToggled);
                timeline->blockSignals(block);
            }
        }
    }
}

void CalculationMgr::onFrameSwitched(int frame) {
    //�ֶ��ƶ�ʱ����

    //ֹͣ����
    m_playTimer->stop();
    if (auto mainWin = zenoApp->getMainWindow()) {
        if (auto timeline = mainWin->timeline()) {
            bool block = timeline->signalsBlocked();
            timeline->blockSignals(true);
            timeline->setPlayButtonChecked(false);
            timeline->blockSignals(block);
        }
    }

    auto& sess = zeno::getSession();
    sess.switchToFrame(frame);
}

void CalculationMgr::kill()
{
    zeno::getSession().interrupt();
    zeno::getSession().globalState->set_working(false);
}

void CalculationMgr::registerRenderWid(DisplayWidget* pDisp)
{
    m_registerRenders.insert(pDisp);
    connect(this, &CalculationMgr::calcFinished, pDisp, &DisplayWidget::onCalcFinished);
    connect(pDisp, &DisplayWidget::render_objects_loaded, this, &CalculationMgr::on_render_objects_loaded);
}

void CalculationMgr::unRegisterRenderWid(DisplayWidget* pDisp) {
    m_loadedRender.remove(pDisp);
}

bool CalculationMgr::isMultiThreadRunning() const {
    return m_bMultiThread;
}

void CalculationMgr::on_render_objects_loaded()
{
    DisplayWidget* pWid = qobject_cast<DisplayWidget*>(sender());
    ZASSERT_EXIT(pWid);
    m_loadedRender.insert(pWid);
    if (m_loadedRender.size() == m_registerRenders.size())
    {
        //todo: notify calc to continue, if still have something to calculate.
    }
}
