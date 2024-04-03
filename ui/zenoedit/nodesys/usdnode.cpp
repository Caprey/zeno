#include "usdnode.h"

#include <zeno/utils/eulerangle.h>

std::string getOutNameOfGeoNode(ZENO_HANDLE targetGraph, ZENO_HANDLE node) {
	/*
	* for CreateXXX node, use 'prim' as output name
	* for PrimitiveTransform, use 'outPrim' as output name
	*/
	std::string name;
	Zeno_GetName(targetGraph, node, name);
	name = (name == "PrimitiveTransform") ? "outPrim" : "prim";
	return name;
}

EvalUSDPrim::EvalUSDPrim(const NodeUtilParam& params, QGraphicsItem* parent)
	: ZenoNode(params, parent) {
	;
}

EvalUSDPrim::~EvalUSDPrim() {
	;
}

ZGraphicsLayout* EvalUSDPrim::initCustomParamWidgets() {
	ZGraphicsLayout* pHLayout = new ZGraphicsLayout(true);
	ZGraphicsLayout* pHLayoutNode = new ZGraphicsLayout(true);
	ZSimpleTextItem* pNodeItem = new ZSimpleTextItem("Eval");

	pNodeItem->setBrush(m_renderParams.socketClr.color());
	pNodeItem->setFont(m_renderParams.socketFont);
	pNodeItem->updateBoundingRect();
	pHLayoutNode->addItem(pNodeItem);

	ZenoParamPushButton* pNodeBtn = new ZenoParamPushButton("Generate", -1, QSizePolicy::Expanding);
	pHLayoutNode->addItem(pNodeBtn);
	connect(pNodeBtn, SIGNAL(clicked()), this, SLOT(_onEvalClicked()));

	_param_ctrl paramNode;
	paramNode.param_name = pNodeItem;
	paramNode.param_control = pNodeBtn;
	paramNode.ctrl_layout = pHLayoutNode;
	addParam(paramNode);

	pHLayout->addLayout(pHLayoutNode);

	return pHLayout;
}

void EvalUSDPrim::_getNodeInputs() {
	ZENO_HANDLE hGraph = Zeno_GetGraph("main");
	ZENO_HANDLE curNode = index().internalId();
	ZVARIANT usdPath;
	ZVARIANT primPath;
	std::string type;

	Zeno_GetInputDefl(hGraph, curNode, "USDDescription", usdPath, type);
	mUSDPath = std::get<std::string>(usdPath);

	Zeno_GetInputDefl(hGraph, curNode, "primPath", primPath, type);
	mPrimPath = std::get<std::string>(primPath);
}

void EvalUSDPrim::_onEvalFinished() {
	mUSDPath = "";
	mPrimPath = "";
	;
}

