#include "zensim/io/MeshIO.hpp"
#include "zensim/math/bit/Bits.h"
#include "zensim/types/Property.h"
#include <atomic>
#include <zeno/VDBGrid.h>
#include <zeno/types/ListObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/StringObject.h>


#include "zensim/omp/execution/ExecutionPolicy.hpp"
#include "kernel/calculate_facet_normal.hpp"
#include "kernel/topology.hpp"
#include "kernel/compute_characteristic_length.hpp"
// #include "kernel/calculate_bisector_normal.hpp"
#include "kernel/tiled_vector_ops.hpp"
#include "kernel/calculate_edge_normal.hpp"

#include "kernel/topology.hpp"
#include "kernel/intersection.hpp"

#include "../fem/collision_energy/evaluate_collision.hpp"



#include <iostream>


#define COLLISION_VIS_DEBUG

#define MAX_FP_COLLISION_PAIRS 6

namespace zeno {

    using T = float;
    using vec3 = zs::vec<T,3>;
    using vec4 = zs::vec<T,4>;
    using mat3 = zs::vec<T,3,3>;
    using mat4 = zs::vec<T,4,4>;
    
    template<typename VTILEVEC> 
    constexpr vec3 eval_center(const VTILEVEC& verts,const zs::vec<int,4>& tet) {
        auto res = vec3::zeros();
        for(int i = 0;i != 4;++i)
            res += verts.template pack<3>("x",tet[i]) / (T)4.0;
        return res;
    } 

    template<typename VTILEVEC> 
    constexpr vec3 eval_center(const VTILEVEC& verts,const zs::vec<int,3>& tri) {
        auto res = vec3::zeros();
        for(int i = 0;i < 3;++i)
            res += verts.template pack<3>("x",tri[i]) / (T)3.0;
        return res;
    } 
    template<typename VTILEVEC> 
    constexpr vec3 eval_center(const VTILEVEC& verts,const zs::vec<int,2>& line) {
        auto res = vec3::zeros();
        for(int i = 0;i < 2;++i)
            res += verts.template pack<3>("x",line[i]) / (T)2.0;
        return res;
    } 

    struct VisualizeSurfaceMesh : INode {
        virtual void apply() override {
            using namespace zs;
            auto zsparticles = get_input<ZenoParticles>("ZSParticles");

            if(!zsparticles->hasAuxData(ZenoParticles::s_surfTriTag)){
                throw std::runtime_error("the input zsparticles has no surface tris");
            }
            if(!zsparticles->hasAuxData(ZenoParticles::s_surfEdgeTag)) {
                throw std::runtime_error("the input zsparticles has no surface lines");
            }
            if(!zsparticles->hasAuxData(ZenoParticles::s_surfVertTag)) {
                throw std::runtime_error("the input zsparticles has no surface points");
            }
            const auto &tris = zsparticles->category == ZenoParticles::category_e::tet ? (*zsparticles)[ZenoParticles::s_surfTriTag] : zsparticles->getQuadraturePoints();
            const auto& points  = (*zsparticles)[ZenoParticles::s_surfVertTag];
            const auto& verts = zsparticles->getParticles();

            // if(!tris.hasProperty("fp_inds") || tris.getPropertySize("fp_inds") != 3) {
            //     throw std::runtime_error("call ZSInitSurfaceTopology first before VisualizeSurfaceMesh");
            // }

            // transfer the data from gpu to cpu
            constexpr auto cuda_space = execspace_e::cuda;
            auto cudaPol = cuda_exec(); 

            bcht<int,int,true,universal_hash<int>,32> ptab{points.get_allocator(),points.size()};
            Vector<int> spi{points.get_allocator(),points.size()};
            cudaPol(range(points.size()),
            [ptab = proxy<cuda_space>(ptab),points = proxy<cuda_space>({},points),spi = proxy<cuda_space>(spi)] ZS_LAMBDA(int pi) mutable {
                auto pidx = reinterpret_bits<int>(points("inds",pi));
                if(int no = ptab.insert(pidx);no >= 0)
                    spi[no] = pi;
                else{
                    printf("same point [%d] has been inserted twice\n",pidx);
                }
            });

            auto nm_points = points.size();
            auto nm_tris = tris.size();

            auto xtag = get_param<std::string>("xtag");



            // auto surf_verts_buffer = typename ZenoParticles::particles_t({{"x",3}},points.size(),zs::memsrc_e::device,0);
            zs::Vector<zs::vec<float,3>> surf_verts_buffer{points.get_allocator(),points.size()};
            zs::Vector<zs::vec<int,3>> surf_tris_buffer{tris.get_allocator(),tris.size()};
            // auto surf_tris_buffer  = typename ZenoParticles::particles_t({{"inds",3}},tris.size(),zs::memsrc_e::device,0);
            // copy the verts' pos data to buffer
            cudaPol(zs::range(points.size()),
                [verts = proxy<cuda_space>({},verts),xtag = zs::SmallString(xtag),
                        points = proxy<cuda_space>({},points),surf_verts_buffer = proxy<cuda_space>(surf_verts_buffer)] ZS_LAMBDA(int pi) mutable {
                    auto v_idx = reinterpret_bits<int>(points("inds",pi));
                    surf_verts_buffer[pi] = verts.template pack<3>(xtag,v_idx);
            }); 

            // copy the tris topo to buffer
            // TILEVEC_OPS::copy<3>(cudaPol,tris,"fp_inds",surf_tris_buffer,"inds");
            cudaPol(zs::range(tris.size()),[
                surf_tris_buffer = proxy<cuda_space>(surf_tris_buffer),
                tris = proxy<cuda_space>({},tris),spi = proxy<cuda_space>(spi),ptab = proxy<cuda_space>(ptab)] ZS_LAMBDA(int ti) mutable {
                    auto inds = tris.pack(dim_c<3>,"inds",ti,int_c);
                    zs::vec<int,3> tinds{};
                    for(int i = 0;i != 3;++i) {
                        auto no = ptab.query(inds[i]);
                        tinds[i] = spi[no];
                    }
                    surf_tris_buffer[ti] = tinds;
            });

            surf_verts_buffer = surf_verts_buffer.clone({zs::memsrc_e::host});
            surf_tris_buffer = surf_tris_buffer.clone({zs::memsrc_e::host});


            auto sprim = std::make_shared<zeno::PrimitiveObject>();
            auto& sverts = sprim->verts;
            auto& stris = sprim->tris;

            sverts.resize(nm_points);
            stris.resize(nm_tris);

            auto ompPol = omp_exec();
            constexpr auto omp_space = execspace_e::openmp;

            ompPol(zs::range(sverts.size()),
                [&sverts,surf_verts_buffer = proxy<omp_space>(surf_verts_buffer)] (int vi) mutable {
                    // auto v = surf_verts_buffer.template pack<3>("x",vi);
                    sverts[vi] = surf_verts_buffer[vi].to_array();
            });

            ompPol(zs::range(stris.size()),
                [&stris,surf_tris_buffer = proxy<omp_space>(surf_tris_buffer)] (int ti) mutable {
                    // auto t = surf_tris_buffer[ti];
                    stris[ti] = surf_tris_buffer[ti].to_array();
            });

            set_output("prim",std::move(sprim));
        }
    };

    ZENDEFNODE(VisualizeSurfaceMesh, {{{"ZSParticles"}},
                                {{"prim"}},
                                {
                                    {"string","xtag","x"}
                                },
                                {"ZSGeometry"}});


    struct MarkSelfIntersectionRegion : zeno::INode {

        using T = float;
        using Ti = int;
        using dtiles_t = zs::TileVector<T,32>;
        using tiles_t = typename ZenoParticles::particles_t;
        using bvh_t = zs::LBvh<3,int,T>;
        using bv_t = zs::AABBBox<3, T>;
        using vec3 = zs::vec<T, 3>; 

        virtual void apply() override {
            using namespace zs;
            auto zsparticles = get_input<ZenoParticles>("zsparticles");
            auto& verts = zsparticles->getParticles();
            bool is_tet_volume_mesh = zsparticles->category == ZenoParticles::category_e::tet;
            const auto &tris = is_tet_volume_mesh ? (*zsparticles)[ZenoParticles::s_surfTriTag] : zsparticles->getQuadraturePoints(); 

            constexpr auto cuda_space = execspace_e::cuda;
            auto cudaPol = cuda_exec();  

            dtiles_t tri_buffer{tris.get_allocator(),{
                {"inds",3},
                {"nrm",3},
                {"he_inds",1}
            },tris.size()};
            dtiles_t verts_buffer{verts.get_allocator(),{
                {"inds",1},
                {"x",3},
                {"he_inds",1}
            },is_tet_volume_mesh ? (*zsparticles)[ZenoParticles::s_surfVertTag].size() : verts.size()};

            TILEVEC_OPS::copy(cudaPol,tris,"he_inds",tri_buffer,"he_inds");
            if(is_tet_volume_mesh) {
                const auto &points = (*zsparticles)[ZenoParticles::s_surfVertTag];
                TILEVEC_OPS::copy(cudaPol,points,"inds",verts_buffer,"inds");
                TILEVEC_OPS::copy(cudaPol,points,"he_inds",verts_buffer,"he_inds");
                topological_sample(cudaPol,points,verts,"x",verts_buffer);
                TILEVEC_OPS::copy(cudaPol,tris,"inds",tri_buffer,"inds");
                reorder_topology(cudaPol,points,tri_buffer);

            }else {
                TILEVEC_OPS::copy(cudaPol,tris,"inds",tri_buffer,"inds");
                TILEVEC_OPS::copy(cudaPol,verts,"x",verts_buffer,"x");
                cudaPol(zs::range(verts.size()),[
                    verts = proxy<cuda_space>({},verts),
                    verts_buffer = proxy<cuda_space>({},verts_buffer)] ZS_LAMBDA(int vi) mutable {
                        verts_buffer("inds",vi) = reinterpret_bits<T>(vi);
                });
            }

            calculate_facet_normal(cudaPol,verts_buffer,"x",tri_buffer,tri_buffer,"nrm");

            dtiles_t inst_buffer_info{tris.get_allocator(),{
                {"pair",2},
                {"type",1},
                {"its_edge_mark",6},
                {"int_points",6}
            },tris.size() * 2};

            dtiles_t gia_res{verts_buffer.get_allocator(),{
                {"ring_mask",1},
                {"type_mask",1},
                {"color_mask",1},
                {"is_loop_vertex",1}
            },verts_buffer.size()};

            dtiles_t tris_gia_res{tri_buffer.get_allocator(),{
                {"ring_mask",1},
                {"type_mask",1},
                {"color_mask",1},
            },tri_buffer.size()};

            auto& halfedges = (*zsparticles)[ZenoParticles::s_surfHalfEdgeTag];
            // auto nm_insts = do_global_self_intersection_analysis_on_surface_mesh_info(
            //     cudaPol,verts_buffer,"x",tri_buffer,halfedges,inst_buffer_info,gia_res,false);  
            // zs::bht<int,2,int> conn_of_first_ring{halfedges.get_allocator(),halfedges.size()};      
            auto ring_mask_width = do_global_self_intersection_analysis(cudaPol,
                verts_buffer,"x",tri_buffer,halfedges,gia_res,tris_gia_res);    
   

            auto markTag = get_input2<std::string>("markTag");

            if(!verts.hasProperty("markTag")) {
                verts.append_channels(cudaPol,{{markTag,1}});
            }
            TILEVEC_OPS::fill(cudaPol,verts,markTag,(T)0.0);
            cudaPol(zs::range(verts_buffer.size()),[
                gia_res = proxy<cuda_space>({},gia_res),
                verts = proxy<cuda_space>({},verts),
                ring_mask_width = ring_mask_width,
                verts_buffer = proxy<cuda_space>({},verts_buffer),
                markTag = zs::SmallString(markTag)
            ] ZS_LAMBDA(int pi) mutable {
                auto vi = zs::reinterpret_bits<int>(verts_buffer("inds",pi));
                int ring_mask = 0;
                for(int i = 0;i != ring_mask_width;++i) {
                    ring_mask |= zs::reinterpret_bits<int>(gia_res("ring_mask",pi * ring_mask_width + i));
                }
                verts(markTag,vi) = ring_mask == 0 ? (T)0.0 : (T)1.0;
            });
            set_output("zsparticles",zsparticles);
        } 

    };

