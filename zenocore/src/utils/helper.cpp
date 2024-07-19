#include <zeno/utils/helper.h>
#include <regex>
#include <zeno/core/CoreParam.h>
#include <zeno/core/INode.h>


namespace zeno {

    ZENO_API ParamType convertToType(std::string const& type) {
        //TODO: deprecated literal representation.
        if (type == "string" || type == "readpath" || type == "writepath" || type == "diratory") { return Param_String; }
        else if (type == "bool") { return Param_Bool; }
        else if (type == "int") { return Param_Int; }
        else if (type == "float") { return Param_Float; }
        else if (type == "NumericObject") { return Param_Float; }
        else if (type == "vec2i") { return Param_Vec2i; }
        else if (type == "vec3i") { return Param_Vec3i; }
        else if (type == "vec4i") { return Param_Vec4i; }
        else if (type == "vec2f") { return Param_Vec2f; }
        else if (type == "vec3f") { return Param_Vec3f; }
        else if (type == "vec4f") { return Param_Vec4f; }
        else if (type == "prim") { return Param_Prim; }
        else if (type == "list") { return Param_List; }
        else if (type == "dict" || type == "DictObject" || type == "DictObject:NumericObject") { return Param_Dict; }
        else if (type == "colorvec3f") { return Param_Vec3f; }
        else if (type == "color") { return Param_Heatmap; }
        else if (type == "curve") { return Param_Curve; }
        else if (starts_with(type, "enum ")) { return Param_String; }
        else return Param_Null;
    }

    ZENO_API std::string paramTypeToString(ParamType type)
    {
        switch (type)
        {
        case Param_Null:    return "";
        case Param_Bool:    return "bool";
        case Param_Int:     return "int";
        case Param_String:  return "string";
        case Param_Float:   return "float";
        case Param_Vec2i:   return "vec2i";
        case Param_Vec3i:   return "vec3i";
        case Param_Vec4i:   return "vec4i";
        case Param_Vec2f:   return "vec2f";
        case Param_Vec3f:   return "vec3f";
        case Param_Vec4f:   return "vec4f";
        case Param_Prim:    return "prim";
        case Param_Dict:    return "dict";
        case Param_List:    return "list";
        case Param_Curve:   return "curve";
        case Param_Heatmap: return "color";
        case Param_SrcDst:  return "";
        default:
            return "";
        }
    }

    ZENO_API zvariant str2var(std::string const& defl, ParamType const& type) {
        if (defl.empty())
            return initDeflValue(type);
        switch (type) {
        case Param_String: {
            return defl;
        }
        case Param_Bool: {
            if (defl == "0" || defl == "false")    return 0;
            if (defl == "1" || defl == "true")     return 1;
            return zvariant();
        }
        case Param_Int: {
            return std::stoi(defl);
        }
        case Param_Float: {
            return std::stof(defl);
        }
        case Param_Vec2i:
        case Param_Vec3i:
        case Param_Vec4i:
        {
            std::vector<int> vec;
            for (auto v : split_str(defl, ',')) {
                vec.push_back(std::stoi(v));
            }
            if (Param_Vec2i == type) {
                return vec2i(vec[0], vec[1]);
            }
            else if (Param_Vec3i == type) {
                return vec3i(vec[0], vec[1], vec[2]);
            }
            else {
                return vec4i(vec[0], vec[1], vec[2], vec[3]);
            }
            return zvariant();
        }
        case Param_Vec2f:
        case Param_Vec3f:
        case Param_Vec4f:
        {
            std::vector<float> vec;
            for (auto v : split_str(defl, ',')) {
                vec.push_back(std::stof(v));
            }

            if (Param_Vec2f == type) {
                if (vec.size() != 2)
                    return vec2f();
                return vec2f(vec[0], vec[1]);
            }
            else if (Param_Vec3f == type) {
                if (vec.size() != 3)
                    return vec3f();
                return vec3f(vec[0], vec[1], vec[2]);
            }
            else {
                if (vec.size() != 4)
                    return vec4f();
                return vec4f(vec[0], vec[1], vec[2], vec[3]);
            }
            return zvariant();
        }
        default:
            return defl;
        }
    }