void EvalUSDPrim::_onEvalClicked() {
	_getNodeInputs();
	if (mUSDPath.empty() || mPrimPath.empty()) {
		std::cout << "[EvalUSDPrim] [Warn] found USD path or prim path is empty" << std::endl;
		return;
	}

	auto stage = pxr::UsdStage::Open(mUSDPath);
	if (!stage) {
		std::cout << "failed to load usd stage " << mUSDPath << std::endl;
		return;
	}

	mUSDPrim = stage->GetPrimAtPath(pxr::SdfPath(mPrimPath));
	if (!mUSDPrim.IsValid()) {
		std::cout << "failed to load usd prim at " << mPrimPath << std::endl;
		return;
	}

	std::string primType = "";
	primType = mUSDPrim.GetTypeName();
	if (primType.empty()) {
		std::cout << "failed to read prim type of " << mPrimPath << std::endl;
		return;
	}

	auto mainGraph = Zeno_GetGraph("main");

	ZENO_HANDLE primNode = 0;
	if (primType == "Sphere") {
		primNode = _emitCreateSphereNode(mainGraph);
	} else if (primType == "Capsule"){
		primNode = _emitCreateCapsuleNode(mainGraph);
	} else if (primType == "Cube"){
		primNode = _emitCreateCubeNode(mainGraph);
	} else if (primType == "Cylinder"){
		primNode = _emitCreateCylinderNode(mainGraph);
	} else if (primType == "Cone"){
		primNode = _emitCreateConeNode(mainGraph);
	} else if (primType == "Plane"){
		primNode = _emitCreatePlaneNode(mainGraph);
	} else if (primType == "Mesh") {
		primNode = _emitImportUSDMeshNode(mainGraph);
	} else if (primType == "CylinderLight") {
		// create a geo node and a light node, then connect them
		auto geoNode = _emitCreateCylinderNode(mainGraph, true);
		auto lightNode = _emitLightNode(mainGraph, "Diffuse", "TriangleMesh");
		if (geoNode && lightNode) {
			Zeno_AddLink(mainGraph, geoNode, getOutNameOfGeoNode(mainGraph, geoNode), lightNode, "prim");
			Zeno_SetView(mainGraph, lightNode, true);
			primNode = lightNode;
		} else {
			zeno::log_warn("failed to create CreateCylinder or LightNode while evaling prim " + mPrimPath);
			primNode = geoNode + lightNode;
		}
	} else if (primType == "DiskLight") {
		/*
		* Details of DiskLight from USD doc:
		* Light emitted from one side of a circular disk.
		* The disk is centered in the XY plane and emits light along the -Z axis.
		*/
		auto geoNode = _emitCreateDiskNode(mainGraph, true);
		auto lightNode = _emitLightNode(mainGraph, "Diffuse", "TriangleMesh");
		if (geoNode && lightNode) {
			Zeno_AddLink(mainGraph, geoNode, getOutNameOfGeoNode(mainGraph, geoNode), lightNode, "prim");
			Zeno_SetView(mainGraph, lightNode, true);
			primNode = lightNode;
		} else {
			zeno::log_warn("failed to create CreateDisk or LightNode while evaling prim " + mPrimPath);
			primNode = geoNode + lightNode;
		}
	} else if (primType == "DomeLight") {
		// I think this type is not fully supported yet
		auto geoNode = _emitCreateSphereNode(mainGraph, true);
		auto lightNode = _emitLightNode(mainGraph, "Diffuse", "TriangleMesh");
		if (geoNode && lightNode) {
			Zeno_SetInputDefl(mainGraph, geoNode, "isFlipFace", true); // emit inside the sphere
			Zeno_AddLink(mainGraph, geoNode, "prim", lightNode, "prim");
			Zeno_SetView(mainGraph, lightNode, true);
			primNode = lightNode;
		} else {
			zeno::log_warn("failed to create CreateSphere or LightNode while evaling prim " + mPrimPath);
			primNode = geoNode + lightNode;
		}
	} else if (primType == "DistantLight") {
		// this type is not support yet
		zeno::log_warn("DistantLight is not supported by zeno yet.");
	} else if (primType == "RectLight") {
		/*
		* Description from USD doc about RectLight:
		* Light emitted from one side of a rectangle.
		* The rectangle is centered in the XY plane and emits light along the - Z axis.
		* The rectangle is 1 unit in length in the X and Y axis.
		* In the default position, a texture file's min coordinates should be at (+X, +Y) and max coordinates at (-X, -Y).
		*/
		// this type is not fully supported yet
		auto geoNode = _emitCreatePlaneNode(mainGraph, true);
		auto lightNode = _emitLightNode(mainGraph, "Diffuse", "TriangleMesh");
		if (geoNode && lightNode) {
			Zeno_AddLink(mainGraph, geoNode, "prim", lightNode, "prim");
			Zeno_SetView(mainGraph, lightNode, true);
			primNode = lightNode;
		} else {
			zeno::log_warn("failed to create CreatePlane or LightNode while evaling prim " + mPrimPath);
			primNode = geoNode + lightNode;
		}
	} else if (primType == "SphereLight") {
		// this type is not fully supported yet
		auto geoNode = _emitCreateSphereNode(mainGraph, true);
		auto lightNode = _emitLightNode(mainGraph, "Diffuse", "TriangleMesh");
		if (geoNode && lightNode) {
			Zeno_AddLink(mainGraph, geoNode, getOutNameOfGeoNode(mainGraph, geoNode), lightNode, "prim");
			Zeno_SetView(mainGraph, lightNode, true);
			primNode = lightNode;
		} else {
			zeno::log_warn("failed to create CreateSphere or LightNode while evaling prim " + mPrimPath);
			primNode = geoNode + lightNode;
		}
	} else if (primType == "GeometryLight") {
		;
	}

	_emitPrimitiveTransformNodes(mainGraph, primNode);

	zeno::log_info("USD prim evaling finished.");
}