    ZENDEFNODE(MarkSelfIntersectionRegion, {{{"zsparticles"},{"string","markTag","markTag"}},
                                {{"zsparticles"}},
                                {
                                    
                                },
                                {"ZSGeometry"}});



    struct VisualizeSurfaceNormal : INode {
        virtual void apply() override {
            using namespace zs;
            auto cudaExec = cuda_exec();
            constexpr auto space = zs::execspace_e::cuda;

            auto zsparticles = get_input<ZenoParticles>("ZSParticles");
            auto& tris = zsparticles->category == ZenoParticles::category_e::tet ? 
                (*zsparticles)[ZenoParticles::s_surfTriTag] : 
                zsparticles->getQuadraturePoints();

            const auto& verts = zsparticles->getParticles();
            if(!tris.hasProperty("nrm"))
                tris.append_channels(cudaExec,{{"nrm",3}});

            calculate_facet_normal(cudaExec,verts,"x",tris,tris,"nrm");

            auto buffer = typename ZenoParticles::particles_t({{"dir",3},{"x",3}},tris.size(),zs::memsrc_e::device,0);

            cudaExec(zs::range(tris.size()),
                [tris = proxy<space>({},tris),
                        buffer = proxy<space>({},buffer),
                        verts = proxy<space>({},verts)] ZS_LAMBDA(int ti) mutable {
                    auto inds = tris.template pack<3>("inds",ti).reinterpret_bits(int_c);
                    zs::vec<T,3> tp[3];
                    for(int i = 0;i != 3;++i)
                        tp[i] = verts.template pack<3>("x",inds[i]);
                    auto center = (tp[0] + tp[1] + tp[2]) / (T)3.0;

                    buffer.template tuple<3>("dir",ti) = tris.template pack<3>("nrm",ti);
                    buffer.template tuple<3>("x",ti) = center;
            });                        

            buffer = buffer.clone({zs::memsrc_e::host});
            auto prim = std::make_shared<zeno::PrimitiveObject>();
            auto& pverts = prim->verts;
            pverts.resize(buffer.size() * 2);
            auto& lines = prim->lines;
            lines.resize(buffer.size());

            auto ompExec = omp_exec();
            constexpr auto ompSpace = zs::execspace_e::openmp;

            auto extrude_offset = get_param<float>("offset");

            ompExec(zs::range(buffer.size()),
                [buffer = proxy<ompSpace>({},buffer),&pverts,&lines,extrude_offset] (int ti) mutable {
                    auto xs = buffer.template pack<3>("x",ti);
                    auto dir = buffer.template pack<3>("dir",ti);
                    auto xe = xs + extrude_offset * dir;
                    pverts[ti * 2 + 0] = zeno::vec3f(xs[0],xs[1],xs[2]);
                    pverts[ti * 2 + 1] = zeno::vec3f(xe[0],xe[1],xe[2]);

                    lines[ti] = zeno::vec2i(ti * 2 + 0,ti * 2 + 1);
            });

            set_output("prim",std::move(prim));
        }
    };

    ZENDEFNODE(VisualizeSurfaceNormal, {{{"ZSParticles"}},
                                {{"prim"}},
                                {{"float","offset","1"}},
                                {"ZSGeometry"}});


    struct VisualizeSurfaceEdgeNormal : INode {
        virtual void apply() override {
            using namespace zs;

            auto zsparticles = get_input<ZenoParticles>("ZSParticles");
            if(!zsparticles->hasAuxData(ZenoParticles::s_surfTriTag)){
                throw std::runtime_error("the input zsparticles has no surface tris");
            }
            if(!zsparticles->hasAuxData(ZenoParticles::s_surfEdgeTag)) {
                throw std::runtime_error("the input zsparticles has no surface lines");
            }   

            auto& tris      = (*zsparticles)[ZenoParticles::s_surfTriTag];
            if(!tris.hasProperty("ff_inds") || !tris.hasProperty("fe_inds"))
                throw std::runtime_error("please call ZSInitTopoConnect first before this node");           
            auto& lines     = (*zsparticles)[ZenoParticles::s_surfEdgeTag];
            if(!lines.hasProperty("fe_inds"))
                throw std::runtime_error("please call ZSInitTopoConnect first before this node");             

            const auto& verts = zsparticles->getParticles();
            auto cudaExec = cuda_exec();
            constexpr auto space = zs::execspace_e::cuda;

            if(!tris.hasProperty("nrm"))
                tris.append_channels(cudaExec,{{"nrm",3}});

            // std::cout << "CALCULATE SURFACE NORMAL" << std::endl;

            calculate_facet_normal(cudaExec,verts,"x",tris,tris,"nrm");


            auto buffer = typename ZenoParticles::particles_t({{"nrm",3},{"x",3}},lines.size(),zs::memsrc_e::device,0);  
            if(!calculate_edge_normal_from_facet_normal(cudaExec,tris,"nrm",buffer,"nrm",lines))
                throw std::runtime_error("VisualizeSurfaceEdgeNormal::calculate_edge_normal_from_facet_normal fail");


            cudaExec(zs::range(lines.size()),[
                    buffer = proxy<space>({},buffer),
                    lines = proxy<space>({},lines),
                    tris = proxy<space>({},tris),
                    verts = proxy<space>({},verts)] ZS_LAMBDA(int ei) mutable {
                        auto linds = lines.template pack<2>("inds",ei).reinterpret_bits(int_c);
                        auto fe_inds = lines.template pack<2>("fe_inds",ei).reinterpret_bits(int_c);

                        auto n0 = tris.template pack<3>("nrm",fe_inds[0]);
                        auto n1 = tris.template pack<3>("nrm",fe_inds[1]);

                        auto v0 = verts.template pack<3>("x",linds[0]);
                        auto v1 = verts.template pack<3>("x",linds[1]);

                        // buffer.template tuple<3>("nrm",ei) = (n0 + n1).normalized();
                        // buffer.template tuple<3>("nrm",ei) = lines.template pack<3>("nrm",ei);
                        buffer.template tuple<3>("x",ei) = (v0 + v1) / (T)2.0;
            }); 

            buffer = buffer.clone({zs::memsrc_e::host});

            auto prim = std::make_shared<zeno::PrimitiveObject>();
            auto& pverts = prim->verts;
            auto& plines = prim->lines;
            pverts.resize(buffer.size() * 2);
            plines.resize(buffer.size());

            auto ompExec = omp_exec();
            constexpr auto omp_space = execspace_e::openmp;

            auto offset = get_param<float>("offset");

            ompExec(zs::range(buffer.size()),
                [buffer = proxy<omp_space>({},buffer),&pverts,&plines,offset] (int li) mutable {
                    auto ps = buffer.template pack<3>("x",li);
                    auto dp = buffer.template pack<3>("nrm",li);
                    auto pe = ps + dp * offset;
                    pverts[li * 2 + 0] = zeno::vec3f(ps[0],ps[1],ps[2]);
                    pverts[li * 2 + 1] = zeno::vec3f(pe[0],pe[1],pe[2]);

                    plines[li] = zeno::vec2i(li * 2 + 0,li * 2 + 1);
            });

            set_output("prim",std::move(prim));
        }
    };

    ZENDEFNODE(VisualizeSurfaceEdgeNormal, {{{"ZSParticles"}},
                                {{"prim"}},
                                {{"float","offset","1"}},
                                {"ZSGeometry"}});

    // struct ZSCalSurfaceCollisionCell : INode {
    //     virtual void apply() override {
    //         using namespace zs;

    //         auto zsparticles = get_input<ZenoParticles>("ZSParticles");
    //         if(!zsparticles->hasAuxData(ZenoParticles::s_surfTriTag)){
    //             throw std::runtime_error("the input zsparticles has no surface tris");
    //             // auto& tris = (*particles)[ZenoParticles::s_surfTriTag];
    //             // tris = typename ZenoParticles::particles_t({{"inds",3}});
    //         }
    //         if(!zsparticles->hasAuxData(ZenoParticles::s_surfEdgeTag)) {
    //             throw std::runtime_error("the input zsparticles has no surface lines");
    //         }

    //         auto& tris      = (*zsparticles)[ZenoParticles::s_surfTriTag];
    //         if(!tris.hasProperty("ff_inds") || !tris.hasProperty("fe_inds"))
    //             throw std::runtime_error("please call ZSInitTopoConnect first before this node");           
    //         auto& lines     = (*zsparticles)[ZenoParticles::s_surfEdgeTag];
    //         if(!lines.hasProperty("fe_inds"))
    //             throw std::runtime_error("please call ZSInitTopoConnect first before this node"); 

