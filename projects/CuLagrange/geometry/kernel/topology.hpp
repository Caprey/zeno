#pragma once

#include "../../Structures.hpp"
#include "../../Utils.hpp"
#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/omp/execution/ExecutionPolicy.hpp"
#include "zensim/container/Bvh.hpp"
#include "zensim/container/Bcht.hpp"
#include "zensim/zpc_tpls/fmt/format.h"
#include "tiled_vector_ops.hpp"

#include "zensim/math/matrix/SparseMatrix.hpp"

#include "zensim/graph/ConnectedComponents.hpp"

#include "zensim/container/Bht.hpp"


namespace zeno {

    constexpr int is_edge_edge_match(const zs::vec<int,2>& e0,const zs::vec<int,2>& e1) {
        if(e0[0] == e1[0] && e0[1] == e1[1])
            return 0;
        if(e0[0] == e1[1] && e0[1] == e1[0])
            return 1;
        return -1;
    }

    constexpr int is_tri_edge_match(const zs::vec<int,3>& tri,const zs::vec<int,2>& edge) {
        for(int i = 0;i != 3;++i)
            if(is_edge_edge_match(zs::vec<int,2>(tri[(i+0)%3],tri[(i+1)%3]),edge) != -1)
                return i;
        return -1;
    }

    constexpr int is_tri_point_match(const zs::vec<int,3>& tri,int pidx) {
        for(int i = 0;i != 3;++i)
            if(tri[i] == pidx)
                return i;
        return -1;
    }

    constexpr int is_edge_point_match(const zs::vec<int,2>& edge,int pidx) {
        for(int i = 0;i != 2;++i)
            if(edge[i] == pidx)
                return i;
        return -1;
    }

    template<typename Pol,typename VTileVec,typename TriTileVec,typename TetTileVec>
    bool compute_ft_neigh_topo(Pol& pol,const VTileVec& verts,TriTileVec& tris,const TetTileVec& tets,const zs::SmallString& neighTag,float bvh_thickness) {
        using namespace zs;
        using T = typename VTileVec::value_type;
        using bv_t = AABBBox<3,T>;

        if(!tris.hasProperty(neighTag) || tris.getChannelSize(neighTag) != 1)
            return false;
        
        constexpr auto space = zs::execspace_e::cuda;
        auto tetsBvh = LBvh<3,int,T>{};
        
        auto bvs = retrieve_bounding_volumes(pol,verts,tets,wrapv<4>{},bvh_thickness,"x");
        tetsBvh.build(pol,bvs);

        size_t nmTris = tris.size();
        pol(zs::range(nmTris),
            [tets = proxy<space>({},tets),
                verts = proxy<space>({},verts),
                tris = proxy<space>({},tris),
                tetsBvh = proxy<space>(tetsBvh),
                neighTag] ZS_LAMBDA(int ti) mutable {
                    auto tri = tris.pack(dim_c<3>,"inds",ti).reinterpret_bits(int_c);
                    tris(neighTag,ti) = zs::reinterpret_bits<float>((int)-1);
                    int nm_found = 0;
                    auto cv = zs::vec<T, 3>::zeros();
                    for(int i = 0;i != 3;++i)
                        cv += verts.pack(dim_c<3>,"x",tri[i])/(T)3.0;
                    tetsBvh.iter_neighbors(cv,[&](int ntet) {
                        // if(ti == 0)
                        //     printf("test tet[%d] and tri[%d]\n",ntet,ti);
                        if(nm_found > 0)
                            return;
                        auto tet = tets.pack(dim_c<4>,"inds",ntet).reinterpret_bits(int_c);
                        for(int i = 0;i != 3;++i){
                            bool found_idx = false;
                            for(int j = 0;j != 4;++j)
                                if(tet[j] == tri[i]){
                                    found_idx = true;
                                    break;
                                }
                            if(!found_idx)
                                return;
                        }

                        nm_found++;
                        tris(neighTag,ti) = reinterpret_bits<float>(ntet);
                    });

                    if(nm_found == 0)
                        printf("found no neighbored tet for tri[%d]\n",ti);

        });

        return true;
    }

    template<typename Pol,typename VTileVec,typename TTileVec>
    bool compute_ff_neigh_topo(Pol& pol,const VTileVec& verts,TTileVec& tris,const zs::SmallString neighTag,float bvh_thickness) {
        using namespace zs;
        using T = typename VTileVec::value_type;
        using bv_t = AABBBox<3,T>;

        if(!tris.hasProperty(neighTag) || (tris.getChannelSize(neighTag) != 3)){
            return false;
        }

        constexpr auto space = zs::execspace_e::cuda;
        auto trisBvh = LBvh<3,int,T>{};
        auto bvs = retrieve_bounding_volumes(pol,verts,tris,wrapv<3>{},bvh_thickness,"x");
        trisBvh.build(pol,bvs);

        size_t nmTris = tris.size();
        // std::cout << "CALCULATE INCIDENT TRIS " << nmTris << std::endl;
        if(!tris.hasProperty("non_manfold"))
            tris.append_channels(pol,{{"non_manifold",1}});
        

        pol(zs::range(nmTris),
            [tris = proxy<space>({},tris),
                    verts = proxy<space>({},verts),
                    trisBvh = proxy<space>(trisBvh),
                    neighTag] ZS_LAMBDA(int ti) mutable {
                tris("non_manifold",ti) = (T)0;
                auto tri = tris.template pack<3>("inds",ti).template reinterpret_bits<int>();
                tris.template tuple<3>(neighTag,ti) = zs::vec<int,3>{-1,-1,-1}.template reinterpret_bits<float>();
                for(int i = 0;i < 3; ++i) {
                    int a0 = tri[(i + 0) % 3];
                    int a1 = tri[(i + 1) % 3];

                    auto v0 = verts.template pack<3>("x",a0);
                    auto v1 = verts.template pack<3>("x",a1);
                    auto cv = (v0 + v1) / 2;

                    // if(ti == 13759)
                    //     printf("T<%d> V: %d %d %d\n",ti,tri[0],tri[1],tri[2]);

                    bool found = false;
                    int nm_found = 0;
                    trisBvh.iter_neighbors(cv,[&](int nti) {
                        if(found || nti == ti)
                            return;
                        auto ntri = tris.template pack<3>("inds",nti).template reinterpret_bits<int>();
                        for(int j = 0;j < 3;++j) {
                            int b0 = ntri[(j + 0) % 3];
                            int b1 = ntri[(j + 1) % 3];

                            if(is_edge_edge_match(zs::vec<int,2>(a0,a1),zs::vec<int,2>(b0,b1)) != -1){
                                // found = true;
                                // if(ti == 844)
                                //     printf("found incident tri<%d>[%d %d %d] : ntr<%d>[%d %d %d]\n",
                                //         ti,tri[0],tri[1],tri[2],
                                //         nti,ntri[0],ntri[1],ntri[2]);
                                nm_found++;
                                tris(neighTag,i,ti) = reinterpret_bits<T>(nti);
                                return;
                            }
                        }
                    });
                    if(nm_found > 1) {
                        // printf("found a non-manifold facet %d %d\n",ti,nm_found);
                        tris("non_manifold",ti) = (T)1.0;
                    }
                    if(nm_found == 0) {
                        // printf("found boundary facet %d\n",ti);
                    }

                }
        });

        return true;
    }

