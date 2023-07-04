// https://github.com/alembic/alembic/blob/master/lib/Alembic/AbcGeom/Tests/PolyMeshTest.cpp
// WHY THE FKING ALEMBIC OFFICIAL GIVES NO DOC BUT ONLY "TESTS" FOR ME TO LEARN THEIR FKING LIB
#include <zeno/zeno.h>
#include <zeno/utils/logger.h>
#include <zeno/utils/fileio.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/PrimitiveTools.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/UserData.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/utils/string.h>
#include "rapidjson/document.h"

#include "draco/mesh/mesh.h"
#include "draco/core/decoder_buffer.h"
#include "draco/compression/decode.h"

namespace zeno {
struct ReadTile : INode {
    virtual void apply() override {
        auto path = get_input2<std::string>("path");
        auto json = zeno::file_get_content(path);
        rapidjson::Document doc;
        doc.Parse(json.c_str());
        const auto& root = doc["root"];
        const auto& children = root["children"];

        zeno::log_info("count {}", children.Size());

        for (auto i = 0; i < children.Size(); i++) {
            const auto &c = children[i];
            std::string uri = c["content"]["uri"].GetString();
            const auto &box = c["boundingVolume"]["box"];
            vec3f center = {
                    box[0].GetFloat(),
                    box[1].GetFloat(),
                    box[2].GetFloat(),
            };
            zeno::log_info("{}: {} {}", i, uri, center);
        }
    }
};

ZENDEFNODE(ReadTile, {
    {
        {"readpath", "path"},
    },
    {},
    {},
    {"alembic"},
});

namespace zeno_gltf {
    enum class ComponentType {
        GL_BYTE = 0x1400,
        GL_UNSIGNED_BYTE = 0x1401,
        GL_SHORT = 0x1402,
        GL_UNSIGNED_SHORT = 0x1403,
        GL_INT = 0x1404,
        GL_UNSIGNED_INT = 0x1405,
        GL_FLOAT = 0x1406,
        GL_DOUBLE = 0x140A,
    };
    enum class Type {
        SCALAR,
        VEC2,
        VEC3,
        VEC4,
    };
    struct Accessor {
        std::optional<int> bufferView;
        int count;
        ComponentType componentType;
        Type type;
    };
    struct BufferView {
        int buffer;
        int byteOffset;
        int byteLength;
        int byteStride;
    };
namespace fs = std::filesystem;

struct LoadGLTFModel : INode {
    virtual void apply() override {
        auto path = get_input2<std::string>("path");
        rapidjson::Document doc;
        std::vector<std::vector<char>> buffers;
        if (zeno::ends_with(path, ".glb", false)) {
            auto data = zeno::file_get_binary(path);
            auto reader = BinaryReader(data);
            reader.seek_from_begin(8);
            auto total_len = reader.read_LE<int>();
            auto json_len = reader.read_LE<int>();
            reader.skip(4);
            std::string json;
            for (auto i = 0; i < json_len; i++) {
                json.push_back(reader.read_LE<char>());
            }
            zeno::file_put_content("fuck3.json", json);
            doc.Parse(json.c_str());
            while (!reader.is_eof()) {
                auto len = reader.read_LE<int>();
                reader.skip(4);
                std::vector<char> buffer;
                for (auto i = 0; i < len; i++) {
                    buffer.push_back(reader.read_LE<char>());
                }
                buffers.push_back(buffer);
            }
        }
        else {
            auto json = zeno::file_get_content(path);
            doc.Parse(json.c_str());

            zeno::log_info("buffers {}", doc["buffers"].Size());
            for (auto i = 0; i < doc["buffers"].Size(); i++) {
                fs::path p = path;
                auto parent = p.parent_path().string();
                std::string bin_path = parent + '/' + doc["buffers"][i]["uri"].GetString();
                auto buffer = zeno::file_get_binary(bin_path);
                zeno::log_info("{}", bin_path);
                buffers.push_back(buffer);
            }
        }
        std::vector<Accessor> accessors;
        {
            for (auto i = 0; i < doc["accessors"].Size(); i++) {
                const auto & a = doc["accessors"][i];
                Accessor accessor;
                accessor.bufferView = std::nullopt;
                if (a.HasMember("bufferView")) {
                    accessor.bufferView = a["bufferView"].GetInt();
                }
                accessor.count = a["count"].GetInt();
                accessor.componentType = ComponentType(a["componentType"].GetInt());
                std::string str_type = a["type"].GetString();
                if (str_type == "SCALAR") {
                    accessor.type = Type::SCALAR;
                }
                else if (str_type == "VEC2") {
                    accessor.type = Type::VEC2;
                }
                else if (str_type == "VEC3") {
                    accessor.type = Type::VEC3;
                }
                else if (str_type == "VEC4") {
                    accessor.type = Type::VEC4;
                }
                accessors.push_back(accessor);
            }
        }

        std::vector<BufferView> bufferViews;
        {
            for (auto i = 0; i < doc["bufferViews"].Size(); i++) {
                const auto & v = doc["bufferViews"][i];
                BufferView bufferView;
                bufferView.buffer = v["buffer"].GetInt();
                bufferView.byteOffset = v.HasMember("byteOffset")? v["byteOffset"].GetInt() : 0;
                bufferView.byteStride = v.HasMember("byteStride")? v["byteStride"].GetInt() : 0;
                bufferView.byteLength = v["byteLength"].GetInt();
                bufferViews.push_back(bufferView);
            }
        }

        auto prim = std::make_shared<zeno::PrimitiveObject>();
        {
            const auto &mesh = doc["meshes"][0];
            const auto &primitive = mesh["primitives"][0];
            if (primitive.HasMember("extensions") && primitive["extensions"].HasMember("KHR_draco_mesh_compression")) {
                const auto &draco_info = primitive["extensions"]["KHR_draco_mesh_compression"];
                auto bufferView = draco_info["bufferView"].GetInt();
                zeno::log_info("KHR_draco_mesh_compression bufferView: {}", bufferView);
                const auto &bv = bufferViews[bufferView];
                draco::Decoder dracoDecoder;
                draco::DecoderBuffer dracoDecoderBuffer;
                auto pData = &buffers[bv.buffer][bv.byteOffset];
                dracoDecoderBuffer.Init(reinterpret_cast<char *>(pData), bv.byteLength);
                auto type_statusor = draco::Decoder::GetEncodedGeometryType(&dracoDecoderBuffer);
                const draco::EncodedGeometryType geom_type = type_statusor.value();
                if (geom_type != draco::TRIANGULAR_MESH) {
                    zeno::log_error("not draco::TRIANGULAR_MESH");
                    throw std::runtime_error("not draco::TRIANGULAR_MESH");
                }

                draco::StatusOr<std::unique_ptr<draco::Mesh>> decoderStatus = dracoDecoder.DecodeMeshFromBuffer(&dracoDecoderBuffer);
                auto mesh = std::move(decoderStatus).value();
                auto vertexCount = mesh->num_points();
                prim->resize(vertexCount);
                auto faceCount = mesh->num_faces();
                prim->tris.resize(faceCount);

                zeno::log_info("vertexCount: {}, faceCount: {}", vertexCount, faceCount);

                // from https://github.com/google/draco/blob/master/src/draco/io/obj_encoder.cc
                const draco::PointAttribute *const att = mesh->GetNamedAttribute(draco::GeometryAttribute::POSITION);
                for (draco::AttributeValueIndex i(0); i < static_cast<uint32_t>(att->size()); ++i) {
                    att->ConvertValue<float, 3>(i, prim->verts[i.value()].data());
                }
                for (draco::FaceIndex i(0); i < faceCount; ++i) {
                    int _0 = att->mapped_index(mesh->face(i)[0]).value();
                    int _1 = att->mapped_index(mesh->face(i)[1]).value();
                    int _2 = att->mapped_index(mesh->face(i)[2]).value();
                    prim->tris[i.value()] = {_0, _1, _2};
                }
            }
            else {
                {
                    const auto &position = primitive["attributes"]["POSITION"].GetInt();
                    const auto &acc = accessors[position];
                    const auto &bv = bufferViews[acc.bufferView.value()];
                    auto reader = BinaryReader(buffers[bv.buffer]);
                    reader.seek_from_begin(bv.byteOffset);
                    prim->resize(acc.count);
                    for (auto i = 0; i < acc.count; i++) {
                        prim->verts[i] = reader.read_LE<vec3f>();
                    }
                }
                {
                    auto index = primitive["indices"].GetInt();
                    const auto &acc = accessors[index];
                    const auto &bv = bufferViews[acc.bufferView.value()];
                    auto reader = BinaryReader(buffers[bv.buffer]);
                    reader.seek_from_begin(bv.byteOffset);
                    auto count = acc.count / 3;
                    prim->tris.resize(count);
                    for (auto i = 0; i < count; i++) {
                        if (acc.componentType == ComponentType::GL_UNSIGNED_SHORT) {
                            auto f0 = reader.read_LE<uint16_t>();
                            auto f1 = reader.read_LE<uint16_t>();
                            auto f2 = reader.read_LE<uint16_t>();
                            prim->tris[i] = {f0, f1, f2};
                        }
                        else {
                            zeno::log_info("not support componentType: {}", int(acc.componentType));
                        }
                    }
                }
            }
        }

        set_output("prim", std::move(prim));
    }
};

ZENDEFNODE(LoadGLTFModel, {
    {
        {"readpath", "path"},
    },
    {
        "prim"
    },
    {},
    {"alembic"},
});
}


} // namespace zeno