    //         const auto& verts = zsparticles->getParticles();
    //         auto cudaExec = cuda_exec();
    //         // constexpr auto space = zs::execspace_e::cuda;

    //         if(!tris.hasProperty("nrm"))
    //             tris.append_channels(cudaExec,{{"nrm",3}});

    //         // std::cout << "CALCULATE SURFACE NORMAL" << std::endl;

    //         calculate_facet_normal(cudaExec,verts,"x",tris,tris,"nrm");
    //         // std::cout << "FINISH CALCULATE SURFACE NORMAL" << std::endl;

    //         auto ceNrmTag = get_param<std::string>("ceNrmTag");
    //         if(!lines.hasProperty(ceNrmTag))
    //             lines.append_channels(cudaExec,{{ceNrmTag,3}});
            
    //         // evalute the normal of edge plane
    //         // cudaExec(range(lines.size()),
    //         //     [verts = proxy<space>({},verts),
    //         //         tris = proxy<space>({},tris),
    //         //         lines = proxy<space>({},lines),
    //         //         ceNrmTag = zs::SmallString(ceNrmTag)] ZS_LAMBDA(int ei) mutable {
    //         //             auto e_inds = lines.template pack<2>("inds",ei).template reinterpret_bits<int>();
    //         //             auto fe_inds = lines.template pack<2>("fe_inds",ei).template reinterpret_bits<int>();
    //         //             auto n0 = tris.template pack<3>("nrm",fe_inds[0]);
    //         //             auto n1 = tris.template pack<3>("nrm",fe_inds[1]);

    //         //             auto ne = (n0 + n1).normalized();
    //         //             auto e0 = verts.template pack<3>("x",e_inds[0]);
    //         //             auto e1 = verts.template pack<3>("x",e_inds[1]);
    //         //             auto e10 = e1 - e0;

    //         //             lines.template tuple<3>(ceNrmTag,ei) = e10.cross(ne).normalized();
    //         // });

    //         COLLISION_UTILS::calculate_cell_bisector_normal(cudaExec,
    //             verts,"x",
    //             lines,
    //             tris,
    //             tris,"nrm",
    //             lines,ceNrmTag);


    //         set_output("ZSParticles",zsparticles);
    //     }

    // };

    // ZENDEFNODE(ZSCalSurfaceCollisionCell, {{{"ZSParticles"}},
    //                             {{"ZSParticles"}},
    //                             {{"string","ceNrmTag","nrm"}},
    //                             {"ZSGeometry"}});


//     struct VisualizeCollisionCell : INode {
//         virtual void apply() override {
//             using namespace zs;

//             auto zsparticles = get_input<ZenoParticles>("ZSParticles");
//             auto ceNrmTag = get_param<std::string>("ceNrmTag");
//             // auto out_offset = get_input2<float>("out_offset");
//             // auto in_offset = get_input2<float>("in_offset");
//             auto collisionEps = get_input2<float>("collisionEps");
//             auto nrm_offset = get_input2<float>("nrm_offset");

//             if(!zsparticles->hasAuxData(ZenoParticles::s_surfTriTag))
//                 throw std::runtime_error("the input zsparticles has no surface tris");
//             if(!zsparticles->hasAuxData(ZenoParticles::s_surfEdgeTag))
//                 throw std::runtime_error("the input zsparticles has no surface lines");

//             auto& tris      = (*zsparticles)[ZenoParticles::s_surfTriTag];
//             if(!tris.hasProperty("ff_inds") || !tris.hasProperty("fe_inds"))
//                 throw std::runtime_error("please call ZSCalSurfaceCollisionCell first before this node");           
//             auto& lines     = (*zsparticles)[ZenoParticles::s_surfEdgeTag];
//             if(!lines.hasProperty("fe_inds") || !lines.hasProperty(ceNrmTag))
//                 throw std::runtime_error("please call ZSCalSurfaceCollisionCell first before this node"); 
//             auto& verts = zsparticles->getParticles();
//             // cell data per facet
//             std::vector<zs::PropertyTag> tags{{"x",9},{"dir",9},{"nrm",9},{"center",3}};
//             auto cell_buffer = typename ZenoParticles::particles_t(tags,tris.size(),zs::memsrc_e::device,0);
//             // auto cell_buffer = typename ZenoParticles::particles_t(tags,1,zs::memsrc_e::device,0);
//             // transfer the data from gpu to cpu
//             constexpr auto cuda_space = execspace_e::cuda;
//             auto cudaPol = cuda_exec();      

//             cudaPol(zs::range(cell_buffer.size()),
//                 [cell_buffer = proxy<cuda_space>({},cell_buffer),
//                     verts = proxy<cuda_space>({},verts),
//                     lines = proxy<cuda_space>({},lines),
//                     tris = proxy<cuda_space>({},tris),
//                     ceNrmTag = zs::SmallString(ceNrmTag)] ZS_LAMBDA(int ci) mutable {
//                 auto inds       = tris.template pack<3>("inds",ci).template reinterpret_bits<int>();
//                 auto fe_inds    = tris.template pack<3>("fe_inds",ci).template reinterpret_bits<int>();

//                 auto nrm = tris.template pack<3>("nrm",ci);

//                 #ifdef COLLISION_VIS_DEBUG
                
//                     zs::vec<T,3> vs[3];
//                     for(int i = 0;i != 3;++i)
//                         vs[i] = verts.template pack<3>("x",inds[i]);
//                     auto vc = (vs[0] + vs[1] + vs[2]) / (T)3.0;

//                     zs::vec<T,3> ec[3];
//                     for(int i = 0;i != 3;++i)
//                         ec[i] = (vs[i] + vs[(i+1)%3])/2.0;

//                     // make sure all the bisector facet orient in-ward
//                     // for(int i = 0;i != 3;++i){
//                     //     auto ec_vc = vc - ec[i];
//                     //     auto e1 = fe_inds[i];
//                     //     auto n1 = lines.template pack<3>(ceNrmTag,e1);
//                     //     if(is_edge_edge_match(lines.template pack<2>("inds",e1).template reinterpret_bits<int>(),zs::vec<int,2>{inds[i],inds[((i + 1) % 3)]}) == 1)
//                     //         n1 = (T)-1 * n1;
//                     //     auto check_dir = n1.dot(ec_vc);
//                     //     if(check_dir < 0) {
//                     //         printf("invalid check dir %f %d %d\n",(float)check_dir,ci,i);
//                     //     }
//                     // }

//                     // auto cell_center = vec3::zeros();
//                     // cell_center = (vs[0] + vs[1] + vs[2])/(T)3.0;
//                     // T check_dist{};
//                     // auto check_intersect = COLLISION_UTILS::is_inside_the_cell(verts,"x",
//                     //     lines,tris,
//                     //     tris,"nrm",
//                     //     lines,ceNrmTag,
//                     //     ci,cell_center,in_offset,out_offset);
//                     // if(check_intersect == 1)
//                     //     printf("invalid cell intersection check offset and inset : %d %f %f %f\n",ci,(float)check_dist,(float)out_offset,(float)in_offset);
//                     // if(check_intersect == 2)
//                     //     printf("invalid cell intersection check bisector : %d\n",ci);


//                 #endif

//                 cell_buffer.template tuple<3>("center",ci) = vec3::zeros();
//                 for(int i = 0;i < 3;++i){
//                     auto vert = verts.template pack<3>("x",inds[i]);
//                     cell_buffer.template tuple<3>("center",ci) = cell_buffer.template pack<3>("center",ci) + vert/(T)3.0;
//                     for(int j = 0;j < 3;++j) {
//                         cell_buffer("x",i * 3 + j,ci) = vert[j];
//                     }
                    
// #if 0
//                     auto e0 = fe_inds[(i + 3 -1) % 3];
//                     auto e1 = fe_inds[i];

//                     auto n0 = lines.template pack<3>(ceNrmTag,e0);
//                     auto n1 = lines.template pack<3>(ceNrmTag,e1);

//                     for(int j = 0;j != 3;++j)
//                         cell_buffer("nrm",i*3 + j,ci) = n1[j];

//                     if(is_edge_edge_match(lines.template pack<2>("inds",e0).template reinterpret_bits<int>(),zs::vec<int,2>{inds[((i + 3 - 1) % 3)],inds[i]}) == 1)
//                         n0 =  (T)-1 * n0;
//                     if(is_edge_edge_match(lines.template pack<2>("inds",e1).template reinterpret_bits<int>(),zs::vec<int,2>{inds[i],inds[((i + 1) % 3)]}) == 1)
//                         n1 = (T)-1 * n1;
// #else

//                     auto n0 = COLLISION_UTILS::get_bisector_orient(lines,tris,
//                         lines,ceNrmTag,
//                         ci,(i + 3 - 1) % 3);
//                     auto n1 = COLLISION_UTILS::get_bisector_orient(lines,tris,
//                         lines,ceNrmTag,ci,i);

//                     for(int j = 0;j != 3;++j)
//                         cell_buffer("nrm",i*3 + j,ci) = n1[j];

// #endif
//                     auto dir = n1.cross(n0).normalized();

//                     // do some checking
//                     // #ifdef COLLISION_VIS_DEBUG

//                     // #endif


//                     // auto orient = dir.dot(nrm);
//                     // if(orient > 0) {
//                     //     printf("invalid normal dir %f on %d\n",(float)orient,ci);
//                     // }
//                     // printf("dir = %f %f %f\n",(float)dir[0],(float)dir[1],(float)dir[2]);
//                     // printf("n0 = %f %f %f\n",(float)n0[0],(float)n0[1],(float)n0[2]);
//                     // printf("n1 = %f %f %f\n",(float)n1[0],(float)n1[1],(float)n1[2]);
//                     for(int j = 0;j < 3;++j){
//                         cell_buffer("dir",i * 3 + j,ci) = dir[j];
//                         // cell_buffer("dir",i * 3 + j,ci) = nrm[j];
//                     }
                    
//                 }
//             });  

//             cell_buffer = cell_buffer.clone({zs::memsrc_e::host});   
//             constexpr auto omp_space = execspace_e::openmp;
//             auto ompPol = omp_exec();            

//             auto cell = std::make_shared<zeno::PrimitiveObject>();

//             auto& cell_verts = cell->verts;
//             auto& cell_lines = cell->lines;
//             auto& cell_tris = cell->tris;
//             cell_verts.resize(cell_buffer.size() * 6);
//             cell_lines.resize(cell_buffer.size() * 9);
//             cell_tris.resize(cell_buffer.size() * 6);


//             auto offset_ratio = get_input2<float>("offset_ratio");

//             ompPol(zs::range(cell_buffer.size()),
//                 [cell_buffer = proxy<omp_space>({},cell_buffer),
//                     &cell_verts,&cell_lines,&cell_tris,collisionEps = collisionEps,offset_ratio = offset_ratio] (int ci) mutable {

//                 auto vs_ = cell_buffer.template pack<9>("x",ci);
//                 auto ds_ = cell_buffer.template pack<9>("dir",ci);

//                 auto center = cell_buffer.template pack<3>("center",ci);

//                 for(int i = 0;i < 3;++i) {
//                     auto p = vec3{vs_[i*3 + 0],vs_[i*3 + 1],vs_[i*3 + 2]};
//                     auto dp = vec3{ds_[i*3 + 0],ds_[i*3 + 1],ds_[i*3 + 2]};

//                     auto p0 = p - dp * collisionEps;
//                     auto p1 = p + dp * collisionEps;

//                     auto dp0 = p0 - center;
//                     auto dp1 = p1 - center;

//                     dp0 *= offset_ratio;
//                     dp1 *= offset_ratio;

//                     p0 = dp0 + center;
//                     p1 = dp1 + center;

//                     // printf("ci = %d \t dp = %f %f %f\n",ci,(float)dp[0],(float)dp[1],(float)dp[2]);

//                     cell_verts[ci * 6 + i * 2 + 0] = zeno::vec3f{p0[0],p0[1],p0[2]};
//                     cell_verts[ci * 6 + i * 2 + 1] = zeno::vec3f{p1[0],p1[1],p1[2]};

//                     cell_lines[ci * 9 + 0 + i] = zeno::vec2i{ci * 6 + i * 2 + 0,ci * 6 + i * 2 + 1};
//                 }

//                 for(int i = 0;i < 3;++i) {
//                     cell_lines[ci * 9 + 3 + i] = zeno::vec2i{ci * 6 + i * 2 + 0,ci * 6 + ((i+1)%3) * 2 + 0};
//                     cell_lines[ci * 9 + 6 + i] = zeno::vec2i{ci * 6 + i * 2 + 1,ci * 6 + ((i+1)%3) * 2 + 1}; 

//                     cell_tris[ci * 6 + i * 2 + 0] = zeno::vec3i{ci * 6 + i * 2 + 0,ci * 6 + i* 2 + 1,ci * 6 + ((i+1)%3) * 2 + 0};
//                     cell_tris[ci * 6 + i * 2 + 1] = zeno::vec3i{ci * 6 + i * 2 + 1,ci * 6 + ((i+1)%3) * 2 + 1,ci * 6 + ((i+1)%3) * 2 + 0};
//                 }

//             });
//             cell_lines.resize(0);

//             auto tcell = std::make_shared<zeno::PrimitiveObject>();
//             // tcell->resize(cell_buffer.size() * 6);
//             auto& tcell_verts = tcell->verts;
//             tcell_verts.resize(cell_buffer.size() * 6);
//             auto& tcell_lines = tcell->lines;
//             tcell_lines.resize(cell_buffer.size() * 3);
//             ompPol(zs::range(cell_buffer.size()),
//                 [cell_buffer = proxy<omp_space>({},cell_buffer),
//                     &tcell_verts,&tcell_lines,&collisionEps,&offset_ratio,&nrm_offset] (int ci) mutable {

//                 auto vs_ = cell_buffer.template pack<9>("x",ci);
//                 auto ds_ = cell_buffer.template pack<9>("dir",ci);


//                 auto center = cell_buffer.template pack<3>("center",ci);

//                 for(int i = 0;i < 3;++i) {
//                     auto p = vec3{vs_[i*3 + 0],vs_[i*3 + 1],vs_[i*3 + 2]};
//                     auto dp = vec3{ds_[i*3 + 0],ds_[i*3 + 1],ds_[i*3 + 2]};

//                     auto p0 = p - dp * collisionEps;
//                     auto p1 = p + dp * collisionEps;

//                     auto dp0 = p0 - center;
//                     auto dp1 = p1 - center;

//                     dp0 *= offset_ratio;
//                     dp1 *= offset_ratio;

//                     p0 = dp0 + center;
//                     p1 = dp1 + center;

//                     tcell_verts[ci * 6 + i * 2 + 0] = zeno::vec3f{p0[0],p0[1],p0[2]};
//                     tcell_verts[ci * 6 + i * 2 + 1] = zeno::vec3f{p1[0],p1[1],p1[2]};

//                     tcell_lines[ci * 3 + i] = zeno::vec2i{ci * 6 + i * 2 + 0,ci * 6 + i * 2 + 1};
//                 }
//             });


//             auto ncell = std::make_shared<zeno::PrimitiveObject>();
//             auto& ncell_verts = ncell->verts;
//             auto& ncell_lines = ncell->lines;
//             ncell_verts.resize(cell_buffer.size() * 6);
//             ncell_lines.resize(cell_buffer.size() * 3);
//             ompPol(zs::range(cell_buffer.size()),
//                 [cell_buffer = proxy<omp_space>({},cell_buffer),
//                     &ncell_verts,&ncell_lines,&offset_ratio,&nrm_offset] (int ci) mutable {    
//                 auto vs_ = cell_buffer.template pack<9>("x",ci);
//                 auto nrm_ = cell_buffer.template pack<9>("nrm",ci);

//                 auto center = cell_buffer.template pack<3>("center",ci);
//                 for(int i = 0;i != 3;++i)   {
//                     auto edge_center = vec3::zeros();
//                     for(int j = 0;j != 3;++j)
//                         edge_center[j] = (vs_[i * 3 + j] + vs_[((i + 1) % 3) * 3 + j])/(T)2.0;
//                     auto nrm = vec3{nrm_[i*3 + 0],nrm_[i*3 + 1],nrm_[i*3 + 2]};
//                     auto dp = edge_center - center;
//                     dp *= offset_ratio;
//                     edge_center = dp + center;

//                     auto p0 = edge_center;
//                     auto p1 = edge_center + nrm * nrm_offset;

//                     ncell_verts[ci * 6 + i * 2 + 0] = zeno::vec3f{p0[0],p0[1],p0[2]};
//                     ncell_verts[ci * 6 + i * 2 + 1] = zeno::vec3f{p1[0],p1[1],p1[2]};

//                     ncell_lines[ci * 3 + i] = zeno::vec2i{ci * 6 + i * 2 + 0,ci * 6 + i * 2 + 1};

//                 }
//             });



//             set_output("collision_cell",std::move(cell));
//             set_output("ccell_tangent",std::move(tcell));
//             set_output("ccell_normal",std::move(ncell));

//         }
//     };

//     ZENDEFNODE(VisualizeCollisionCell, {{{"ZSParticles"},{"float","collisionEps","0.01"},{"float","nrm_offset","0.1"},{"float","offset_ratio","0.8"}},
//                                 {{"collision_cell"},{"ccell_tangent"},{"ccell_normal"}},
//                                 {{"string","ceNrmTag","nrm"}},
//                                 {"ZSGeometry"}});





struct VisualizeSelfIntersections : zeno::INode {

