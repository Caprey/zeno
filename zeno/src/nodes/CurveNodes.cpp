#include <zeno/zeno.h>
#include <zeno/funcs/ParseObjectFromUi.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/para/parallel_for.h>
#include <zeno/utils/safe_at.h>
#include <zeno/utils/zeno_p.h>
#include <zeno/utils/arrayindex.h>
#include <reflect/container/any>
#include <reflect/type.hpp>


namespace zeno {

struct MakeCurve : zeno::INode {
    virtual void apply() override {
        bool bExist = false;
        const auto& param = get_input_prim_param("curve", &bExist);
        auto pCurve = get_input_prim<CurvesData>("curve");
        if (!pCurve) {
            throw;
        }
        set_primitive_output("curve", *pCurve);
    }
};

ZENO_DEFNODE(MakeCurve)({
    {
    },
    {
        {gParamType_Curve, "curve"},
    },
    {
        //{gParamType_String, "madebypengsensei", "pybyes"},
        {gParamType_Curve, "curve", ""},
    },
    {"curve"},
});

struct EvalCurve : zeno::INode {
    virtual void apply() override {
        auto curve = get_input_prim<CurvesData>("curve");
        if (!curve) {
            throw;
        }

        auto input = get_input2<NumericValue>("value");
        auto output = std::visit([&] (auto const &src) -> NumericValue {
            return curve->eval(src);
        }, input);
        set_output2("value", output);
    }
};

ZENO_DEFNODE(EvalCurve)({
    {
        {gParamType_Float, "value"},
        {gParamType_Curve, "curve", "", zeno::Socket_Primitve},
    },
    {
        {gParamType_Float, "value"},
    },
    {},
    {"curve"},
});

struct EvalCurveOnPrimAttr : zeno::INode {
    virtual void apply() override {
        auto prim = get_input<PrimitiveObject>("prim");
        auto curve = get_input_prim<CurvesData>("curve");
        if (!curve || !prim) {
            throw;
        }

        auto attrName = get_input2<std::string>("attrName");
        auto dstName = get_input2<std::string>("dstName");
        prim->attr_visit(attrName, [&](auto &arr) {
            if (dstName.empty() || dstName == attrName) {
                parallel_for_each(arr.begin(), arr.end(), [&] (auto &val) {
                    val = curve->eval(val);
                });
            }
            else {
                using T = std::decay_t<decltype(arr[0])>;
                auto& dstAttr = prim->add_attr<T>(dstName);
                parallel_for(arr.size(), [&] (auto i) {
                    dstAttr[i] = curve->eval(arr[i]);
                });
            }
        });
        set_output("prim", prim);
    }
};

ZENO_DEFNODE(EvalCurveOnPrimAttr)({
    {
        {gParamType_Primitive, "prim", "", zeno::Socket_ReadOnly},
        {gParamType_String, "attrName", "tmp"},
        {gParamType_String, "dstName", ""},
        {gParamType_Curve, "curve", "", zeno::Socket_Primitve},
    },
    {
        {gParamType_Primitive, "prim"},
    },
    {},
    {"curve"},
});


#ifdef ENABLE_LEGACY_ZENO_NODE
struct GetCurveControlPoint : zeno::INode {
    virtual void apply() override {
        auto curve = get_input<CurvesData>("curve");
        auto key = get_input2<std::string>("key");
        int i = get_input2<int>("index");

        auto &data = safe_at(curve->keys, key, "curve key");
        if (i < 0 || i >= data.cpbases.size())
            throw makeError<KeyError>(std::to_string(i), "out of range of " + std::to_string(data.cpbases.size()));
        set_output2("point_x", data.cpbases[i]);
        set_output2("point_y", data.cpoints[i].v);
        set_output2("left_handler", data.cpoints[i].left_handler);
        set_output2("right_handler", data.cpoints[i].right_handler);
    }
};

ZENO_DEFNODE(GetCurveControlPoint)({
    {
        {gParamType_Curve, "curve", "", zeno::Socket_Primitve},
        {gParamType_String, "key", "x"},
        {gParamType_Int, "index", "0"},
    },
    {
        {gParamType_Float, "point_x"},
        {gParamType_Float, "point_y"},
        {gParamType_Vec2f, "left_handler"},
        {gParamType_Vec2f, "right_handler"},
    },
    {},
    {"curve"},
});

struct UpdateCurveControlPoint : zeno::INode {
    virtual void apply() override {
        auto curve = const_cast<CurvesData*>(get_input_prim<CurvesData>("curve"));
        auto key = get_input2<std::string>("key");
        int i = get_input2<int>("index");

        auto &data = safe_at(curve->keys, key, "curve key");
        if (i < 0 || i >= data.cpbases.size())
            throw makeError<KeyError>(std::to_string(i), "out of range of " + std::to_string(data.cpbases.size()));
        if (has_input("point_x"))
            data.cpbases[i] = get_input2<float>("point_x");
        if (has_input("point_y"))
            data.cpoints[i].v = get_input2<float>("point_y");
        if (has_input("left_handler"))
            data.cpoints[i].left_handler = get_input2<vec2f>("left_handler");
        if (has_input("right_handler"))
            data.cpoints[i].right_handler = get_input2<vec2f>("right_handler");

        set_primitive_output("curve", *curve);
    }
};

ZENO_DEFNODE(UpdateCurveControlPoint)({
    {
        {gParamType_Curve, "curve", "", zeno::Socket_Primitve},
        {gParamType_String, "key", "x"},
        {gParamType_Int, "index", "0"},
        {"optional float", "point_x"},
        {"optional float", "point_y"},
        {"optional vec2f", "left_handler"},
        {"optional vec2f", "right_handler"},
    },
    {
        {gParamType_Curve, "curve"},
    },
    {},
    {"curve"},
});

struct UpdateCurveCycleType : zeno::INode {
    virtual void apply() override {
        auto curve = const_cast<CurvesData*>(get_input_prim<CurvesData>("curve"));
        auto key = get_input2<std::string>("key");
        auto type = get_input2<std::string>("type");
        auto typeIndex = array_index_safe({"CLAMP", "CYCLE", "MIRROR"}, type, "CycleType");
        if (key.empty()) {
            for (auto &[k, v]: curve->keys) {
                v.cycleType = static_cast<CurveData::CycleType>(typeIndex); 
            }
        } else {
            curve->keys.at(key).cycleType = static_cast<CurveData::CycleType>(typeIndex); 
        }
        set_primitive_output("curve", *curve);
    }
};

ZENO_DEFNODE(UpdateCurveCycleType)({
    {
        {gParamType_Curve, "curve", "", zeno::Socket_Primitve},
        {gParamType_String, "key", "x"},
        {"enum CLAMP CYCLE MIRROR", "type", "CLAMP"},
    },
    {
        {gParamType_Curve, "curve"},
    },
    {},
    {"curve"},
});

struct UpdateCurveXYRange : zeno::INode {
    virtual void apply() override {
        auto curve = const_cast<CurvesData*>(get_input_prim<CurvesData>("curve"));
        auto key = get_input2<std::string>("key");
        auto &data = curve->keys.at(key);
        auto rg = data.rg;
        if (has_input("x_from"))
            rg.xFrom = get_input2<float>("x_from");
        if (has_input("x_to"))
            rg.xTo = get_input2<float>("x_to");
        if (has_input("y_from"))
            rg.yFrom = get_input2<float>("y_from");
        if (has_input("y_to"))
            rg.yTo = get_input2<float>("y_to");
        data.updateRange(rg);

        set_primitive_output("curve", *curve);
    }
};

ZENO_DEFNODE(UpdateCurveXYRange)({
    {
        {gParamType_Curve, "curve", "", zeno::Socket_Primitve},
        {gParamType_String, "key", "x"},
        {"optional float", "x_from"},
        {"optional float", "x_to"},
        {"optional float", "y_from"},
        {"optional float", "y_to"},
    },
    {
        {gParamType_Curve, "curve"},
    },
    {},
    {"curve"},
});

#endif

}