    zvariant zeno::initDeflValue(ParamType const& type)
    {
        if (type == zeno::Param_String) {
            return "";
        }
        else if (type == zeno::Param_Float)
        {
            return (float)0.;
        }
        else if (type == zeno::Param_Int)
        {
            return (int)0;
        }
        else if (type == zeno::Param_Bool)
        {
            return false;
        }
        else if (type == zeno::Param_Vec2i)
        {
            return vec2i();
        }
        else if (type == zeno::Param_Vec2f)
        {
            return vec2f();
        }
        else if (type == zeno::Param_Vec3i)
        {
            return vec3i();
        }
        else if (type == zeno::Param_Vec3f)
        {
            return vec3f();
        }
        else if (type == zeno::Param_Vec4i)
        {
            return vec4i();
        }
        else if (type == zeno::Param_Vec4f)
        {
            return vec4f();
        }
        else if (type == zeno::Param_Curve)
        {
            return "{}";
        }
        else if (type == zeno::Param_Heatmap)
        {
            return "{\"nres\":1024, \"color\":\"\"}";
        }
        return zvariant();
    }

    EdgeInfo getEdgeInfo(std::shared_ptr<ObjectLink> spLink) {
        EdgeInfo edge;
        auto spOutParam = spLink->fromparam;
        auto spInParam = spLink->toparam;
        if (!spOutParam || !spInParam)
            return edge;

        auto spOutNode = spOutParam->m_wpNode.lock();
        auto spInNode = spInParam->m_wpNode.lock();
        if (!spOutNode || !spInNode)
            return edge;

        const std::string& outNode = spOutNode->get_name();
        const std::string& outParam = spOutParam->name;
        const std::string& inNode = spInNode->get_name();
        const std::string& inParam = spInParam->name;
        edge = { outNode, outParam, spLink->fromkey, inNode, inParam, spLink->tokey, true };
        return edge;
    }

    EdgeInfo getEdgeInfo(std::shared_ptr<PrimitiveLink> spLink) {
        EdgeInfo edge;
        auto spOutParam = spLink->fromparam;
        auto spInParam = spLink->toparam;
        if (!spOutParam || !spInParam)
            return edge;

        auto spOutNode = spOutParam->m_wpNode.lock();
        auto spInNode = spInParam->m_wpNode.lock();
        if (!spOutNode || !spInNode)
            return edge;

        const std::string& outNode = spOutNode->get_name();
        const std::string& outParam = spOutParam->name;
        const std::string& inNode = spInNode->get_name();
        const std::string& inParam = spInParam->name;
        edge = { outNode, outParam, "", inNode, inParam, "", false};
        return edge;
    }

    std::string generateObjKey(std::shared_ptr<IObject> spObject) {
        return "";    //TODO
    }

    ZENO_API std::string objPathToStr(ObjPath path) {
        return path;
    }

    ObjPath zeno::strToObjPath(const std::string& str)
    {
        return str;
    }

    PrimitiveParams customUiToParams(const CustomUIParams& customparams) {
        PrimitiveParams params;
        for (auto tab : customparams.tabs) {
            for (auto group : tab.groups) {
                params.insert(params.end(), group.params.begin(), group.params.end());
            }
        }
        return params;
    }