    template<typename Pol,typename VTileVec,typename PTileVec,typename TTileVec>
    bool compute_fp_neigh_topo(Pol& pol,const VTileVec& verts,const PTileVec& points,TTileVec& tris,const zs::SmallString& neighTag,float bvh_thickness) {
        using namespace zs;
        using T = typename VTileVec::value_type;
        using bv_t = AABBBox<3,T>;

        if(!tris.hasProperty(neighTag) || tris.getChannelSize(neighTag) != 3) 
            return false;

        constexpr auto space = zs::execspace_e::cuda;
        auto trisBvh = LBvh<3,int,T>{};
        auto bvs = retrieve_bounding_volumes(pol,verts,tris,wrapv<3>{},bvh_thickness,"x");
        trisBvh.build(pol,bvs);

        size_t nm_points = points.size();
        // std::cout << "nm_points : " << nm_points << std::endl;
        pol(range(nm_points),
            [   trisBvh = proxy<space>(trisBvh),
                verts = proxy<space>({},verts),
                points = proxy<space>({},points),
                tris = proxy<space>({},tris),neighTag] ZS_LAMBDA(int pi) mutable {
                    auto pidx = reinterpret_bits<int>(points("inds",pi));
                    auto p = verts.template pack<3>("x",pidx);

                    trisBvh.iter_neighbors(p,[&](int nti){
                        auto ntri = tris.template pack<3>("inds",nti).reinterpret_bits(int_c);
                        int match_idx = is_tri_point_match(ntri,pidx);

                        if(match_idx != -1) {
                            tris(neighTag,match_idx,nti) = reinterpret_bits<T>(pi);
                        }
                    });
        });

        return true;
    }

    // template<typename Pol,typename VTileVec,typename ETileVec,typename PTileVec>
    // bool compute_ep_neigh_topo(Pol& pol,const VTileVec& verts,PTileVec& points,ETileVec& edges,const zs::SmallString& neighTag,float bvh_thickness) {
    //     using namespace zs;
    //     using T = typename VTileVec::value_type;
    //     using bv_t = AABBBox<3,T>;

    //     if(!edges.hasProperty(neighTag) || edges.getChannelSize(neighTag) != 2)
    //         return false;

    //     constexpr auto space = zs::execspace_e::cuda;
    //     auto edgesBvh LBvh<3,int,T>{};
    //     auto bvs = retrieve_bounding_volumes(pol,verts,tris,wrapv<2>{},bvh_thickness,"x");
    //     edgesBvh.build(pol,bvs);

    //     pol(range(points.size()),[
    //             verts = proxy<space>({},verts),
    //             points = proxy<space>({},points),
    //             edges = proxy<space>({},edges),
    //             edgesBvh = proxy<space>(edgesBvh),
    //             neighTag,thickness = bvh_thickness] ZS_LAMBDA(int pi) mutable {
    //                 auto pidx = reinterpret_bits<int>(points("inds",pi));
    //                 auto v = verts.pack(dim_c<3>,"x",pidx);
                    

    //     });

    // }


    template<typename Pol,typename VTileVec,typename ETileVec,typename TTileVec>
    bool compute_fe_neigh_topo(Pol& pol,const VTileVec& verts,ETileVec& edges,TTileVec& tris,const zs::SmallString& neighTag,float bvh_thickness) {
        using namespace zs;
        using T = typename VTileVec::value_type;
        using bv_t = AABBBox<3,T>;

        if(!edges.hasProperty(neighTag) || edges.getChannelSize(neighTag) != 2)
            return false;

        if(!tris.hasProperty(neighTag) || tris.getChannelSize(neighTag) != 3)
            return false;

        constexpr auto space = zs::execspace_e::cuda;
        auto trisBvh = LBvh<3,int,T>{};
        auto bvs = retrieve_bounding_volumes(pol,verts,tris,wrapv<3>{},bvh_thickness,"x");
        trisBvh.build(pol,bvs);

        size_t nm_edges = edges.size();
        pol(range(nm_edges),
            [   trisBvh = proxy<space>(trisBvh),
                verts = proxy<space>({},verts),
                edges = proxy<space>({},edges),
                tris = proxy<space>({},tris),neighTag] ZS_LAMBDA(int ei) mutable {
                    auto einds = edges.template pack<2>("inds",ei).template reinterpret_bits<int>();
                    auto a0 = einds[0];
                    auto a1 = einds[1];
                    auto v0 = verts.template pack<3>("x",a0);
                    auto v1 = verts.template pack<3>("x",a1);

                    auto ec = (v0 + v1)/(T)2.0;
                    int found_nm = 0;

                    trisBvh.iter_neighbors(ec,[&](int nti) {
                        if(found_nm == 2)
                            return;
                        // if(ei == 0)
                        //     printf("incident facet : %d\n",nti);
                        auto ntri = tris.template pack<3>("inds",nti).template reinterpret_bits<int>();
                        auto match_idx = is_tri_edge_match(ntri,einds);


                        if(match_idx < 0)
                            return;

                        auto t0 = verts.template pack<3>("x",ntri[0]);
                        auto t1 = verts.template pack<3>("x",ntri[1]);
                        auto t2 = verts.template pack<3>("x",ntri[2]);
                        auto tc = (t0 + t1 + t2) / (T)3.0;
                        auto dist = (tc - ec).norm();
                        if(dist > 10.0)
                            printf("the pair<%d %d> : [%d %d], [%d %d %d] is too far with dist = %f\n",
                                ei,nti,einds[0],einds[1],ntri[0],ntri[1],ntri[2],(float)dist);

                        // if(ei == 0) {
                        //     printf("do match on[%d] : %d %d : %d %d %d\n",match_idx,
                        //         einds[0],einds[1],ntri[0],ntri[1],ntri[2]);
                        // }
                        tris(neighTag,match_idx,nti) = reinterpret_bits<T>(ei);

                        int b0 = ntri[(match_idx + 0) % 3];
                        int b1 = ntri[(match_idx + 1) % 3];

                        // printf("pair edge[%d] and facet[%d]\n",ei,nti);

                        match_idx = is_edge_edge_match(zs::vec<int,2>(a0,a1),zs::vec<int,2>(b0,b1));
                        if(match_idx < 0)
                            printf("INVALID EDGE NEIGHBOR DETECTED, CHECK ALGORITHM\n");
                        edges(neighTag,match_idx,ei) = reinterpret_bits<T>(nti); 
                        found_nm++;
                        return;
                    });  


                    if(found_nm > 2) {
                        printf("found non-manifold edge : %d %d\n",ei,found_nm);
                    }  
                    if(found_nm == 1){
                        printf("found boundary edge : %d\n",ei);
                    }                
        });

        return true;
    }