    using T = float;
    using Ti = int;
    using dtiles_t = zs::TileVector<T,32>;
    using tiles_t = typename ZenoParticles::particles_t;
    using bvh_t = zs::LBvh<3,int,T>;
    using bv_t = zs::AABBBox<3, T>;
    using vec3 = zs::vec<T, 3>;

    virtual void apply() override {
        using namespace zs;
        auto zsparticles = get_input<ZenoParticles>("zsparticles");
        bool is_tet_volume_mesh = zsparticles->category == ZenoParticles::category_e::tet;
        const auto &tris = is_tet_volume_mesh ? (*zsparticles)[ZenoParticles::s_surfTriTag] : zsparticles->getQuadraturePoints(); 
        // const auto& points = (*zsparticles)[ZenoParticles::s_surfPointTag];
        const auto& verts = zsparticles->getParticles();

        constexpr auto cuda_space = execspace_e::cuda;
        auto cudaPol = cuda_exec();  
        constexpr auto omp_space = execspace_e::openmp;
        auto ompPol = omp_exec();  
                                                                                                                                                                                                              
        dtiles_t tri_buffer{tris.get_allocator(),{
            {"inds",3},
            {"nrm",3},
            {"he_inds",1}
        },tris.size()};
        dtiles_t verts_buffer{verts.get_allocator(),{
            {"inds",1},
            {"x",3},
            {"X",3},
            {"he_inds",1},
            {"check_pos",3}
        },is_tet_volume_mesh ? (*zsparticles)[ZenoParticles::s_surfVertTag].size() : verts.size()};

        TILEVEC_OPS::copy(cudaPol,tris,"he_inds",tri_buffer,"he_inds");
        if(is_tet_volume_mesh) {
            const auto &points = (*zsparticles)[ZenoParticles::s_surfVertTag];
            TILEVEC_OPS::copy(cudaPol,points,"inds",verts_buffer,"inds");
            TILEVEC_OPS::copy(cudaPol,points,"he_inds",verts_buffer,"he_inds");
            topological_sample(cudaPol,points,verts,"x",verts_buffer);
            topological_sample(cudaPol,points,verts,"X",verts_buffer);
            if(verts.hasProperty("check_pos"))
                topological_sample(cudaPol,points,verts,"check_pos",verts_buffer);
            else
                TILEVEC_OPS::copy(cudaPol,verts_buffer,"x",verts_buffer,"check_pos");
            TILEVEC_OPS::copy(cudaPol,tris,"inds",tri_buffer,"inds");
            reorder_topology(cudaPol,points,tri_buffer);

        }else {
            TILEVEC_OPS::copy(cudaPol,tris,"inds",tri_buffer,"inds");
            TILEVEC_OPS::copy(cudaPol,verts,"x",verts_buffer,"x");
            TILEVEC_OPS::copy(cudaPol,verts,"he_inds",verts_buffer,"he_inds");
            cudaPol(zs::range(verts.size()),[
                verts = proxy<cuda_space>({},verts),
                verts_buffer = proxy<cuda_space>({},verts_buffer)] ZS_LAMBDA(int vi) mutable {
                    verts_buffer("inds",vi) = reinterpret_bits<T>(vi);
            });
        }

        dtiles_t gia_res{verts_buffer.get_allocator(),{
            {"ring_mask",1},
            {"type_mask",1},
            {"color_mask",1},
            {"is_loop_vertex",1}
        },verts_buffer.size()};
        dtiles_t tris_gia_res{tri_buffer.get_allocator(),{
            {"ring_mask",1},
            {"type_mask",1},
            {"color_mask",1},
        },tri_buffer.size()};

        auto& halfedges = (*zsparticles)[ZenoParticles::s_surfHalfEdgeTag];
        auto ring_mask_width = do_global_self_intersection_analysis(cudaPol,
            verts_buffer,"x",tri_buffer,halfedges,gia_res,tris_gia_res);   

        dtiles_t flood_region{verts_buffer.get_allocator(),{
            {"x",3}
        },(size_t)verts_buffer.size()};
        TILEVEC_OPS::copy(cudaPol,verts_buffer,"x",flood_region,"x");
        // verts_buffer = verts_buffer.clone({zs::memsrc_e::host});
        // tri_buffer = tri_buffer.clone({zs::memsrc_e::host});
        flood_region = flood_region.clone({zs::memsrc_e::host});
        gia_res = gia_res.clone({zs::memsrc_e::host});

        auto flood_region_vis = std::make_shared<zeno::PrimitiveObject>();
        flood_region_vis->resize(verts.size());
        auto& flood_region_verts = flood_region_vis->verts;
        auto& flood_region_mark = flood_region_vis->add_attr<float>("flood");
        auto& is_corner_mark = flood_region_vis->add_attr<float>("is_loop_vertex");
        
        ompPol(zs::range(verts_buffer.size()),[
            &flood_region_verts,
            &flood_region_mark,
            &is_corner_mark,
            ring_mask_width = ring_mask_width,
            flood_region = proxy<omp_space>({},flood_region),
            gia_res = proxy<omp_space>({},gia_res)] (int vi) mutable {
                auto p = flood_region.pack(dim_c<3>,"x",vi);
                flood_region_verts[vi] = p.to_array();
                int ring_mask = 0;
                bool is_corner = false;
                for(int i = 0;i != ring_mask_width;++i) {
                    ring_mask |= zs::reinterpret_bits<int>(gia_res("ring_mask",vi * ring_mask_width + i));
                    if(gia_res("is_loop_vertex",vi) > (T)0.5)
                        is_corner = true;
                }


                flood_region_mark[vi] = ring_mask == 0 ? (float)0.0 : (float)1.0;
                // auto is_corner = gia_res("is_loop_vertex",vi);
                is_corner_mark[vi] = is_corner ? (T)1.0 : (T)0.0;
        });
        set_output("flood_region",std::move(flood_region_vis));


    }
};

ZENDEFNODE(VisualizeSelfIntersections, {{"zsparticles"},
                                  {
                                        // "st_ring_vis",
                                        // "st_facet_rest_vis",
                                        // "st_facet_vis",                                                           
                                        "flood_region",
                                        // "be_vis"
                                        // "wire_fr_vis"
                                    },
                                  {
                                    
                                  },
                                  {"ZSGeometry"}});



struct VisualizeIntersections : zeno::INode {

