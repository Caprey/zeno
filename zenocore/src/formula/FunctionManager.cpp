#include <zeno/core/FunctionManager.h>
#include <zeno/core/ReferManager.h>
#include <zeno/core/Session.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/utils/Error.h>
#include <zeno/core/Graph.h>
#include <zeno/utils/log.h>
#include <zeno/utils/helper.h>
#include <regex>
#include <variant>
#include <functional>
#include <zeno/utils/format.h>
#include <numeric>


namespace zeno {

    FunctionManager::FunctionManager() {
        init();
    }

    std::vector<std::string> FunctionManager::getCandidates(const std::string& prefix, bool bFunc) const {
        std::vector<std::string> candidates;
        if (bFunc && prefix.empty())
            return candidates;

        if (bFunc) {
            for (auto& [k, v] : m_funcs) {
                //TODO: optimize the search
                if (k.substr(0, prefix.size()) == prefix) {
                    candidates.push_back(k);
                }
            }
        }
        else {
            static std::vector<std::string> vars = { "F", "FPS", "T", "PI" };
            for (auto& var : vars) {
                if (var.substr(0, prefix.size()) == prefix) {
                    candidates.push_back(var);
                }
            }
        }
        return candidates;
    }

    std::string FunctionManager::getFuncTip(const std::string& funcName, bool& bExist) const {
        auto iter = m_funcs.find(funcName);
        if (iter == m_funcs.end()) {
            bExist = false;
            return "";
        }
        bExist = true;
        return iter->second.tip;
    }

    FUNC_INFO FunctionManager::getFuncInfo(const std::string& funcName) const {
        auto iter = m_funcs.find(funcName);
        if (iter == m_funcs.end()) {
            return FUNC_INFO();
        }
        return iter->second;
    }

    float FunctionManager::callRef(const std::string& ref, ZfxContext* pContext) {
        //TODO: vec type.
        std::string fullPath, graphAbsPath;

        if (ref.empty()) {
            throw makeError<UnimplError>();
        }

        auto thisNode = pContext->spNode.lock();
        const std::string& thisnodePath = thisNode->get_path();
        graphAbsPath = thisnodePath.substr(0, thisnodePath.find_last_of('/'));

        if (ref.front() == '/') {
            fullPath = ref;
        } else {
            fullPath = graphAbsPath + "/" + ref;
        }

        int idx = fullPath.find_last_of('/');
        if (idx == std::string::npos) {
            throw makeError<UnimplError>();
        }

        const std::string& nodePath = fullPath.substr(idx + 1);

        idx = nodePath.find('.');
        if (idx == std::string::npos) {
            throw makeError<UnimplError>();
        }
        std::string nodename = nodePath.substr(0, idx);
        std::string parampath = nodePath.substr(idx + 1);

        std::string nodeAbsPath = graphAbsPath + '/' + nodename;
        std::shared_ptr<INode> spNode = zeno::getSession().mainGraph->getNodeByPath(nodeAbsPath);

        if (!spNode) {
            throw makeError<UnimplError>();
        }

        auto items = split_str(parampath, '.');
        std::string paramname = items[0];

        bool bExist = false;
        ParamPrimitive paramData = spNode->get_input_prim_param(paramname, &bExist);
        if (!bExist)
            throw makeError<UnimplError>();

        if (items.size() == 1) {
            if (std::holds_alternative<int>(paramData.defl)) {
                return std::get<int>(paramData.defl);
            }
            else if (std::holds_alternative<float>(paramData.defl)) {
                return std::get<float>(paramData.defl);
            }
            else {
                throw makeError<UnimplError>();
            }
        }

        if (items.size() == 2 &&
            (paramData.type == Param_Vec2f || paramData.type == Param_Vec2i ||
                paramData.type == Param_Vec3f || paramData.type == Param_Vec3i ||
                paramData.type == Param_Vec4f || paramData.type == Param_Vec4i))
        {
            if (items[1].size() != 1)
                throw makeError<UnimplError>();

            int idx = -1;
            switch (items[1][0])
            {
            case 'x': idx = 0; break;
            case 'y': idx = 1; break;
            case 'z': idx = 2; break;
            case 'w': idx = 3; break;
            default:
                throw makeError<UnimplError>();
            }
            if (paramData.type == Param_Vec2f || paramData.type == Param_Vec2i) {
                if (idx < 2) {
                    return paramData.type == Param_Vec2f ? std::get<vec2f>(paramData.defl)[idx] :
                        std::get<vec2i>(paramData.defl)[idx];
                }
                else {
                    throw makeError<UnimplError>();
                }
            }
            if (paramData.type == Param_Vec3f || paramData.type == Param_Vec3i) {
                if (idx < 3) {
                    return paramData.type == Param_Vec3f ? std::get<vec3f>(paramData.defl)[idx] :
                        std::get<vec3i>(paramData.defl)[idx];
                }
                else {
                    throw makeError<UnimplError>();
                }
            }
            if (paramData.type == Param_Vec4f || paramData.type == Param_Vec4i) {
                if (idx < 4) {
                    return paramData.type == Param_Vec4f ? std::get<vec4f>(paramData.defl)[idx] :
                        std::get<vec4i>(paramData.defl)[idx];
                }
                else {
                    throw makeError<UnimplError>();
                }
            }
        }
        throw makeError<UnimplError>();
    }

    void FunctionManager::executeZfx(std::shared_ptr<ZfxASTNode> root, ZfxContext* pCtx) {
        //debug
        //
        markOrder(root, 0);
        parsingAttr(root, nullptr, pCtx);
        removeAttrvarDeclareAssign(root, pCtx);
        //markOrder(root, 0);
        //printSyntaxTree(root, pCtx->code);
        zeno::log_only_print(decompile(root));
        if (pCtx->spObject)
            execute(root, pCtx);
    }

    template <class T>
    static T get_zfxvar(zfxvariant value) {
        return std::visit([](auto const& val) -> T {
            using V = std::decay_t<decltype(val)>;
            if constexpr (!std::is_constructible_v<T, V>) {
                throw makeError<TypeError>(typeid(T), typeid(V), "get<zfxvariant>");
            }
            else {
                return T(val);
            }
        }, value);
    }

    template<typename Operator>
    zfxvariant calc_exp(const zfxvariant& lhs, const zfxvariant& rhs, Operator method) {
        return std::visit([method](auto&& lval, auto&& rval)->zfxvariant {
            using T = std::decay_t<decltype(lval)>;
            using E = std::decay_t<decltype(rval)>;
            using Op = std::decay_t<decltype(method)>;
            if constexpr (std::is_same_v<T, int>) {
                if constexpr (std::is_same_v<E, int>) {
                    return method(lval, rval);
                }
                else if constexpr (std::is_same_v<E, float>) {
                    return method(E(lval), rval);
                }
                else {
                    //�ݲ�����һ��Ԫ�غ�һ��������ӵ������
                    throw UnimplError("");
                }
            }
            else if constexpr (std::is_same_v<T, float>) {
                if constexpr (std::is_same_v<E, int>) {
                    return method(lval, T(rval));
                }
                else if constexpr (std::is_same_v<E, float>) {
                    return method(lval, rval);
                }
                throw UnimplError("");
            }
            else if constexpr (std::is_same_v<T, glm::vec2> && std::is_same_v<T, E> ||
                            std::is_same_v<T, glm::vec3> && std::is_same_v<T, E> ||
                            std::is_same_v<T, glm::vec4> && std::is_same_v<T, E>)
            {
                if constexpr (std::is_same_v<Op, std::less_equal<>>) {
                    throw UnimplError("");
                }
                else if constexpr (std::is_same_v<Op, std::less<>>) {
                    throw UnimplError("");
                }
                else if constexpr (std::is_same_v<Op, std::greater<>>) {
                    throw UnimplError("");
                }
                else if constexpr (std::is_same_v<Op, std::greater_equal<>>) {
                    throw UnimplError("");
                }
                else
                {
                    return method(lval, rval);
                }
            }
            else if constexpr (std::is_same_v<T, glm::mat3> && std::is_same_v<T, E> ||
                               std::is_same_v<T, glm::mat4> && std::is_same_v<T, E> ||
                               std::is_same_v<T, glm::mat2> && std::is_same_v<T, E>) {

                if constexpr (std::is_same_v<Op, std::less_equal<>>) {
                    throw UnimplError("");
                }
                else if constexpr (std::is_same_v<Op, std::less<>>) {
                    throw UnimplError("");
                }
                else if constexpr (std::is_same_v<Op, std::greater<>>) {
                    throw UnimplError("");
                }
                else if constexpr (std::is_same_v<Op, std::greater_equal<>>) {
                    throw UnimplError("");
                }
                else if constexpr (std::is_same_v<Op, std::multiplies<>>)
                {
                    //glm��ʵ����˷���˳���෴�ģ�����A*B, ��ʵ����������B * A.
                    return method(rval, lval);
                }
                else {
                    return method(lval, rval);
                }
            }
            else {
                throw UnimplError("");
            }
        }, lhs, rhs);
    }