    constexpr void elm_to_edges(const zs::vec<int,2>& single_edge,zs::vec<int,2> out_edges[1]) {
        out_edges[0] = single_edge;
    }

    constexpr void elm_to_edges(const zs::vec<int,3>& tri,zs::vec<int,2> out_edges[3]) {
        out_edges[0] = zs::vec<int,2>{tri[0],tri[1]};
        out_edges[1] = zs::vec<int,2>{tri[1],tri[2]};
        out_edges[2] = zs::vec<int,2>{tri[2],tri[0]};
    }

    constexpr void elm_to_edges(const zs::vec<int,4>& tet,zs::vec<int,2> out_edges[6]) {
        out_edges[0] = zs::vec<int,2>{tet[0],tet[1]};
        out_edges[1] = zs::vec<int,2>{tet[0],tet[2]};
        out_edges[2] = zs::vec<int,2>{tet[0],tet[3]};
        out_edges[3] = zs::vec<int,2>{tet[1],tet[2]};
        out_edges[4] = zs::vec<int,2>{tet[1],tet[3]};
        out_edges[5] = zs::vec<int,2>{tet[2],tet[3]};
    }

    // template<typename Pol,typename TopoTileVec,int CODIM>
    // bool topo_to_incident_matrix(Pol& pol,
    //     const TopoTileVec& topo,
    //     const zs::wrapv<CODIM>& codimv,
    //     zs::SparseMatrix<int,true>& spmat
    // ) {
    //     using namespace zs;
    //     using vec2i = zs::vec<int,2>;
    //     using vec3i = zs::vec<int,3>;
    //     using vec4i = zs::vec<int,4>;

    //     using IV = zs::vec<int,2>;
    //     constexpr auto space = RM_CVREF_T(pol)::exec_tag::value;
    //     constexpr int NM_EDGES = CODIM * (CODIM - 1) / 2;

    //     auto simplex_size = topo.getPropertySize("inds");
    //     zs::bht<int,2,int> tab{topo.get_allocator(),topo.size() * NM_EDGES};
    //     tab.reset(pol,true);
    //     pol(range(topo.size()),[
    //         topo = proxy<space>({},topo),
    //         tab = proxy<space>(tab)] ZS_LAMBDA(int ti) mutable {
    //             auto inds = topo.pack(dim_c<CODIM>,"inds",ti,int_c);
    //             zs::vec<int,2> out_edges[NM_EDGES] = {};
    //             elm_to_edges(inds,out_edges);

    //             for(int i = 0;i != NM_EDGES;++i) {
    //                 auto a = out_edges[i][0];
    //                 auto b = out_edges[i][1];
    //                 if(a > b){
    //                     auto tmp = a;
    //                     a = b;
    //                     b = tmp;
    //                 }

    //                 tab.insert(IV{a,b});                       
    //             }
    //     });

    //     auto nmEntries = tab.size();
        
    //     zs::Vector<int> is{topo.get_allocator(),nmEntries},js{topo.get_allocator(),nmEntries};
    //     pol(zip(is,js,range(tab._activeKeys)),[] ZS_LAMBDA(int &i,int &j,const auto& ij){
    //         i = ij[0];
    //         j = ij[1];
    //     });
    //     spmat.build(pol,(int)fasBuffer.size(),(int)fasBuffer.size(),range(is),range(js),true_c);
    // }

    template<typename Pol>
    int mark_disconnected_island(Pol& pol,
            const zs::Vector<zs::vec<int,2>>& topo,
            // const zs::Vector<bool>& topo_disable_buffer,
            zs::Vector<int>& fasBuffer) {
        using namespace zs;
        using vec2i = zs::vec<int,2>;
        using vec3i = zs::vec<int,3>;
        using vec4i = zs::vec<int,4>;

        using IV = zs::vec<int,2>;

        constexpr auto space = RM_CVREF_T(pol)::exec_tag::value;

        // setup the incident matrix
        // auto simplex_size = topo.getPropertySize("inds");
        constexpr int simplex_size = 2;

        zs::bcht<IV,int,true,zs::universal_hash<IV>,16> tab{topo.get_allocator(),topo.size() * simplex_size};
        zs::Vector<int> is{topo.get_allocator(),0},js{topo.get_allocator(),0};
        // bool use_disable = topo_disable_buffer.size() == fasBuffer.size();
        // auto build_topo = [&](const auto& eles) mutable {
        // fmt::print("initialize incident matrix topo\n");
        std::cout << "initialize incident matrix topo" << std::endl;
        pol(range(topo.size()),[
            topo = proxy<space>(topo),
            // use_disable = use_disable,
            // topo_disable_buffer = proxy<space>(topo_disable_buffer),
            // simplex_size = simplex_size,
            tab = proxy<space>(tab)] ZS_LAMBDA(int ei) mutable {
                // if(use_disable)
                //     if(topo_disable_buffer[ei])
                //         return;
                // for(int i = 0;i != simplex_size;++i) {
                //     auto a = reinterpret_bits<int>(eles("inds",(i + 0) % simplex_size,ei));
                //     auto b = reinterpret_bits<int>(eles("inds",(i + 1) % simplex_size,ei));
                //     if(a > b){
                //         auto tmp = a;
                //         a = b;
                //         b = tmp;
                //     }

                //     tab.insert(IV{a,b});
                // }
                auto a = topo[ei][0];
                auto b = topo[ei][1];
                if(a > b){
                    auto tmp = a;
                    a = b;
                    b = tmp;
                }

                tab.insert(IV{a,b});                    
        });

        auto nmEntries = tab.size();
        std::cout << "nmEntries of Topo : " << nmEntries << std::endl;
        is.resize(nmEntries);
        js.resize(nmEntries);

        pol(zip(is,js,range(tab._activeKeys)),[] ZS_LAMBDA(int &i,int &j,const auto& ij){
            i = ij[0];
            j = ij[1];
        });
        {
            int offset = is.size();
            is.resize(offset + fasBuffer.size());
            js.resize(offset + fasBuffer.size());
            pol(range(fasBuffer.size()),[is = proxy<space>(is),js = proxy<space>(js),offset] ZS_LAMBDA(int i) mutable {
                is[offset + i] = i;
                js[offset + i] = i;
            });
        }

        zs::SparseMatrix<int,true> spmat{topo.get_allocator(),(int)fasBuffer.size(),(int)fasBuffer.size()};
        spmat.build(pol,(int)fasBuffer.size(),(int)fasBuffer.size(),range(is),range(js),true_c);

        union_find(pol,spmat,range(fasBuffer));
        zs::bcht<int, int, true, zs::universal_hash<int>, 16> vtab{fasBuffer.get_allocator(),fasBuffer.size()};        
        pol(range(fasBuffer.size()),[
            vtab = proxy<space>(vtab),
            fasBuffer = proxy<space>(fasBuffer)] ZS_LAMBDA(int vi) mutable {
                auto fa = fasBuffer[vi];
                while(fa != fasBuffer[fa])
                    fa = fasBuffer[fa];
                fasBuffer[vi] = fa;
                vtab.insert(fa);
        });

        pol(range(fasBuffer.size()),[
            fasBuffer = proxy<space>(fasBuffer),vtab = proxy<space>(vtab)] ZS_LAMBDA(int vi) mutable {
                auto ancestor = fasBuffer[vi];
                auto setNo = vtab.query(ancestor);
                fasBuffer[vi] = setNo;
        });

        auto nmSets = vtab.size();
        return nmSets;
        fmt::print("{} disjoint sets in total.\n",nmSets);
    }