    using T = float;
    using Ti = int;
    using dtiles_t = zs::TileVector<T,32>;
    using tiles_t = typename ZenoParticles::particles_t;
    using bvh_t = zs::LBvh<3,int,T>;
    using bv_t = zs::AABBBox<3, T>;
    using vec3 = zs::vec<T, 3>;
    using table_vec2i_type = zs::bht<int,2,int>;
    using table_int_type = zs::bht<int,1,int>;

    virtual void apply() override { 
        using namespace zs;
        constexpr auto cuda_space = execspace_e::cuda;
        auto cudaPol = cuda_exec();  
        constexpr auto omp_space = execspace_e::openmp;
        auto ompPol = omp_exec();  

        auto zsparticles = get_input<ZenoParticles>("zsparticles");
        bool is_tet_volume_mesh = zsparticles->category == ZenoParticles::category_e::tet;
        const auto &tris = is_tet_volume_mesh ? (*zsparticles)[ZenoParticles::s_surfTriTag] : zsparticles->getQuadraturePoints(); 
        // const auto& points = (*zsparticles)[ZenoParticles::s_surfPointTag];
        const auto& verts = zsparticles->getParticles();
        auto& halfedges = (*zsparticles)[ZenoParticles::s_surfHalfEdgeTag];

        // auto kinematics = get_input<ListObject>("kinematics")->get2<ZenoParticles*>();
        // auto kinematics = RETRIEVE_OBJECT_PTRS(ZenoParticles,"kinematics");
        auto kinematic = get_input<ZenoParticles>("kinematics");
        auto& kverts = kinematic->getParticles();
        const auto& ktris = kinematic->getQuadraturePoints();
        const auto& khalfedges = (*kinematic)[ZenoParticles::s_surfHalfEdgeTag];
        // if(!verts.hasProperty("flood"))
        //     verts.append_channels(cudaPol,{{"flood",1}});
        // TILEVEC_OPS::fill(cudaPol,verts,"flood",(T)0.0);

        dtiles_t tri_buffer{tris.get_allocator(),{
            {"inds",3},
            {"he_inds",1}
        },tris.size()};
        dtiles_t verts_buffer{verts.get_allocator(),{
            {"inds",1},
            {"x",3},
            {"flood",1},
            // {"he_inds",1},
        },is_tet_volume_mesh ? (*zsparticles)[ZenoParticles::s_surfVertTag].size() : verts.size()};
        TILEVEC_OPS::fill(cudaPol,verts_buffer,"flood",(T)0.0);
        TILEVEC_OPS::copy(cudaPol,tris,"he_inds",tri_buffer,"he_inds");
        if(is_tet_volume_mesh) {
            const auto &points = (*zsparticles)[ZenoParticles::s_surfVertTag];
            TILEVEC_OPS::copy(cudaPol,points,"inds",verts_buffer,"inds");
            // TILEVEC_OPS::copy(cudaPol,points,"he_inds",verts_buffer,"he_inds");
            // TILEVEC_OPS::copy(cudaPol,points,"inds",verts_buffer,"inds");
            std::cout << "do_topological_sample" << std::endl;
            topological_sample(cudaPol,points,verts,"x",verts_buffer);
            // topological_sample(cudaPol,points,verts,"X",verts_buffer);
            TILEVEC_OPS::copy(cudaPol,tris,"inds",tri_buffer,"inds");
            std::cout << "do_reorder_topology" << std::endl;
            reorder_topology(cudaPol,points,tri_buffer);
        }else {
            TILEVEC_OPS::copy(cudaPol,tris,"inds",tri_buffer,"inds");
            TILEVEC_OPS::copy(cudaPol,verts,"x",verts_buffer,"x");
            // TILEVEC_OPS::copy(cudaPol,verts,"he_inds",verts_buffer,"he_inds");
            cudaPol(zs::range(verts.size()),[
                verts = proxy<cuda_space>({},verts),
                verts_buffer = proxy<cuda_space>({},verts_buffer)] ZS_LAMBDA(int vi) mutable {
                    verts_buffer("inds",vi) = reinterpret_bits<T>(vi);
            });
        }

        dtiles_t kverts_buffer{verts.get_allocator(),{
            {"flood",1},
            {"x",3}
        },kverts.size()};
        TILEVEC_OPS::fill(cudaPol,kverts_buffer,"flood",(T)0.0);
        TILEVEC_OPS::copy(cudaPol,kverts,"x",kverts_buffer,"x");

        zs::Vector<int> gia_res{verts_buffer.get_allocator(),0};

        zs::Vector<int> tris_gia_res{tri_buffer.get_allocator(),0};


        bool use_zs_interior = get_input2<bool>("use_zsparticles_interior");
        bool use_kin_interior = get_input2<bool>("use_kinematic_interior");

        // TODO: The must ExcludeMark is not include
        {
            std::cout << "do_global_intersection_analysis_with_connected_manifolds" << std::endl;

            auto ring_mask_width = do_global_intersection_analysis_with_connected_manifolds(cudaPol,
                verts_buffer,"x",tri_buffer,halfedges,use_zs_interior,
                kverts_buffer,"x",ktris,khalfedges,use_kin_interior,
                gia_res,tris_gia_res);

            std::cout << "finish do_global_intersection_analysis_with_connected_manifolds" << std::endl;

            zs::Vector<int> nmFloodVerts{verts_buffer.get_allocator(),1};
            nmFloodVerts.setVal(0);

            cudaPol(zs::range(verts_buffer.size()),[
                ring_mask_width = ring_mask_width,
                nmFloodVerts = proxy<cuda_space>(nmFloodVerts),
                verts_buffer = proxy<cuda_space>({},verts_buffer),
                gia_res = proxy<cuda_space>(gia_res)] ZS_LAMBDA(int vi) mutable {
                    for(int i = 0;i != ring_mask_width;++i) {
                        auto ring_mask = gia_res[vi * ring_mask_width + i];
                        if(ring_mask > 0) {
                            verts_buffer("flood",vi) = (T)1.0;
                            atomic_add(zs::exec_cuda,&nmFloodVerts[0],(int)1);
                            return;
                        }
                    }
            });

            std::cout << "nm_flood_vertices : " << nmFloodVerts.getVal(0) << std::endl;

            auto k_offset = verts_buffer.size();
            cudaPol(zs::range(kverts.size()),[
                ring_mask_width = ring_mask_width,
                kverts_buffer = proxy<cuda_space>({},kverts_buffer),
                gia_res = proxy<cuda_space>(gia_res),
                k_offset = k_offset] ZS_LAMBDA(int kvi) mutable {
                    for(int i = 0;i != ring_mask_width;++i) {
                        auto ring_mask = gia_res[(kvi + k_offset) * ring_mask_width + i];
                        if(ring_mask > 0){
                            kverts_buffer("flood",kvi) = (T)1.0;
                            return;
                        }
                    }
            });
        }
        
        std::cout << "finish marking" << std::endl;
        verts_buffer = verts_buffer.clone({zs::memsrc_e::host});
        kverts_buffer = kverts_buffer.clone({zs::memsrc_e::host});

        std::cout << "flood_dynamic" << std::endl;

        auto flood_dynamic = std::make_shared<zeno::PrimitiveObject>();
        auto& dyn_verts = flood_dynamic->verts;
        dyn_verts.resize(verts_buffer.size());
        auto& dfloods = flood_dynamic->add_attr<T>("flood");

        ompPol(zs::range(verts_buffer.size()),[
            verts_buffer = proxy<omp_space>({},verts_buffer),
            &dyn_verts,&dfloods] (int vi) mutable {
                auto p = verts_buffer.pack(dim_c<3>,"x",vi);
                dyn_verts[vi] = p.to_array();
                auto flood = verts_buffer("flood",vi);
                dfloods[vi] = flood > (T)0.5 ? (T)1.0 : (T)0.0;
        });
        
        std::cout << "flood_kinematic" << std::endl;

        auto flood_kinematic = std::make_shared<zeno::PrimitiveObject>();
        auto& kin_verts = flood_kinematic->verts;
        kin_verts.resize(kverts_buffer.size());
        auto& kfloods = flood_kinematic->add_attr<T>("flood");
        ompPol(zs::range(kverts_buffer.size()),[
            kverts_buffer = proxy<omp_space>({},kverts_buffer),
            &kin_verts,&kfloods] (int kvi) mutable {
                auto p = kverts_buffer.pack(dim_c<3>,"x",kvi);
                kin_verts[kvi] = p.to_array();
                auto flood = kverts_buffer("flood",kvi);
                kfloods[kvi] = flood > (T)0.5 ? (T)1.0 : (T)0.0;
        });

        std::cout << "output" << std::endl;

        set_output("flood_dynamic",std::move(flood_dynamic));
        set_output("flood_kinematic",std::move(flood_kinematic));
    }
};


ZENDEFNODE(VisualizeIntersections, {{"zsparticles",
                                        "kinematics",
                                        {"bool","use_zsparticles_interior","1"},    
                                        {"bool","use_kinematic_interior","1"}
                                    },
                                  {
                                        "flood_dynamic",
                                        "flood_kinematic",
                                        // "dyn_edges_vis",
                                        // "kin_edges_vis"
                                    },
                                  {
                                    
                                  },
                                  {"ZSGeometry"}});

struct VisualizeIntersections2 : zeno::INode {
    using T = float;
    using Ti = int;
    using dtiles_t = zs::TileVector<T,32>;
    using tiles_t = typename ZenoParticles::particles_t;
    using bvh_t = zs::LBvh<3,int,T>;
    using bv_t = zs::AABBBox<3, T>;
    using vec3 = zs::vec<T, 3>;
    using table_vec2i_type = zs::bht<int,2,int>;
    using table_int_type = zs::bht<int,1,int>;