ZENO_HANDLE EvalUSDPrim::_emitCreateSphereNode(ZENO_HANDLE targetGraph, bool isLightGeo) {
	double radius;
	if (isLightGeo) {
		pxr::UsdAttribute attr;
		pxr::VtValue val;
		mUSDPrim.GetAttribute(pxr::TfToken("light:shaderId")).Get(&val);
		if (val.Get<pxr::TfToken>().GetString() == "DomeLight") {
			attr = mUSDPrim.GetAttribute(pxr::TfToken("guideRadius"));
		} else { // SphereLight
			attr = mUSDPrim.GetAttribute(pxr::TfToken("inputs:radius"));
		}
		attr.Get(&val);
		radius = static_cast<double>(val.Get<float>());
	} else {
		auto radiusAttr = mUSDPrim.GetAttribute(pxr::TfToken("radius"));
		radiusAttr.Get(&radius);
	}

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "CreateSphere");
	if (newNode == 0) {
		zeno::log_error("failed to emit CreateSphere node");
		return 0;
	}
	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	Zeno_SetInputDefl(targetGraph, newNode, "radius", radius);
	Zeno_SetPos(targetGraph, newNode, nodePos);
	Zeno_SetView(targetGraph, newNode, true);

	return newNode;
}

// TODO: we need a CreateCapsule node
ZENO_HANDLE EvalUSDPrim::_emitCreateCapsuleNode(ZENO_HANDLE targetGraph){
	pxr::UsdAttribute attr;
	pxr::VtValue attrValue;
	char axis;
	double radius;
	double height;
	attr = mUSDPrim.GetAttribute(pxr::TfToken("axis"));
	if (!attr.HasValue()) return 0;
	attr.Get(&attrValue);
	axis = attrValue.Get<pxr::TfToken>().GetString()[0];

	attr = mUSDPrim.GetAttribute(pxr::TfToken("radius"));
	attr.Get(&radius);

	attr = mUSDPrim.GetAttribute(pxr::TfToken("height"));
	attr.Get(&height);

	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "CreateCapsule");
	if (newNode == 0) {
		zeno::log_error("failed to emit CreateCapsule node");
		return 0;
	}

	Zeno_SetInputDefl(targetGraph, newNode, "radius", radius);
	Zeno_SetInputDefl(targetGraph, newNode, "height", height);
	Zeno_SetPos(targetGraph, newNode, nodePos);
	Zeno_SetView(targetGraph, newNode, true);

	if (axis != 'Y') {
		auto transNode = Zeno_AddNode(targetGraph, "PrimitiveTransform");
		if (transNode == 0) {
			std::cout << "failed to create PrimitiveTransform for USD capsule" << std::endl;
		} else {
			Zeno_AddLink(targetGraph, newNode, "prim", transNode, "prim");
			if (axis == 'X') {
				Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(0.0f, 0.0f, -90.0f));
			} else if (axis == 'Z') {
				Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(90.0f, 0.0f, 0.0f));
			} else {} // ??
			Zeno_SetPos(targetGraph, transNode, { nodePos.first + 50.0f, nodePos.second });
			Zeno_SetView(targetGraph, newNode, false);
			Zeno_SetView(targetGraph, transNode, true);
			return transNode;
		}
	}

	return newNode;
}

ZENO_HANDLE EvalUSDPrim::_emitCreateCubeNode(ZENO_HANDLE targetGraph){
	pxr::UsdAttribute attr = mUSDPrim.GetAttribute(pxr::TfToken("size"));
	double size;
	attr.Get(&size);

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "CreateCube");
	if (newNode == 0) {
		zeno::log_error("failed to emit CreateCube node");
		return 0;
	}

	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);
	Zeno_SetInputDefl(targetGraph, newNode, "size", size);
	Zeno_SetPos(targetGraph, newNode, nodePos);
	Zeno_SetView(targetGraph, newNode, true);

	return newNode;
}