    template<typename Pol>
    int mark_disconnected_island(Pol& pol,
            const zs::SparseMatrix<int,true>& spmat,
            zs::Vector<int>& fasBuffer) {
        using namespace zs;
        using vec2i = zs::vec<int,2>;
        using vec3i = zs::vec<int,3>;
        using vec4i = zs::vec<int,4>;

        using IV = zs::vec<int,2>;

        constexpr auto space = RM_CVREF_T(pol)::exec_tag::value;

        if(spmat.rows() != spmat.cols()){
            throw std::runtime_error("mark_disconnected_island : only square incident matrix is supported");
        }
        if(spmat.rows() != fasBuffer.size() || spmat.cols() != fasBuffer.size()){
            throw std::runtime_error("mark_diconnnected_island : the input fasBuffer size and spmat size not match");
        }

        union_find(pol,spmat,range(fasBuffer));
        zs::bcht<int, int, true, zs::universal_hash<int>, 16> vtab{fasBuffer.get_allocator(),fasBuffer.size()};        
        pol(range(fasBuffer.size()),[
            vtab = proxy<space>(vtab),
            fasBuffer = proxy<space>(fasBuffer)] ZS_LAMBDA(int vi) mutable {
                auto fa = fasBuffer[vi];
                while(fa != fasBuffer[fa])
                    fa = fasBuffer[fa];
                fasBuffer[vi] = fa;
                vtab.insert(fa);
        });

        pol(range(fasBuffer.size()),[
            fasBuffer = proxy<space>(fasBuffer),vtab = proxy<space>(vtab)] ZS_LAMBDA(int vi) mutable {
                auto ancestor = fasBuffer[vi];
                auto setNo = vtab.query(ancestor);
                fasBuffer[vi] = setNo;
        });

        auto nmSets = vtab.size();
        return nmSets;
    }

    template<typename Pol>
    int mark_disconnected_island(Pol& pol,
            const zs::Vector<zs::vec<int,2>>& topo,
            const zs::bht<int,1,int>& disable_points,
            const zs::bht<int,2,int>& disable_lines,
            zs::Vector<int>& fasBuffer
    ) {
        using namespace zs;
        using vec2i = zs::vec<int,2>;
        using vec3i = zs::vec<int,3>;
        using vec4i = zs::vec<int,4>;
        using table_vec2i_type = zs::bht<int,2,int>;
        // using table_int_type = zs::bcht<int,int,true,zs::universal_hash<int>,16>;
        using table_int_type = zs::bht<int,1,int>;
        using IV = zs::vec<int,2>;

        constexpr auto space = RM_CVREF_T(pol)::exec_tag::value;    

        table_vec2i_type tab{topo.get_allocator(),topo.size() * 2};
        tab.reset(pol,true);
        zs::Vector<int> is{topo.get_allocator(),0},js{topo.get_allocator(),0};
        // bool use_disable = topo_disable_buffer.size() == fasBuffer.size();

        pol(range(topo.size()),[
            topo = proxy<space>(topo),
            disable_points = proxy<space>(disable_points),
            disable_lines = proxy<space>(disable_lines),
            tab = proxy<space>(tab)] ZS_LAMBDA(int ei) mutable {
                auto a = topo[ei][0];
                auto b = topo[ei][1];

                auto setNo = disable_points.query(a);
                if(setNo >= 0){
                    // printf("skip line [%d %d] due to corner point[%d]\n",a,b,a);
                    return;
                }
                setNo = disable_points.query(b);
                if(setNo >= 0){
                    // printf("skip line [%d %d] due to corner point[%d]\n",a,b,b);
                    return;
                }

                if(a < 0 || b < 0)
                    return;
                if(a > b){
                    auto tmp = a;
                    a = b;
                    b = tmp;
                }
                setNo = disable_lines.query(vec2i{a,b});
                if(setNo >= 0)
                    return;
                // setNo = disable_lines.query(vec2i{b,a});
                // if(setNo >= 0)
                //     return;

                tab.insert(IV{a,b});                    
        });

        auto nmEntries = tab.size();
        // std::cout << "nmEntries of Topo : " << nmEntries << std::endl;
        is.resize(nmEntries);
        js.resize(nmEntries);

        pol(zip(is,js,range(tab._activeKeys)),[] ZS_LAMBDA(int &i,int &j,const auto& ij){
            i = ij[0];
            j = ij[1];
        });

        zs::SparseMatrix<int,true> spmat{topo.get_allocator(),(int)fasBuffer.size(),(int)fasBuffer.size()};
        spmat.build(pol,(int)fasBuffer.size(),(int)fasBuffer.size(),range(is),range(js),true_c);

        return mark_disconnected_island(pol,spmat,fasBuffer);    
    }


    template<typename Pol>
    int mark_disconnected_island(Pol& pol,
            const zs::Vector<zs::vec<int,2>>& topo,
            const zs::bht<int,1,int>& disable_points,
            const zs::bht<int,2,int>& disable_lines,
            zs::Vector<int>& fasBuffer,
            zs::bht<int,2,int>& tab) {
        using namespace zs;
        using vec2i = zs::vec<int,2>;
        using vec3i = zs::vec<int,3>;
        using vec4i = zs::vec<int,4>;
        using table_vec2i_type = zs::bht<int,2,int>;
        // using table_int_type = zs::bcht<int,int,true,zs::universal_hash<int>,16>;
        using table_int_type = zs::bht<int,1,int>;
        using IV = zs::vec<int,2>;

        constexpr auto space = RM_CVREF_T(pol)::exec_tag::value;    

        // table_vec2i_type tab{topo.get_allocator(),topo.size() * 2};
        tab.reset(pol,true);
        zs::Vector<int> is{topo.get_allocator(),0},js{topo.get_allocator(),0};
        // bool use_disable = topo_disable_buffer.size() == fasBuffer.size();

        pol(range(topo.size()),[
            topo = proxy<space>(topo),
            disable_points = proxy<space>(disable_points),
            disable_lines = proxy<space>(disable_lines),
            tab = proxy<space>(tab)] ZS_LAMBDA(int ei) mutable {
                auto a = topo[ei][0];
                auto b = topo[ei][1];

                auto setNo = disable_points.query(a);
                if(setNo >= 0){
                    // printf("skip line [%d %d] due to corner point[%d]\n",a,b,a);
                    return;
                }
                setNo = disable_points.query(b);
                if(setNo >= 0){
                    // printf("skip line [%d %d] due to corner point[%d]\n",a,b,b);
                    return;
                }

                if(a < 0 || b < 0)
                    return;
                if(a > b){
                    auto tmp = a;
                    a = b;
                    b = tmp;
                }
                setNo = disable_lines.query(vec2i{a,b});
                if(setNo >= 0)
                    return;
                // setNo = disable_lines.query(vec2i{b,a});
                // if(setNo >= 0)
                //     return;

                tab.insert(IV{a,b});                    
        });

        auto nmEntries = tab.size();
        std::cout << "nmEntries of Topo : " << nmEntries << std::endl;
        is.resize(nmEntries);
        js.resize(nmEntries);

        pol(zip(is,js,range(tab._activeKeys)),[] ZS_LAMBDA(int &i,int &j,const auto& ij){
            i = ij[0];
            j = ij[1];
        });

        zs::SparseMatrix<int,true> spmat{topo.get_allocator(),(int)fasBuffer.size(),(int)fasBuffer.size()};
        spmat.build(pol,(int)fasBuffer.size(),(int)fasBuffer.size(),range(is),range(js),true_c);

        return mark_disconnected_island(pol,spmat,fasBuffer);    
    }