    virtual void apply() override { 
        using namespace zs;
        constexpr auto cuda_space = execspace_e::cuda;
        auto cudaPol = cuda_exec();  
        constexpr auto omp_space = execspace_e::openmp;
        auto ompPol = omp_exec();  

        auto zsparticles = get_input<ZenoParticles>("zsparticles");
        bool is_tet_volume_mesh = zsparticles->category == ZenoParticles::category_e::tet;
        const auto &tris = (*zsparticles)[ZenoParticles::s_surfTriTag]; 
        const auto& tets = zsparticles->getQuadraturePoints();
        const auto& points = (*zsparticles)[ZenoParticles::s_surfVertTag];
        auto& verts = zsparticles->getParticles();
        auto& halfedges = (*zsparticles)[ZenoParticles::s_surfHalfEdgeTag];
        // const auto& points = 

        auto in_collisionEps = get_input2<T>("in_collisionEps");
        auto out_collisionEps = get_input2<T>("out_collisionEps");

        // auto kinematics = get_input<ListObject>("kinematics")->get2<ZenoParticles*>();
        auto kinematics = RETRIEVE_OBJECT_PTRS(ZenoParticles,"kinematics");
        zs::bht<int,2,int> csPT{verts.get_allocator(),100000};
        csPT.reset(cudaPol,true);
        zs::Vector<int> csPTOffsets{verts.get_allocator(),kinematics.size()};
        // auto nm_csPT = COLLISION_UTILS::do_tetrahedra_surface_mesh_and_kinematic_boundary_collision_detection(cudaPol,
        //     kinematics,
        //     verts,"x",
        //     tets,
        //     points,tris,
        //     halfedges,
        //     out_collisionEps,
        //     in_collisionEps,
        //     csPT,
        //     csPTOffsets,
        //     true);

        int nm_kverts = 0;
        for(auto kinematic : kinematics) {
            nm_kverts += kinematic->getParticles().size();
        }

        dtiles_t flood_dyn{verts.get_allocator(),{
            {"x",3},
            {"flood",1}
        },verts.size()};
        dtiles_t all_kverts_buffer{verts.get_allocator(),{
            {"x",3},
            {"flood",1}
        },(size_t)nm_kverts};

        TILEVEC_OPS::copy(cudaPol,verts,"x",flood_dyn,"x");
        TILEVEC_OPS::copy(cudaPol,verts,"flood",flood_dyn,"flood");

        int voffset = 0;
        for(auto kinematic : kinematics) {
            const auto& kverts = kinematic->getParticles();
            TILEVEC_OPS::copy(cudaPol,kverts,"x",all_kverts_buffer,"x",voffset);
            TILEVEC_OPS::copy(cudaPol,kverts,"flood",all_kverts_buffer,"flood",voffset);
            voffset += kverts.size();
        }

        dtiles_t csPTBuffer{verts.get_allocator(),{
            {"x0",3},
            {"x1",3}
        },(size_t)csPT.size()};
        cudaPol(zip(zs::range(csPT.size()),zs::range(csPT._activeKeys)),[
            tris = proxy<cuda_space>({},tris),
            verts = proxy<cuda_space>({},verts),
            csPTBuffer = proxy<cuda_space>({},csPTBuffer),
            all_kverts_buffer = proxy<cuda_space>({},all_kverts_buffer)] ZS_LAMBDA(auto ci,const auto& pair) mutable {
                auto kvi = pair[0];
                auto ti = pair[1];
                auto tri = tris.pack(dim_c<3>,"inds",ti,int_c);
                zs::vec<T,3> tV[3] = {};
                for(int i = 0;i != 3;++i)
                    tV[i] = verts.pack(dim_c<3>,"x",tri[i]);
                
                auto tC = zs::vec<T,3>::zeros();
                for(int i = 0;i != 3;++i)   
                    tC += tV[i] / (T)3.0;
                
                auto kv = all_kverts_buffer.pack(dim_c<3>,"x",kvi);

                csPTBuffer.tuple(dim_c<3>,"x0",ci) = kv;
                csPTBuffer.tuple(dim_c<3>,"x1",ci) = tC;
        });

        flood_dyn = flood_dyn.clone({memsrc_e::host});
        all_kverts_buffer = all_kverts_buffer.clone({memsrc_e::host});
        csPTBuffer = csPTBuffer.clone({memsrc_e::host});

        auto flood_dyn_vis = std::make_shared<zeno::PrimitiveObject>();
        flood_dyn_vis->resize(flood_dyn.size());
        auto& flood_dyn_verts = flood_dyn_vis->verts;
        auto& flood_dyn_tags = flood_dyn_verts.add_attr<T>("flood");
        ompPol(zs::range(flood_dyn.size()),[
            flood_dyn = proxy<omp_space>({},flood_dyn),
            &flood_dyn_verts,&flood_dyn_tags] (int vi) mutable {
                auto pv = flood_dyn.pack(dim_c<3>,"x",vi);
                auto flood = flood_dyn("flood",vi);
                flood_dyn_verts[vi] = pv.to_array();
                flood_dyn_tags[vi] = flood;
        });
        set_output("flood_dynamic",std::move(flood_dyn_vis));

        auto flood_kin_vis = std::make_shared<zeno::PrimitiveObject>();
        flood_kin_vis->resize(all_kverts_buffer.size());
        auto& flood_kin_verts = flood_kin_vis->verts;
        auto& flood_kin_tags = flood_kin_verts.add_attr<T>("flood");
        ompPol(zs::range(all_kverts_buffer.size()),[
            all_kverts_buffer = proxy<omp_space>({},all_kverts_buffer),
            &flood_kin_verts,&flood_kin_tags] (int kvi) mutable {
                auto kv = all_kverts_buffer.pack(dim_c<3>,"x",kvi);
                auto flood =all_kverts_buffer("flood",kvi);
                flood_kin_verts[kvi] = kv.to_array();
                flood_kin_tags[kvi] = flood;
        });
        set_output("flood_kinematic",std::move(flood_kin_vis));

        auto csPTVis = std::make_shared<zeno::PrimitiveObject>();
        auto& csPT_verts = csPTVis->verts;
        csPT_verts.resize(csPTBuffer.size() * 2);
        auto& csPT_lines = csPTVis->lines;
        csPT_lines.resize(csPTBuffer.size());

        ompPol(zs::range(csPTBuffer.size()),[
            csPTBuffer = proxy<omp_space>({},csPTBuffer),
            &csPT_verts,&csPT_lines] (int ci) mutable {
                auto x0 = csPTBuffer.pack(dim_c<3>,"x0",ci);
                auto x1 = csPTBuffer.pack(dim_c<3>,"x1",ci);
                csPT_verts[ci * 2 + 0] = x0.to_array();
                csPT_verts[ci * 2 + 1] = x1.to_array();
                csPT_lines[ci] = zeno::vec2i{ci * 2 + 0,ci * 2 + 1};
        });
        set_output("cspt_vis",std::move(csPTVis));
    }
};

ZENDEFNODE(VisualizeIntersections2, {{
                                        "zsparticles",
                                        "kinematics",
                                        {"float","out_collisionEps","0.1"},
                                        {"float","in_collisionEps","0.1"},    
                                    },
                                  {
                                        "flood_dynamic",
                                        "flood_kinematic",
                                        "cspt_vis",
                                        // "kin_edges_vis"
                                    },
                                  {
                                    
                                  },
                                  {"ZSGeometry"}});


struct VisualizeCollision2 : zeno::INode {
    using T = float;