ZENO_HANDLE EvalUSDPrim::_emitCreateCylinderNode(ZENO_HANDLE targetGraph, bool isLightGeo){
	char axis;
	double radius;
	double height;
	pxr::UsdAttribute attr;
	pxr::VtValue attrValue;
	if (isLightGeo) {
		axis = 'X';
	} else {
		attr = mUSDPrim.GetAttribute(pxr::TfToken("axis"));
		if (!attr.HasValue()) return 0;
		attr.Get(&attrValue);
		axis = attrValue.Get<pxr::TfToken>().GetString()[0];
	}

	if (isLightGeo) {
		attr = mUSDPrim.GetAttribute(pxr::TfToken("inputs:radius")); // CylinderLight
		attr.Get(&attrValue);
		radius = static_cast<double>(attrValue.Get<float>());
	} else {
		attr = mUSDPrim.GetAttribute(pxr::TfToken("radius"));
		attr.Get(&radius);
	}

	if (isLightGeo) {
		attr = mUSDPrim.GetAttribute(pxr::TfToken("inputs:length")); // in CylinderLight, height is called 'length' :)
		attr.Get(&attrValue);
		height = static_cast<double>(attrValue.Get<float>());
	} else {
		attr = mUSDPrim.GetAttribute(pxr::TfToken("height"));
		attr.Get(&height);
	}

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "CreateCylinder");
	if (newNode == 0) {
		zeno::log_error("failed to emit CreateCylinder node");
		return 0;
	}
	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	Zeno_SetInputDefl(targetGraph, newNode, "radius", radius);
	Zeno_SetInputDefl(targetGraph, newNode, "height", height);
	Zeno_SetPos(targetGraph, newNode, nodePos);
	Zeno_SetView(targetGraph, newNode, true);

	if (axis != 'Y') {
		auto transNode = Zeno_AddNode(targetGraph, "PrimitiveTransform");
		if (transNode == 0) {
			std::cout << "failed to create PrimitiveTransform for USD Cylinder" << std::endl;
			return newNode;
		}

		Zeno_AddLink(targetGraph, newNode, "prim", transNode, "prim");
		if (axis == 'X') {
			Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(0.0f, 0.0f, -90.0f));
		} else if (axis == 'Z') {
			Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(90.0f, 0.0f, 0.0f));
		} else {} // ??
		Zeno_SetPos(targetGraph, transNode, { nodePos.first + 50.0f, nodePos.second });
		Zeno_SetView(targetGraph, newNode, false);
		Zeno_SetView(targetGraph, transNode, true);
		return transNode;
	}

	return newNode;
}

ZENO_HANDLE EvalUSDPrim::_emitCreateConeNode(ZENO_HANDLE targetGraph){
	char axis;
	double radius;
	double height;
	pxr::UsdAttribute attr;
	pxr::VtValue attrValue;
	attr = mUSDPrim.GetAttribute(pxr::TfToken("axis"));
	if (!attr.HasValue()) return 0;
	attr.Get(&attrValue);
	axis = attrValue.Get<pxr::TfToken>().GetString()[0];

	attr = mUSDPrim.GetAttribute(pxr::TfToken("radius"));
	attr.Get(&radius);

	attr = mUSDPrim.GetAttribute(pxr::TfToken("height"));
	attr.Get(&height);

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "CreateCone");
	if (newNode == 0) {
		zeno::log_error("failed to emit CreateCone node");
		return 0;
	}

	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	Zeno_SetInputDefl(targetGraph, newNode, "radius", radius);
	Zeno_SetInputDefl(targetGraph, newNode, "height", height);
	Zeno_SetPos(targetGraph, newNode, nodePos);
	Zeno_SetView(targetGraph, newNode, true);

	if (axis != 'Y') {
		auto transNode = Zeno_AddNode(targetGraph, "PrimitiveTransform");
		if (transNode == 0) {
			std::cout << "failed to create PrimitiveTransform for USD cone" << std::endl;
		} else {
			Zeno_AddLink(targetGraph, newNode, "prim", transNode, "prim");
			if (axis == 'X') {
				Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(0.0f, 0.0f, -90.0f));
			} else if (axis == 'Z') {
				Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(90.0f, 0.0f, 0.0f));
			} else {} // ??
			Zeno_SetPos(targetGraph, transNode, { nodePos.first + 50.0f, nodePos.second });
			Zeno_SetView(targetGraph, newNode, false);
			Zeno_SetView(targetGraph, transNode, true);
			return transNode;
		}
	}

	return newNode;
}

