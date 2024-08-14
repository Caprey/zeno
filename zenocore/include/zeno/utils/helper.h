#ifndef __HELPER_H__
#define __HELPER_H__

#include <rapidjson/document.h>
#include <zeno/core/data.h>
#include <zeno/utils/string.h>
#include <zeno/core/IObject.h>
#include <zeno/types/StringObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/utils/log.h>
#include <zeno/core/CoreParam.h>
#include "zeno_types/reflect/reflection.generated.hpp"


namespace zeno {

    ZENO_API ParamType convertToType(std::string const& type, const std::string_view& param_name = "");
    ZENO_API bool isAnyEqual(const zeno::reflect::Any& lhs, const zeno::reflect::Any& rhs);
    ZENO_API std::string paramTypeToString(ParamType type);
    ZENO_API zvariant str2var(std::string const& defl, ParamType const& type);
    ZENO_API zeno::reflect::Any str2any(std::string const& defl, ParamType const& type);
    ZENO_API zvariant initDeflValue(ParamType const& type);
    ZENO_API zeno::reflect::Any initAnyDeflValue(ParamType const& type);
    ZENO_API zvariant AnyToZVariant(zeno::reflect::Any const& var);
    ZENO_API std::string getControlDesc(zeno::ParamControl ctrl, zeno::ParamType type);
    ZENO_API zeno::ParamControl getDefaultControl(const zeno::ParamType type);
    bool isEqual(const zvariant& lhs, const zvariant& rhs, ParamType const type);
    zany strToZAny(std::string const& defl, ParamType const& type);
    EdgeInfo getEdgeInfo(std::shared_ptr<ObjectLink> spLink);
    EdgeInfo getEdgeInfo(std::shared_ptr<PrimitiveLink> spLink);
    std::string generateObjKey(std::shared_ptr<IObject> spObject);
    ZENO_API std::string objPathToStr(ObjPath path);
    ObjPath strToObjPath(const std::string& str);
    bool getParamInfo(const CustomUI& customui, std::vector<ParamPrimitive>& inputs, std::vector<ParamPrimitive>& outputs);
    bool isPrimitiveType(const ParamType type);
    ZENO_API PrimitiveParams customUiToParams(const CustomUIParams& customparams);
    ZENO_API void parseUpdateInfo(const CustomUI& customui, ParamsUpdateInfo& infos);
    void initControlsByType(CustomUI& ui);
    std::string absolutePath(std::string currentPath, const std::string& path);
    std::string relativePath(std::string currentPath, const std::string& path);
    std::set<std::string> getReferPath(const std::string& path);
    std::set<std::string> getReferPaths(const zvariant& val);

    bool isObjectType(const zeno::reflect::RTTITypeInfo& type, bool& isConstPtr);
    bool isObjectType(ParamType type);
    bool isNumericType(zeno::ParamType type);
    bool isNumericVecType(zeno::ParamType type);
    bool isSameDimensionNumericVecType(zeno::ParamType left, zeno::ParamType right);
    ZENO_API bool outParamTypeCanConvertInParamType(zeno::ParamType outType, zeno::ParamType inType);
}


#endif