    CustomUI descToCustomui(const Descriptor& desc) {
        //������ǰд�ĸ���ZENDEFINE
        CustomUI ui;

        ui.nickname = desc.displayName;
        ui.iconResPath = desc.iconResPath;
        ui.doc = desc.doc;
        if (!desc.categories.empty())
            ui.category = desc.categories[0];   //�ܶ�cate��ֻ��һ��

        ParamGroup default;
        for (const SocketDescriptor& param_desc : desc.inputs) {
            ParamType type = zeno::convertToType(param_desc.type);
            if (isPrimitiveType(type)) {
                //�������ֵ���ͣ�����ӵ�����
                ParamPrimitive param;
                param.name = param_desc.name;
                param.type = type;
                param.defl = zeno::str2var(param_desc.defl, param.type);
                if (param_desc.socketType != zeno::NoSocket)
                    param.socketType = param_desc.socketType;
                if (param_desc.control != NullControl)
                    param.control = param_desc.control;
                if (starts_with(param_desc.type, "enum ")) {
                    //compatible with old case of combobox items.
                    param.type = Param_String;
                    param.control = Combobox;
                    std::vector<std::string> items = split_str(param_desc.type, ' ');
                    if (!items.empty()) {
                        items.erase(items.begin());
                        ControlProperty props = ControlProperty();
                        props.items = items;
                        param.ctrlProps = props;
                    }
                }
                if (param.type != Param_Null && param.control == NullControl)
                    param.control = getDefaultControl(param.type);
                param.tooltip = param_desc.doc;
                param.prop = Socket_Normal;
                param.wildCardGroup = param_desc.wildCard;
                default.params.emplace_back(std::move(param));
            }
            else
            {
                //����һ����Ϊ�Ƕ���ZenoĿǰ�����͹���ǳ����ң���Щ����ֵ�ǿ��ַ���������������Ƕ�������
                ParamObject param;
                param.name = param_desc.name;
                param.type = type;
                if (param_desc.socketType != zeno::NoSocket)
                    param.socketType = param_desc.socketType;
                param.bInput = true;
                param.wildCardGroup = param_desc.wildCard;
                ui.inputObjs.emplace_back(std::move(param));
            }
        }
        for (const ParamDescriptor& param_desc : desc.params) {
            ParamPrimitive param;
            param.name = param_desc.name;
            param.type = zeno::convertToType(param_desc.type);
            param.defl = zeno::str2var(param_desc.defl, param.type);
            param.socketType = NoSocket;
            //�����ؼ������Ǹ��������ƶϵġ�
            if (starts_with(param_desc.type, "enum ")) {
                //compatible with old case of combobox items.
                param.type = Param_String;
                param.control = Combobox;
                std::vector<std::string> items = split_str(param_desc.type, ' ');
                if (!items.empty()) {
                    items.erase(items.begin());
                    ControlProperty props = ControlProperty();
                    props.items = items;
                    param.ctrlProps = props;
                }
            }
            if (param.type != Param_Null && param.control == NullControl)
                param.control = getDefaultControl(param.type);
            param.tooltip = param_desc.doc;
            default.params.emplace_back(std::move(param));
        }
        for (const SocketDescriptor& param_desc : desc.outputs) {
            ParamType type = zeno::convertToType(param_desc.type);
            if (isPrimitiveType(type)) {
                //�������ֵ���ͣ�����ӵ�����
                ParamPrimitive param;
                param.name = param_desc.name;
                param.type = type;
                param.defl = zeno::str2var(param_desc.defl, param.type);
                if (param_desc.socketType != zeno::NoSocket)
                    param.socketType = param_desc.socketType;
                param.control = NullControl;
                param.tooltip = param_desc.doc;
                param.prop = Socket_Normal;
                param.wildCardGroup = param_desc.wildCard;
                ui.outputPrims.emplace_back(std::move(param));
            }
            else
            {
                //����һ����Ϊ�Ƕ���ZenoĿǰ�����͹���ǳ����ң���Щ����ֵ�ǿ��ַ���������������Ƕ�������
                ParamObject param;
                param.name = param_desc.name;
                param.type = type;
                if (param_desc.socketType != zeno::NoSocket)
                    param.socketType = param_desc.socketType;
                param.bInput = true;
                param.prop = Socket_Normal;
                param.wildCardGroup = param_desc.wildCard;
                ui.outputObjs.emplace_back(std::move(param));
            }
        }
        ParamTab tab;
        tab.groups.emplace_back(std::move(default));
        ui.inputPrims.tabs.emplace_back(std::move(tab));
        return ui;
    }

    void initControlsByType(CustomUI& ui) {
        for (ParamTab& tab : ui.inputPrims.tabs)
        {
            for (ParamGroup& group : tab.groups)
            {
                for (ParamPrimitive& param : group.params)
                {
                    if (param.type != Param_Null && param.control == NullControl)
                        param.control = getDefaultControl(param.type);
                }
            }
        }
    }

    std::set<std::string> zeno::getReferPaths(const zvariant& val)
    {
        return std::visit([](const auto& arg)->std::set<std::string> {
            using T = std::decay_t<decltype(arg)>;
            std::set<std::string> paths;
            if constexpr (std::is_same_v<T, std::string>) {
                paths = getReferPath(arg);
            }
            else if constexpr (std::is_same_v<T, zeno::vec2s> || std::is_same_v<T, zeno::vec3s> || std::is_same_v<T, zeno::vec4s>)
            {
                for (int i = 0; i < arg.size(); i++)
                {
                    auto res = getReferPath(arg[i]);
                    paths.insert(res.begin(), res.end());
                }
            }
            return paths;
        }, val);
    }

    std::string absolutePath(std::string currentPath, const std::string& path)
    {
        if (!zeno::starts_with(path, "./") && !zeno::starts_with(path, "../"))
            return path;
        if (starts_with(path, "./"))
            return currentPath + path.substr(1, path.size() - 1);
        auto vec = split_str(path, '/');
        std::string tmpPath;
        if (zeno::ends_with(currentPath, "/"))
            currentPath = currentPath.substr(0, currentPath.size() - 1);
        for (int i = vec.size() - 1; i >= 0; i--)
        {
            if (vec[i] == "..")
            {
                currentPath = currentPath.substr(0, currentPath.find_last_of("/"));
            }
            else
            {
                if (tmpPath.empty())
                    tmpPath = vec[i];
                else
                    tmpPath = vec[i] + "/" + tmpPath;
            }
        }
        return currentPath + "/" + tmpPath;
    }