    template<typename Pol,typename SurfTriTileVec,typename PosTileVec>
    void eval_intersection_ring_of_surf_tris(Pol& pol,
            const SurfTriTileVec& tris0,
            const PosTileVec& verts0,
            const SurfTriTileVec& tris1,
            const PosTileVec& verts1,
            zs::Vector<zs::vec<int,2>>& intersect_buffer,
            zs::Vector<int>& ring_tag) {
        
    }

    template<typename Pol,typename SurfTriTileVec,typename PosTileVec>
    void eval_self_intersection_ring_of_surf_tris(Pol& pol,
            const SurfTriTileVec& tris,
            const PosTileVec& verts) {

    }

    template<typename Pol,typename TetTileVec,typename HalfFacetTileVec>
    bool build_tetrahedra_half_facet(Pol& pol,
            TetTileVec& tets,
            HalfFacetTileVec& halfFacet) {
        using namespace zs;
        using vec2i = zs::vec<int,2>;
        using vec3i = zs::vec<int,3>;
        using vec4i = zs::vec<int,4>;
        using T = typename TetTileVec::value_type;

        constexpr auto space = Pol::exec_tag::value;
        halfFacet.resize(tets.size() * 4);

        TILEVEC_OPS::fill(pol,halfFacet,"opposite_hf",reinterpret_bits<T>((int)-1));
        TILEVEC_OPS::fill(pol,halfFacet,"next_hf",reinterpret_bits<T>((int)-1));
        TILEVEC_OPS::fill(pol,halfFacet,"to_tet",reinterpret_bits<T>((int)-1));
        TILEVEC_OPS::fill(pol,halfFacet,"local_idx",reinterpret_bits<T>((int)-1));

        bcht<vec3i,int,true,universal_hash<vec3i>,32> hftab{tets.get_allocator(),tets.size() * 4};

        zs::vec<int,3 * 4> facet_indices{
            1,3,2,
            0,2,3,
            0,3,1,
            0,1,2
        };
        pol(zs::range(tets.size()),[
            tets = proxy<space>({},tets),
            halfFacet = proxy<space>({},halfFacet),
            facet_indices = facet_indices,
            hftab = proxy<space>(hftab)] ZS_LAMBDA(int ti) mutable {
                auto tet = tets.pack(dim_c<4>,"inds",ti,int_c);
                vec4i hinds{};
                for(int i = 0;i != 4;++i) {
                    auto facet = vec3i{
                        tet[facet_indices[i * 3 + 0]],
                        tet[facet_indices[i * 3 + 1]],
                        tet[facet_indices[i * 3 + 2]]};
                    int min_idx = 0;
                    int min_id = facet[i];
                    for(int i = 1;i != 3;++i)
                        if(facet[i] < min_id){
                            min_idx = i;
                            min_id = facet[i];
                        }
                    for(int i = 0;i != min_idx;++i) {
                        auto tmp = facet[0];
                        facet[0] = facet[1];
                        facet[1] = facet[2];
                        facet[2] = tmp;
                    }

                    if(hinds[i] = hftab.insert(facet);hinds[i] >= 0){
                        halfFacet("to_tet",hinds[i]) = zs::reinterpret_bits<T>(ti);
                        halfFacet("local_idx",hinds[i]) = zs::reinterpret_bits<T>(i);
                    }

                    if(i == 0) {
                        tets("hf_inds",ti) = zs::reinterpret_bits<T>((int)hinds[i]);
                    }
                }
                for(int i = 0;i != 4;++i)
                    halfFacet("next_hf",hinds[i]) = zs::reinterpret_bits<T>((int)hinds[(i + 1) % 3]);
        });

        pol(zs::range(halfFacet.size()),[
            tets = proxy<space>({},tets),
            halfFacet = proxy<space>({},halfFacet),
            facet_indices = facet_indices,
            hftab = proxy<space>(hftab)] ZS_LAMBDA(int hi) mutable {
                auto ti = zs::reinterpret_bits<int>(halfFacet("to_tet",hi));
                auto tet = tets.pack(dim_c<4>,"inds",ti,int_c);
                auto local_idx = zs::reinterpret_bits<int>(halfFacet("local_idx",hi));
                auto facet = vec3i{
                        tet[facet_indices[local_idx * 3 + 0]],
                        tet[facet_indices[local_idx * 3 + 1]],
                        tet[facet_indices[local_idx * 3 + 2]]};
                int min_idx = 0;
                int min_id = facet[0];
                for(int i = 1;i != 3;++i)
                    if(facet[i] < min_id){
                        min_idx = i;
                        min_id = facet[i];
                    }
                for(int i = 0;i != min_idx;++i) {
                    auto tmp = facet[0];
                    facet[0] = facet[1];
                    facet[1] = facet[2];
                    facet[2] = tmp;
                }
                auto tmp = facet[1];
                facet[1] = facet[2];
                facet[2] = tmp;
                if(auto no = hftab.query(facet);no >= 0) {
                    halfFacet("opposite_hf",hi) = zs::reinterpret_bits<T>((int)no);
                }
        });

        return true;
    }