ZENO_HANDLE EvalUSDPrim::_emitCreatePlaneNode(ZENO_HANDLE targetGraph, bool isLightGeo){
	char axis;
	double height, width;
	pxr::UsdAttribute attr;
	pxr::VtValue val;
	if (isLightGeo) { // RectLight
		axis = 'Z';

		attr = mUSDPrim.GetAttribute(pxr::TfToken("inputs:height"));
		attr.Get(&val);
		height = static_cast<double>(val.Get<float>());

		attr = mUSDPrim.GetAttribute(pxr::TfToken("inputs:width"));
		attr.Get(&val);
		width = static_cast<double>(val.Get<float>());
	} else { // Plane
		attr = mUSDPrim.GetAttribute(pxr::TfToken("axis"));
		if (!attr.HasValue()) return 0;
		attr.Get(&val);
		axis = val.Get<pxr::TfToken>().GetString()[0];

		attr = mUSDPrim.GetAttribute(pxr::TfToken("length"));
		attr.Get(&height);

		attr = mUSDPrim.GetAttribute(pxr::TfToken("width"));
		attr.Get(&width);
	}

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "CreatePlane");
	if (newNode == 0) {
		zeno::log_error("failed to emit CreatePlane node");
		return 0;
	}
	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	Zeno_SetInputDefl(targetGraph, newNode, "scaleSize", zeno::vec3f(width, 1.0f, height));
	// Yeah, we don't need to add the PrimitiveTransform node :)
	if (axis == 'X') {
		Zeno_SetInputDefl(targetGraph, newNode, "rotate", zeno::vec3f(90.0f, 0.0f, 0.0f));
	} else if (axis == 'Z') {
		Zeno_SetInputDefl(targetGraph, newNode, "rotate", isLightGeo ? zeno::vec3f(-90.0f, 180.0f, 0.0f) : zeno::vec3f(0.0f, 0.0f, 90.0f));
	}

	Zeno_SetInputDefl(targetGraph, newNode, "size", 1.0f);
	Zeno_SetPos(targetGraph, newNode, nodePos);
	Zeno_SetView(targetGraph, newNode, true);

	return newNode;
}

ZENO_HANDLE EvalUSDPrim::_emitCreateDiskNode(ZENO_HANDLE targetGraph, bool isLightGeo) {
	char axis = 'Z';
	if (isLightGeo) {
		axis = 'Z'; // disk light emit towards -Z
	} else {
		; // TODO
	}

	float radius;
	if (isLightGeo) {
		mUSDPrim.GetAttribute(pxr::TfToken("inputs:radius")).Get(&radius);
	} else {
		radius = 0.0f; // TODO
	}

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "CreateDisk");
	if (newNode == 0) {
		zeno::log_error("failed to emit CreateDisk node");
		return 0;
	}
	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	Zeno_SetInputDefl(targetGraph, newNode, "radius", radius);
	Zeno_SetPos(targetGraph, newNode, nodePos);

	if (axis != 'Y') {
		auto transNode = Zeno_AddNode(targetGraph, "PrimitiveTransform");
		if (!transNode) {
			zeno::log_error("failed to create node: PrimitiveTransform");
		} else {
			Zeno_AddLink(targetGraph, newNode, "prim", transNode, "prim");
			if (axis == 'X') {
				Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(0.0f, 0.0f, isLightGeo ? 90.0f : -90.0f));
			} else if (axis == 'Z') {
				Zeno_SetInputDefl(targetGraph, transNode, "eulerXYZ", zeno::vec3f(isLightGeo ? -90.0f : 90.0f, 0.0f, 0.0f));
			} else {} // ??
			Zeno_SetPos(targetGraph, transNode, { nodePos.first + 50.0f, nodePos.second });
			Zeno_SetView(targetGraph, newNode, false);
			Zeno_SetView(targetGraph, transNode, true);
			return transNode;
		}
	}
	return newNode;
}

