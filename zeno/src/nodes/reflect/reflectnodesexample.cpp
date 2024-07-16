#include <zeno/core/INode.h>
#include <zeno/core/IObject.h>
#include "reflect/core.hpp"
#include "reflect/type"
#include "reflect/reflection_traits.hpp"
#include "reflect/reflection.generated.hpp"


namespace zeno
{
    struct ZRECORD() TestReflectExample : public zeno::INode
    {
        TestReflectExample() = default;

        ZMETHOD(Name = "��Щ��223")
        int apply(std::string wtf, char c) {
            param_bb = wtf;
            param_c = c;
            return 233;
        }

        virtual zeno::reflect::TypeHandle gettype() {
            return zeno::reflect::get_type<TestReflectExample>();
        }

        //ZPROPERTY(Role = "input", DisplayName = "Input Object")
        //zSharedObject m_prim;

        ZPROPERTY(Role = "input", DisplayName = "Param A")
        int param_aa = 3;

        ZPROPERTY(Role = "input", DisplayName = "Param B", Control = "Multiline")
        std::string param_bb = "default";

        ZPROPERTY(Role = "input", DisplayName = "Param Options", Control = "ComboBox"/*, ComboBoxItems = ("option A", "option B")*/)
        std::string param_options = "option A";

        char param_c = 'c';
    };
}