    // the input mesh should be a manifold
    template<typename Pol,typename TriTileVec,typename PointTileVec,typename HalfEdgeTileVec>
    bool build_surf_half_edge(Pol& pol,
            TriTileVec& tris,
            // EdgeTileVec& lines,
            PointTileVec& points,
            HalfEdgeTileVec& halfEdge) {
        using namespace zs;
        using vec2i = zs::vec<int, 2>;
		using vec3i = zs::vec<int, 3>;
        using T = typename TriTileVec::value_type;

        constexpr auto space = Pol::exec_tag::value;

        halfEdge.resize(tris.size() * 3);

        TILEVEC_OPS::fill(pol,halfEdge,"local_vertex_id",reinterpret_bits<T>((int)-1));
        TILEVEC_OPS::fill(pol,halfEdge,"to_face",reinterpret_bits<T>((int)-1));
        // TILEVEC_OPS::fill(pol,halfEdge,"to_edge",reinterpret_bits<T>((int)-1));
        TILEVEC_OPS::fill(pol,halfEdge,"opposite_he",reinterpret_bits<T>((int)-1));
        TILEVEC_OPS::fill(pol,halfEdge,"next_he",reinterpret_bits<T>((int)-1));      
        // we might also need a space hash structure here, map from [i1,i2]->[ej]

        // surface tri edges' indexing the halfedge list
        bcht<vec2i,int,true,universal_hash<vec2i>,32> hetab{tris.get_allocator(),tris.size() * 3};
        // Vector<int> sfi{tris.get_allocator(),tris.size() * 3};

        // surface points' indexing one of the connected half-edge
        bcht<int,int,true,universal_hash<int>,32> ptab{points.get_allocator(),points.size()};
        Vector<int> spi{points.get_allocator(),points.size()};
        pol(range(points.size()),
            [ptab = proxy<space>(ptab),points = proxy<space>({},points),spi = proxy<space>(spi)] ZS_LAMBDA(int pi) mutable {
                auto pidx = reinterpret_bits<int>(points("inds",pi));
                if(int no = ptab.insert(pidx);no >= 0)
                    spi[no] = pi;
                else{
                    printf("same point [%d] has been inserted twice\n",pidx);
                }
        });

        pol(range(tris.size()),
            [hetab = proxy<space>(hetab),
                points = proxy<space>({},points),
                ptab = proxy<space>(ptab),
                spi = proxy<space>(spi),
                // lines = proxy<space>({},lines),
                // etab = proxy<space>(etab),
                // sei = proxy<space>(sei),
                halfEdge = proxy<space>({},halfEdge),
                tris = proxy<space>({},tris)] ZS_LAMBDA(int ti) mutable {
                    auto tri = tris.pack(dim_c<3>,"inds",ti).reinterpret_bits(int_c);
                    vec3i hinds{};
                    for(int i = 0;i != 3;++i){
                        if(hinds[i] = hetab.insert(vec2i{tri[i],tri[(i+1)%3]});hinds[i] >= 0){
                            int no = hinds[i];
                            auto pno = spi[ptab.query(tri[i])];
                            halfEdge("local_vertex_id",no) = reinterpret_bits<T>((int)i);
                            points("he_inds",pno) = reinterpret_bits<T>(no);

                            // if(tri[i] < tri[(i+1)%3]){
                            auto a = tri[i];
                            auto b = tri[(i+1)%3];
                            if(a > b){
                                auto tmp = a;
                                a = b;
                                b = tmp;
                            }
                            // int eno = sei[etab.query(vec2i{a,b})];
                            // halfEdge("to_edge",no) = reinterpret_bits<T>(eno);
                            // lines("he_inds",eno) = reinterpret_bits<T>(no);
                            // }
                            halfEdge("to_face",no) = reinterpret_bits<T>(ti);
                            if(i == 0)
                                tris("he_inds",ti) = reinterpret_bits<T>(no);
                        }else {
                            auto no = hinds[i];
                            int hid = hetab.query(vec2i{tri[i],tri[(i+1)%3]});
                            int ori_ti = reinterpret_bits<int>(halfEdge("to_face",hid));
                            auto ori_tri = tris.pack(dim_c<3>,"inds",ori_ti,int_c);
                            printf("the same directed edge <%d %d> has been inserted twice! original hetab[%d], cur: %d <%d %d %d> ori: %d <%d %d %d>\n",
                                tri[i],tri[(i+1)%3],hid,ti,tri[0],tri[1],tri[2],ori_ti,ori_tri[0],ori_tri[1],ori_tri[2]);
                        }
                    }
                    for(int i = 0;i != 3;++i)
                        halfEdge("next_he",hinds[i]) = reinterpret_bits<T>((int)hinds[(i+1) % 3]);
        });

        pol(range(halfEdge.size()),
            [halfEdge = proxy<space>({},halfEdge),hetab = proxy<space>(hetab),tris = proxy<space>({},tris)] ZS_LAMBDA(int hi) mutable {
                auto ti = zs::reinterpret_bits<int>(halfEdge("to_face",hi));
                auto tri = tris.pack(dim_c<3>,"inds",ti,int_c);
                auto local_vidx = reinterpret_bits<int>(halfEdge("local_vertex_id",hi));
                auto a = tri[local_vidx];
                // auto nxtHalfEdgeIdx = reinterpret_bits<int>(halfEdge("next_he",hi));
                auto b = tri[(local_vidx + 1) % 3];
                auto key = vec2i{b,a};

                // printf("half_edge[%d] = [%d %d]\n",hi,a,b);

                if(int hno = hetab.query(key);hno >= 0) {
                    halfEdge("opposite_he",hi) = reinterpret_bits<T>(hno);
                }else {
                    halfEdge("opposite_he",hi) = reinterpret_bits<T>((int)-1);
                }
                
        });

        // std::cout << "nm_tris : " << tris.size() << std::endl;
        // std::cout << "nm_edges : " << lines.size() << std::endl;
        // std::cout << "nm_points : " << points.size() << std::endl;
        // std::cout << "nm_half_edges : " << halfEdge.size() << std::endl;

        return true;
    }

    template<typename HalfEdgeTileVec>
    constexpr int get_next_half_edge(int hei,const HalfEdgeTileVec& half_edges,int step = 1,bool reverse = false) {
        using namespace zs;
        for(int i = 0;i != step;++i)
            hei = reinterpret_bits<int>(half_edges("next_he",hei));
        if(reverse)
            hei = reinterpret_bits<int>(half_edges("opposite_he",hei));
        return hei;
    }

    template<typename HalfEdgeTileVec,typename TriTileVec>
    constexpr int half_edge_get_another_vertex(int hei,const HalfEdgeTileVec& half_edges,const TriTileVec& tris) {
        using namespace zs;
        // hei = reinterpret_bits<int>(half_edges("next_he",hei));
        hei = get_next_half_edge(hei,half_edges,1,false);
        auto ti = zs::reinterpret_bits<int>(half_edges("to_face",hei));
        auto tri = tris.pack(dim_c<3>,"inds",ti,int_c);
        return tri[reinterpret_bits<int>(half_edges("local_vertex_id",hei))];
    }

