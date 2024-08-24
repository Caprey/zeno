#include "ztooltip.h"

ZToolTip* ZToolTip::getInstance()
{
    static ZToolTip toolTip;
    return &toolTip;
}
void ZToolTip::showText(QPoint pos, const QString& text)
{
    ZToolTip* pToolTip = getInstance();
    pToolTip->setProperty("hasIcon", false);
    pToolTip->setText(text);
    QFontMetrics fm(QApplication::font());
    auto width = fm.width(text);
    auto height = fm.height();
    pToolTip->resize(width + 20, height + 20);
    pToolTip->move(pos);
    pToolTip->show();
}

void ZToolTip::showIconText(QString icon, QPoint pos, const QString& text)
{
    ZToolTip* pToolTip = getInstance();
    pToolTip->setProperty("hasIcon", true);
    pToolTip->setProperty("iconPath", icon);
    pToolTip->setText(text);
    QFontMetrics fm(QApplication::font());
    auto width = fm.width(text);
    auto height = fm.height();
    pToolTip->resize(width + 40, height + 20);
    pToolTip->move(pos);
    pToolTip->show();
}

void ZToolTip::hideText()
{
    ZToolTip* pToolTip = getInstance();
    pToolTip->hide();
}

ZToolTip::ZToolTip(QWidget* parent) : 
    QLabel(parent, Qt::ToolTip | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
}

void ZToolTip::paintEvent(QPaintEvent* evt)
{
    QPainter painter(this);
    QRect rc = this->rect();
    //draw shadow
    QRect shawRect = rc.adjusted(10, 10, -10, -10);
    QColor color(0, 0, 0);
    QPen pen;
    for (int i = 0; i < 10; i++)
    {
        shawRect.adjust(-1, -1, 1, 1);
        color.setAlpha(150 - qSqrt(i) * 50);
        pen.setJoinStyle(Qt::MiterJoin);
        pen.setWidth(1);
        pen.setColor(color);
        painter.setPen(pen);
        painter.drawRect(shawRect);
    }
    rc.adjust(6, 6, -6, -6);
    painter.fillRect(rc, QColor(31, 31, 31));
    //draw icon
    bool hasIcon =  this->property("hasIcon").isValid() ? this->property("hasIcon").toBool() : false;
    if (hasIcon) {
        QPixmap icon = QPixmap(this->property("iconPath").toString()).scaled(15, 15, Qt::KeepAspectRatio);
        if (!icon.isNull()) {
            painter.drawPixmap(QPoint(rc.left() + 4, rc.top() + 4), icon);
        }
    }
    //draw text
    pen.setWidth(2);
    pen.setColor(Qt::white);
    painter.setPen(pen);
    painter.drawText(QPoint(rc.left() + 8 + (hasIcon ? 15 : 0), rc.center().y() + 6), this->text());
}