ZENO_HANDLE EvalUSDPrim::_emitImportUSDMeshNode(ZENO_HANDLE targetGraph) {
	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "ImportUSDMesh");
	if (newNode == 0) {
		zeno::log_error("failed to emit ImportUSDMesh node");
		return 0;
	}
	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	Zeno_SetInputDefl(targetGraph, newNode, "USDDescription", mUSDPath);
	Zeno_SetInputDefl(targetGraph, newNode, "primPath", mPrimPath);
	Zeno_SetPos(targetGraph, newNode, nodePos);
	Zeno_SetView(targetGraph, newNode, true);

	return newNode;
}

ZENO_HANDLE EvalUSDPrim::_emitLightNode(ZENO_HANDLE targetGraph, const std::string& lightType, const std::string& shapeType) {
	// read parameter from USD light prim
	auto light = pxr::UsdLuxLightAPI(mUSDPrim);
	float exposure;
	float intensity;
	pxr::UsdAttribute attr;
	pxr::VtValue attrValue;

	attr = light.GetIntensityAttr();
	attr.Get(&intensity);
	attr = light.GetExposureAttr();
	attr.Get(&exposure);

	ZENO_HANDLE newNode = Zeno_AddNode(targetGraph, "LightNode");
	if (newNode == 0) {
		zeno::log_warn("failed to create node: LightNode");
		return 0;
	}
	ZENO_HANDLE curNode = index().internalId();
	std::pair<float, float> nodePos;
	Zeno_GetPos(targetGraph, curNode, nodePos);

	// deal with light color
	bool enableTemperature = false;
	light.GetEnableColorTemperatureAttr().Get<bool>(&enableTemperature);
	if (enableTemperature) {
		float temp;
		attr = light.GetColorTemperatureAttr();
		attr.Get(&temp);

		auto tempNode = Zeno_AddNode(targetGraph, "ColorTemperatureToRGB");
		if (!tempNode) {
			zeno::log_warn("failed to create node: ColorTemperatureToRGB");
			return 0;
		}

		Zeno_SetInputDefl(targetGraph, tempNode, "temerature", temp);
		Zeno_SetPos(targetGraph, tempNode, {nodePos.first - 100.0f, nodePos.second});
		Zeno_AddLink(targetGraph, tempNode, "color", newNode, "color");
	} else {
		attr = light.GetColorAttr();
		pxr::GfVec3f _col;
		attr.Get(&_col);
		zeno::vec3f color = { _col[0], _col[1], _col[2] };
		Zeno_SetInputDefl(targetGraph, newNode, "color", color);
	}

	// final setup
	Zeno_SetInputDefl(targetGraph, newNode, "intensity", intensity);
	Zeno_SetInputDefl(targetGraph, newNode, "exposure", exposure);
	/*
	* shape: Plane Ellipse Sphere Point TriangleMesh
	* type: Diffuse Direction IES Spot Projector
	*/
	Zeno_SetInputDefl(targetGraph, newNode, "type", lightType);
	Zeno_SetInputDefl(targetGraph, newNode, "shape", shapeType);

	Zeno_SetPos(targetGraph, newNode, nodePos);

	return newNode;
}