    // some operation with half edge structure
    template<int MAX_NEIGHS,typename HalfEdgeTileVec,typename TriTileVec>
    constexpr zs::vec<int,MAX_NEIGHS> get_one_ring_neigh_points(int hei,const HalfEdgeTileVec& half_edges,const TriTileVec& tris) {
        using namespace zs;
        auto res = zs::vec<int,MAX_NEIGHS>::uniform(-1);
        auto hei0 = hei;
        int i = 0;
        // res[0] = half_edge_get_another_vertex(hei,half_edges);
        for(i = 0;i != MAX_NEIGHS;++i) {
            res[i] = half_edge_get_another_vertex(hei,half_edges,tris);
            auto nhei = get_next_half_edge(hei,half_edges,2,true);
            if(nhei == hei0)
                break;
            if(nhei < 0 && (i+1) < MAX_NEIGHS) {
                nhei = get_next_half_edge(hei,half_edges,2,false);
                if(nhei > 0){
                    auto ti = zs::reinterpret_bits<int>(half_edges("to_face",nhei));
                    auto tri = tris.pack(dim_c<3>,"inds",ti,int_c);
                    res[i + 1] = tri[reinterpret_bits<int>(half_edges("local_vertex_id",nhei))];
                    break;
                }
            }
            hei = nhei;
        }
        if(i == MAX_NEIGHS)
            printf("the max_one_ring_neighbor limit exceeds");

        return res;
    }

    template<int MAX_NEIGHS,typename HalfEdgeTileVec>
    constexpr zs::vec<int,MAX_NEIGHS> get_one_ring_neigh_edges(int hei,const HalfEdgeTileVec& half_edges) {
        using namespace zs;
        auto res = zs::vec<int,MAX_NEIGHS>::uniform(-1);
        auto hei0 = hei;
        auto nhei = hei;
        int i = 0;
        for(i = 0;i != MAX_NEIGHS;++i) {
            res[i] = reinterpret_bits<int>(half_edges("to_edge",hei));
            nhei = get_next_half_edge(hei,half_edges,2,true);
            if(hei0 == nhei || nhei == -1)
                break;
            hei = nhei;
        }
        if(i < MAX_NEIGHS-1 && nhei == -1) {
            ++i;
            hei = get_next_half_edge(hei,half_edges,2,false);
            res[i] = reinterpret_bits<int>(half_edges("to_edge",hei));
        }
        return res;
    }

    template<int MAX_NEIGHS,typename HalfEdgeTileVec>
    constexpr zs::vec<int,MAX_NEIGHS> get_one_ring_neigh_tris(int hei,const HalfEdgeTileVec& half_edges) {
        using namespace zs;
        auto res = zs::vec<int,MAX_NEIGHS>::uniform(-1);
        auto hei0 = hei;
        int i = 0;
        res[0] = reinterpret_bits<int>(half_edges("to_face",hei));
        for(int i = 1;i != MAX_NEIGHS;++i) {
            hei = get_next_half_edge(hei,half_edges,1,true);
            if(hei == hei0 || hei < 0)
                break;
            res[i] = reinterpret_bits<int>(half_edges("to_face",hei));
        }

        if(i == MAX_NEIGHS)
            printf("the max_one_ring_neighbor limit exceeds");

        return res;

    }

    template<typename Pol,typename VecTI/*,zs::enable_if_all<VecTI::dim == 1, (VecTI::extent >= 2), (VecTI::etent <= 4)> = 0*/>
    void topological_incidence_matrix(Pol& pol,
            int nm_points,
            const zs::Vector<VecTI>& topos,
            zs::SparseMatrix<int,true>& spmat) {
        using namespace zs;
        using ICoord = zs::vec<int, 2>;
        constexpr auto CDIM = VecTI::extent;
        constexpr auto space = Pol::exec_tag::value;
        constexpr auto execTag = wrapv<space>{};


        // auto cudaPol = cuda_exec();

        zs::Vector<int> exclusive_offsets{topos.get_allocator(),nm_points + 1};
        zs::Vector<int> p2ts{topos.get_allocator(),0};

        {
            zs::Vector<int> cnts{topos.get_allocator(),nm_points};
            zs::Vector<int> tab_buffer{topos.get_allocator(), topos.size() * CDIM};
            bht<int,2,int> tab{topos.get_allocator(), topos.size() * CDIM};
            tab.reset(pol, true);

            cnts.reset(0);
            pol(zs::range(topos.size()),[
                topos = proxy<space>(topos),
                tab = proxy<space>(tab),
                tab_buffer = proxy<space>(tab_buffer),
                cnts = proxy<space>(cnts)] ZS_LAMBDA(int ti) mutable {
                    for(int i = 0;i != CDIM;++i)
                        if(topos[ti][i] < 0)
                            break;
                        else{
                            int local_offset = atomic_add(execTag,&cnts[topos[ti][i]], (int)1);
                            if(auto id = tab.insert(ICoord{topos[ti][i],local_offset}); id != bht<int,2,int>::sentinel_v){
                                tab_buffer[id] = ti;
                            }
                        }
            });

            exclusive_scan(pol,std::begin(cnts),std::end(cnts),std::begin(exclusive_offsets));
            auto nmPTEntries = exclusive_offsets.getVal(nm_points);
            p2ts.resize(nmPTEntries);


            pol(zs::range(nm_points),[
                topos = proxy<space>(topos),
                tab = proxy<space>(tab),
                cnts = proxy<space>(cnts),
                p2ts = proxy<space>(p2ts),
                tab_buffer = proxy<space>(tab_buffer),
                exclusive_offsets = proxy<space>(exclusive_offsets)] ZS_LAMBDA(int pi) mutable {
                    auto pt_count = cnts[pi];
                    auto ex_offset = exclusive_offsets[pi];
                    for(int i = 0;i != pt_count;++i)
                        if(auto id = tab.query(ICoord{pi,i}); id != bht<int,2,int>::sentinel_v) {
                            auto ti = tab_buffer[id];
                            p2ts[ex_offset + i] = ti;
                        }
            });
        }

        zs::Vector<int> is{topos.get_allocator(),topos.size()};
        zs::Vector<int> js{topos.get_allocator(),topos.size()};
        pol(enumerate(is, js), [] ZS_LAMBDA(int no, int &i, int &j) mutable { i = j = no; });
        auto reserveStorage = [&is, &js](std::size_t n) {
            auto size = is.size();
            is.resize(size + n);
            js.resize(size + n);
            return size;
        };
        // auto tets_entry_offset = reserveStorage(p2ts.size());

        {
            bool success = false;
            zs::Vector<int> cnts{topos.get_allocator(),topos.size()};

            // the buffer size might need to be resized
            bht<int,2,int> tab{topos.get_allocator(),topos.size() * CDIM * 2};
            zs::Vector<int> tab_buffer{topos.get_allocator(), topos.size() * CDIM * 2};

            cnts.reset(0);
            pol(range(topos.size()),[
                topos = proxy<space>(topos),
                tab = proxy<space>(tab),
                tab_buffer = proxy<space>(tab_buffer),
                p2ts = proxy<space>(p2ts),
                cnts = proxy<space>(cnts),
                execTag,
                CDIM,
                exclusive_offsets = proxy<space>(exclusive_offsets)] ZS_LAMBDA(int ti) mutable {
                    auto topo = topos[ti];
                    for(int i = 0;i != CDIM;++i){
                        auto vi = topo[i];
                        if(vi < 0)
                            return;
                        auto ex_offset = exclusive_offsets[vi];
                        auto nm_nts = exclusive_offsets[vi + 1] - exclusive_offsets[vi];
                        for(int j = 0;j != nm_nts;++j) {
                            auto nti = p2ts[ex_offset + j];
                            if(nti > ti)
                                continue;
                            if(auto id = tab.insert(ICoord{ti,atomic_add(execTag,&cnts[ti],(int)1)}); id != bht<int,2,int>::sentinel_v){
                                tab_buffer[id] = nti;
                            }
                        }
                    }
            });
            exclusive_offsets.resize(topos.size() + 1);
            exclusive_scan(pol,std::begin(cnts),std::end(cnts),std::begin(exclusive_offsets));
            int nm_topo_incidences = exclusive_offsets.getVal(topos.size());
            auto topo_conn_entry_offset = reserveStorage(nm_topo_incidences);

            pol(range(topos.size()),[
                topos = proxy<space>(topos),
                topo_conn_entry_offset = topo_conn_entry_offset,
                exclusive_offsets = proxy<space>(exclusive_offsets),
                tab = proxy<space>(tab),
                is = proxy<space>(is),
                js = proxy<space>(js),
                tab_buffer = proxy<space>(tab_buffer)] ZS_LAMBDA(int ti) mutable {
                    auto ex_offset = exclusive_offsets[ti];
                    auto nm_ntopos = exclusive_offsets[ti + 1] - exclusive_offsets[ti];
                    for(int i = 0;i != nm_ntopos;++i) {
                        if(auto id = tab.insert(ICoord{ti,i}); id != bht<int,2,int>::sentinel_v){
                            auto nti = tab_buffer[id];
                            is[ex_offset + i] = ti;
                            js[ex_offset + i] = nti;
                        }                        
                    }
            });


        }

        spmat = zs::SparseMatrix<int,true>{topos.get_allocator(),(int)topos.size(),(int)topos.size()};
        spmat.build(pol,(int)nm_points,(int)topos.size(),zs::range(is),zs::range(js)/*,zs::range(rs)*/,zs::false_c);
        spmat.localOrdering(pol,zs::false_c);
        spmat._vals.resize(spmat.nnz());
        spmat._vals.reset((int)1);
    }