    virtual void apply() override {
        using namespace zs;
        using dtiles_t = typename ZenoParticles::particles_t;

        constexpr auto cuda_space = execspace_e::cuda;
        auto cudaPol = cuda_exec();  
        constexpr auto omp_space = execspace_e::openmp;
        auto ompPol = omp_exec();  

        auto zsparticles = get_input<ZenoParticles>("zsparticles");
        auto& verts = zsparticles->getParticles();
        const auto& tris = (*zsparticles)[ZenoParticles::s_surfTriTag];
        const auto& points = (*zsparticles)[ZenoParticles::s_surfVertTag];
        const auto& tets = zsparticles->getQuadraturePoints();
        auto& halfedges = (*zsparticles)[ZenoParticles::s_surfHalfEdgeTag];
        const auto& halffacets = (*zsparticles)[ZenoParticles::s_tetHalfFacetTag];

        zs::bht<int,2,int> csPT{verts.get_allocator(),10000};
        auto out_collisionEps = get_input2<float>("out_collisionEps");
        auto in_collisionEps = get_input2<float>("in_collisionEps");


        dtiles_t verts_buffer{verts.get_allocator(),{
            {"x",3},
            {"flood",1},
            {"ring_mask",1}
        },verts.size()};
        TILEVEC_OPS::copy(cudaPol,verts,"x",verts_buffer,"x");

        COLLISION_UTILS::do_tetrahedra_surface_tris_and_points_self_collision_detection(
            cudaPol,verts_buffer,"x",
            tets,
            points,tris,
            halfedges,halffacets,
            out_collisionEps,
            in_collisionEps,
            csPT,true);

        auto nm_ints = csPT.size();
        std::cout << "nm_ints : " << nm_ints << std::endl;

        dtiles_t tris_buffer{tris.get_allocator(),{
            {"x0",3},
            {"x1",3},
            {"x2",3}
        },nm_ints};

        dtiles_t points_buffer{points.get_allocator(),{
            {"x0",3}
        },nm_ints};

        dtiles_t lines_buffer{points.get_allocator(),{
            {"x0",3},
            {"x1",3}
        },nm_ints};


        cudaPol(zip(zs::range(csPT.size()),zs::range(csPT._activeKeys)),[
            tris_buffer = proxy<cuda_space>({},tris_buffer),
            points_buffer = proxy<cuda_space>({},points_buffer),
            lines_buffer = proxy<cuda_space>({},lines_buffer),
            verts = proxy<cuda_space>({},verts),
            points = proxy<cuda_space>({},points),
            tris = proxy<cuda_space>({},tris)] ZS_LAMBDA(auto ci,const auto& pair) mutable {
                auto pi = pair[0];
                auto ti = pair[1];
                auto vi = zs::reinterpret_bits<int>(points("inds",pi));
                auto tri = tris.pack(dim_c<3>,"inds",ti,int_c);

                auto p = verts.pack(dim_c<3>,"x",vi);
                zs::vec<T,3> tV[3] = {};
                for(int i = 0;i != 3;++i)
                    tV[i] = verts.pack(dim_c<3>,"x",tri[i]);

                auto tC = zs::vec<T,3>::zeros();
                for(int i = 0;i != 3;++i)
                    tC += tV[i] / (T)3.0;

                tris_buffer.tuple(dim_c<3>,"x0",ci) = tV[0];
                tris_buffer.tuple(dim_c<3>,"x1",ci) = tV[1];
                tris_buffer.tuple(dim_c<3>,"x2",ci) = tV[2];

                points_buffer.tuple(dim_c<3>,"x0",ci) = p;

                lines_buffer.tuple(dim_c<3>,"x0",ci) = p;
                lines_buffer.tuple(dim_c<3>,"x1",ci) = tC;
        });

        tris_buffer = tris_buffer.clone({memsrc_e::host});
        points_buffer = points_buffer.clone({memsrc_e::host});
        lines_buffer = lines_buffer.clone({memsrc_e::host});
        verts_buffer = verts_buffer.clone({memsrc_e::host});

        auto flood_vis = std::make_shared<zeno::PrimitiveObject>();
        auto& flood_verts = flood_vis->verts;
        flood_verts.resize(verts_buffer.size());
        auto& flood_tag = flood_vis->add_attr<float>("flood");
        ompPol(zs::range(verts_buffer.size()),[
            verts_buffer = proxy<omp_space>({},verts_buffer),
            &flood_verts,&flood_tag] (int vi) mutable {
                auto v = verts_buffer.pack(dim_c<3>,"x",vi);
                flood_verts[vi] = v.to_array();
                flood_tag[vi] = verts_buffer("flood",vi);
        });
        set_output("flood_vis",std::move(flood_vis));

        auto tris_vis = std::make_shared<zeno::PrimitiveObject>();
        auto& tris_vis_verts = tris_vis->verts;
        auto& tris_vis_tris = tris_vis->tris;
        tris_vis_verts.resize(nm_ints * 3);
        tris_vis_tris.resize(nm_ints);
        ompPol(zs::range(nm_ints),[
            tris_buffer = proxy<omp_space>({},tris_buffer),
            &tris_vis_verts,&tris_vis_tris] (int ci) mutable {
                auto x0 = tris_buffer.pack(dim_c<3>,"x0",ci);
                auto x1 = tris_buffer.pack(dim_c<3>,"x1",ci);
                auto x2 = tris_buffer.pack(dim_c<3>,"x2",ci);

                tris_vis_verts[ci * 3 + 0] = x0.to_array();
                tris_vis_verts[ci * 3 + 1] = x1.to_array();
                tris_vis_verts[ci * 3 + 2] = x2.to_array();

                tris_vis_tris[ci] = zeno::vec3i{ci * 3 + 0,ci * 3 + 1,ci * 3 + 2};
        });
        set_output("tris_vis",std::move(tris_vis));

        auto points_vis = std::make_shared<zeno::PrimitiveObject>();
        auto& points_vis_verts = points_vis->verts;
        points_vis_verts.resize(nm_ints);
        ompPol(zs::range(nm_ints),[
            points_buffer = proxy<omp_space>({},points_buffer),
            &points_vis_verts] (int ci) mutable {
                auto x0 = points_buffer.pack(dim_c<3>,"x0",ci);
                points_vis_verts[ci] = x0.to_array();
        });
        set_output("points_vis",std::move(points_vis));

        auto lines_vis = std::make_shared<zeno::PrimitiveObject>();
        auto& lines_vis_verts = lines_vis->verts;
        auto& lines_vis_lines = lines_vis->lines;
        lines_vis_verts.resize(nm_ints * 2);
        lines_vis_lines.resize(nm_ints);
        ompPol(zs::range(nm_ints),[
            lines_buffer = proxy<omp_space>({},lines_buffer),
            &lines_vis_verts,&lines_vis_lines] (int ci) mutable {
                auto x0 = lines_buffer.pack(dim_c<3>,"x0",ci);
                auto x1 = lines_buffer.pack(dim_c<3>,"x1",ci);

                lines_vis_verts[ci * 2 + 0] = x0.to_array();
                lines_vis_verts[ci * 2 + 1] = x1.to_array();
                lines_vis_lines[ci] = zeno::vec2i{ci * 2 + 0,ci * 2 + 1};
        });
        set_output("lines_vis",std::move(lines_vis));
    }
};

ZENDEFNODE(VisualizeCollision2, {{
                                    "zsparticles",
                                    {"float","out_collisionEps","0.1"},
                                    {"float","in_collisionEps","0.1"},    
                                },
                                  {
                                        "tris_vis",
                                        "points_vis",
                                        "lines_vis",
                                        "flood_vis"
                                    },
                                  {
                                    
                                  },
                                  {"ZSGeometry"}});


struct VisualizeIntersections3 : zeno::INode {
    using T = float;
    using Ti = int;
    using dtiles_t = zs::TileVector<T,32>;
    using tiles_t = typename ZenoParticles::particles_t;
    using bvh_t = zs::LBvh<3,int,T>;
    using bv_t = zs::AABBBox<3, T>;
    using vec3 = zs::vec<T, 3>;
    using table_vec2i_type = zs::bht<int,2,int>;
    using table_int_type = zs::bht<int,1,int>;