    void FunctionManager::testExp() {

        glm::vec3 v1(1, 2, 3);
        glm::vec3 v2(1, 3, 1);

        zfxvariant vec1 = v1;
        zfxvariant vec2 = v2;
        zfxvariant vec3 = calc_exp(v1, v2, std::divides());

        glm::mat3 mat1 = glm::mat3({ {1., 0, 2.}, {2., 1., -1.}, {0, 1., 1.} });
        glm::mat3 mat2 = glm::mat3({ {1., 0, 0}, {0, -1., 1.}, {0, 0, -1.} });
        glm::mat3 mat3 = glm::mat3({ {1, 0, 0}, {0, 1, 0}, {0, 0, 1} });
        glm::mat3 mat4 = glm::mat3({ {1, 0, 0}, {0, -1, 1}, {0, 0, -1} });
        glm::mat3 mm = mat3 * mat4;
        mm = mat1 * mat2;

        zfxvariant bval = calc_exp(mat1, mat2, std::equal_to());
        zfxvariant mmm = calc_exp(mat1, mat2, std::multiplies());

        //glm::mat3 mm2 = glm::dot(mat1, mat2);
    }

    static void set_array_element(zfxvariant& zfxarr, int idx, const zfxvariant& zfxvalue) {
        std::visit([idx](auto& arr, auto& value) {
            using T = std::decay_t<decltype(arr)>;
            using V = std::decay_t<decltype(value)>;
            //matȡһ���������ܾ���vec...
            if constexpr ((std::is_same_v<T, zfxintarr> || std::is_same_v<T, zfxfloatarr>) && 
                std::is_arithmetic_v<V>) {
                arr[idx] = value;
            }
            else if constexpr ((std::is_same_v<T, glm::vec2> ||
                std::is_same_v<T, glm::vec3> ||
                std::is_same_v<T, glm::vec4>) && std::is_same_v<V, float>) {
                arr[idx] = value;
            }
        }, zfxarr, zfxvalue);
    }

