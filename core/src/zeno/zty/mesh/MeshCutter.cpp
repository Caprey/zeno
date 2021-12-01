#include <zeno/zty/mesh/MeshCutter.h>
#include <zeno/zmt/log.h>
#include <mcut/mcut.h>
#include <stdexcept>


static void mcCheckError_(McResult err, const char *expr, const char *file, int line)
{
    // https://cutdigital.github.io/mcut.site/tutorials/debugging/
    [[unlikely]] if (err != MC_NO_ERROR) {
        std::string errorStr;
        switch (err) {
            case MC_INVALID_OPERATION: errorStr = "MC_INVALID_OPERATION"; break;
            case MC_INVALID_VALUE:     errorStr = "MC_INVALID_VALUE"; break;
            case MC_OUT_OF_MEMORY:     errorStr = "MC_OUT_OF_MEMORY"; break;
            default:                   errorStr = std::to_string(err);
        }
        throw std::runtime_error(
            zmt::format("In {}:{}: {}: {}", file, line, expr, errorStr));
    }
}

#define mcCheckError(expr) mcCheckError_((expr), #expr, __FILE__, __LINE__)


ZENO_NAMESPACE_BEGIN
namespace zty {


struct MeshCutter::Impl {
    McContext ctx = MC_NULL_HANDLE;
    std::vector<McConnectedComponent> connComps;
};


MeshCutter::MeshCutter(Mesh const &mesh1, Mesh const &mesh2)
    : impl(std::make_unique<Impl>())
{
    mcCheckError(mcCreateContext
    ( &impl->ctx
    , MC_NULL_HANDLE
    ));

    printf("%ld %ld\n", mesh1.vert.size(), mesh1.poly.size());
    printf("%ld %ld\n", mesh2.vert.size(), mesh2.poly.size());

    mcCheckError(mcDispatch
    ( impl->ctx
    , MC_DISPATCH_VERTEX_ARRAY_FLOAT
    , (float *)mesh1.vert.data()
    , mesh1.loop.data()
    , mesh1.poly.data()
    , mesh1.vert.size()
    , mesh1.poly.size()
    , (float *)mesh2.vert.data()
    , mesh2.loop.data()
    , mesh2.poly.data()
    , mesh2.vert.size()
    , mesh2.poly.size()
    ));
}


void MeshCutter::selectComponents(CompType compType) const
{
    McConnectedComponentType mcCompType;
    switch (compType) {
    case CompType::all:
        mcCompType = MC_CONNECTED_COMPONENT_TYPE_ALL;
        break;
    case CompType::fragment:
        mcCompType = MC_CONNECTED_COMPONENT_TYPE_FRAGMENT;
        break;
    case CompType::patch:
        mcCompType = MC_CONNECTED_COMPONENT_TYPE_PATCH;
        break;
    case CompType::seam:
        mcCompType = MC_CONNECTED_COMPONENT_TYPE_SEAM;
        break;
    case CompType::input:
        mcCompType = MC_CONNECTED_COMPONENT_TYPE_INPUT;
        break;
    }

    uint32_t numConnComps;
    mcCheckError(mcGetConnectedComponents
    ( impl->ctx
    , mcCompType
    , 0
    , NULL
    , &numConnComps
    ));
    impl->connComps.resize(numConnComps);
    mcCheckError(mcGetConnectedComponents
    ( impl->ctx
    , mcCompType
    , numConnComps
    , impl->connComps.data()
    , NULL
    ));
}


size_t MeshCutter::getNumComponents() const
{
    return impl->connComps.size();
}


void MeshCutter::getComponent(size_t i, Mesh &mesh) const
{
    McConnectedComponent connComp = impl->connComps.at(i);
    uint64_t numBytes;

    mcCheckError(mcGetConnectedComponentData
    ( impl->ctx
    , connComp
    , MC_CONNECTED_COMPONENT_DATA_VERTEX_FLOAT
    , 0
    , NULL
    , &numBytes
    ));
    std::vector<math::vec3f> vertices(numBytes / sizeof(math::vec3f));
    mcCheckError(mcGetConnectedComponentData
    ( impl->ctx
    , connComp
    , MC_CONNECTED_COMPONENT_DATA_VERTEX_FLOAT
    , numBytes
    , vertices.data()
    , NULL
    ));

    mcCheckError(mcGetConnectedComponentData
    ( impl->ctx
    , connComp
    , MC_CONNECTED_COMPONENT_DATA_FACE
    , 0
    , NULL
    , &numBytes
    ));
    std::vector<uint32_t> faces(numBytes / sizeof(uint32_t));
    mcCheckError(mcGetConnectedComponentData
    ( impl->ctx
    , connComp
    , MC_CONNECTED_COMPONENT_DATA_FACE
    , numBytes
    , faces.data()
    , NULL
    ));

    mcCheckError(mcGetConnectedComponentData
    ( impl->ctx
    , connComp
    , MC_CONNECTED_COMPONENT_DATA_FACE_SIZE
    , 0
    , NULL
    , &numBytes
    ));
    std::vector<uint32_t> faceSizes(numBytes / sizeof(uint32_t));
    mcCheckError(mcGetConnectedComponentData
    ( impl->ctx
    , connComp
    , MC_CONNECTED_COMPONENT_DATA_FACE_SIZE
    , numBytes
    , faceSizes.data()
    , NULL
    ));

    mesh.vert = std::move(vertices);
    mesh.loop = std::move(faces);
    mesh.poly = std::move(faceSizes);
}


MeshCutter::~MeshCutter()
{
    mcCheckError(mcReleaseConnectedComponents
    ( impl->ctx
    , 0
    , NULL
    ));
    mcCheckError(mcReleaseContext
    ( impl->ctx
    ));
}


}
ZENO_NAMESPACE_END