    std::string zeno::relativePath(std::string currentPath, const std::string& path)
    {
        if (path.find(currentPath) != std::string::npos)
        {
            std::regex pattern(currentPath);
            return std::regex_replace(path, pattern, ".");
        }
        std::string str;
        if (zeno::ends_with(currentPath, "/"))
            currentPath = currentPath.substr(0, currentPath.size() - 1);
        int pos = currentPath.find_last_of("/");
        while (pos != std::string::npos)
        {
            if (path.find(currentPath) == std::string::npos)
            {
                if (str.empty())
                    str = "..";
                else
                    str = "../" + str;

                currentPath = currentPath.substr(0, pos);
                pos = currentPath.find_last_of("/");
            }
            else
            {
                break;
            }
        }
        std::regex regx(currentPath);
        return std::regex_replace(path, regx, str);
    }

    std::set<std::string> getReferPath(const std::string& path)
    {
        std::regex words_regex(".*ref\\(\"(.*)\"\\).*");
        std::set<std::string> result;
        std::string str = path;
        std::smatch match;
        while (std::regex_match(str, match, words_regex))
        {
            std::string val = match[1].str();
            std::regex rgx("(\\.x|\\.y|\\.z|\\.w)$");
            std::string newVal = std::regex_replace(val, rgx, "");
            result.emplace(newVal);
            val = "ref(\"" + val + "\")";
            str.replace(str.find(val), val.size(), "");
        }
        return result;
    }

    bool getParamInfo(const CustomUI& customui, std::vector<ParamPrimitive>& inputs, std::vector<ParamPrimitive>& outputs) {
        return false;
    }

    bool isPrimitiveType(const zeno::ParamType type) {
        return type == Param_String || type == Param_Int || type == Param_Float || type == Param_Vec2i ||
            type == Param_Vec3i || type == Param_Vec4i || type == Param_Vec2f || type == Param_Vec3f ||
            type == Param_Vec4f || type == Param_Bool || type == Param_Heatmap || type == Param_Curve;//TODO: heatmap type.
    }

    zany strToZAny(std::string const& defl, ParamType const& type) {
        switch (type) {
        case Param_String: {
            zany res = std::make_shared<zeno::StringObject>(defl);
            return res;
        }
        case Param_Int: {
            return std::make_shared<NumericObject>(std::stoi(defl));
        }
        case Param_Float: {
            return std::make_shared<NumericObject>(std::stof(defl));
        }
        case Param_Vec2i:
        case Param_Vec3i:
        case Param_Vec4i:
        {
            std::vector<int> vec;
            for (auto v : split_str(defl, ',')) {
                vec.push_back(std::stoi(v));
            }

            if (Param_Vec2i == type) {
                return std::make_shared<NumericObject>(vec2i(vec[0], vec[1]));
            }
            else if (Param_Vec3i == type) {
                return std::make_shared<NumericObject>(vec3i(vec[0], vec[1], vec[2]));
            }
            else {
                return std::make_shared<NumericObject>(vec4i(vec[0], vec[1], vec[2], vec[3]));
            }
        }
        case Param_Vec2f:
        case Param_Vec3f:
        case Param_Vec4f:
        {
            std::vector<float> vec;
            for (auto v : split_str(defl, ',')) {
                vec.push_back(std::stof(v));
            }

            if (Param_Vec2f == type) {
                return std::make_shared<NumericObject>(vec2f(vec[0], vec[1]));
            }
            else if (Param_Vec3f == type) {
                return std::make_shared<NumericObject>(vec3f(vec[0], vec[1], vec[2]));
            }
            else {
                return std::make_shared<NumericObject>(vec4f(vec[0], vec[1], vec[2], vec[3]));
            }
        }
        default:
            return nullptr;
        }
    }

