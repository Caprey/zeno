//
// Created by zh on 2023/3/23.
//

#ifndef ZENO_ZENOIMAGEPANEL_H
#define ZENO_ZENOIMAGEPANEL_H

#include <QtWidgets>
class ZenoImageView;

class ZenoImagePanel : public QWidget {
    Q_OBJECT

    QLabel* pStatusBar = new QLabel();
    QLabel* pPrimName = new QLabel();
    QCheckBox *pGamma = new QCheckBox("Gamma");
    ZenoImageView *image_view = nullptr;

public:
    ZenoImagePanel(QWidget* parent = nullptr);
    void clear();
    void setPrim(std::string primid);
};


#endif //ZENO_ZENOIMAGEPANEL_H
