#include "zveceditor.h"
#include "style/zenostyle.h"
#include "util/uihelper.h"
#include "zlineedit.h"
#include "util/curveutil.h"
#include <zeno/utils/log.h>
#include "panel/zenoproppanel.h"


ZVecEditor::ZVecEditor(const QVariant& vec, bool bFloat, int deflSize, QString styleCls, QWidget* parent)
    : QWidget(parent)
    , m_bFloat(bFloat)
    , m_deflSize(deflSize)
    , m_styleCls(styleCls)
    , m_hintlist(nullptr)
    , m_descLabel(nullptr)
{
    initUI(vec);
}

bool ZVecEditor::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::ContextMenu) {
        for (int i = 0; i < m_editors.size(); i++) {
            if (m_editors[i] == watched) {
                qApp->sendEvent(this, event);
                return true;
            }
        }
    }
    else if (event->type() == QEvent::FocusIn)
    {
        for (int i = 0; i < m_editors.size(); i++) {
            if (m_editors[i] != watched) {
                m_editors[i]->hide();
            }
        }
    }
    else if (event->type() == QEvent::FocusOut)
    {
        if (m_hintlist && !m_hintlist->isVisible())
        {
            if (ZLineEdit* edit = qobject_cast<ZLineEdit*>(watched))
            {
                if (!edit->hasFocus() && !edit->showingSlider())
                {
                    for (int i = 0; i < m_editors.size(); i++) {
                        if (!m_editors[i]->isVisible())
                            m_editors[i]->show();
                    }
                }
            }
        }
    }
    else if (event->type() == QEvent::KeyPress)
    {
        if (QKeyEvent* e = static_cast<QKeyEvent*>(event))
        {
            if (e->key() == Qt::Key_Escape)
            {
                if (ZLineEdit* edit = qobject_cast<ZLineEdit*>(watched))
                {
                    if (m_hintlist && m_hintlist->isVisible()) {
                        m_hintlist->hide();
                    } else if (m_descLabel && m_descLabel->isVisible()) {
                        m_descLabel->hide();
                    }
                    else {
                        edit->clearFocus();
                        for (int i = 0; i < m_editors.size(); i++) {
                            if (m_editors[i] != watched) {
                                m_editors[i]->show();
                            }
                        }
                    }
                }
            }
            else if (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter)
            {
                if (ZLineEdit* edit = qobject_cast<ZLineEdit*>(watched))
                {
                    if (m_hintlist && m_hintlist->isVisible())
                    {
                        m_hintlist->hide();
                        edit->hintSelectedSetText(m_hintlist->getCurrentText());
                    } else if (m_descLabel && m_descLabel->isVisible()) {
                        m_descLabel->hide();
                    } else {
                        edit->clearFocus();
                        for (int i = 0; i < m_editors.size(); i++) {
                            if (m_editors[i] != watched) {
                                m_editors[i]->show();
                            }
                        }
                    }
                    return true;
                }
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}

void ZVecEditor::initUI(const QVariant &vec) {
    QHBoxLayout* pLayout = new QHBoxLayout;
    pLayout->setContentsMargins(0, 0, 0, 0);
    pLayout->setSpacing(5);
    int n = m_deflSize;
    if (vec.canConvert<UI_VECTYPE>())
        n = vec.value<UI_VECTYPE>().size();
    else if (vec.canConvert<UI_VECSTRING>())
        n = vec.value<UI_VECSTRING>().size();

    m_editors.resize(n);
    for (int i = 0; i < m_editors.size(); i++)
    {
        m_editors[i] = new ZLineEdit;
        if (m_bFloat) {
            m_editors[i]->installEventFilter(this);
        }

        m_editors[i]->setNumSlider(UiHelper::getSlideStep("", m_bFloat ? zeno::Param_Float : zeno::Param_Int));
        //m_editors[i]->setFixedWidth(ZenoStyle::dpiScaled(64));
        m_editors[i]->setProperty("cssClass", m_styleCls);
        if (m_nodeIdx.isValid())
            m_editors[i]->setNodeIdx(m_nodeIdx);

        if (vec.canConvert<UI_VECTYPE>())
            setText(vec.value<UI_VECTYPE>().at(i), m_editors[i]);
        else if (vec.canConvert<UI_VECSTRING>())
            setText(vec.value<UI_VECSTRING>().at(i), m_editors[i]);

        pLayout->addWidget(m_editors[i]);
        connect(m_editors[i], &ZLineEdit::editingFinished, this, &ZVecEditor::editingFinished);
    }
    setLayout(pLayout);
    setStyleSheet("ZVecEditor { background: transparent; } ");
}

bool ZVecEditor::isFloat() const
{
    return m_bFloat;
}

UI_VECTYPE ZVecEditor::text() const 
{
    UI_VECTYPE vec;
    for (int i = 0; i < m_editors.size(); i++) {
        if (m_bFloat) 
            vec.append(m_editors[i]->text().toFloat());
        else
            vec.append(m_editors[i]->text().toInt());
    }
    return vec;
}

QVariant ZVecEditor::vec() const
{
	QVariant value;
    UI_VECTYPE vec;
    UI_VECSTRING vecStr;
	for (int i = 0; i < m_editors.size(); i++)
	{
        if (m_bFloat) {
            {
                bool bOK = false;
                float val = m_editors[i]->text().toFloat(&bOK);
                if (bOK && vecStr.isEmpty())
                    vec.append(val);
                else {
                    for (auto data : vec) {
                        vecStr.append(QString::number(data));
                    }
                    vec.clear();
                    QString str = m_editors[i]->text();
                    vecStr.append(str);
                }
            }
        }
        else {
            bool bOK = false;
            int val = m_editors[i]->text().toInt(&bOK);
            if (bOK && vecStr.isEmpty())
                vec.append(val);
            else {
                for (auto data : vec) {
                    vecStr.append(QString::number(data));
                }
                vec.clear();
                QString str = m_editors[i]->text();
                vecStr.append(str);
            }
        }
	}
    if (vec.size() == m_editors.size()) 
    {
        value = QVariant::fromValue(vec);
    } 
    else if (vecStr.size() == m_editors.size()) 
    {
        value = QVariant::fromValue(vecStr);
    }
    return value;
}

void ZVecEditor::setVec(const QVariant& vec, bool bFloat)
{
    int size = m_editors.size();
    if (vec.canConvert<UI_VECTYPE>())
        size = vec.value<UI_VECTYPE>().size();
    else if (vec.canConvert<UI_VECSTRING>())
        size = vec.value<UI_VECSTRING>().size();
    if (bFloat != m_bFloat || size != m_editors.size())
    {
        initUI(vec);
    }
    else
    {
        for (int i = 0; i < m_editors.size(); i++) 
        {
            if (vec.canConvert<UI_VECTYPE>())
                setText(vec.value<UI_VECTYPE>().at(i), m_editors[i]);
            else if (vec.canConvert<UI_VECSTRING>())
                setText(vec.value<UI_VECSTRING>().at(i), m_editors[i]);
        }
    }
}

void ZVecEditor::showNoFocusLineEdits(QWidget* lineEdit)
{
    if (lineEdit)
    {
        for (int i = 0; i < m_editors.size(); i++) {
            if (m_editors[i] == lineEdit)
                return;
        }
        for (int i = 0; i < m_editors.size(); i++) {
            if (!m_editors[i]->isVisible())
                m_editors[i]->show();
        }
    }
}

void ZVecEditor::setText(const QVariant &value, ZLineEdit* lineEdit) 
{
    QString text = UiHelper::variantToString(value);
    lineEdit->setText(text);
}

int ZVecEditor::getCurrentEditor() 
{
    QPoint pos = QCursor::pos();
    pos = mapFromGlobal(pos);
    for (int i = 0; i < m_editors.size(); i++)
    {
        if (m_editors.at(i)->geometry().contains(pos)) {
            return i;
        }
    }
    return -1;
}

void ZVecEditor::setNodeIdx(const QModelIndex& index)
{
    m_nodeIdx = index;
    for (int i = 0; i < m_editors.size(); i++)
    {
        m_editors[i]->setNodeIdx(m_nodeIdx);
    }
}

void ZVecEditor::updateProperties(const QVector<QString>& properties)
{
    for (int i = 0; i < m_editors.size(); i++)
    {
        QString property;
        if (i >= properties.size())
        {
            property = properties.first();
        }
        else
        {
            property = properties.at(i);
        }
        m_editors[i]->setProperty(g_setKey, property);
        m_editors[i]->style()->unpolish(m_editors[i]);
        m_editors[i]->style()->polish(m_editors[i]);
        m_editors[i]->update();
    }
}

void ZVecEditor::setHintListWidget(ZenoHintListWidget* hintlist, ZenoFuncDescriptionLabel* descLabl)
{
    m_hintlist = hintlist;
    m_descLabel = descLabl;
    for (int i = 0; i < m_editors.size(); i++) {
        m_editors[i]->setHintListWidget(hintlist, descLabl);
    }
    connect(m_hintlist, &ZenoHintListWidget::clickOutSideHide, this, &ZVecEditor::showNoFocusLineEdits);
}