    bool isEqual(const zvariant& lhs, const zvariant& rhs, ParamType const type) {
        if (lhs.index() != rhs.index())
            return false;

        std::visit([&](auto&& arg1, auto&& arg2) -> bool {
            using T = std::decay_t<decltype(arg1)>;
            using E = std::decay_t<decltype(arg2)>;
            if constexpr (std::is_same_v<T, int> && std::is_same_v<E, int>) {
                return arg1 == arg2;
            }
            else if constexpr (std::is_same_v<T, float> && std::is_same_v<E, float>) {
                return arg1 == arg2;
            }
            else if constexpr (std::is_same_v<T, std::string> && std::is_same_v<E, std::string>) {
                return arg1 == arg2;
            }
            else if constexpr (std::is_same_v<T, zeno::vec2i> && std::is_same_v<E, zeno::vec2i>)
            {
                return (arg1[0] == arg2[0] && arg1[1] == arg2[1]);
            }
            else if constexpr (std::is_same_v<T, zeno::vec2f> && std::is_same_v<E, zeno::vec2f>)
            {
                return (arg1[0] == arg2[0] && arg1[1] == arg2[1]);
            }
            else if constexpr (std::is_same_v<T, zeno::vec3i> && std::is_same_v<E, zeno::vec3i>)
            {
                return (arg1[0] == arg2[0] && arg1[1] == arg2[1] && arg1[2] == arg2[2]);
            }
            else if constexpr (std::is_same_v<T, zeno::vec3f> && std::is_same_v<E, zeno::vec3f>)
            {
                return (arg1[0] == arg2[0] && arg1[1] == arg2[1] && arg1[2] == arg2[2]);
            }
            else if constexpr (std::is_same_v<T, zeno::vec4i> && std::is_same_v<E, zeno::vec4i>)
            {
                return (arg1[0] == arg2[0] && arg1[1] == arg2[1] && arg1[2] == arg2[2] && arg1[3] == arg2[3]);
            }
            else if constexpr (std::is_same_v<T, zeno::vec4f> && std::is_same_v<E, zeno::vec4f>)
            {
                return (arg1[0] == arg2[0] && arg1[1] == arg2[1] && arg1[2] == arg2[2] && arg1[3] == arg2[3]);
            }
            else
            {
                return false;
            }
        }, lhs, rhs);
    }

    ZENO_API zeno::ParamControl getDefaultControl(const zeno::ParamType type)
    {
        switch (type)
        {
        case zeno::Param_Null:      return zeno::NullControl;
        case zeno::Param_Bool:      return zeno::Checkbox;
        case zeno::Param_Int:       return zeno::Lineedit;
        case zeno::Param_String:    return zeno::Lineedit;
        case zeno::Param_Float:     return zeno::Lineedit;
        case zeno::Param_Vec2i:     return zeno::Vec2edit;
        case zeno::Param_Vec3i:     return zeno::Vec3edit;
        case zeno::Param_Vec4i:     return zeno::Vec4edit;
        case zeno::Param_Vec2f:     return zeno::Vec2edit;
        case zeno::Param_Vec3f:     return zeno::Vec3edit;
        case zeno::Param_Vec4f:     return zeno::Vec4edit;
        case zeno::Param_Prim:
        case zeno::Param_Dict:
        case zeno::Param_List:      return zeno::NullControl;
            //Param_Color:  //need this?
        case zeno::Param_Curve:     return zeno::CurveEditor;
        case zeno::Param_Heatmap: return zeno::Heatmap;
        case zeno::Param_SrcDst:
        default:
            return zeno::NullControl;
        }
    }

    ZENO_API std::string getControlDesc(zeno::ParamControl ctrl, zeno::ParamType type)
    {
        switch (ctrl)
        {
        case zeno::Lineedit:
        {
            switch (type) {
            case zeno::Param_Float:     return "Float";
            case zeno::Param_Int:       return "Integer";
            case zeno::Param_String:    return "String";
            }
            return "";
        }
        case zeno::Checkbox:
        {
            return "Boolean";
        }
        case zeno::Multiline:           return "Multiline String";
        case zeno::ReadPathEdit:            return "read path";
        case zeno::WritePathEdit:            return "write path";
        case zeno::DirectoryPathEdit:            return "directory";
        case zeno::Combobox:            return "Enum";
        case zeno::Vec4edit:
        {
            return type == zeno::Param_Int ? "Integer Vector 4" : "Float Vector 4";
        }
        case zeno::Vec3edit:
        {
            return type == zeno::Param_Int ? "Integer Vector 3" : "Float Vector 3";
        }
        case zeno::Vec2edit:
        {
            return type == zeno::Param_Int ? "Integer Vector 2" : "Float Vector 2";
        }
        case zeno::Heatmap:             return "Color";
        case zeno::ColorVec:            return "Color Vec3f";
        case zeno::CurveEditor:         return "Curve";
        case zeno::SpinBox:             return "SpinBox";
        case zeno::DoubleSpinBox:       return "DoubleSpinBox";
        case zeno::Slider:              return "Slider";
        case zeno::SpinBoxSlider:       return "SpinBoxSlider";
        case zeno::Seperator:           return "group-line";
        case zeno::PythonEditor:        return "PythonEditor";
        case zeno::CodeEditor:        return "CodeEditor";
        default:
            return "";
        }
    }
}