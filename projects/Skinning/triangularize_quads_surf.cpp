#include <zeno/zeno.h>
#include <zeno/logger.h>
#include <zeno/ListObject.h>
#include <zeno/NumericObject.h>
#include <zeno/PrimitiveObject.h>
#include <zeno/utils/UserData.h>
#include <zeno/StringObject.h>
#include <igl/copyleft/tetgen/tetrahedralize.h>

namespace{
using namespace zeno;

// struct TriangularizeQuadSurface : zeno::INode {
//     virtual void apply() override {
//         // the input surface use
//         auto surf = get_input<zeno::PrimitiveObject>("surf");
//         auto res = std::make_shared<zeno::PrimitiveObject>();

//         Eigen::MatrixXd V;
//         Eigen::MatrixXi F;

//         V.resize(surf->size(),3);
//         for(size_t i = 0;i < surf->size();++i)
//             V.row(i) << surf->verts[i][0],surf->verts[i][1],surf->verts[i][2];

//         F.resize(surf->tris.size(),3);
//         for(size_t i = 0;i < surf->tris.size();++i)
//             F.row(i) << surf->tris[i][0],surf->tris[i][1],surf->tris[i][2];

//         Eigen::MatrixXd TV;
//         Eigen::MatrixXi TT;
//         Eigen::MatrixXi TF;
//         igl::copyleft::tetgen::tetrahedralize(V,F,"pq1.414Y", TV,TT,TF);

//         res->resize(TV.rows());
//         for(size_t i = 0;i < res->size();++i)
//             res->verts[i] = zeno::vec3f(TV.row(i)[0],TV.row(i)[1],TV.row(i)[2]);

//         res->quads.resize(TT.rows());
//         for(size_t i = 0;i < TT.rows();++i)
//             res->quads[i] = zeno::vec4i(TT.row(i)[0],TT.row(i)[1],TT.row(i)[2],TT.row(i)[3]);

//         // res->tris.resize(TF.rows());
//         // for(size_t i = 0;i < TF.rows();++i)
//         //     res->tris[i] = zeno::vec3i(TF.row(i)[0],TF.row(i)[1],TF.row(i)[2]);

//         for(size_t i = 0;i < res->quads.size();++i){
//             auto tet = res->quads[i];
//             res->tris.emplace_back(tet[0],tet[1],tet[2]);
//             res->tris.emplace_back(tet[1],tet[3],tet[2]);
//             res->tris.emplace_back(tet[0],tet[2],tet[3]);
//             res->tris.emplace_back(tet[0],tet[3],tet[1]);
//         }


//         set_output("tets",std::move(res));
//     }
// };

// ZENDEFNODE(TetrahedralizeSurface, {
//     {"surf"},
//     {"tets"},
//     {},
//     {"Skinning"},
// });


// map the topology of src primitive to the destination primitive
struct AlignPrimitiveTopolgy : zeno::INode {
    virtual void apply() override {
        auto primSrc = get_input<zeno::PrimitiveObject>("primSrc");
        auto primDst = get_input<zeno::PrimitiveObject>("primDst");

        if(primSrc->size() != primDst->size()){
            throw std::runtime_error("THE SRC AND DST PRIM SHOULD HAVE SAME SIZE");
        }

        primDst->tris.resize(primSrc->tris.size());
        std::copy(primSrc->tris.begin(),primSrc->tris.end(),primDst->tris.begin());

        primDst->quads.resize(primSrc->quads.size());
        std::copy(primDst->quads.begin(),primSrc->quads.end(),primSrc->quads.begin());

        set_output("primOut",primDst);
    }
};

ZENDEFNODE(AlignPrimitiveTopolgy, {
    {{gParamType_Primitive, "primSrc"}, {gParamType_Primitive, "primDst"}},
    {{gParamType_Primitive, "primOut"}},
    {},
    {"Skinning"},
});


};