    virtual void apply() override { 
        using namespace zs;
        constexpr auto cuda_space = execspace_e::cuda;
        auto cudaPol = cuda_exec();  
        constexpr auto omp_space = execspace_e::openmp;
        auto ompPol = omp_exec();  

        auto zsparticles = get_input<ZenoParticles>("zsparticles");
        const auto &tris = (*zsparticles)[ZenoParticles::s_surfTriTag]; 
        const auto& tets = zsparticles->getQuadraturePoints();
        const auto& points = (*zsparticles)[ZenoParticles::s_surfVertTag];
        auto& verts = zsparticles->getParticles();
        auto& halfedges = (*zsparticles)[ZenoParticles::s_surfHalfEdgeTag];
        // const auto& points = 

        auto in_collisionEps = get_input2<T>("in_collisionEps");
        auto out_collisionEps = get_input2<T>("out_collisionEps");

        // auto kinematics = get_input<ListObject>("kinematics")->get2<ZenoParticles*>();
        // auto kinematics = RETRIEVE_OBJECT_PTRS(ZenoParticles,"kinematics");
        auto kinematic = get_input<ZenoParticles>("kinematic");
    
        zs::bht<int,2,int> csPT{verts.get_allocator(),100000};
        csPT.reset(cudaPol,true);
        // zs::Vector<int> csPTOffsets{verts.get_allocator(),kinematics.size()};
        // std::cout << "number of kinematics : " << kinematics.size() << std::endl;
        auto collide_from_exterior = get_input2<bool>("collide_from_exterior");

        // auto nm_csPT = COLLISION_UTILS::do_tetrahedra_surface_points_and_kinematic_boundary_collision_detection(cudaPol,
        //     kinematic,
        //     verts,"x",
        //     tets,
        //     points,tris,
        //     halfedges,
        //     out_collisionEps,
        //     in_collisionEps,
        //     csPT,
        //     collide_from_exterior,
        //     true);

        // std::cout << "do_tetrahedra_surface_points_and_kinematic_boundary_collision_detection with csPT : " << nm_csPT << std::endl;

        int nm_kverts = kinematic->getParticles().size();
        int nm_ktris = kinematic->getQuadraturePoints().size();

        // for(auto kinematic : kinematics) {
        //     nm_kverts += kinematic->getParticles().size();
        //     nm_ktris += kinematic->getQuadraturePoints().size();
        // }

        dtiles_t flood_dyn{verts.get_allocator(),{
            {"x",3},
            {"flood",1}
        },verts.size()};
        dtiles_t all_kverts_buffer{verts.get_allocator(),{
            {"x",3},
            {"flood",1}
        },(size_t)nm_kverts};
        dtiles_t all_ktri_verts_buffer{verts.get_allocator(),{
            {"x0",3},
            {"x1",3},
            {"x2",3}
        },(size_t)nm_ktris};

        TILEVEC_OPS::copy(cudaPol,verts,"x",flood_dyn,"x");
        TILEVEC_OPS::copy(cudaPol,verts,"flood",flood_dyn,"flood");

        int toffset = 0;
        int voffset = 0;
        // for(auto kinematic : kinematics) {
        const auto& kverts = kinematic->getParticles();
        const auto& ktris = kinematic->getQuadraturePoints();
        TILEVEC_OPS::copy(cudaPol,kverts,"x",all_kverts_buffer,"x",voffset);
        TILEVEC_OPS::copy(cudaPol,kverts,"flood",all_kverts_buffer,"flood",voffset);
        cudaPol(zs::range(ktris.size()),[
            ktris = proxy<cuda_space>({},ktris),
            kverts = proxy<cuda_space>({},kverts),
            all_ktri_verts_buffer = proxy<cuda_space>({},all_ktri_verts_buffer),
            toffset = toffset] ZS_LAMBDA(int kti) mutable {
                auto ktri = ktris.pack(dim_c<3>,"inds",kti,int_c);
                for(int i = 0;i != 3;++i) {
                    all_ktri_verts_buffer.tuple(dim_c<3>,"x0",toffset + kti) = kverts.pack(dim_c<3>,"x",ktri[0]);
                    all_ktri_verts_buffer.tuple(dim_c<3>,"x1",toffset + kti) = kverts.pack(dim_c<3>,"x",ktri[1]);
                    all_ktri_verts_buffer.tuple(dim_c<3>,"x2",toffset + kti) = kverts.pack(dim_c<3>,"x",ktri[2]);
                }
        });
        //     voffset += kverts.size();
        //     toffset += ktris.size();
        // }

        dtiles_t csPTBuffer{verts.get_allocator(),{
            {"x0",3},
            {"x1",3},
            {"t0",3},
            {"t1",3},
            {"t2",3}
        },(size_t)csPT.size()};
        cudaPol(zip(zs::range(csPT.size()),zs::range(csPT._activeKeys)),[
            tris = proxy<cuda_space>({},tris),
            verts = proxy<cuda_space>({},verts),
            points = proxy<cuda_space>({},points),
            all_ktri_verts_buffer = proxy<cuda_space>({},all_ktri_verts_buffer),
            csPTBuffer = proxy<cuda_space>({},csPTBuffer),
            all_kverts_buffer = proxy<cuda_space>({},all_kverts_buffer)] ZS_LAMBDA(auto ci,const auto& pair) mutable {
                auto pi = pair[0];
                auto kti = pair[1];
                zs::vec<T,3> ktV[3] = {};
                // for(int i = 0;i != 3;++i)
                ktV[0] = all_ktri_verts_buffer.pack(dim_c<3>,"x0",kti);
                ktV[1] = all_ktri_verts_buffer.pack(dim_c<3>,"x1",kti);
                ktV[2] = all_ktri_verts_buffer.pack(dim_c<3>,"x2",kti);
                
                auto ktC = zs::vec<T,3>::zeros();
                for(int i = 0;i != 3;++i)   
                    ktC += ktV[i] / (T)3.0;
                
                auto vi = zs::reinterpret_bits<int>(points("inds",pi));
                auto pv = verts.pack(dim_c<3>,"x",vi);

                csPTBuffer.tuple(dim_c<3>,"x0",ci) = pv;
                csPTBuffer.tuple(dim_c<3>,"x1",ci) = ktC;
                csPTBuffer.tuple(dim_c<3>,"t0",ci) = ktV[0];
                csPTBuffer.tuple(dim_c<3>,"t1",ci) = ktV[1];
                csPTBuffer.tuple(dim_c<3>,"t2",ci) = ktV[2];
        });
        csPTBuffer = csPTBuffer.clone({memsrc_e::host});

        dtiles_t intersectHalfEdges{halfedges.get_allocator(),{
            {"x0",3},
            {"x1",3}
        },(size_t)halfedges.size()};
        cudaPol(zs::range(halfedges.size()),[
            halfedges = proxy<cuda_space>({},halfedges),
            verts = proxy<cuda_space>({},verts),
            tris = proxy<cuda_space>({},tris),
            intersectHalfEdges = proxy<cuda_space>({},intersectHalfEdges)] ZS_LAMBDA(int hi) mutable {
                auto ti = zs::reinterpret_bits<int>(halfedges("to_face",hi));
                auto tri = tris.pack(dim_c<3>,"inds",ti,int_c);
                auto local_vertex_id = zs::reinterpret_bits<int>(halfedges("local_vertex_id",hi));

                auto intersect = halfedges("intersect",hi);
                intersectHalfEdges.tuple(dim_c<3>,"x0",hi) = verts.pack(dim_c<3>,"x",tri[local_vertex_id]);
                intersectHalfEdges.tuple(dim_c<3>,"x1",hi) = verts.pack(dim_c<3>,"x",tri[local_vertex_id]);

                if(intersect > (T)0.5) {
                    intersectHalfEdges.tuple(dim_c<3>,"x1",hi) = verts.pack(dim_c<3>,"x",tri[(local_vertex_id + 1) % 3]);
                }
        });
        intersectHalfEdges = intersectHalfEdges.clone({memsrc_e::host});

        flood_dyn = flood_dyn.clone({memsrc_e::host});
        all_kverts_buffer = all_kverts_buffer.clone({memsrc_e::host});


        auto flood_dyn_vis = std::make_shared<zeno::PrimitiveObject>();
        flood_dyn_vis->resize(flood_dyn.size());
        auto& flood_dyn_verts = flood_dyn_vis->verts;
        auto& flood_dyn_tags = flood_dyn_verts.add_attr<T>("flood");
        ompPol(zs::range(flood_dyn.size()),[
            flood_dyn = proxy<omp_space>({},flood_dyn),
            &flood_dyn_verts,&flood_dyn_tags] (int vi) mutable {
                auto pv = flood_dyn.pack(dim_c<3>,"x",vi);
                auto flood = flood_dyn("flood",vi);
                flood_dyn_verts[vi] = pv.to_array();
                flood_dyn_tags[vi] = flood;
        });
        set_output("flood_dynamic",std::move(flood_dyn_vis));

        auto flood_kin_vis = std::make_shared<zeno::PrimitiveObject>();
        flood_kin_vis->resize(all_kverts_buffer.size());
        auto& flood_kin_verts = flood_kin_vis->verts;
        auto& flood_kin_tags = flood_kin_verts.add_attr<T>("flood");
        ompPol(zs::range(all_kverts_buffer.size()),[
            all_kverts_buffer = proxy<omp_space>({},all_kverts_buffer),
            &flood_kin_verts,&flood_kin_tags] (int kvi) mutable {
                auto kv = all_kverts_buffer.pack(dim_c<3>,"x",kvi);
                auto flood =all_kverts_buffer("flood",kvi);
                flood_kin_verts[kvi] = kv.to_array();
                flood_kin_tags[kvi] = flood;
        });
        set_output("flood_kinematic",std::move(flood_kin_vis));

        auto csPTVis = std::make_shared<zeno::PrimitiveObject>();
        auto& csPT_verts = csPTVis->verts;
        csPT_verts.resize(csPTBuffer.size() * 2);
        auto& csPT_lines = csPTVis->lines;
        csPT_lines.resize(csPTBuffer.size());

        ompPol(zs::range(csPTBuffer.size()),[
            csPTBuffer = proxy<omp_space>({},csPTBuffer),
            &csPT_verts,&csPT_lines] (int ci) mutable {
                auto x0 = csPTBuffer.pack(dim_c<3>,"x0",ci);
                auto x1 = csPTBuffer.pack(dim_c<3>,"x1",ci);
                csPT_verts[ci * 2 + 0] = x0.to_array();
                csPT_verts[ci * 2 + 1] = x1.to_array();
                csPT_lines[ci] = zeno::vec2i{ci * 2 + 0,ci * 2 + 1};
        });
        set_output("cspt_vis",std::move(csPTVis));

        auto csPTTri = std::make_shared<zeno::PrimitiveObject>();
        auto& csPT_tri_verts = csPTTri->verts;
        csPT_tri_verts.resize(csPTBuffer.size() * 3);
        auto& csPT_tri_tris = csPTTri->tris;
        csPT_tri_tris.resize(csPTBuffer.size());

        ompPol(zs::range(csPTBuffer.size()),[
            csPTBuffer = proxy<omp_space>({},csPTBuffer),
            &csPT_tri_verts,&csPT_tri_tris] (int ci) mutable {
                auto t0 = csPTBuffer.pack(dim_c<3>,"t0",ci);
                auto t1 = csPTBuffer.pack(dim_c<3>,"t1",ci);
                auto t2 = csPTBuffer.pack(dim_c<3>,"t2",ci);
                csPT_tri_verts[ci * 3 + 0] = t0.to_array();
                csPT_tri_verts[ci * 3 + 1] = t1.to_array();
                csPT_tri_verts[ci * 3 + 2] = t2.to_array();
                csPT_tri_tris[ci] = zeno::vec3i{ci * 3 + 0,ci * 3 + 1,ci * 3 + 2};
        });
        set_output("cspt_tri_vis",std::move(csPTTri));

        // intersectHalfEdges
        auto intersect_edges = std::make_shared<zeno::PrimitiveObject>();
        auto& ih_verts = intersect_edges->verts;
        auto& ih_lines = intersect_edges->lines;
        ih_verts.resize(intersectHalfEdges.size() * 2);
        ih_lines.resize(intersectHalfEdges.size());

        ompPol(zs::range(intersectHalfEdges.size()),[
            intersectHalfEdges = proxy<omp_space>({},intersectHalfEdges),
            &ih_verts,&ih_lines] (int hi) mutable {
                auto x0 = intersectHalfEdges.pack(dim_c<3>,"x0",hi);
                auto x1 = intersectHalfEdges.pack(dim_c<3>,"x1",hi);

                ih_verts[hi * 2 + 0] = x0.to_array();
                ih_verts[hi * 2 + 1] = x1.to_array();
                ih_lines[hi] = zeno::vec2i{hi * 2 + 0,hi * 2 + 1};
        });

        set_output("intersect_edges",std::move(intersect_edges));
    }
};

ZENDEFNODE(VisualizeIntersections3, {{
                                        "zsparticles",
                                        "kinematic",
                                        {"float","out_collisionEps","0.1"},
                                        {"float","in_collisionEps","0.1"},    
                                        {"bool","collide_from_exterior","1"}
                                    },
                                  {
                                        "flood_dynamic",
                                        "flood_kinematic",
                                        "cspt_vis",
                                        "cspt_tri_vis",
                                        "intersect_edges"
                                    },
                                  {
                                    
                                  },
                                  {"ZSGeometry"}});

};