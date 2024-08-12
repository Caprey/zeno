#include <zeno/core/INode.h>
#include <zeno/core/IObject.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/core/reflectdef.h>


namespace zeno
{
    struct ZRECORD() TestReflectNode : zeno::INode
    {
        TestReflectNode() = default;

        ZENO_REFLECT_TYPE(TestReflectNode)

        ZMETHOD(Name = "做些事")
        int apply(std::string wtf, zeno::vec3f c) {
            param_b = wtf;
            return 233;
        }

        ZPROPERTY(Role = zeno::Role_InputObject, DisplayName = "Input Object", Socket = zeno::Socket_Owning)
        std::shared_ptr<zeno::IObject> m_prim;

        ZPROPERTY(Role = zeno::Role_OutputObject, DisplayName = "Output Object")
        std::shared_ptr<zeno::IObject> m_result;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Param A")
        int param_a = 3;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName="Param B", Control = zeno::Multiline)
        std::string param_b = "default";

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Param Options", Control = zeno::Combobox, ComboBoxItems = ("option A", "option B"))
        std::string param_options = "Option A";

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Dt", Control = zeno::Slider, range = (1, 100, 1))
        int dt = 0;

        ZPROPERTY(Role = zeno::Role_InputPrimitive, DisplayName = "Position")
        zeno::vec3f pos;

        ZPROPERTY(Role = zeno::Role_OutputPrimitive)
        zeno::vec3f outvec;
    };

    struct ZRECORD() SimpleReflect : zeno::INode
    {
        SimpleReflect() = default;

        ZENO_REFLECT_TYPE(SimpleReflect)

        std::string apply(std::shared_ptr<zeno::PrimitiveObject> input_obj, std::string wtf = "abc", zeno::vec3f c = zeno::vec3f({ 0,1,0 })/*, float& ret1, std::shared_ptr<zeno::IObject>&output_obj*/) {
            //ret1 = 8;
            return "";
        }
    };

    struct ZRECORD() ReadOnlyNode : zeno::INode
    {
        ZENO_REFLECT_TYPE(ReadOnlyNode)

        ReflectCustomUI m_uilayout = {
            _ObjectGroup {
                {
                    _ObjectParam {"input_obj", "Input Object", Socket_Owning},
                }
            },
            _ObjectGroup {
                {
                    //空字符串默认mapping到 apply的输出值
                    _ObjectParam {"", "Output Object", Socket_Owning},
                }
            },
            _ParamTab {
                "Tab1",
                {
                    _ParamGroup {
                        "Group1",
                        {
                            _Param { "name1", "Name 1", std::string("a1") },
                            _Param { "name2", "Name 2", std::string("a2") },
                        }
                    },
                    _ParamGroup {
                        "Group2",
                        {
                            _Param { "a1", "A1", 345}
                        }
                    },
                }
            },
            _ParamGroup {

            }
        };

        std::shared_ptr<const zeno::IObject> apply(
            std::shared_ptr<zeno::PrimitiveObject> input_obj,
            const std::string& name1 = "a1",
            const std::string& name2 = "a2",
            int a = 234,
            float b = 456.2)
        {
            return input_obj;
        }
    };
}