    static zfxvariant get_array_element(const zfxvariant& arr, int idx) {
        return std::visit([idx](auto&& arg) -> zfxvariant {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, zfxintarr> ||
                std::is_same_v<T, zfxfloatarr> ||
                std::is_same_v<T, zfxstringarr> ||
                std::is_same_v<T, glm::vec2> ||
                std::is_same_v<T, glm::vec3> ||
                std::is_same_v<T, glm::vec4> ||
                std::is_same_v<T, glm::mat2> ||
                std::is_same_v<T, glm::mat3> ||
                std::is_same_v<T, glm::mat4>
                ) {
                return (T(arg))[idx];
            }
            else {
                throw makeError<UnimplError>("get elemvar from arr");
            }
        }, arr);
    }

    static zfxvariant get_element_by_name(const zfxvariant& arr, const std::string& name) {
        int idx = -1;
        if (name == "x") idx = 0;
        else if (name == "y") idx = 1;
        else if (name == "z") idx = 2;
        else if (name == "w") idx = 3;
        else
        {
            throw makeError<UnimplError>("Indexing Exceed");
        }
        return get_array_element(arr, idx);
    }

    static void set_element_by_name(zfxvariant& arr, const std::string& name, const zfxvariant& value) {
        int idx = -1;
        if (name == "x") idx = 0;
        else if (name == "y") idx = 1;
        else if (name == "z") idx = 2;
        else if (name == "w") idx = 3;
        else throw makeError<UnimplError>("index error.");
        set_array_element(arr, idx, value);
    }

    static void selfIncOrDec(zfxvariant& var, bool bInc) {
        std::visit([bInc](auto&& arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>) {
                bInc ? (T)arg++ : (T)arg--;
            }
            else {
                throw makeError<UnimplError>("Type Error");
            }
        }, var);
    }

    std::vector<zfxvariant> FunctionManager::process_args(std::shared_ptr<ZfxASTNode> parent, ZfxContext* pContext) {
        std::vector<zfxvariant> args;
        for (auto pChild : parent->children) {
            zfxvariant argval = execute(pChild, pContext);
            args.push_back(argval);
        }
        return args;
    }

    void FunctionManager::pushStack() {
        m_stacks.push_back(ZfxStackEnv());
    }

    void FunctionManager::popStack() {
        m_stacks.pop_back();
    }

    ZfxVariable& FunctionManager::getVariableRef(const std::string& name) {
        for (auto iter = m_stacks.rbegin(); iter != m_stacks.rend(); iter++) {
            auto& stackvars = iter->table;
            auto iter_ = stackvars.find(name);
            if (iter_ != stackvars.end()) {
                return stackvars.at(name);
            }
        }
        if (!name.empty() && name.at(0) == '@') {
            ZfxVariable var;
            var.attachAttrs.insert(name);
            declareVariable(name, var);
            return getVariableRef(name);
        }
        throw makeError<KeyError>(name, "variable `" + name + "` not founded");
    }

    zfxvariant FunctionManager::getVariable(const std::string& name) const {
        for (auto iter = m_stacks.rbegin(); iter != m_stacks.rend(); iter++) {
            auto& stackvars = iter->table;
            auto iter_ = stackvars.find(name);
            if (iter_ != stackvars.end()) {
                return iter_->second.value;
            }
        }
        throw makeError<KeyError>(name, "variable `" + name + "` not founded");
    }

    bool FunctionManager::declareVariable(const std::string& name, ZfxVariable var) {
        if (m_stacks.empty()) {
            return false;
        }
        auto iterCurrentStack = m_stacks.rbegin();
        VariableTable& stackvars = iterCurrentStack->table;
        if (stackvars.find(name) != stackvars.end()) {
            return false;
        }
        stackvars.insert(std::make_pair(name, var));
        return true;
    }

    bool FunctionManager::declareVariable(const std::string& name, zfxvariant var) {
        if (m_stacks.empty()) {
            return false;
        }
        auto iterCurrentStack = m_stacks.rbegin();
        auto& vars = iterCurrentStack->table;
        if (vars.find(name) != vars.end()) {
            return false;
        }
        ZfxVariable variable;
        variable.value = var;
        vars.insert(std::make_pair(name, variable));
        return true;
    }

    bool FunctionManager::assignVariable(const std::string& name, ZfxVariable var) {
        if (m_stacks.empty()) {
            return false;
        }
        ZfxVariable& self = getVariableRef(name);
        self = var;
        return true;
    }

    void FunctionManager::validateVar(operatorVals vartype, zfxvariant& newvar) {
        switch (vartype)
        {
            case TYPE_INT: {
                if (std::holds_alternative<float>(newvar)) {
                    newvar = (int)std::get<float>(newvar);
                }
                else if (std::holds_alternative<int>(newvar)) {

                }
                else {
                    throw makeError<UnimplError>("type dismatch TYPE_INT");
                }
                break;
            }
            case TYPE_INT_ARR: {
                if (std::holds_alternative<zfxfloatarr>(newvar)) {
                    zfxfloatarr floatarr;
                    for (auto&& val : std::get<zfxfloatarr>(newvar))
                        floatarr.push_back(val);
                    newvar = floatarr;
                }
                else if (!std::holds_alternative<zfxintarr>(newvar)) {
                    throw makeError<UnimplError>("type dismatch TYPE_INT_ARR");
                }
                break;
            }
            case TYPE_FLOAT: {
                if (std::holds_alternative<float>(newvar)) {

                }
                else if (std::holds_alternative<int>(newvar)) {
                    newvar = (float)std::get<int>(newvar);
                }
                else {
                    throw makeError<UnimplError>("type dismatch TYPE_FLOAT");
                }
                break;
            }
            case TYPE_FLOAT_ARR: {
                if (std::holds_alternative<zfxintarr>(newvar)) {
                    zfxintarr intarr;
                    for (auto&& val : std::get<zfxintarr>(newvar))
                        intarr.push_back(val);
                    newvar = intarr;
                }
                else if (!std::holds_alternative<zfxfloatarr>(newvar)) {
                    throw makeError<UnimplError>("type dismatch TYPE_FLOAT_ARR");
                }
                break;
            }
            case TYPE_STRING: {
                if (!std::holds_alternative<std::string>(newvar)) {
                    throw makeError<UnimplError>("type dismatch TYPE_STRING");
                }
                break;
            }
            case TYPE_STRING_ARR: {
                if (!std::holds_alternative<zfxstringarr>(newvar)) {
                    throw makeError<UnimplError>("type dismatch TYPE_STRING_ARR");
                }
                break;
            }
            case TYPE_VECTOR2: {
                if (std::holds_alternative<zfxfloatarr>(newvar)) {
                    zfxfloatarr arr = std::get<zfxfloatarr>(newvar);
                    if (arr.size() != 2) {
                        throw makeError<UnimplError>("num of elements of arr dismatch");
                    }
                    glm::vec2 vec = { arr[0], arr[1] };
                    newvar = vec;
                }
                else if (std::holds_alternative<glm::vec2>(newvar)) {

                }
                else {
                    throw makeError<UnimplError>("type dismatch TYPE_VECTOR2");
                }
                break;
            }
            case TYPE_VECTOR3: {
                if (std::holds_alternative<zfxfloatarr>(newvar)) {
                    zfxfloatarr arr = std::get<zfxfloatarr>(newvar);
                    if (arr.size() != 3) {
                        throw makeError<UnimplError>("num of elements of arr dismatch");
                    }
                    glm::vec3 vec = { arr[0], arr[1], arr[2] };
                    newvar = vec;
                }
                else if (std::holds_alternative<glm::vec3>(newvar)) {

                }
                else {
                    throw makeError<UnimplError>("type dismatch TYPE_VECTOR3");
                }
                break;
            }
            case TYPE_VECTOR4: {
                if (std::holds_alternative<zfxfloatarr>(newvar)) {
                    zfxfloatarr arr = std::get<zfxfloatarr>(newvar);
                    if (arr.size() != 4) {
                        throw makeError<UnimplError>("num of elements of arr dismatch");
                    }
                    glm::vec4 vec = { arr[0], arr[1], arr[2], arr[3] };
                    newvar = vec;
                }
                else if (std::holds_alternative<glm::vec4>(newvar)) {

                }
                else {
                    throw makeError<UnimplError>("type dismatch TYPE_VECTOR4");
                }
                break;
            }
            case TYPE_MATRIX2: {
                if (!std::holds_alternative<glm::mat2>(newvar)) {
                    throw makeError<UnimplError>("type dismatch TYPE_MATRIX2");
                }
                break;
            }
            case TYPE_MATRIX3: {
                if (!std::holds_alternative<glm::mat3>(newvar)) {
                    throw makeError<UnimplError>("type dismatch TYPE_MATRIX3");
                }
                break;
            }
            case TYPE_MATRIX4: {
                if (!std::holds_alternative<glm::mat4>(newvar)) {
                    throw makeError<UnimplError>("type dismatch TYPE_MATRIX4");
                }
                break;
            }
        }
    }

    zfxvariant FunctionManager::parseArray(std::shared_ptr<ZfxASTNode> pNode, ZfxContext* pContext) {
        std::vector<zfxvariant> args = process_args(pNode, pContext);
        //���ڲ�������ͣ������ȸ���ֵ���͹���һ��zfxvariant���س�ȥ�������ⲿ���Ͷ���/��ֵ������
        //����zfxvariant��û�ж�ά���飬���������ά���飬ֱ�ӹ�������������ʧ�ܣ��Ǿ�throw��
        if (args.empty()) {
            //ֱ�ӷ���Ĭ�ϵ�
            return zfxvariant();
        }

        zfxvariant current;
        operatorVals dataType = UNDEFINE_OP;
        for (int idx = 0; idx < args.size(); idx++) {
            auto& arg = args[idx];
            if (std::holds_alternative<int>(arg)) {
                if (dataType == UNDEFINE_OP) {
                    dataType = TYPE_FLOAT_ARR;
                    current = zfxfloatarr();
                }
                if (dataType == TYPE_FLOAT_ARR) {
                    auto& arr = std::get<zfxfloatarr>(current);
                    arr.push_back(std::get<int>(arg));
                }
                else {
                    throw makeError<UnimplError>("data type inconsistent");
                }
            }
            else if (std::holds_alternative<float>(arg)) {
                if (dataType != UNDEFINE_OP && dataType != TYPE_FLOAT_ARR) {
                    throw makeError<UnimplError>("data type inconsistent");
                }
                if (dataType == UNDEFINE_OP) {
                    dataType = TYPE_FLOAT_ARR;
                    current = zfxfloatarr();
                }
                if (dataType == TYPE_FLOAT_ARR) {
                    auto& arr = std::get<zfxfloatarr>(current);
                    arr.push_back(std::get<float>(arg));
                }
            }
            else if (std::holds_alternative<std::string>(arg)) {
                if (dataType != UNDEFINE_OP && dataType != TYPE_STRING_ARR) {
                    throw makeError<UnimplError>("data type inconsistent");
                }
                if (dataType == UNDEFINE_OP) {
                    dataType = TYPE_STRING_ARR;
                    current = zfxstringarr();
                }
                if (dataType == TYPE_STRING_ARR) {
                    auto& arr = std::get<zfxstringarr>(current);
                    arr.push_back(std::get<std::string>(arg));
                }
            }
            //������intarr����Ϊglm��vector/matrix���Ǵ���float
            else if (std::holds_alternative<zfxfloatarr>(arg)) {
                if (dataType != UNDEFINE_OP && dataType != TYPE_MATRIX2 && dataType != TYPE_MATRIX3 && dataType != TYPE_MATRIX4) {
                    throw makeError<UnimplError>("data type inconsistent");
                }

                auto& arr = std::get<zfxfloatarr>(arg);

                if (dataType == UNDEFINE_OP) {
                    if (arr.size() == 2) {
                        dataType = TYPE_MATRIX2;
                        current = glm::mat2();
                    }
                    else if (arr.size() == 3) {
                        dataType = TYPE_MATRIX3;
                        current = glm::mat3();
                    }
                    else if (arr.size() == 4) {
                        dataType = TYPE_MATRIX4;
                        current = glm::mat4();
                    }
                }

                //{{0, 1}, {2, 3}}
                if (dataType == TYPE_MATRIX2 && arr.size() == 2 && idx < 2) {
                    auto& mat = std::get<glm::mat2>(current);
                    mat[idx][0] = arr[0];
                    mat[idx][1] = arr[1];
                }
                //{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}
                else if (dataType == TYPE_MATRIX3 && arr.size() == 3 && idx < 3) {
                    auto& mat = std::get<glm::mat3>(current);
                    mat[idx][0] = arr[0];
                    mat[idx][1] = arr[1];
                    mat[idx][2] = arr[2];
                }
                //{{1, 0, 0, 1}, {0, 1, 0, 1}, {0, 0, 1, 1}, {0, 0, 1, 1}}
                else if (dataType == TYPE_MATRIX4 && arr.size() == 4 && idx < 4) {
                    auto& mat = std::get<glm::mat4>(current);
                    mat[idx][0] = arr[0];
                    mat[idx][1] = arr[1];
                    mat[idx][2] = arr[2];
                    mat[idx][3] = arr[3];
                }
                else {
                    throw makeError<UnimplError>("mat element dims inconsistent");
                }
            }
            else {
                throw makeError<UnimplError>("data type inconsistent");
            }
        }
        return current;
    }

    void FunctionManager::getDependingVariables(const std::string& assignedVar, std::set<std::string>& vars) {
        //���ܣ���ñ���ֵ����assignedVar�����б��ʽ�����������б��������ԣ��Լ���Щ���������Ĺ��������εݹ�
        if (assignedVar.empty())
            return;

        //���Ը�ֵҲ��������������ֱ��������
        //if (assignedVar.at(0) == '@')
        //    return;

        //��ֹ�ظ���ӵ����
        if (vars.find(assignedVar) != vars.end())
            return;

        ZfxVariable& zenvar = getVariableRef(assignedVar);

        for (auto iter = zenvar.assignStmts.rbegin(); iter != zenvar.assignStmts.rend(); iter++)
        {
            auto spStmt = *iter;
            if (!spStmt)
                throw makeError<UnimplError>("stmt nullptr");

            std::set<std::string> depvars;
            findAllZenVar(spStmt, depvars);

            for (auto depvar : depvars) {
                //assignedVar�������������ԣ�����������ֱ����ӵ����������ٵݹ��ѯ��
                if (depvar == assignedVar) {
                    vars.insert(depvar);
                    continue;
                }
                //if (depvar.at(0) == '@') {
                //    vars.insert(depvar);
                //}
                getDependingVariables(depvar, vars);
            }
        }
    }

    void FunctionManager::removeAttrvarDeclareAssign(std::shared_ptr<ZfxASTNode> root, ZfxContext* pContext) {
        if (root->bOverridedIfLoop && root->type == IF) {
            //������һ����������飬���ǰ�������Ϊtrue����ӵط��������if
            //�ô��ǿ����õݹ���������������εĲ��֣�ɾ����ɾ��������䡣
            auto& children = root->children;
            auto trueCond = std::make_shared<ZfxASTNode>();
            trueCond->type = NUMBER;
            trueCond->value = 1;
            children[0] = trueCond;
        }

        for (auto iter = root->children.begin(); iter != root->children.end();) {
            removeAttrvarDeclareAssign(*iter, pContext);
            if ((*iter)->AttrAssociateVar) {
                iter = root->children.erase(iter);
            }
            else {
                iter++;
            }
        }
    }

    std::set<std::string> FunctionManager::parsingAttr(std::shared_ptr<ZfxASTNode> root, std::shared_ptr<ZfxASTNode> spOverrideStmt, ZfxContext* pContext) {
        if (!root) {
            throw makeError<UnimplError>("null ASTNODE");
        }
        switch (root->type)
        {
        case NUMBER:
        case STRING:
        case BOOLTYPE:  return {};
        case ZENVAR: {
            std::string varname = get_zfxvar<std::string>(root->value);
            ZfxVariable& newvar = getVariableRef(varname);
            if (!newvar.attachAttrs.empty() &&
                (root->opVal == AutoIncreaseFirst ||
                root->opVal == AutoIncreaseLast ||
                root->opVal == AutoDecreaseFirst ||
                root->opVal == AutoDecreaseLast))
            {
                //������ֵ��صı�������������Ҫ�ӽ�ȥ��
                root->AttrAssociateVar = true;
                auto spClonedStmt = clone(root);
                spClonedStmt->bOverridedStmt = spOverrideStmt != nullptr;
                newvar.assignStmts.push_back(spClonedStmt);
            }
            return newvar.attachAttrs;
        }
        case DECLARE: {
            //��������
            int nChildren = root->children.size();
            if (nChildren != 2 && nChildren != 3) {
                throw makeError<UnimplError>("args of DECLARE");
            }
            std::shared_ptr<ZfxASTNode> typeNode = root->children[0];
            std::shared_ptr<ZfxASTNode> nameNode = root->children[1];
            ZfxVariable newvar;
            bool bOnlyDeclare = nChildren == 2;
            operatorVals vartype = typeNode->opVal;
            if (bOnlyDeclare) {
                switch (vartype)
                {
                case TYPE_INT:      newvar.value = 0; break;
                case TYPE_INT_ARR:  newvar.value = zfxintarr();   break;
                case TYPE_FLOAT:    newvar.value = 0.f;   break;
                case TYPE_FLOAT_ARR:    newvar.value = zfxfloatarr(); break;
                case TYPE_STRING:       newvar.value = "";    break;
                case TYPE_STRING_ARR:   newvar.value = zfxstringarr(); break;
                case TYPE_VECTOR2:  newvar.value = glm::vec2(); break;
                case TYPE_VECTOR3:  newvar.value = glm::vec3(); break;
                case TYPE_VECTOR4:  newvar.value = glm::vec4(); break;
                case TYPE_MATRIX2:  newvar.value = glm::mat2(); break;
                case TYPE_MATRIX3:  newvar.value = glm::mat3(); break;
                case TYPE_MATRIX4:  newvar.value = glm::mat4(); break;
                }
            }
            else {
                std::shared_ptr<ZfxASTNode> valueNode = root->children[2];
                newvar.attachAttrs = parsingAttr(valueNode, spOverrideStmt, pContext);
                if (!newvar.attachAttrs.empty()) {
                    root->AttrAssociateVar = true;
                }
            }

            //validateVar(vartype, newvar.value);
            auto spClonedStmt = clone(root);
            spClonedStmt->bOverridedStmt = spOverrideStmt != nullptr;
            newvar.assignStmts.push_back(spClonedStmt);

            std::string varname = get_zfxvar<std::string>(nameNode->value);
            //��ʱ�������Զ���ṹ��
            bool bret = declareVariable(varname, newvar);
            if (!bret) {
                throw makeError<UnimplError>("assign variable failed.");
            }

            bret = assignVariable(varname, newvar);
            if (!bret) {
                throw makeError<UnimplError>("assign variable failed.");
            }
            break;
        }
        case ASSIGNMENT: {
            //��ֵ
            if (root->children.size() != 2) {
                throw makeError<UnimplError>("assign variable failed.");
            }
            std::shared_ptr<ZfxASTNode> zenvarNode = root->children[0];
            std::shared_ptr<ZfxASTNode> valNode = root->children[1];

            //��ͨ�����ĸ�ֵ
            const std::string& targetvar = get_zfxvar<std::string>(zenvarNode->value);
            ZfxVariable& var = getVariableRef(targetvar);

            std::set<std::string> rightsideAttrs;
            rightsideAttrs = parsingAttr(valNode, spOverrideStmt, pContext);
            var.attachAttrs.insert(rightsideAttrs.begin(), rightsideAttrs.end());

            auto spClonedStmt = clone(root);
            spClonedStmt->bOverridedStmt = spOverrideStmt != nullptr;
            var.assignStmts.push_back(spClonedStmt);
            if (!var.attachAttrs.empty() && !zenvarNode->bAttr) {
                root->AttrAssociateVar = true;
                //����������Ǻ�����Ҫɾ��������ġ�
            }

            if (zenvarNode->bAttr) {
                //��Ҫ��foreachѭ��
                embeddingForeach(root, spOverrideStmt, pContext);
                break;
            }
            break;
        }
        case FUNC:
        {
            std::string funcname =  get_zfxvar<std::string>(root->value);
            if (!isEvalFunction(funcname)) {
                //������ֻ�����Ը�ֵ��log����ҪǶ��foreach
                embeddingForeach(root, spOverrideStmt, pContext);
                break;
            }
        }
        case FOUROPERATIONS:
        case COMPOP:
        case CONDEXP:
        {
            //�������������Ƕ�룬�����ⲿ�ĸ�ֵ���ߺ��������
            std::set<std::string> attrnames;
            for (auto pChild : root->children) {
                std::set<std::string> names = parsingAttr(pChild, spOverrideStmt, pContext);
                attrnames.insert(names.begin(), names.end());
            }
            return attrnames;
        }
        case IF:
        {
            //������houdini�Ĺ��򣬷���houdini��ѣ��������Ա�������������Ƕ�������ִ������

            //����������ȼ���������ʽ�Ƿ����������ر���
            auto spCond = root->children[0];
            auto spExecute = root->children[1];
            auto& condAttrs = parsingAttr(spCond, spOverrideStmt, pContext);

            std::set<std::string> attrs;
            if (spOverrideStmt || condAttrs.empty()) {
                attrs = parsingAttr(spExecute, spOverrideStmt, pContext);
                //����ⲿ�Ѿ���if�����ˣ���ô��ǰ����ڲ���if��Ҳ����Ҫ�ˡ�
                if (spOverrideStmt)
                    root->bOverridedIfLoop = true;
            }
            else {
                //��ǰ�����root�������Σ����������ԣ��ڲ�����ִ����䶼Ҫ�������override stmt��
                //���Һ��Ը�ֵ�Ͷ�����䣨��Ϊoverride stmt�Ѿ������ˣ�û��Ҫ�ټ��ˣ�
                attrs = parsingAttr(spExecute, root, pContext);
                root->bOverridedIfLoop = true;
            }
            return attrs;
        }
        case WHILE:
        case DOWHILE:
        {
            //��parse������µ����Ա���
            std::set<std::string> attrs = parsingAttr(root->children[1], spOverrideStmt, pContext);

            //houdini�Ĺ��������ҲҪ�ռ���Ƕ�뵽���Ա������ʽ��ġ�

            //�ٴ��������ε�����չ�����⡣
            //embeddingForeach(root, pContext);
            return {};
        }
        case FOR:
        {
            //ѹջ
            pushStack();
            scope_exit sp([this]() {this->popStack(); });

            std::set<std::string> attrnames;
            //ֱ�Ӱѵ�������Ԫ��ȫ��parseһ��Ϳ�����
            for (auto pChild : root->children) {
                std::set<std::string> names = parsingAttr(pChild, spOverrideStmt, pContext);
                attrnames.insert(names.begin(), names.end());
            }
            //houdini��forѭ�����ܲ��ס�
            //embeddingForeach(root, pContext);
            return attrnames;
        }
        case FOREACH:
        {
            //ѹջ
            pushStack();
            scope_exit sp([this]() {this->popStack(); });

            int nChild = root->children.size();
            auto idxNode = nChild == 3 ? nullptr : root->children[0];
            auto varNode = nChild == 3 ? root->children[0] : root->children[1];
            auto arrNode = nChild == 3 ? root->children[1] : root->children[2];
            auto codeSeg = nChild == 3 ? root->children[2] : root->children[3];

            //����ҪԤ�ȶ����ö�ٱ�����ֵ����������parse�����������޶������
            std::string idxName;
            if (idxNode) {
                idxName = get_zfxvar<std::string>(idxNode->value);
                declareVariable(idxName);
            }
            const std::string& varName = get_zfxvar<std::string>(varNode->value);
            declareVariable(varName);

            std::set<std::string> attrnames;
            std::set<std::string> names = parsingAttr(arrNode, spOverrideStmt, pContext);
            attrnames.insert(names.begin(), names.end());
            names = parsingAttr(codeSeg, spOverrideStmt, pContext);
            attrnames.insert(names.begin(), names.end());
            return attrnames;
        }
        case CODEBLOCK:
        {
            pushStack();
            scope_exit sp([this]() {this->popStack(); });

            std::set<std::string> attrnames;
            for (auto pChild : root->children) {
                std::set<std::string> names = parsingAttr(pChild, spOverrideStmt, pContext);
                attrnames.insert(names.begin(), names.end());
            }
            return attrnames;
        }
        case JUMP:
        {
        }
        default:
        {
            
        }
        }
        return {};
    }

    bool FunctionManager::isEvalFunction(const std::string& funcname) {
        //��ʱ����ô�жϣ��������к�����Ϣע��ȫ�ֱ�
        if (funcname != "sin" &&
            funcname != "cos" &&
            funcname != "sinh" &&
            funcname != "cosh" &&
            funcname != "abs" &&
            funcname != "rand" &&
            funcname != "ref" &&
            funcname != "volumesample" &&
            funcname != "colormap"
            ) {
            return false;
        }
        return true;
    }

    bool FunctionManager::removeIrrelevantCode(std::shared_ptr<ZfxASTNode> root, int currentExecId, const std::set<std::string>& allDepvars, std::set<std::string>& allFindAttrs)
    {
        switch (root->type)
        {
        case NUMBER:            //����
        case BOOLTYPE:
        case STRING:            //�ַ���
            return false;
        case FUNC:              //����
        {
            const std::string& funcname = get_zfxvar<std::string>(root->value);
            if (!isEvalFunction(funcname) && root->sortOrderNum != currentExecId)
            {
                return true;
            }
            return false;
        }

        case ZENVAR:
        {
            if (root->bAttr) {
                const std::string& attrname = get_zfxvar<std::string>(root->value);
                allFindAttrs.insert(attrname);
            }
            return false;
            //�����������Լ��������̫�鷳�ˡ�
        }
        case FOUROPERATIONS:    //��������+ - * / %

        case COMPOP:            //������
        case CONDEXP:           //�������ʽ
        case ARRAY:
        case PLACEHOLDER:
        case IF:
        case FOR:
        case FOREACH:
        case FOREACH_ATTR:
        case EACH_ATTRS:
        case WHILE:
        case DOWHILE:
        case CODEBLOCK:         //����﷨����Ϊchildren�Ĵ����
        case JUMP:
        {
            for (auto iter = root->children.begin(); iter != root->children.end();)
            {
                bool ret = removeIrrelevantCode(*iter, currentExecId, allDepvars, allFindAttrs);
                if (ret) {
                    iter = root->children.erase(iter);
                }else{
                    iter++;
                }
            }
            return false;
        }
        case DECLARE:           //��������
        {
            //if (root->children.size() == 3)
            //    removeIrrelevantCode(root->children[2], currentExecId, allDepvars, allFindAttrs);
            return false;
        }
        case ASSIGNMENT:          //��ֵ
        {
            std::shared_ptr<ZfxASTNode> zenvarNode = root->children[0];
            const std::string& targetvar = get_zfxvar<std::string>(zenvarNode->value);
            if (allDepvars.find(targetvar) == allDepvars.end()) {
                //�����޹ر����ĸ�ֵ��Ҫ�������
                return true;
            }
            return false;
        }
        }
        return false;
    }

    void FunctionManager::embeddingForeach(std::shared_ptr<ZfxASTNode> root, std::shared_ptr<ZfxASTNode> spOverrideStmt, ZfxContext* pContext)
    {
        //����ú����ڵ�ĵײ��õ���������ر�����
        std::set<std::string> vars;
        findAllZenVar(root, vars);
        //ֻ�е�ǰ���ȷʵ���������ԣ���ȥ��override�����������ԡ�
        if (!vars.empty())
            findAllZenVar(spOverrideStmt, vars);

        std::set<std::string> allDepVars;
        for (auto var : vars) {
            //���var�����ԣ�ֱ����ӽ�ȥ
            if (var.at(0) == '@') {
                allDepVars.insert(var);
                continue;
            }

            //�ж�var�ǲ���������ر���
            ZfxVariable& refVar = getVariableRef(var);
            if (refVar.attachAttrs.empty())
                continue;

            allDepVars.insert(refVar.attachAttrs.begin(), refVar.attachAttrs.end());

            std::set<std::string> depVars;
            getDependingVariables(var, depVars);
            allDepVars.insert(depVars.begin(), depVars.end());
        }

        //�ҵ����ж��帳ֵstatements��ͳͳ����һ��������
        std::vector<std::shared_ptr<ZfxASTNode>> allStmtsForVars;
        std::set<std::string> allAttrs;
        for (auto var : allDepVars) {
            if (var.at(0) == '@') {
                allAttrs.insert(var);
            }
            ZfxVariable& refVar = getVariableRef(var);
            if (refVar.attachAttrs.empty())
                continue;
            allStmtsForVars.insert(allStmtsForVars.end(),
                refVar.assignStmts.begin(), refVar.assignStmts.end());
        }

        if (allAttrs.empty())
            return;

        //ȥ�����б��ⲿ���ǵ������
        for (auto iter = allStmtsForVars.begin(); iter != allStmtsForVars.end();)
        {
            /*
                ����if (@P.x > 3) {
                        int b = @P.x + 2;
                        b += 2;
                        int c = @N.y +...
                ���������if����ʽ�ڲ�ÿһ�����帳ֵ��䣬���ᱻ���浽����������б���Է����ѯ������ϵ��
                ������Щ���帳ֵ��䣬�ǲ���Ҫ����������ӵ�allStmtsForVars��ģ���Ϊ�������ǻ��
                ����if�������ȥ��
             */
            if ((*iter)->bOverridedStmt) {
                iter = allStmtsForVars.erase(iter);
            }
            else {
                iter++;
            }
        }

        //ֱ�Ӷ�����statements���﷨����˳���������
        std::sort(allStmtsForVars.begin(), allStmtsForVars.end(), [=](const auto& lhs, const auto& rhs) {
            return lhs->sortOrderNum < rhs->sortOrderNum;
            });

        //��foreach��������.
        std::shared_ptr<ZfxASTNode> spForeach = std::make_shared<ZfxASTNode>();
        spForeach->type = FOREACH_ATTR;

        std::shared_ptr<ZfxASTNode> spForBody = std::make_shared<ZfxASTNode>();
        spForBody->type = CODEBLOCK;
        for (auto spStmt : allStmtsForVars) {
            appendChild(spForBody, spStmt);
        }

        if (spOverrideStmt) {
            auto spClonedOverride = clone(spOverrideStmt);
            //������������ĸ�����䣬��Ҫɾ��������allDepVars�޹ص�ִ����䣬Ҳ����������������(if for while)
            //��ʱ�Ȳ�������Щ������� ���� ������ر���������������Ͼ�̫��������

            //�պ��õ��������id����Ϊ��clone������Ӧ����һ�µģ�������ʶ��ǰ�����Ŀ�����
            //˳���ռ�һ�����override stmt���������Ա���������foreach��ö�١�
            removeIrrelevantCode(spClonedOverride, root->sortOrderNum, allDepVars, allAttrs);

            //ֱ�Ӱ�override stmt�ӽ�ȥ�����ˡ����ùܵ�ǰ��stmt��(�Ѿ���������override��ͷ�ˣ���
            appendChild(spForBody, spClonedOverride);
        }
        else {
            //�ѵ�ǰ���뿽һ���׵�foreachѭ����body�
            //���Ը�ֵ�������뿽������Ϊ�Ѿ�������allStmtsForVars��ͷ�ˡ�
            if (root->type != ASSIGNMENT) {
                auto currentExecuteStmt = clone(root);
                appendChild(spForBody, currentExecuteStmt);
            }
        }

        //��each�������������Ա���
        std::shared_ptr<ZfxASTNode> spEach = std::make_shared<ZfxASTNode>();
        spEach->type = EACH_ATTRS;
        for (auto var : allAttrs) {
            std::shared_ptr<ZfxASTNode> spAttrNode = std::make_shared<ZfxASTNode>();
            spAttrNode->type = ZENVAR;
            spAttrNode->value = var;
            spAttrNode->bAttr = true;
            appendChild(spEach, spAttrNode);
        }

        appendChild(spForeach, spEach);
        appendChild(spForeach, spForBody);

        //��spForeach�滻��root.
        auto parent = root->parent.lock();
        auto& children = parent->children;
        auto& iterRoot = std::find(children.begin(), children.end(), root);
        auto idxRoot = std::distance(children.begin(), iterRoot);
        children[idxRoot] = spForeach;
        spForeach->parent = parent;
    }

    void FunctionManager::updateGeomAttr(const std::string& attrname, zfxvariant value, operatorVals op, zfxvariant opval, ZfxContext* pContext)
    {
        if (attrname == "P") {
            if (op == Indexing) {

            }
            else if (op == COMPVISIT) {

            }
            else {

            }
        }
        else if (attrname == "N") {

        }
        else if (attrname == "Cd") {

        }
        else if (attrname == "ptnum") {

        }
    }

    zfxvariant FunctionManager::execute(std::shared_ptr<ZfxASTNode> root, ZfxContext* pContext) {
        if (!root) {
            throw makeError<UnimplError>("Indexing Error.");
        }
        switch (root->type)
        {
            case NUMBER:
            case STRING:
            case BOOLTYPE: return root->value;
            case ZENVAR: {
                zfxvariant var;
                //����ָ����ȡzenvar��ֵ�����ϲ�ļ�������������ֵ�������ߵ�����

                //����ֵ��ִ�е�ʱ������Ҫ��һ���ⲿ��foreach��ִ�У����Ҵ��浽��ǰ�Ķ�ջ�
                const std::string& varname = get_zfxvar<std::string>(root->value);
                var = getVariable(varname);

                switch (root->opVal) {
                case Indexing: {
                    if (root->children.size() != 1) {
                        throw makeError<UnimplError>("Indexing Error.");
                    }
                    int idx = get_zfxvar<int>(execute(root->children[0], pContext));
                    zfxvariant elemvar = get_array_element(var, idx);
                    return elemvar;
                }
                case COMPVISIT: {
                    if (root->children.size() != 1) {
                        throw makeError<UnimplError>("Indexing Error on NameVisit");
                    }
                    std::string component = get_zfxvar<std::string>(root->children[0]->value);
                    return get_element_by_name(var, component);
                }
                case BulitInVar: {
                    std::string attrname = get_zfxvar<std::string>(root->value);
                    if (attrname.size() < 2 || attrname[0] != '$') {
                        throw makeError<UnimplError>("build in var");
                    }
                    attrname = attrname.substr(1);
                    if (attrname == "F") {
                        //TODO
                    }
                    else if (attrname == "T") {
                        //TODO
                    }
                    else if (attrname == "FPS") {
                        //TODO
                    }
                }
                case AutoIncreaseFirst: {
                    selfIncOrDec(var, true);
                    return var;
                }
                case AutoDecreaseFirst: {
                    selfIncOrDec(var, false);
                    return var;
                }
                case AutoIncreaseLast:
                case AutoDecreaseLast:  //������������/��
                {
                    //TODO: @P.x++�������û����
                    const std::string& varname = get_zfxvar<std::string>(root->value);
                    ZfxVariable& varself = getVariableRef(varname);
                    selfIncOrDec(varself.value, AutoIncreaseLast == root->opVal);
                    return varself.value;
                }
                default: {
                    return var;
                }
                }
            }
            case FUNC: {
                //����
                std::vector<zfxvariant> args = process_args(root, pContext);
                const std::string& funcname = get_zfxvar<std::string>(root->value);
                zfxvariant result = eval(funcname, args, pContext);
                return result;
            }
            case FOUROPERATIONS: {
                //��������+ - * / %
                std::vector<zfxvariant> args = process_args(root, pContext);
                if (args.size() != 2) {
                    throw makeError<UnimplError>("op args");
                }
                switch (root->opVal) {
                case PLUS:  return calc_exp(args[0], args[1], std::plus());
                case MINUS: return calc_exp(args[0], args[1], std::minus());
                case MUL:   return calc_exp(args[0], args[1], std::multiplies());
                case DIV:   return calc_exp(args[0], args[1], std::divides());
                default:
                    throw makeError<UnimplError>("op error");
                }
            }
            case COMPOP: {
                //������
                std::vector<zfxvariant> args = process_args(root, pContext);
                if (args.size() != 2) {
                    throw makeError<UnimplError>("compare op args");
                }
                switch (root->opVal) {
                case Less:          return calc_exp(args[0], args[1], std::less());
                case LessEqual:     return calc_exp(args[0], args[1], std::less_equal());
                case Greater:       return calc_exp(args[0], args[1], std::greater());
                case GreaterEqual:  return calc_exp(args[0], args[1], std::greater_equal());
                case Equal:         return calc_exp(args[0], args[1], std::equal_to());
                case NotEqual:      return calc_exp(args[0], args[1], std::not_equal_to());
                default:
                    throw makeError<UnimplError>("compare op error");
                }
            }             
            case CONDEXP: {
                //�������ʽ
                std::vector<zfxvariant> args = process_args(root, pContext);
                if (args.size() != 3) {
                    throw makeError<UnimplError>("cond exp args");
                }
                return std::visit([args](auto&& val) -> zfxvariant {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, int> || std::is_same_v<T, float>) {
                        int condval = (int)val;
                        if (condval) {
                            return args[1];
                        }
                        else {
                            return args[2];
                        }
                    }
                    else {
                        throw makeError<UnimplError>("condexp type error");
                    }
                }, args[0]);
            }
            case ARRAY:{
                return parseArray(root, pContext);
            }
            case PLACEHOLDER:{
                return zfxvariant();
            }
            case DECLARE:{
                //��������
                int nChildren = root->children.size();
                if (nChildren != 2 && nChildren != 3) {
                    throw makeError<UnimplError>("args of DECLARE");
                }
                std::shared_ptr<ZfxASTNode> typeNode = root->children[0];
                std::shared_ptr<ZfxASTNode> nameNode = root->children[1];
                ZfxVariable newvar;
                bool bOnlyDeclare = nChildren == 2;
                operatorVals vartype = typeNode->opVal;
                if (bOnlyDeclare) {
                    switch (vartype)
                    {
                    case TYPE_INT:      newvar.value = 0; break;
                    case TYPE_INT_ARR:  newvar.value = zfxintarr();   break;
                    case TYPE_FLOAT:    newvar.value = 0.f;   break;
                    case TYPE_FLOAT_ARR:    newvar.value = zfxfloatarr();   break;
                    case TYPE_STRING:       newvar.value = "";              break;
                    case TYPE_STRING_ARR:   newvar.value = zfxstringarr();  break;
                    case TYPE_VECTOR2:  newvar.value = glm::vec2(); break;
                    case TYPE_VECTOR3:  newvar.value = glm::vec3(); break;
                    case TYPE_VECTOR4:  newvar.value = glm::vec4(); break;
                    case TYPE_MATRIX2:  newvar.value = glm::mat2(); break;
                    case TYPE_MATRIX3:  newvar.value = glm::mat3(); break;
                    case TYPE_MATRIX4:  newvar.value = glm::mat4(); break;
                    }
                }
                else {
                    std::shared_ptr<ZfxASTNode> valueNode = root->children[2];
                    newvar.value = execute(valueNode, pContext);
                }

                std::string varname = get_zfxvar<std::string>(nameNode->value);
                //��ʱ�������Զ���ṹ��
                bool bret = declareVariable(varname, newvar);
                if (!bret) {
                    throw makeError<UnimplError>("assign variable failed.");
                }

                validateVar(vartype, newvar.value);

                bret = assignVariable(varname, newvar);
                if (!bret) {
                    throw makeError<UnimplError>("assign variable failed.");
                }
                break;
            }
            case ASSIGNMENT:{
                //��ֵ
                if (root->children.size() != 2) {
                    throw makeError<UnimplError>("assign variable failed.");
                }
                std::shared_ptr<ZfxASTNode> zenvarNode = root->children[0];
                std::shared_ptr<ZfxASTNode> valNode = root->children[1];

                const std::string& targetvar = get_zfxvar<std::string>(zenvarNode->value);

                zfxvariant res = execute(valNode, pContext);

                if (root->opVal == AddAssign) {
                    zfxvariant varres = execute(zenvarNode, pContext);
                    res = calc_exp(varres, res, std::plus());
                }
                else if (root->opVal == MulAssign) {
                    zfxvariant varres = execute(zenvarNode, pContext);
                    res = calc_exp(varres, res, std::multiplies());
                }
                else if (root->opVal == SubAssign) {
                    zfxvariant varres = execute(zenvarNode, pContext);
                    res = calc_exp(varres, res, std::minus());
                }
                else if (root->opVal == DivAssign) {
                    zfxvariant varres = execute(zenvarNode, pContext);
                    res = calc_exp(varres, res, std::divides());
                }

                //���Ա����������������ͨ����һ��������ջ������
                /*
                if (zenvarNode->bAttr) {
                    std::string attrname = targetvar;
                    zfxvariant opval;
                    if (!zenvarNode->children.empty()) {
                        opval = execute(zenvarNode->children[0], pContext);
                    }
                    updateGeomAttr(attrname, res, zenvarNode->opVal, opval, pContext);
                }
                else
                */
                {
                    //ֱ�ӽ�������
                    ZfxVariable& var = getVariableRef(targetvar);

                    if (zenvarNode->bAttr) {
                        var.bAttrUpdated = true;
                    }

                    switch (zenvarNode->opVal) {
                    case Indexing: {
                        if (zenvarNode->children.size() != 1) {
                            throw makeError<UnimplError>("Indexing Error.");
                        }
                        int idx = get_zfxvar<int>(execute(zenvarNode->children[0], pContext));
                        set_array_element(var.value, idx, res);
                        return zfxvariant();  //���践��ʲô�����ֵ
                    }
                    case COMPVISIT: {
                        if (zenvarNode->children.size() != 1) {
                            throw makeError<UnimplError>("Indexing Error on NameVisit");
                        }
                        std::string component = get_zfxvar<std::string>(zenvarNode->children[0]->value);
                        set_element_by_name(var.value, component, res);
                        return zfxvariant();
                    }
                    case BulitInVar: {
                        //TODO: ʲô�������Ҫ�޸����ֱ���
                        //$F $T��Щò�Ʋ���ͨ���ű�ȥ�ģ�houdiniҲ����������֪����û������
                        throw makeError<UnimplError>("Read-only variable cannot be modified.");
                    }
                    case AutoDecreaseFirst: 
                    case AutoIncreaseFirst:
                    case AutoIncreaseLast:
                    case AutoDecreaseLast:
                    default: {
                        //������/��,�ٸ�ֵ���ƺ�û�����壬���Ժ���
                        var.value = res;
                        return zfxvariant();
                    }
                    }
                }
                break;
            }
            case FOREACH_ATTR: {

                //ѹջ
                pushStack();
                scope_exit sp([&]() {
                    //commit to object

                    this->popStack();
                });

                assert(root->children.size() == 2);

                //�����������Ա���
                auto spAttrs = root->children[0];
                assert(EACH_ATTRS == spAttrs->type);

                auto spBody = root->children[1];
                assert(CODEBLOCK == spBody->type);

                //�ȼٶ���points�������
                for (auto spAttr : spAttrs->children)
                {
                    const std::string attrname = get_zfxvar<std::string>(spAttr->value);
                    declareVariable(attrname);
                }

                //����runover���������ļ���
                if (pContext->runover == RunOver_Points)
                {
                    auto iter = getPointsBeginIter(pContext);

                    m_stacks.rbegin()->iterRunOverObjs = iter;

                    while (iter != getPointsEndIter(pContext)) {

                        for (auto spAttr : spAttrs->children)
                        {
                            const std::string attrname = get_zfxvar<std::string>(spAttr->value);
                            zfxvariant elemval = getAttrValue(attrname, iter, pContext);
                            auto& attrRef = getVariableRef(attrname);
                            attrRef.value = elemval;
                        }

                        execute(spBody, pContext);

                        //commit changes on attr value into prim.
                        commitToPrim(iter);

                        iter = getPointsNextIter(pContext);
                    }
                }

                break;
            }
            case IF:{
                if (root->children.size() != 2) {
                    throw makeError<UnimplError>("if cond failed.");
                }
                auto pCondExp = root->children[0];
                //todo: self inc
                int cond = get_zfxvar<int>(execute(pCondExp, pContext));
                if (cond) {
                    auto pCodesExp = root->children[1];
                    execute(pCodesExp, pContext);
                }
                return zfxvariant();
            }
            case FOR:{
                if (root->children.size() != 4) {
                    throw makeError<UnimplError>("for failed.");
                }
                auto forBegin = root->children[0];
                auto forCond = root->children[1];
                auto forStep = root->children[2];
                auto loopContent = root->children[3];

                //ѹջ
                pushStack();
                scope_exit sp([this]() {this->popStack(); });

                //�鿴begin������Ƿ��ж�����䣬��ֵ���
                switch (forBegin->type)
                {
                case DECLARE:
                {
                    execute(forBegin, pContext);
                    break;
                }
                case ASSIGNMENT:
                {
                    execute(forBegin, pContext);
                    break;
                }
                case PLACEHOLDER:
                default:
                    //����������û�����壬�������ܵ��½�������
                    execute(forBegin, pContext);
                    break;
                }

                int cond = get_zfxvar<int>(execute(forCond, pContext));
                while (cond) {
                    execute(loopContent, pContext);     //CodeBlock������ܻ��ѹջһ�Σ�û��ϵ���������ǿ��õ���

                    if (pContext->jumpFlag == JUMP_BREAK)
                        break;
                    if (pContext->jumpFlag == JUMP_CONTINUE)
                        continue;
                    if (pContext->jumpFlag == JUMP_RETURN)
                        return zfxvariant();

                    execute(forStep, pContext);
                    cond = get_zfxvar<int>(execute(forCond, pContext));
                }
                break;
            }
            case FOREACH:{
                //��Ӧ�ķ���FOREACH LPAREN foreach-step COLON zenvar RPAREN code-block
                //foreach-step ������һ���������ӳ�Ա��������vec<ASTNode>
                int nChild = root->children.size();
                if (nChild == 3 || nChild == 4) {
                    //û������
                    auto idxNode = nChild == 3 ? nullptr : root->children[0];
                    auto varNode = nChild == 3 ? root->children[0] : root->children[1];
                    auto arrNode = nChild == 3 ? root->children[1] : root->children[2];
                    auto codeSeg = nChild == 3 ? root->children[2] : root->children[3];
                    if (!varNode || !arrNode || !codeSeg)
                        throw makeError<UnimplError>("elements on foreach error.");

                    zfxvariant arr = execute(arrNode, pContext);

                    //ѹջ
                    pushStack();
                    scope_exit sp([this]() {this->popStack(); });

                    //����idxNode��ָ�������Ϊֵ i
                    //����varNode��ָ�������Ϊֵ arrtest[i]
                    std::string idxName;
                    if (idxNode) {
                        idxName = get_zfxvar<std::string>(idxNode->value);
                        declareVariable(idxName);
                    }
                    const std::string& varName = get_zfxvar<std::string>(varNode->value);
                    declareVariable(varName);

                    std::visit([&](auto&& val) {
                        using T = std::decay_t<decltype(val)>;
                        if constexpr (std::is_same_v<T, zfxintarr> ||
                            std::is_same_v<T, zfxfloatarr> ||
                            std::is_same_v<T, zfxstringarr>) {

                            for (int i = 0; i < val.size(); i++) {
                                //�޸ı�����������ֵΪi, arrtest[i];
                                if (idxNode) {
                                    ZfxVariable zfxvar;
                                    zfxvar.value = i;
                                    assignVariable(idxName, zfxvar);
                                }

                                ZfxVariable zfxvar;
                                zfxvar.value = val[i];
                                assignVariable(varName, zfxvar);

                                //�޸Ķ�����ٴ�����code
                                execute(codeSeg, pContext);

                                //����Ƿ�����ת, continue��execute�ڲ��Ѿ������ˣ����ﲻ��Ҫ����
                                if (pContext->jumpFlag == JUMP_BREAK ||
                                    pContext->jumpFlag == JUMP_RETURN) {
                                    return;
                                }
                            }
                        }
                        else if constexpr (std::is_same_v<T, glm::vec2> ||
                            std::is_same_v<T, glm::vec3> ||
                            std::is_same_v<T, glm::vec4>) {

                            for (int i = 0; i < val.length(); i++) {
                                //�޸ı�����������ֵΪi, arrtest[i];
                                if (idxNode) {
                                    ZfxVariable zfxvar;
                                    zfxvar.value = i;
                                    assignVariable(idxName, zfxvar);
                                }

                                ZfxVariable zfxvar;
                                zfxvar.value = val[i];
                                assignVariable(varName, zfxvar);

                                //�޸Ķ�����ٴ�����code
                                execute(codeSeg, pContext);

                                //����Ƿ�����ת, continue��execute�ڲ��Ѿ������ˣ����ﲻ��Ҫ����
                                if (pContext->jumpFlag == JUMP_BREAK ||
                                    pContext->jumpFlag == JUMP_RETURN) {
                                    return;
                                }
                            }
                        }
                        else {
                            throw makeError<UnimplError>("foreach error: no array type");
                        }
                    }, arr);
                    return zfxvariant();
                }
                else {
                    throw makeError<UnimplError>("foreach error.");
                }
                break;
            }
            case WHILE:{
                if (root->children.size() != 2) {
                    throw makeError<UnimplError>("while failed.");
                }
   
                auto forCond = root->children[0];
                auto loopContent = root->children[1];

                //ѹջ
                pushStack();
                scope_exit sp([this]() {this->popStack(); });

                int cond = get_zfxvar<int>(execute(forCond, pContext));
                while (cond) {
                    execute(loopContent, pContext);     //CodeBlock������ܻ��ѹջһ�Σ�û��ϵ���������ǿ��õ���

                    if (pContext->jumpFlag == JUMP_BREAK)
                        break;
                    if (pContext->jumpFlag == JUMP_CONTINUE)
                        continue;
                    if (pContext->jumpFlag == JUMP_RETURN)
                        return zfxvariant();

                    cond = get_zfxvar<int>(execute(forCond, pContext));
                }
                break;
            }
            case DOWHILE:{
                if (root->children.size() != 2) {
                    throw makeError<UnimplError>("while failed.");
                }

                auto forCond = root->children[1];
                auto loopContent = root->children[0];

                //ѹջ
                pushStack();
                scope_exit sp([this]() {this->popStack(); });
                int cond = 0;

                do {
                    execute(loopContent, pContext);     //CodeBlock������ܻ��ѹջһ�Σ�û��ϵ���������ǿ��õ���

                    if (pContext->jumpFlag == JUMP_BREAK)
                        break;
                    if (pContext->jumpFlag == JUMP_CONTINUE)
                        continue;
                    if (pContext->jumpFlag == JUMP_RETURN)
                        return zfxvariant();
                    int cond = get_zfxvar<int>(execute(forCond, pContext));
                } while (cond);

                break;
            }
            case CODEBLOCK:{
                //����﷨����Ϊchildren�Ĵ����
                //ѹջ����������ջ��
                pushStack();
                scope_exit sp([this]() {this->popStack(); });
                for (auto pSegment : root->children) {
                    execute(pSegment, pContext);
                    if (pContext->jumpFlag == JUMP_BREAK ||
                        pContext->jumpFlag == JUMP_CONTINUE ||
                        pContext->jumpFlag == JUMP_RETURN) {
                        return zfxvariant();
                    }
                }
            }
            case JUMP:{
                pContext->jumpFlag = root->opVal;
                return zfxvariant();
            }
            case VARIABLETYPE:{
                //�������ͣ�����int vector3 float string��
            }
            default: {
                break;
            }
        }
        return zfxvariant();
    }

    PointsIterator FunctionManager::getPointsBeginIter(ZfxContext* pContext) {
        assert(pContext->spObject);
        return pContext->spObject->verts.begin();
    }

    PointsIterator FunctionManager::getPointsNextIter(ZfxContext* pContext) {
        assert(pContext->spObject);
        for (auto iter = m_stacks.rbegin(); iter != m_stacks.rend(); iter++) {
            if (iter->iterRunOverObjs != getPointsEndIter(pContext)) {
                if (iter->bAttrAddOrRemoved) {
                    return iter->iterRunOverObjs;
                }
                else {
                    return ++iter->iterRunOverObjs;
                }
            }
        }
        throw makeError<UnimplError>("error on iteration from runover prims");
    }

    PointsIterator FunctionManager::getPointsEndIter(ZfxContext* pContext) {
        assert(pContext->spObject);
        return pContext->spObject->verts.end();
    }

    void FunctionManager::commitToPrim(PointsIterator currIter) {
        //�ҵ���ǰ��ջ���������ԣ���һ�ύ
        assert(!m_stacks.empty());
        auto topStack = m_stacks.rbegin();
        for (auto iter = topStack->table.begin(); iter != topStack->table.end(); iter++)
        {
            const std::string& varname = iter->first;
            if (!iter->second.bAttrUpdated)
                continue;
            assert(!varname.empty());
            auto& vec = *currIter;
            if (varname.at(0) == '@') {
                if (varname == "@P") {
                    std::visit([&](auto&& val) {
                        using T = std::decay_t<decltype(val)>;
                        if constexpr (std::is_same_v<T, glm::vec2>) {
                            vec[0] = val[0];
                            vec[1] = val[1];
                        }
                        else if constexpr (std::is_same_v<T, glm::vec3>) {
                            vec[0] = val[0];
                            vec[1] = val[1];
                            vec[2] = val[2];
                        }
                        else if constexpr (std::is_same_v<T, glm::vec4>) {
                            vec[0] = val[0];
                            vec[1] = val[1];
                            vec[2] = val[2];
                            vec[3] = val[3];
                        }
                        else {
                            throw makeError<UnimplError>("error type of @P stored on stack");
                        }
                    }, iter->second.value);
                }
                else if (varname == "@N") {

                }
                else if (varname == "@ptnum") {
                    assert(false);
                }
                else {
                    //TODO
                }
            }
        }
    }

    zfxvariant FunctionManager::getAttrValue(const std::string& attrname, PointsIterator iter, ZfxContext* pContext) {
        if (attrname == "@P") {
            auto& vec = *iter;
            int n = vec.size();
            if (n == 2) {
                return glm::vec2(vec[0], vec[1]);
            }
            else if (n == 3) {
                return glm::vec3(vec[0], vec[1], vec[2]);
            }
            else if (n == 4) {
                return glm::vec4(vec[0], vec[1], vec[2], vec[3]);
            }
            else {
                throw makeError<UnimplError>("invalid vector size from attrs of prim");
            }
            return zfxvariant();
        }
        else if (attrname == "@ptnum") {
            auto beginIter = getPointsBeginIter(pContext);
            int idx = std::distance(beginIter, iter);
            return idx;
        }
        else if (attrname == "@N") {
            //TODO
            return zfxvariant();
        }
        else if (attrname == "@Cd") {
            return zfxvariant();
        }
        else {
            return zfxvariant();
        }
    }

    zfxvariant FunctionManager::calc(std::shared_ptr<ZfxASTNode> root, ZfxContext* pContext) {
        switch (root->type)
        {
            case nodeType::NUMBER:
            case nodeType::STRING: return root->value;
            case nodeType::ZENVAR:
            {
                const std::string& var = std::get<std::string>(root->value);
                if (var == "F") {
                    return (float)zeno::getSession().globalState->getFrameId();
                }
                else if (var == "FPS") {
                    //TODO
                    return zfxvariant();
                }
                else if (var == "T") {
                    //TODO
                    return zfxvariant();
                }
            }
            case nodeType::FOUROPERATIONS:
            {
                if (root->children.size() != 2)
                {
                    throw makeError<UnimplError>();
                }
                zfxvariant lhs = calc(root->children[0], pContext);
                zfxvariant rhs = calc(root->children[1], pContext);

                const std::string& var = std::get<std::string>(root->value);
                if (var == "+") {
                    //TODO: vector
                    return std::get<float>(lhs) + std::get<float>(rhs);
                }
                else if (var == "-") {
                    return std::get<float>(lhs) - std::get<float>(rhs);
                }
                else if (var == "*") {
                    return std::get<float>(lhs) * std::get<float>(rhs);
                }
                else if (var == "/") {
                    if (std::get<float>(rhs) == 0)
                        throw makeError<UnimplError>();
                    return std::get<float>(lhs) / std::get<float>(rhs);
                }
                else {
                    return zfxvariant();
                }
            }
            case nodeType::FUNC:
            {
                const std::string& funcname = std::get<std::string>(root->value);
                if (funcname == "ref") {
                    if (root->children.size() != 1) throw makeError<UnimplError>();
                    const std::string ref = std::get<std::string>(calc(root->children[0], pContext));
                    float res = callRef(ref, pContext);
                    return res;
                }
                else {
                    //�ȼ�ƥ�����
                    if (funcname == "sin") {
                        if (root->children.size() != 1) throw makeError<UnimplError>();
                        float val = std::get<float>(calc(root->children[0], pContext));
                        return sin(val);
                    }
                    else if (funcname == "cos") {
                        if (root->children.size() != 1) throw makeError<UnimplError>();
                        float val = std::get<float>(calc(root->children[0], pContext));
                        return cos(val);
                    }
                    else if (funcname == "sinh") {
                        if (root->children.size() != 1) throw makeError<UnimplError>();
                        float val = std::get<float>(calc(root->children[0], pContext));
                        return sinh(val);
                    }
                    else if (funcname == "cosh") {
                        if (root->children.size() != 1) throw makeError<UnimplError>();
                        float val = std::get<float>(calc(root->children[0], pContext));
                        return cosh(val);
                    }
                    else if (funcname == "rand") {
                        if (!root->children.empty()) throw makeError<UnimplError>();
                        return rand();
                    }
                    else {
                        throw makeError<UnimplError>();
                    }
                }
            }
        }
        return zfxvariant();
    }

    std::string format_variable_size(const char* fmt, std::vector<zfxvariant> args) {
        return std::accumulate(
            std::begin(args),
            std::end(args),
            std::string{ fmt },
            [](std::string toFmt, zfxvariant arg) {
                return std::visit([toFmt](auto&& val)->std::string {
                    using T = std::decay_t<decltype(val)>;
                    if constexpr (std::is_same_v<T, int>) {
                        return format(toFmt, val);
                    }
                    else if constexpr (std::is_same_v<T, float>) {
                        return format(toFmt, val);
                    }
                    else if constexpr (std::is_same_v<T, std::string>) {
                        return format(toFmt, val);
                    }
                    else {
                        throw makeError<UnimplError>("error type on format string");
                    }
                    }, arg);
            }
        );
    }

    zfxvariant FunctionManager::eval(const std::string& funcname, const std::vector<zfxvariant>& args, ZfxContext* pContext) {
        if (funcname == "ref") {
            if (args.size() != 1)
                throw makeError<UnimplError>();
            const std::string ref = get_zfxvar<std::string>(args[0]);
            float res = callRef(ref, pContext);
            return res;
        }
        else if (funcname == "log") {
            if (args.empty()) {
                throw makeError<UnimplError>("empty args on log");
            }
            std::string formatString = get_zfxvar<std::string>(args[0]);

            std::vector<zfxvariant> _args = args;
            _args.erase(_args.begin());

            std::string ret = format_variable_size(formatString.c_str(), _args);
            zeno::log_only_print(ret);
            //pContext->printContent += ret;
        }
        else {
            //�ȼ�ƥ�����
            if (funcname == "sin") {
                if (args.size() != 1)
                    throw makeError<UnimplError>();
                float val = get_zfxvar<float>(args[0]);
                return sin(val);
            }
            else if (funcname == "cos") {
                if (args.size() != 1)
                    throw makeError<UnimplError>();
                float val = get_zfxvar<float>(args[0]);
                return cos(val);
            }
            else if (funcname == "sinh") {
                if (args.size() != 1)
                    throw makeError<UnimplError>();
                float val = get_zfxvar<float>(args[0]);
                return sinh(val);
            }
            else if (funcname == "cosh") {
                if (args.size() != 1)
                    throw makeError<UnimplError>();
                float val = get_zfxvar<float>(args[0]);
                return cosh(val);
            }
            else if (funcname == "rand") {
                if (!args.empty()) throw makeError<UnimplError>();
                return rand();
            }
            else {
                throw makeError<UnimplError>();
            }
        }
    }

    void FunctionManager::init() {
        m_funcs = {
            {"sin", 
                {"sin",
                "Return the sine of the argument",
                "float",
                {{"degree", "float"}}
                }
            },
            {"cos",
                {"cos",
                "Return the cose of the argument",
                "float",
                { {"degree", "float"}}}
            },
            {"sinh",
                {"sinh",
                "Return the hyperbolic sine of the argument",
                "float",
                { {"number", "float"}}}
            },
            {"cosh",
                {"cosh",
                "Return the hyperbolic cose of the argument",
                "float",
                { {"number", "float"}}}
            },
            {"ref",
                {"ref",
                "Return the value of reference param of node",
                "float",
                { {"path-to-param", "string"}}}
            },
            {"rand",
                {"rand",
                "Returns a pseudo-number number from 0 to 1",
                "float", {}}
            }
        };
    }

}