// return root node of the entire transform link
void EvalUSDPrim::_emitPrimitiveTransformNodes(ZENO_HANDLE targetGraph, ZENO_HANDLE lastNode) {
	auto xform = pxr::UsdGeomXform(mUSDPrim);
	bool resetXformStack;
	auto xformOps = xform.GetOrderedXformOps(&resetXformStack);
	ZENO_HANDLE curNode = index().internalId();

	// get next generate position
	std::pair<float, float> nodePos;
	if (lastNode != 0) {
		Zeno_GetPos(targetGraph, lastNode, nodePos);
	} else {
		Zeno_GetPos(targetGraph, curNode, nodePos);
	}
	nodePos.first += 100.0f;

	// traverse and parse xformOps into PrimitiveTransform nodes
	for (auto& op: xformOps) {
		auto opType = op.GetOpType();
		if (opType == pxr::UsdGeomXformOp::TypeInvalid) {
			std::cout << "[EvalUSDPrim][Warn] found invalid op type while evaling xform ops" << std::endl;
			continue;
		}

		auto precision = op.GetPrecision();
		pxr::VtValue vecValue;
		ZENO_HANDLE newNode = 0;
		ZVARIANT finalVec;

		op.Get(&vecValue);
		if (opType == pxr::UsdGeomXformOp::TypeTransform) {
			continue; // we don't support matrix node yet, so skip it
		} else if (opType == pxr::UsdGeomXformOp::TypeTranslate) {
			finalVec = _parseVector3(precision, vecValue);
		} else if (opType == pxr::UsdGeomXformOp::TypeOrient) {
			finalVec = _parseQuatVector(precision, vecValue);
		} else if (opType == pxr::UsdGeomXformOp::TypeScale){
			finalVec = _parseVector3(precision, vecValue);
		} else if (opType >= pxr::UsdGeomXformOp::TypeRotateXYZ && opType <= pxr::UsdGeomXformOp::TypeRotateZYX) {
			finalVec = _parseVector3(precision, vecValue);
		} else if (opType >= pxr::UsdGeomXformOp::TypeRotateX && opType <= pxr::UsdGeomXformOp::TypeRotateZ) {
			finalVec = _parseScalar(precision, vecValue);
			float euler = std::get<float>(finalVec);
			if (opType == pxr::UsdGeomXformOp::TypeRotateX) {
				finalVec = zeno::vec3f(euler, 0.0f, 0.0f);
			} else if (opType == pxr::UsdGeomXformOp::TypeRotateY) {
				finalVec = zeno::vec3f(0.0f, euler, 0.0f);
			} else { // Z axis
				finalVec = zeno::vec3f(0.0f, 0.0f, euler);
			}
		}

		newNode = _makeTransformNode(targetGraph, opType, finalVec);
		if (newNode == 0) {
			std::cout << "[EvalUSDPrim][_emitPrimitiveTransformNodes] failed to eval transform" << std::endl;
			continue;
		}

		if (lastNode != 0) {
			Zeno_AddLink(targetGraph, lastNode, getOutNameOfGeoNode(targetGraph, lastNode), newNode, "prim");
		}
		lastNode = newNode;

		Zeno_SetPos(targetGraph, newNode, nodePos);
		Zeno_SetView(targetGraph, newNode, true);
		nodePos.first += 100.0f;
	}
}

float EvalUSDPrim::_parseScalar(pxr::UsdGeomXformOp::Precision precision, const pxr::VtValue& value) {
	float ret;
	if (precision == pxr::UsdGeomXformOp::PrecisionDouble) {
		ret = static_cast<float>(value.Get<double>());
	} else if (precision == pxr::UsdGeomXformOp::PrecisionFloat) {
		ret = value.Get<float>();
	} else {
		pxr::GfHalf v = value.Get<pxr::GfHalf>();
		ret = float(v);
	}
	return ret;
}

zeno::vec3f EvalUSDPrim::_parseVector3(pxr::UsdGeomXformOp::Precision precision, const pxr::VtValue& vecValue) {
	zeno::vec3f ret;
	if (precision == pxr::UsdGeomXformOp::PrecisionDouble) {
		pxr::GfVec3d vec = vecValue.Get<pxr::GfVec3d>();
		ret = zeno::vec3f{ (float)vec[0], (float)vec[1], (float)vec[2] };
	} else if (precision == pxr::UsdGeomXformOp::PrecisionFloat) {
		pxr::GfVec3f vec = vecValue.Get<pxr::GfVec3f>();
		ret = { vec[0], vec[1], vec[2] };
	} else {
		pxr::GfVec3h vec = vecValue.Get<pxr::GfVec3h>();
		ret = { (float)vec[0], (float)vec[1], (float)vec[2] };
	}
	return ret;
}

