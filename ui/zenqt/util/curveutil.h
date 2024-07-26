#ifndef __CURVE_UTIL_H__
#define __CURVE_UTIL_H__

#include "uicommon.h"
#include <QGraphicsItem>
#include <zeno/core/data.h>

class CurveModel;

namespace curve_util
{
	enum ItemType
	{
		ITEM_NODE,
		ITEM_LEFTHANDLE,
		ITEM_RIGHTHANDLE,
		ITEM_CURVE,
	};

	enum ItemStatus
	{
		ITEM_UNTOGGLED,
		ITEM_TOGGLED,
		ITEM_SELECTED,
	};

	enum ROLE_CURVE
	{
		ROLE_ItemType = Qt::UserRole + 1,
		ROLE_ItemObjId,
		ROLE_ItemBelongTo,
		ROLE_ItemPos,
		ROLE_ItemStatus,
		ROLE_MouseClicked,
		ROLE_CurveLeftNode,
		ROLE_CurveRightNode
	};

	enum CurveGVType
	{
		CURVE_NODE = QGraphicsItem::UserType + 1,
		CURVE_HANDLE
	};

	QRectF fitInRange(CURVE_RANGE rg, const QMargins& margins);
	QRectF initGridSize(const QSize& sz, const QMargins& margins);
	QModelIndex findUniqueItem(QAbstractItemModel* pModel, int role, QVariant value);
	QPair<int, int> numframes(qreal scaleX, qreal scaleY);
	CurveModel* deflModel(QObject* parent);
	zeno::CurvesData deflCurves();
	CURVE_DATA toLegacyCurve(zeno::CurveData curve);
	CURVES_DATA toLegacyCurves(zeno::CurvesData curves);
	zeno::CurveData fromLegacyCurve(const CURVE_DATA& _curve);
	zeno::CurvesData fromLegacyCurves(const CURVES_DATA& _curves);

    void updateRange(zeno::CurvesData& curves);
    QString getCurveKey(int index);
    bool updateCurve(const QPointF& point, CURVE_DATA &curve);
}

#endif