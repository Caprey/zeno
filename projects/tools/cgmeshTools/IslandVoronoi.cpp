/* auto generated from: arts/cihouzhxxisland.zsg */
#include <zeno/zeno.h>
#include <zeno/extra/ISubgraphNode.h>
namespace {
struct IslandVoronoi : zeno::ISerialSubgraphNode {
    virtual const char *get_subgraph_json() override {
        return R"ZSL(
[["addNode", "PrimMarkIsland", "350cd87c-PrimMarkIsland"], ["bindNodeInput", "350cd87c-PrimMarkIsland", "prim", "04444fc3-SubInput", "port"], ["setNodeInput", "350cd87c-PrimMarkIsland", "tagAttr", "tag"], ["completeNode", "350cd87c-PrimMarkIsland"], ["addNode", "SubInput", "04444fc3-SubInput"], ["setNodeParam", "04444fc3-SubInput", "name", "prim"], ["setNodeParam", "04444fc3-SubInput", "type", ""], ["setNodeParam", "04444fc3-SubInput", "defl", ""], ["completeNode", "04444fc3-SubInput"], ["addNode", "SubOutput", "383e6af0-SubOutput"], ["bindNodeInput", "383e6af0-SubOutput", "port", "6d7b637a-EndForEach", "list"], ["setNodeParam", "383e6af0-SubOutput", "name", "primList"], ["setNodeParam", "383e6af0-SubOutput", "type", ""], ["setNodeParam", "383e6af0-SubOutput", "defl", ""], ["completeNode", "383e6af0-SubOutput"], ["addNode", "PrimitiveVoronoi", "c10e5d5b-PrimitiveVoronoi"], ["bindNodeInput", "c10e5d5b-PrimitiveVoronoi", "prim", "4f65c259-BeginForEach", "object"], ["setNodeInput", "c10e5d5b-PrimitiveVoronoi", "numParticles", 64], ["setNodeParam", "c10e5d5b-PrimitiveVoronoi", "DEPRECATED", ""], ["completeNode", "c10e5d5b-PrimitiveVoronoi"], ["addNode", "PrimUnmerge", "e494c006-PrimUnmerge"], ["bindNodeInput", "e494c006-PrimUnmerge", "prim", "29c274ff-PrimSimplifyTag", "prim"], ["setNodeInput", "e494c006-PrimUnmerge", "tagAttr", "tag"], ["setNodeInput", "e494c006-PrimUnmerge", "method", "verts"], ["completeNode", "e494c006-PrimUnmerge"], ["addNode", "BeginForEach", "4f65c259-BeginForEach"], ["bindNodeInput", "4f65c259-BeginForEach", "list", "e494c006-PrimUnmerge", "listPrim"], ["bindNodeInput", "4f65c259-BeginForEach", "accumate", "ac269373-NumericInt", "value"], ["completeNode", "4f65c259-BeginForEach"], ["addNode", "EndForEach", "0d93c1c3-EndForEach"], ["bindNodeInput", "0d93c1c3-EndForEach", "object", "0befc04c-MakeSmallList", "list"], ["bindNodeInput", "0d93c1c3-EndForEach", "accumate", "c35ae35b-NumericOperator", "ret"], ["setNodeInput", "0d93c1c3-EndForEach", "accept", true], ["bindNodeInput", "0d93c1c3-EndForEach", "FOR", "4f65c259-BeginForEach", "FOR"], ["setNodeParam", "0d93c1c3-EndForEach", "doConcat", false], ["completeNode", "0d93c1c3-EndForEach"], ["addNode", "MakeSmallList", "0befc04c-MakeSmallList"], ["bindNodeInput", "0befc04c-MakeSmallList", "obj0", "c10e5d5b-PrimitiveVoronoi", "primList"], ["bindNodeInput", "0befc04c-MakeSmallList", "obj1", "72dc3fdf-EndForEach", "list"], ["setNodeParam", "0befc04c-MakeSmallList", "doConcat", false], ["completeNode", "0befc04c-MakeSmallList"], ["addNode", "NumericInt", "ac269373-NumericInt"], ["setNodeParam", "ac269373-NumericInt", "value", 0], ["completeNode", "ac269373-NumericInt"], ["addNode", "NumericOperator", "c35ae35b-NumericOperator"], ["bindNodeInput", "c35ae35b-NumericOperator", "lhs", "3dc544af-Route", "output"], ["bindNodeInput", "c35ae35b-NumericOperator", "rhs", "87f10aec-ListLength", "length"], ["setNodeParam", "c35ae35b-NumericOperator", "op_type", "add"], ["completeNode", "c35ae35b-NumericOperator"], ["addNode", "ListLength", "87f10aec-ListLength"], ["bindNodeInput", "87f10aec-ListLength", "list", "c10e5d5b-PrimitiveVoronoi", "neighList"], ["completeNode", "87f10aec-ListLength"], ["addNode", "BeginForEach", "2acb568f-BeginForEach"], ["bindNodeInput", "2acb568f-BeginForEach", "list", "c10e5d5b-PrimitiveVoronoi", "neighList"], ["bindNodeInput", "2acb568f-BeginForEach", "SRC", "3dc544af-Route", "DST"], ["completeNode", "2acb568f-BeginForEach"], ["addNode", "EndForEach", "72dc3fdf-EndForEach"], ["bindNodeInput", "72dc3fdf-EndForEach", "object", "e63e0e88-NumericOperator", "ret"], ["setNodeInput", "72dc3fdf-EndForEach", "accept", true], ["bindNodeInput", "72dc3fdf-EndForEach", "FOR", "2acb568f-BeginForEach", "FOR"], ["setNodeParam", "72dc3fdf-EndForEach", "doConcat", false], ["completeNode", "72dc3fdf-EndForEach"], ["addNode", "Route", "3dc544af-Route"], ["bindNodeInput", "3dc544af-Route", "input", "4f65c259-BeginForEach", "accumate"], ["completeNode", "3dc544af-Route"], ["addNode", "NumericOperator", "e63e0e88-NumericOperator"], ["bindNodeInput", "e63e0e88-NumericOperator", "lhs", "2acb568f-BeginForEach", "object"], ["bindNodeInput", "e63e0e88-NumericOperator", "rhs", "3dc544af-Route", "output"], ["setNodeParam", "e63e0e88-NumericOperator", "op_type", "add"], ["completeNode", "e63e0e88-NumericOperator"], ["addNode", "BeginForEach", "fa6491c3-BeginForEach"], ["bindNodeInput", "fa6491c3-BeginForEach", "list", "0d93c1c3-EndForEach", "list"], ["completeNode", "fa6491c3-BeginForEach"], ["addNode", "EndForEach", "6d7b637a-EndForEach"], ["bindNodeInput", "6d7b637a-EndForEach", "object", "6730eafc-ListGetItem", "object"], ["setNodeInput", "6d7b637a-EndForEach", "accept", true], ["bindNodeInput", "6d7b637a-EndForEach", "FOR", "fa6491c3-BeginForEach", "FOR"], ["setNodeParam", "6d7b637a-EndForEach", "doConcat", false], ["setNodeOption", "6d7b637a-EndForEach", "VIEW"], ["completeNode", "6d7b637a-EndForEach"], ["addNode", "ListGetItem", "6730eafc-ListGetItem"], ["bindNodeInput", "6730eafc-ListGetItem", "list", "fa6491c3-BeginForEach", "object"], ["setNodeInput", "6730eafc-ListGetItem", "index", 0], ["completeNode", "6730eafc-ListGetItem"], ["addNode", "EndForEach", "d3364633-EndForEach"], ["bindNodeInput", "d3364633-EndForEach", "object", "c2b5dccb-ListGetItem", "object"], ["setNodeInput", "d3364633-EndForEach", "accept", true], ["bindNodeInput", "d3364633-EndForEach", "FOR", "6c58306b-BeginForEach", "FOR"], ["setNodeParam", "d3364633-EndForEach", "doConcat", false], ["completeNode", "d3364633-EndForEach"], ["addNode", "ListGetItem", "c2b5dccb-ListGetItem"], ["bindNodeInput", "c2b5dccb-ListGetItem", "list", "6c58306b-BeginForEach", "object"], ["setNodeInput", "c2b5dccb-ListGetItem", "index", 1], ["completeNode", "c2b5dccb-ListGetItem"], ["addNode", "BeginForEach", "6c58306b-BeginForEach"], ["bindNodeInput", "6c58306b-BeginForEach", "list", "0d93c1c3-EndForEach", "list"], ["completeNode", "6c58306b-BeginForEach"], ["addNode", "SubOutput", "b5b81a1e-SubOutput"], ["bindNodeInput", "b5b81a1e-SubOutput", "port", "d3364633-EndForEach", "list"], ["setNodeParam", "b5b81a1e-SubOutput", "name", "neighList"], ["setNodeParam", "b5b81a1e-SubOutput", "type", ""], ["setNodeParam", "b5b81a1e-SubOutput", "defl", ""], ["completeNode", "b5b81a1e-SubOutput"], ["addNode", "PrimSimplifyTag", "29c274ff-PrimSimplifyTag"], ["bindNodeInput", "29c274ff-PrimSimplifyTag", "prim", "350cd87c-PrimMarkIsland", "prim"], ["setNodeInput", "29c274ff-PrimSimplifyTag", "tagAttr", "tag"], ["completeNode", "29c274ff-PrimSimplifyTag"]]
)ZSL";
    }
};
ZENDEFNODE(IslandVoronoi, {
    {{gParamType_Primitive, "prim", ""}},
    {{gParamType_List, "primList", ""}, {gParamType_List, "neighList" ""}},
    {},
    {"deprecated"},
});
}