zeno::vec4f EvalUSDPrim::_parseQuatVector(pxr::UsdGeomXformOp::Precision precision, const pxr::VtValue& vecValue) {
	zeno::vec4f ret;
	if (precision == pxr::UsdGeomXformOp::PrecisionDouble) {
		pxr::GfQuatd quad = vecValue.Get<pxr::GfQuatd>();
		pxr::GfVec3d vec = quad.GetImaginary();
		ret = { float(vec[0]), float(vec[1]), float(vec[2]), float(quad.GetReal()) };
	} else if (precision == pxr::UsdGeomXformOp::PrecisionFloat) {
		pxr::GfQuatf quad = vecValue.Get<pxr::GfQuatf>();
		pxr::GfVec3f vec = quad.GetImaginary();
		ret = { vec[0], vec[1], vec[2], quad.GetReal()};
	} else {
		pxr::GfQuath quad = vecValue.Get<pxr::GfQuath>();
		pxr::GfVec3h vec = quad.GetImaginary();
		ret = { float(vec[0]), float(vec[1]), float(vec[2]), float(quad.GetReal()) };
	}
	return ret;
}

ZENO_HANDLE EvalUSDPrim::_makeTransformNode(ZENO_HANDLE main, const pxr::UsdGeomXformOp::Type& transType, const ZVARIANT& transVec) {
	ZENO_HANDLE newNode = Zeno_AddNode(main, "PrimitiveTransform"); 
	if (newNode == 0) {
		std::cout << "failed to create node named PrimitiveTransform" << std::endl;
		return 0;
	}

	if (transType == pxr::UsdGeomXformOp::TypeTranslate) {
		Zeno_SetInputDefl(main, newNode, "translation", std::get<zeno::vec3f>(transVec));
	} else if (transType == pxr::UsdGeomXformOp::TypeScale) {
		Zeno_SetInputDefl(main, newNode, "scaling", std::get<zeno::vec3f>(transVec));
	} else if (transType >= pxr::UsdGeomXformOp::TypeRotateX && transType <= pxr::UsdGeomXformOp::TypeRotateZ) {
		// only need to rotate one axis, so we don't care rotation order here
		Zeno_SetInputDefl(main, newNode, "eulerXYZ", std::get<zeno::vec3f>(transVec));
	} else if (transType == pxr::UsdGeomXformOp::TypeRotateXYZ) {
		Zeno_SetInputDefl(main, newNode, "eulerXYZ", std::get<zeno::vec3f>(transVec));
		Zeno_SetParam(main, newNode, "EulerRotationOrder", std::string("XYZ"));
	} else if (transType == pxr::UsdGeomXformOp::TypeRotateXZY) {
		Zeno_SetInputDefl(main, newNode, "eulerXYZ", std::get<zeno::vec3f>(transVec));
		Zeno_SetParam(main, newNode, "EulerRotationOrder", std::string("XZY"));
	} else if (transType == pxr::UsdGeomXformOp::TypeRotateYXZ) {
		Zeno_SetInputDefl(main, newNode, "eulerXYZ", std::get<zeno::vec3f>(transVec));
		Zeno_SetParam(main, newNode, "EulerRotationOrder", std::string("YXZ"));
	} else if (transType == pxr::UsdGeomXformOp::TypeRotateYZX) {
		Zeno_SetInputDefl(main, newNode, "eulerXYZ", std::get<zeno::vec3f>(transVec));
		Zeno_SetParam(main, newNode, "EulerRotationOrder", std::string("YZX"));
	} else if (transType == pxr::UsdGeomXformOp::TypeRotateZXY) {
		Zeno_SetInputDefl(main, newNode, "eulerXYZ", std::get<zeno::vec3f>(transVec));
		Zeno_SetParam(main, newNode, "EulerRotationOrder", std::string("ZXY"));
	} else if (transType == pxr::UsdGeomXformOp::TypeRotateZYX) {
		Zeno_SetInputDefl(main, newNode, "eulerXYZ", std::get<zeno::vec3f>(transVec));
		Zeno_SetParam(main, newNode, "EulerRotationOrder", std::string("ZYX"));
	} else if (transType == pxr::UsdGeomXformOp::TypeOrient) {
		Zeno_SetInputDefl(main, newNode, "quatRotation", std::get<zeno::vec4f>(transVec));
	} else if (transType == pxr::UsdGeomXformOp::TypeTransform) {
		; // ?? We don't support a single matrix node here, so...
	} else {
		Zeno_DeleteNode(main, newNode);
		return 0;
	}

	return newNode;
}