    template<typename Pol,typename VecTI>
    void topological_coloring(Pol& pol,
            int nm_points,
            const zs::Vector<VecTI>& topo,
            zs::Vector<int>& coloring) {
        using namespace zs;
        constexpr auto space = Pol::exec_tag::value;

        coloring.resize(topo.size());
        zs::SparseMatrix<int,true> pt_incidence{};
        topo_nodal_incidence_matrix(pol,nm_points,topo,pt_incidence);

        union_find(pol,pt_incidence,range(coloring));
        zs::bcht<int, int, true, zs::universal_hash<int>, 16> vtab{coloring.get_allocator(),coloring.size()};        
        pol(range(coloring.size()),[
            vtab = proxy<space>(vtab),
            coloring = proxy<space>(coloring)] ZS_LAMBDA(int vi) mutable {
                auto fa = coloring[vi];
                while(fa != coloring[fa])
                    fa = coloring[fa];
                coloring[vi] = fa;
                vtab.insert(fa);
        });     

        pol(range(coloring.size()),[
            coloring = proxy<space>(coloring),vtab = proxy<space>(vtab)] ZS_LAMBDA(int vi) mutable {
                auto ancestor = coloring[vi];
                auto setNo = vtab.query(ancestor);
                coloring[vi] = setNo;
        });    
    }

    template<typename Pol,typename TopoTileVec>
    void reorder_topology(Pol& pol,
        const TopoTileVec& reorder_map,
        TopoTileVec& dst) {
        using namespace zs;
        constexpr auto space = RM_CVREF_T(pol)::exec_tag::value;
        using T = typename RM_CVREF_T(reorder_map)::value_type;

        zs::bcht<int,int,true,zs::universal_hash<int>,16> v2p_tab{reorder_map.get_allocator(),reorder_map.size()};
        zs::Vector<int> v2p_buffer{reorder_map.get_allocator(),reorder_map.size()};
        pol(zs::range(reorder_map.size()),[
            points = proxy<space>({},reorder_map),
            v2p_buffer = proxy<space>(v2p_buffer),
            v2p_tab = proxy<space>(v2p_tab)] ZS_LAMBDA(int pi) mutable {
                auto vi = zs::reinterpret_bits<int>(points("inds",pi));
                auto vNo = v2p_tab.insert(vi);
                v2p_buffer[vNo] = pi;
        });  

        auto simplex_size = dst.getPropertySize("inds");

        pol(zs::range(dst.size()),[
            dst = proxy<space>({},dst),
            simplex_size,
            reorder_map = proxy<space>({},reorder_map),
            v2p_tab = proxy<space>(v2p_tab),
            v2p_buffer = proxy<space>(v2p_buffer)] ZS_LAMBDA(int ti) mutable {
                for(int i = 0;i != simplex_size;++i) {
                    auto di = zs::reinterpret_bits<int>(dst("inds",i,ti));
                    auto vNo = v2p_tab.query(di);
                    auto pi = v2p_buffer[vNo];
                    dst("inds",i,ti) = zs::reinterpret_bits<T>(pi);
                }
        });      
    }

    template<typename Pol,typename SampleTileVec,typename TopoTileVec>
    void topological_sample(Pol& pol,
        const TopoTileVec& points,
        const SampleTileVec& verts,
        const std::string& attr_name,
        SampleTileVec& dst
    ) {
        using namespace zs;
        constexpr auto space = RM_CVREF_T(pol)::exec_tag::value;
        using T = typename RM_CVREF_T(verts)::value_type;    

        dst.resize(points.size());
        int attr_dim = verts.getPropertySize(attr_name);

        pol(zs::range(points.size()),[
            dst = proxy<space>({},dst),
            points = proxy<space>({},points),
            verts = proxy<space>({},verts),
            attr_dim,
            attr_name = zs::SmallString(attr_name)] ZS_LAMBDA(int pi) mutable {
                auto vi = reinterpret_bits<int>(points("inds",pi));
                for(int i = 0;i != attr_dim;++i)
                    dst(attr_name,i,pi) = verts(attr_name,i,vi);
        });    
    }
    // template<typename Pol,typename VecTI,zs::enable_if_all<VecTI::dim == 1, VecTI::extent >= 2, VecTI::etent <= 4> = 0>
    // void surface_topological_coloring(Pol& pol,
    //         const zs::Vector<VecTI>& topo,
    //         zs::Vector<int>& coloring) {
    //     using namespace zs;
    //     constexpr auto simplex_degree = VecTI::extent;
    //     constexpr auto space = Pol::exec_tag::value;

    //     coloring.resize(topo.size());
    //     if(simplex_degree == 2)
    //         wire_frame_coloring(pol,topo,coloring);
    //     else if(simplex_degree == 3)
    //         triangle_mesh_coloring(pol,topo,coloring);
    //     else if(simplex_degree == 4)
    //         tet_mesh_coloring(pol,topo)
        
    // }

};