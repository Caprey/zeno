#include "Structures.hpp"
#include "zensim/Logger.hpp"
#include "zensim/cuda/execution/ExecutionPolicy.cuh"
#include "zensim/omp/execution/ExecutionPolicy.hpp"
#include "zensim/geometry/PoissonDisk.hpp"
#include "zensim/geometry/VdbLevelSet.h"
#include "zensim/geometry/VdbSampler.h"
#include "zensim/io/MeshIO.hpp"
#include "zensim/math/bit/Bits.h"
#include "zensim/types/Property.h"
#include <atomic>
#include <zeno/VDBGrid.h>
#include <zeno/types/ListObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/PrimitiveObject.h>
#include <zeno/types/StringObject.h>

#include "../geometry/linear_system/mfcg.hpp"

#include "../geometry/kernel/calculate_facet_normal.hpp"
#include "../geometry/kernel/topology.hpp"
#include "../geometry/kernel/compute_characteristic_length.hpp"
#include "../geometry/kernel/calculate_bisector_normal.hpp"

#include "../geometry/kernel/tiled_vector_ops.hpp"
#include "../geometry/kernel/geo_math.hpp"

#include "../geometry/kernel/calculate_edge_normal.hpp"

#include "zensim/container/Bvh.hpp"
#include "zensim/container/Bvs.hpp"
#include "zensim/container/Bvtt.hpp"

#include "collision_energy/vertex_face_sqrt_collision.hpp"
#include "collision_energy/vertex_face_collision.hpp"
// #include "collision_energy/edge_edge_sqrt_collision.hpp"
// #include "collision_energy/edge_edge_collision.hpp"

#include "collision_energy/evaluate_collision.hpp"
#include "../geometry/kernel/intersection.hpp"

#include "zensim/math/matrix/SparseMatrix.hpp"

namespace zeno {

#define MAX_FP_COLLISION_PAIRS 4

#define USE_SPARSE_MATRIX


template <typename SpmatT, typename VecTM, typename VecTI,
          zs::enable_if_all<VecTM::dim == 2, VecTM::template range_t<0>::value == VecTM::template range_t<1>::value,
                            VecTI::dim == 1, VecTI::extent * 3 == VecTM::template range_t<0>::value> = 0>
__forceinline__ __device__ void
update_hessian(cooperative_groups::thread_block_tile<8, cooperative_groups::thread_block> &tile, SpmatT &spmat,
               const VecTI &inds, const VecTM &hess) {
    using namespace zs;
    constexpr int codim = VecTI::extent;
    using mat3 = typename SpmatT::value_type;
    const auto nnz = spmat.nnz();
    const int cap = __popc(tile.ballot(1)); // assume active pattern 0...001111 [15, 14, ..., 0]
    auto laneId = tile.thread_rank();
#pragma unroll
    for (int i = 0; i != codim; ++i) {
        auto subOffsetI = i * 3;
        auto row = inds[i];
        // diagonal
        auto loc = spmat._ptrs[row];
        auto &mat = const_cast<mat3 &>(spmat._vals[loc]);

        for (int d = laneId; d < 9; d += cap) {
            atomic_add(exec_cuda, &mat(d / 3, d % 3), hess(subOffsetI + d / 3, subOffsetI + d % 3));
        }
        // non-diagonal
        for (int j = i + 1; j < codim; ++j) {
            auto subOffsetJ = j * 3;
            auto col = inds[j];
            if (row < col) {
                auto loc = spmat.locate(row, col, zs::true_c);
                auto &mat = const_cast<mat3 &>(spmat._vals[loc]);
                for (int d = laneId; d < 9; d += cap)
                    atomic_add(exec_cuda, &mat.val(d), hess(subOffsetI + d / 3, subOffsetJ + d % 3));
            } else {
                auto loc = spmat.locate(col, row, zs::true_c);
                auto &mat = const_cast<mat3 &>(spmat._vals[loc]);
                for (int d = laneId; d < 9; d += cap)
                    atomic_add(exec_cuda, &mat.val(d), hess(subOffsetI + d % 3, subOffsetJ + d / 3));
            }
        }
    }
}
template <typename T, zs::enable_if_t<std::is_fundamental_v<T>> = 0>
__forceinline__ __device__ T tile_shfl(cooperative_groups::thread_block_tile<8, cooperative_groups::thread_block> &tile,
                                       T var, int srcLane) {
    return tile.shfl(var, srcLane);
}
template <typename VecT, zs::enable_if_t<zs::is_vec<VecT>::value> = 0>
__forceinline__ __device__ VecT tile_shfl(
    cooperative_groups::thread_block_tile<8, cooperative_groups::thread_block> &tile, const VecT &var, int srcLane) {
    VecT ret{};
    for (typename VecT::index_type i = 0; i != VecT::extent; ++i)
        ret.val(i) = tile_shfl(tile, var.val(i), srcLane);
    return ret;
}
template <typename SpmatT, typename VecTM, typename VecTI,
          zs::enable_if_all<VecTM::dim == 2, VecTM::template range_t<0>::value == VecTM::template range_t<1>::value,
                            VecTI::dim == 1, VecTI::extent * 3 == VecTM::template range_t<0>::value> = 0>
__forceinline__ __device__ void update_hessian(SpmatT &spmat, const VecTI &inds, const VecTM &hess,
                                               bool has_work = true) {
    using namespace zs;
    // constexpr int codim = VecTI::extent;
    auto tile = cg::tiled_partition<8>(cg::this_thread_block());

    u32 work_queue = tile.ballot(has_work);
    while (work_queue) {
        auto cur_rank = __ffs(work_queue) - 1;
        auto cur_work = tile_shfl(tile, hess, cur_rank);
        auto cur_index = tile.shfl(inds, cur_rank); // gather index as well
        update_hessian(tile, spmat, cur_index, cur_work);

        if (tile.thread_rank() == cur_rank)
            has_work = false;
        work_queue = tile.ballot(has_work);
    }
    return;
}

struct FleshDynamicStepping : INode {

    using T = float;
    using Ti = int;
    using dtiles_t = zs::TileVector<T,32>;
    using tiles_t = typename ZenoParticles::particles_t;
    using vec2 = zs::vec<T,2>;
    using vec3 = zs::vec<T, 3>;
    using mat3 = zs::vec<T, 3, 3>;
    using mat9 = zs::vec<T,9,9>;
    using mat12 = zs::vec<T,12,12>;

    using bvh_t = zs::LBvh<3,int,T>;
    using bv_t = zs::AABBBox<3, T>;

    using pair3_t = zs::vec<Ti,3>;
    using pair4_t = zs::vec<Ti,4>;

    using spmat_t = zs::SparseMatrix<mat3, true>;

    // currently only backward euler integrator is supported
    // topology evaluation should be called before applying this node
    struct FEMDynamicSteppingSystem {
        template <typename Model>
        void computeCollisionEnergy(zs::CudaExecutionPolicy& cudaPol,const Model& model,
                dtiles_t& vtemp,
                dtiles_t& etemp,
                dtiles_t& sttemp,
                dtiles_t& setemp,
                dtiles_t& ee_buffer,
                dtiles_t& fe_buffer) {
            using namespace zs;
            constexpr auto space = execspace_e::cuda;

            T lambda = model.lam;
            T mu = model.mu;
        }


        void findInversion(zs::CudaExecutionPolicy& cudaPol,dtiles_t& vtemp,dtiles_t& etemp) {
            using namespace zs;
            constexpr auto space = execspace_e::cuda;
            TILEVEC_OPS::fill(cudaPol,vtemp,"is_inverted",(T)0.0);  
            TILEVEC_OPS::fill(cudaPol,etemp,"is_inverted",(T)0.0);  
            cudaPol(zs::range(eles.size()),
                [vtemp = proxy<space>({},vtemp),
                        quads = proxy<space>({},eles),
                        etemp = proxy<space>({},etemp)] ZS_LAMBDA(int ei) mutable {
                    auto DmInv = quads.template pack<3,3>("IB",ei);
                    auto inds = quads.template pack<4>("inds",ei).reinterpret_bits(int_c);
                    vec3 x1[4] = {vtemp.template pack<3>("xn", inds[0]),
                            vtemp.template pack<3>("xn", inds[1]),
                            vtemp.template pack<3>("xn", inds[2]),
                            vtemp.template pack<3>("xn", inds[3])};   

                    mat3 F{};
                    {
                        auto x1x0 = x1[1] - x1[0];
                        auto x2x0 = x1[2] - x1[0];
                        auto x3x0 = x1[3] - x1[0];
                        auto Ds = mat3{x1x0[0], x2x0[0], x3x0[0], x1x0[1], x2x0[1],
                                        x3x0[1], x1x0[2], x2x0[2], x3x0[2]};
                        F = Ds * DmInv;
                    } 
                    if(zs::determinant(F) < 0.0){
                        // for(int i = 0;i < 4;++i)
                        //     vtemp("is_inverted",inds[i]) = reinterpret_bits<T>((int)1);   
                        etemp("is_inverted",ei) = (T)1.0;   
                    }
                    else {
                        etemp("is_inverted",ei) = (T)0.0;   
                    }               
            });
            cudaPol(zs::range(eles.size()),
                [vtemp = proxy<space>({},vtemp),
                        quads = proxy<space>({},eles),
                        etemp = proxy<space>({},etemp)] ZS_LAMBDA(int ei) mutable {
                auto inds = quads.template pack<4>("inds",ei).reinterpret_bits(int_c);
                auto is_inverted = etemp("is_inverted",ei) > (T)0.5;  
                if(is_inverted)
                    for(int i = 0;i != 4;++i){
                        vtemp("is_inverted",inds[i]) = (T)1.0;     
                    }       
            });
        }


        void accumInversion(zs::CudaExecutionPolicy& cudaPol,dtiles_t& vtemp,dtiles_t& etemp) {
            using namespace zs;
            constexpr auto space = execspace_e::cuda;
            cudaPol(zs::range(eles.size()),
                [vtemp = proxy<space>({},vtemp),
                        quads = proxy<space>({},eles),
                        etemp = proxy<space>({},etemp)] ZS_LAMBDA(int ei) mutable {
                    auto DmInv = quads.template pack<3,3>("IB",ei);
                    auto inds = quads.template pack<4>("inds",ei).reinterpret_bits(int_c);
                    vec3 x1[4] = {vtemp.template pack<3>("xn", inds[0]),
                            vtemp.template pack<3>("xn", inds[1]),
                            vtemp.template pack<3>("xn", inds[2]),
                            vtemp.template pack<3>("xn", inds[3])};   

                    mat3 F{};
                    {
                        auto x1x0 = x1[1] - x1[0];
                        auto x2x0 = x1[2] - x1[0];
                        auto x3x0 = x1[3] - x1[0];
                        auto Ds = mat3{x1x0[0], x2x0[0], x3x0[0], x1x0[1], x2x0[1],
                                        x3x0[1], x1x0[2], x2x0[2], x3x0[2]};
                        F = Ds * DmInv;
                    } 
                    if(zs::determinant(F) < 0.0){
                        // for(int i = 0;i < 4;++i)
                        //     vtemp("is_inverted",inds[i]) = reinterpret_bits<T>((int)1);   
                        etemp("is_inverted",ei) = (T)1.0;   
                    }
                    // else {
                    //     etemp("is_inverted",ei) = reinterpret_bits<T>((int)0);   
                    // }               
            });
            cudaPol(zs::range(eles.size()),
                [vtemp = proxy<space>({},vtemp),
                        quads = proxy<space>({},eles),
                        etemp = proxy<space>({},etemp)] ZS_LAMBDA(int ei) mutable {
                auto inds = quads.template pack<4>("inds",ei).reinterpret_bits(int_c);
                auto is_inverted = etemp("is_inverted",ei) > (T)0.5;  
                if(is_inverted)
                    for(int i = 0;i != 4;++i){
                        vtemp("is_inverted",inds[i]) = (T)1.0;     
                    }       
            });
        }


        void computePositionConstraintGradientAndHessian(zs::CudaExecutionPolicy& cudaPol,
            dtiles_t& vtemp,
            const std::string& binderTag,
            const std::string& thicknessTag,
            const std::string& inversionTag,
            const dtiles_t& kverts,
            dtiles_t& gh_buffer) {
                using namespace zs;
                constexpr auto space = execspace_e::cuda;
                int max_nm_binders = tris.getPropertySize(binderTag);
                // printf("max_nm_binders = %d\n",max_nm_binders);

                cudaPol(zs::range(tris.size()),
                    [vtemp = proxy<space>({},vtemp),
                        verts = proxy<space>({},verts),
                        eles = proxy<space>({},eles),
                        binderTag = zs::SmallString(binderTag),
                        thicknessTag = zs::SmallString(thicknessTag),
                        inversionTag = zs::SmallString(inversionTag),
                        tris = proxy<space>({},tris),
                        kverts = proxy<space>({},kverts),
                        binderStiffness = binderStiffness,
                        max_nm_binders = max_nm_binders,
                        gh_buffer = proxy<space>({},gh_buffer)] ZS_LAMBDA(int ti) mutable {
                    int nm_binders = 0;

                    for(int i = 0;i != max_nm_binders;++i){
                        auto idx = reinterpret_bits<int>(tris(binderTag,i,ti));
                        if(idx < 0)
                            break;
                        ++nm_binders;
                    }

                    // printf("binder_ids[%d] : %d : %d %d %d %d\n",ti,nm_binders,
                    //     reinterpret_bits<int>(tris(binderTag,0,ti)),
                    //     reinterpret_bits<int>(tris(binderTag,1,ti)),
                    //     reinterpret_bits<int>(tris(binderTag,2,ti)),
                    //     reinterpret_bits<int>(tris(binderTag,3,ti)));

                    if(nm_binders == 0)
                        return;
                    auto tri = tris.pack(dim_c<3>,"inds",ti).reinterpret_bits(int_c);
                    if(verts.hasProperty("binder_fail"))
                        for(int i = 0;i != 3;++i)
                            if(verts("binder_fail",tri[i]) > (T)0.5)
                                return;
                    auto binder_weakness_param = (T)1.0;
                    // for(int i = 0;i != 3;++i)
                    //     if(vtemp("is_inverted",tri[i]) > (T)0.5)
                    //         return;

                    auto ei = reinterpret_bits<int>(tris("ft_inds",ti));
                    auto tet = eles.pack(dim_c<4>,"inds",ei).reinterpret_bits(int_c);


                    auto mu = eles("mu",ei);
                    auto lam = eles("lam",ei);
                    // auto vole = tris("vol",ti);
                    vec3 cp[4] = {};

                    cp[1] = vtemp.pack(dim_c<3>,"xn",tri[0]);
                    cp[2] = vtemp.pack(dim_c<3>,"xn",tri[1]);
                    cp[3] = vtemp.pack(dim_c<3>,"xn",tri[2]);


                    auto inds_reorder = zs::vec<int,3>::zeros();
                    for(int i = 0;i != 3;++i){
                        auto idx = tri[i];
                        for(int j = 0;j != 4;++j)
                            if(idx == tet[j])
                                inds_reorder[i] = j;
                    }

                    for(int i = 0;i != nm_binders;++i) {
                        auto idx = reinterpret_bits<int>(tris(binderTag,i,ti));
                        
                        auto ceps = tris(thicknessTag,i,ti);
                        auto from_inside = tris(inversionTag,i,ti) > (T)0.0;
                        if(kverts.hasProperty("b_fail"))
                            if(kverts("b_fail",idx) > (T)0.5)
                                continue;

                        if(idx >= kverts.size()){
                            printf("kverts buffer overflow %d >= %d\n",idx,kverts.size());
                        }

                        cp[0] = kverts.pack(dim_c<3>,"x",idx);
                        auto kstiffness = (T)1.0;
                        if(kverts.hasProperty("binderStiffness"))
                            kstiffness = kverts("binderStiffness",idx);
                        auto alpha = binderStiffness * binder_weakness_param * kstiffness;
                        auto beta = (T)1.0/(T)nm_binders;
                        auto cgrad = -alpha * beta * VERTEX_FACE_SQRT_COLLISION::gradient(cp,mu,lam,ceps,from_inside);
                        auto cH = alpha * beta * VERTEX_FACE_SQRT_COLLISION::hessian(cp,mu,lam,ceps,from_inside);

                        // printf("cgrad : %f cH %f params: %f %f %f %f\n",cgrad.norm(),cH.norm(),
                        //         (float)kstiffness,
                        //         (float)binderStiffness,
                        //         (float)binder_weakness_param,
                        //         (float)alpha);

                        // if(isnan(cH.norm())) {
                        //     printf("nan CH detected at Binder : %d from inside %d and ceps = \n",ti,from_inside,(float)ceps);
                        //     printf("cp : \n%f %f %f\n%f %f %f\n%f %f %f\n%f %f %f\n",
                        //         (float)cp[0][0],(float)cp[0][1],(float)cp[0][2],
                        //         (float)cp[1][0],(float)cp[1][1],(float)cp[1][2],
                        //         (float)cp[2][0],(float)cp[2][1],(float)cp[2][2],
                        //         (float)cp[3][0],(float)cp[3][1],(float)cp[3][2]);
                        // }

                        for(int i = 3;i != 12;++i){
                            int d0 = i % 3;
                            int row = inds_reorder[i/3 - 1]*3 + d0;
                            atomic_add(exec_cuda,&gh_buffer("grad",row,ei),cgrad[i]);
                            for(int j = 3;j != 12;++j){
                                int d1 = j % 3;
                                int col = inds_reorder[j/3 - 1]*3 + d1;
                                if(row >= 12 || col >= 12){
                                    printf("invalid row = %d and col = %d %d %d detected %d %d %d\n",row,col,i/3,j/3,
                                        inds_reorder[0],
                                        inds_reorder[1],
                                        inds_reorder[2]);
                                }
                                atomic_add(exec_cuda,&gh_buffer("H",row*12 + col,ei),cH(i,j));
                            }                    
                        }                        
                    }
                });
        }

        template <typename Model>
        void computeCollisionGradientAndHessian(zs::CudaExecutionPolicy& cudaPol,const Model& model,
                            dtiles_t& vtemp,
                            dtiles_t& etemp,
                            dtiles_t& sttemp,
                            dtiles_t& setemp,
                            // dtiles_t& ee_buffer,
                            dtiles_t& fp_buffer,
                            dtiles_t& kverts,
                            dtiles_t& kc_buffer,
                            dtiles_t& gh_buffer,
                            T kd_theta = (T)0.0,
                            bool explicit_collision = false,
                            bool neglect_inverted = true) {
            using namespace zs;
            constexpr auto space = execspace_e::cuda;

            int offset = eles.size();

            T lambda = model.lam;
            T mu = model.mu; 

            // auto stBvh = bvh_t{};
            // auto bvs = retrieve_bounding_volumes(cudaPol,vtemp,tris,wrapv<3>{},(T)0.0,"xn");
            // stBvh.build(cudaPol,bvs);
            // auto avgl = compute_average_edge_length(cudaPol,vtemp,"xn",tris);
            // auto bvh_thickness = 5 * avgl;            
            // if(!calculate_facet_normal(cudaPol,vtemp,"xn",tris,sttemp,"nrm")){
            //     throw std::runtime_error("fail updating facet normal");
            // }       
            // if(!COLLISION_UTILS::calculate_cell_bisector_normal(cudaPol,
            //     vtemp,"xn",
            //     lines,
            //     tris,
            //     sttemp,"nrm",
            //     setemp,"nrm")){
            //         throw std::runtime_error("fail calculate cell bisector normal");
            // }    


            COLLISION_UTILS::do_facet_point_collision_detection<MAX_FP_COLLISION_PAIRS>(cudaPol,
                vtemp,"xn",
                points,
                lines,
                tris,
                sttemp,
                setemp,
                fp_buffer,
                in_collisionEps,out_collisionEps);

            COLLISION_UTILS::evaluate_fp_collision_grad_and_hessian(cudaPol,
                vtemp,"xn","vn",dt,
                fp_buffer,
                gh_buffer,offset,
                in_collisionEps,out_collisionEps,
                (T)collisionStiffness,
                (T)mu,(T)lambda,(T)kd_theta);
            


            // COLLISION_UTILS::do_kinematic_point_collision_detection<MAX_FP_COLLISION_PAIRS>(cudaPol,
            //     vtemp,"xn",
            //     points,
            //     lines,
            //     tris,
            //     setemp,
            //     sttemp,
            //     kverts,
            //     kc_buffer,
            //     (T)kine_in_collisionEps,(T)kine_out_collisionEps,false);

            // offset = 0;

            // COLLISION_UTILS::evaluate_kinematic_fp_collision_grad_and_hessian(cudaPol,
            //     eles,
            //     vtemp,"xn","vn",dt,
            //     tris,
            //     kverts,
            //     kc_buffer,
            //     gh_buffer,offset,
            //     (T)kine_in_collisionEps,(T)kine_out_collisionEps,
            //     (T)kineCollisionStiffness,
            //     (T)mu,(T)lambda,(T)kd_theta);


            // adding collision damping on self collision
            // int offset = eles.size() + b_verts.size();
            // cudaPol(zs::range(fp_buffer.size() + kc_buffer.size()),
            //     [vtemp = proxy<space>({},vtemp),
            //         gh_buffer = proxy<space>({},gh_buffer),offset,kd_theta] ZS_LAMBDA(int ci) mutable {
            //     auto inds = gh_buffer.pack(dim_c<4>,"inds",ci).reinterpret_bits(int_c);
            //     for(int i = 0;i != 4;++i)
            //         if(inds[i] < 0)
            //             return;
            //     vec3 vs[4] = {};
            //     for(int i = 0;i = 4;++i)
            //         vs[i] = vtemp.pack(dim_c<3>,"vn",inds[i]);
            //     auto H = gh_buffer.pack(dim_c<12*12>,"H",ci);
            //     gh_buffer.tuple(dim_c<12*12>,"H",ci) = H;
            // });
        

        }

        void computePlaneConstraintGradientAndHessian2(zs::CudaExecutionPolicy& cudaPol,
                            const dtiles_t& vtemp,
                            const dtiles_t& sttemp,
                            const dtiles_t& kverts,
                            const dtiles_t& ktris,
                            const std::string& planeConsBaryTag,
                            const std::string& planeConsIDTag,
                            dtiles_t& nodal_gh_buffer,
                            dtiles_t& tris_gh_buffer,
                            T cnorm,bool use_sticky_condition) {
            using namespace zs;
            constexpr auto space = execspace_e::cuda;

            cudaPol(zs::range(verts.size()),[
                    verts = proxy<space>({},verts),
                    vtemp = proxy<space>({},vtemp),
                    kverts = proxy<space>({},kverts),
                    ktris = proxy<space>({},ktris),
                    planeConsBaryTag = zs::SmallString(planeConsBaryTag),
                    planeConsIDTag = zs::SmallString(planeConsIDTag),
                    kine_out_collisionEps = kine_out_collisionEps,
                    plane_constraint_stiffness = plane_constraint_stiffness,
                    use_sticky_condition = use_sticky_condition,
                    nodal_gh_buffer = proxy<space>({},nodal_gh_buffer)] ZS_LAMBDA(int vi) mutable {
                auto idx = reinterpret_bits<int>(verts(planeConsIDTag,vi));
                if(idx < 0)
                    return;      
                auto ktri = ktris.pack(dim_c<3>,"inds",idx).reinterpret_bits(int_c);

                auto is_inverted_vert = vtemp("is_inverted",vi) > (T)0.5;
                if(is_inverted_vert)
                    return;



                auto plane_root = kverts.pack(dim_c<3>,"x",ktri[0]);
                auto plane_nrm = ktris.pack(dim_c<3>,"nrm",idx);

                auto mu = verts("mu",vi);
                auto lam = verts("lam",vi);
                    // if(distance > collisionEps)
                auto eps = kine_out_collisionEps;
                auto p = vtemp.pack(dim_c<3>,"xn",vi);
                auto seg = p - plane_root;

                auto fc = vec3::zeros();
                auto Hc = mat3::zeros();
                auto dist = seg.dot(plane_nrm) - eps;
                if(dist < (T)0 || use_sticky_condition){
                    fc = -dist * mu * plane_constraint_stiffness * plane_nrm;
                    Hc = mu * plane_constraint_stiffness * dyadic_prod(plane_nrm,plane_nrm);
                }

                // printf("apply plane constraint with force : %f %f\n",(float)dist,(float)fc.norm());

                nodal_gh_buffer.tuple(dim_c<3>,"grad",vi) = fc;
                nodal_gh_buffer.tuple(dim_c<3,3>,"H",vi) = Hc;

            });


            cudaPol(zs::range(tris.size()),[
                    vtemp = proxy<space>({},vtemp),
                    sttemp = proxy<space>({},sttemp),
                    verts = proxy<space>({},verts),
                    tris = proxy<space>({},tris),
                    kverts = proxy<space>({},kverts),
                    ktris = proxy<space>({},ktris),
                    cnorm = cnorm,
                    planeConsIDTag = zs::SmallString(planeConsIDTag),
                    kine_out_collisionEps = kine_out_collisionEps,
                    kine_in_collisionEps = kine_in_collisionEps,
                    plane_constraint_stiffness = plane_constraint_stiffness,
                    use_sticky_condition = use_sticky_condition,
                    tris_gh_buffer = proxy<space>({},tris_gh_buffer)] ZS_LAMBDA(int ti) mutable {
                auto kp_idx = reinterpret_bits<int>(tris(planeConsIDTag,ti));
                if(kp_idx < 0)
                    return;
                auto kp = kverts.pack(dim_c<3>,"x",kp_idx);
                auto tri = tris.pack(dim_c<3>,"inds",ti).reinterpret_bits(int_c);
                for(int i = 0;i != 3;++i){
                    auto is_inverted_vert = vtemp("is_inverted",tri[i]) > (T)0.5;
                    if(is_inverted_vert)
                        return;
                }
            
                // auto tnrm = sttemp.pack(dim_c<3>,"nrm",ti);

                auto mu = verts("mu",tri[0]);
                auto lam = verts("lam",tri[0]);

                auto eps = kine_out_collisionEps;
                vec3 vs[4] = {};
                vs[0] = kp;
                for(int i = 0;i != 3;++i)
                    vs[i + 1] = vtemp.pack(dim_c<3>,"xn",tri[i]);
                
                vec3 e[3] = {};
                e[0] = vs[3] - vs[2];
                e[1] = vs[0] - vs[2];
                e[2] = vs[1] - vs[2];

                auto n = e[2].cross(e[0]);
                // if(n.norm() < 1e-4)
                //     return;
                n = n/(n.norm() + 1e-6);

                T springLength = e[1].dot(n) - eps;
                auto gvf = zs::vec<T,9>::zeros();
                if(springLength < (T)0 || use_sticky_condition){
                    auto gvf_v12 = COLLISION_UTILS::springLengthGradient(vs,e,n);
                    if(isnan(gvf_v12.norm()))
                        printf("nan gvf detected at %d %f %f\n",ti,gvf_v12.norm(),n.norm());
                    for(int i = 0;i != 9;++i)
                        gvf[i] = gvf_v12[i + 3];
                }
                cnorm = (T)1.0;
                auto stiffness = plane_constraint_stiffness * cnorm;
                // stiffness = (T)0;            
                auto g = -stiffness * (T)2.0 * mu * springLength * gvf;
                auto H = stiffness * (T)2.0 * mu * zs::dyadic_prod(gvf, gvf);
                
                // if(springLength < (T)0) {
                //     auto springLengthH_M12 = COLLISION_UTILS::springLengthHessian(vs,e,n);
                //     auto springLengthH_M9 = mat9::zeros();
                //     for(int r = 0;r != 9;++r)
                //         for(int c = 0;c != 9;++c)
                //             springLengthH_M9(r,c) = springLengthH_M12(r + 3,c+ 3);
                //     H += springLength * springLengthH_M9 * (T)2.0 * stiffness * mu;
                //     make_pd(H);
                // }

                tris_gh_buffer.tuple(dim_c<9>,"grad",ti) = g;
                tris_gh_buffer.tuple(dim_c<9,9>,"H",ti) = H;           
            });
        }

        void  computePlaneConstraintGradientAndHessian(zs::CudaExecutionPolicy& cudaPol,
                            const dtiles_t& vtemp,
                            const std::string& planeConsPosTag,
                            const std::string& planeConsNrmTag,
                            const std::string& planeConsIDTag,
                            dtiles_t& nodal_gh_buffer) {
            using namespace zs;
            constexpr auto space = execspace_e::cuda;

            cudaPol(zs::range(verts.size()),[
                    verts = proxy<space>({},verts),
                    vtemp = proxy<space>({},vtemp),
                    planeConsPosTag = zs::SmallString(planeConsPosTag),
                    planeConsNrmTag = zs::SmallString(planeConsNrmTag),
                    planeConsIDTag = zs::SmallString(planeConsIDTag),
                    kine_out_collisionEps = kine_out_collisionEps,
                    plane_constraint_stiffness = plane_constraint_stiffness,
                    nodal_gh_buffer = proxy<space>({},nodal_gh_buffer)] ZS_LAMBDA(int vi) mutable {
                auto idx = reinterpret_bits<int>(verts(planeConsIDTag,vi));
                if(idx < 0)
                    return;

                // if(kverts.hasProperty("k_fail"))
                // if(verts("is_inverted",vi) > (T)0.5)
                //     return;


                auto mu = verts("mu",vi);
                auto lam = verts("lam",vi);

                auto eps = kine_out_collisionEps;
                auto plane_nrm = verts.pack(dim_c<3>,planeConsNrmTag,vi);
                auto plane_root = verts.pack(dim_c<3>,planeConsPosTag,vi);

                auto p = vtemp.pack(dim_c<3>,"xn",vi);
                auto seg = p - plane_root;

                auto fc = vec3::zeros();
                auto Hc = mat3::zeros();
                auto dist = seg.dot(plane_nrm) - eps;
                if(dist < (T)0){
                    fc = -dist * mu * plane_constraint_stiffness * plane_nrm;
                    Hc = mu * plane_constraint_stiffness * dyadic_prod(plane_nrm,plane_nrm);
                }

                // printf("apply plane constraint with force : %f %f\n",(float)dist,(float)fc.norm());

                nodal_gh_buffer.tuple(dim_c<3>,"grad",vi) = fc;
                nodal_gh_buffer.tuple(dim_c<3,3>,"H",vi) = Hc;
            });

            // cudaPol(zs::range(tris.size()),[
            //         verts = proxy<space>({},verts),
            //         tris = proxy<space>({},tris),
            //         vtemp = proxy<space>({},vtemp),
            //         planeConsPosTag = zs::SmallString(planeConsPosTag),
            //         planeConsNrmTag = zs::SmallString(planeConsNrmTag),
            //         planeConsIDTag = zs::SmallString(planeConsIDTag),
            //         kine_out_collisionEps = kine_out_collisionEps,
            //         plane_constraint_stiffness = plane_constraint_stiffness,
            //         nodal_gh_buffer = proxy<space>({},nodal_gh_buffer)] ZS_LAMBDA(int ti) mutable {
            //     auto idx = reinterpret_bits<int>(tris(planeConsIDTag,ti));
            //     if(idx < 0)
            //         return;

            //     auto tri = tris.pack(dim_c<3>,"inds",ti).reinterpret_bits(int_c);

            //     auto mu = verts("mu",tri[0]);
            //     auto lam = verts("lam",tri[0]);

            //     auto eps = kine_out_collisionEps * 2.0;
            //     auto plane_nrm = tris.pack(dim_c<3>,planeConsNrmTag,ti);
            //     auto plane_root = tris.pack(dim_c<3>,planeConsPosTag,ti);

            //     auto p = vec3::zeros();
            //     for(int i = 0;i != 3;++i)
            //         p += vtemp.pack(dim_c<3>,"xn",tri[i])/(T)3.0;
            //     auto seg = p - plane_root;

            //     auto fc = vec3::zeros();
            //     auto Hc = mat3::zeros();
            //     auto dist = seg.dot(plane_nrm) - eps;
            //     if(dist < (T)0){
            //         fc = -dist * mu * plane_constraint_stiffness * plane_nrm;
            //         Hc = mu * plane_constraint_stiffness * dyadic_prod(plane_nrm,plane_nrm);
            //     }

            //     // printf("apply plane constraint with force : %f %f\n",(float)dist,(float)fc.norm());
            //     for(int i = 0;i != 3;++i) {
            //         auto vi = tri[i];
            //         for(int d = 0;d != 3;++d)
            //             atomic_add(exec_cuda,&nodal_gh_buffer("grad",d,vi),fc[d]/(T)3.0);
            //         for(int r = 0;r != 3;++r)
            //             for(int c = 0;c != 3;++c)
            //                 atomic_add(exec_cuda,&nodal_gh_buffer("H",r * 3 + c,vi),Hc(r,c)/(T)9.0);
            //     }

            //     // nodal_gh_buffer.tuple(dim_c<3>,"grad",vi) = fc;
            //     // nodal_gh_buffer.tuple(dim_c<3,3>,"H",vi) = Hc;
            // });
        }

        template <typename ElasticModel,typename AnisoElasticModel>
        void computeGradientAndHessian(zs::CudaExecutionPolicy& cudaPol,
                            const ElasticModel& model,
                            const AnisoElasticModel& amodel,
                            const dtiles_t& vtemp,
                            const dtiles_t& etemp,
                            dtiles_t& gh_buffer,
                            T kd_alpha = (T)0.0,
                            T kd_beta = (T)0.0) {        
            using namespace zs;
            constexpr auto space = execspace_e::cuda;

            int offset = 0;
            TILEVEC_OPS::copy<4>(cudaPol,eles,"inds",gh_buffer,"inds",offset);   
            // eval the inertia term gradient
            // cudaPol(zs::range(eles.size()),[dt2 = dt2,
            //             verts = proxy<space>({},verts),
            //             eles = proxy<space>({},eles),
            //             vtemp = proxy<space>({},vtemp),
            //             gh_buffer = proxy<space>({},gh_buffer),
            //             dt = dt,offset = offset] ZS_LAMBDA(int ei) mutable {
            //     auto m = eles("m",ei)/(T)4.0;
            //     auto inds = eles.pack(dim_c<4>,"inds",ei).reinterpret_bits(int_c);
            //     auto pgrad = zs::vec<T,12>::zeros();
            //     // auto H  = zs::vec<T,12,12>::zeros();
            //     // if(eles.hasProperty("dt")) {
            //     //     dt2 = eles("dt",ei) * eles("dt",ei);
            //     // }

            //     auto inertia = (T)1.0;
            //     if(eles.hasProperty("inertia"))
            //         inertia = eles("inertia",ei);
            //     for(int i = 0;i != 4;++i){
            //         auto x1 = vtemp.pack(dim_c<3>,"xn",inds[i]);
            //         auto x0 = vtemp.pack(dim_c<3>,"xp",inds[i]);
            //         auto v0 = vtemp.pack(dim_c<3>,"vp",inds[i]);

            //         auto alpha = inertia * m/dt2;
            //         auto nodal_pgrad = -alpha * (x1 - x0 - v0 * dt);
            //         for(int d = 0;d != 3;++d){
            //             auto idx = i * 3 + d;
            //             gh_buffer("grad",idx,ei) = nodal_pgrad[d];
            //             gh_buffer("H",idx*12 + idx,ei + offset) = alpha;
            //         }
                    
            //     }
            //     // gh_buffer.tuple(dim_c<12>,"grad",ei + offset) = pgrad;
            //     // gh_buffer.template tuple<12*12>("H",ei + offset) = H;
            // });


            cudaPol(zs::range(eles.size()), [dt = dt,dt2 = dt2,aniso_strength = aniso_strength,
                            verts = proxy<space>({},verts),
                            vtemp = proxy<space>({}, vtemp),
                            etemp = proxy<space>({}, etemp),
                            gh_buffer = proxy<space>({},gh_buffer),
                            eles = proxy<space>({}, eles),
                            kd_alpha = kd_alpha,kd_beta = kd_beta,
                            model = model,amodel = amodel, volf = volf,offset = offset] ZS_LAMBDA (int ei) mutable {
                auto DmInv = eles.pack(dim_c<3,3>,"IB",ei);
                auto dFdX = dFdXMatrix(DmInv);
                auto inds = eles.pack(dim_c<4>,"inds",ei).reinterpret_bits(int_c);
                vec3 x1[4] = {vtemp.pack(dim_c<3>,"xn", inds[0]),
                                vtemp.pack(dim_c<3>,"xn", inds[1]),
                                vtemp.pack(dim_c<3>,"xn", inds[2]),
                                vtemp.pack(dim_c<3>,"xn", inds[3])};


                mat3 FAct{};
                mat3 F{};
                {
                    auto x1x0 = x1[1] - x1[0];
                    auto x2x0 = x1[2] - x1[0];
                    auto x3x0 = x1[3] - x1[0];
                    auto Ds = mat3{x1x0[0], x2x0[0], x3x0[0], x1x0[1], x2x0[1],
                                    x3x0[1], x1x0[2], x2x0[2], x3x0[2]};
                    F = Ds * DmInv;
                    FAct = F * etemp.template pack<3,3>("ActInv",ei);
                } 
                auto dFActdF = dFAdF(etemp.template pack<3,3>("ActInv",ei));

                // add the force term in gradient
                if(eles.hasProperty("mu") && eles.hasProperty("lam")) {
                    model.mu = eles("mu",ei);
                    model.lam = eles("lam",ei);
                }

                auto inversion_strength = (T)1.0;
                // for(int i = 0;i != 4;++i)
                //     if(vtemp("is_inverted",inds[i]) < (T)0.5)
                //         inversion_strength = (T)1.0;

                auto P = model.first_piola(FAct) * inversion_strength;
                auto vole = eles("vol", ei);
                auto vecP = flatten(P);
                vecP = dFActdF.transpose() * vecP;
                auto dFdXT = dFdX.transpose();
                auto vf = -vole * (dFdXT * vecP);     

                auto mg = volf * vole / (T)4.0;
                for(int i = 0;i != 4;++i)
                    for(int d = 0;d !=3 ;++d){
                        vf[i*3 + d] += mg[d];
                    }


                // assemble element-wise hessian matrix
                auto Hq = model.first_piola_derivative(FAct, true_c) * inversion_strength;
                auto dFdAct_dFdX = dFActdF * dFdX; 
                // add inertia hessian term
                auto H = dFdAct_dFdX.transpose() * Hq * dFdAct_dFdX * vole;

                if(isnan(H.norm())) {
                    printf("nan CH detected at Elastic : %d %f %f %f %f\nFAct = \n%f %f %f\n%f %f %f\n%f %f %f\nF = \n%f %f %f\n%f %f %f\n%f %f %f\n",ei,
                        (float)Hq.norm(),
                        (float)dFdAct_dFdX.norm(),
                        (float)P.norm(),
                        (float)FAct.norm(),
                        (float)FAct(0,0),(float)FAct(0,1),(float)FAct(0,2),
                        (float)FAct(1,0),(float)FAct(1,1),(float)FAct(1,2),
                        (float)FAct(2,0),(float)FAct(2,1),(float)FAct(2,2),
                        (float)F(0,0),(float)F(0,1),(float)F(0,2),
                        (float)F(1,0),(float)F(1,1),(float)F(1,2),
                        (float)F(2,0),(float)F(2,1),(float)F(2,2)                     
                    );
                }



                // if(eles.hasProperty("Muscle_ID") && (int)eles("Muscle_ID",ei) >= 0) {
                //     auto fiber = eles.pack(dim_c<3>,"fiber",ei);
                //     if(zs::abs(fiber.norm() - 1.0) < 1e-3) {
                //         fiber /= fiber.norm();
                //         // if(eles.hasProperty("mu")) {
                //         //     amodel.mu = eles("mu",ei);
                //         //     // amodel.lam = eles("lam",ei);
                            
                //         // }
                //         auto aP = amodel.do_first_piola(FAct,fiber);
                //         auto vecAP = flatten(P);
                //         vecAP = dFActdF.transpose() * vecP;
                //         vf -= vole  * dFdXT * vecAP *aniso_strength;

                //         auto aHq = amodel.do_first_piola_derivative(FAct,fiber);
                //         H += dFdAct_dFdX.transpose() * aHq * dFdAct_dFdX * vole * aniso_strength;
                //         // if((int)eles("Muscle_ID",ei) == 0){
                //         //     printf("fiber : %f %f %f,Fa = %f,aP = %f,aHq = %f,H = %f\n",fiber[0],fiber[1],fiber[2],(float)FAct.norm(),(float)aP.norm(),(float)aHq.norm(),(float)H.norm());
                //         // }
                //     }
                // }


                // adding rayleigh damping term
                // vec3 v0[4] = {vtemp.pack(dim_c<3>,"vn", inds[0]),
                // vtemp.pack(dim_c<3>,"vn", inds[1]),
                // vtemp.pack(dim_c<3>,"vn", inds[2]),
                // vtemp.pack(dim_c<3>,"vn", inds[3])}; 

                // auto inertia = (T)1.0;
                // if(eles.hasProperty("inertia"))
                //     inertia = eles("inertia",ei);

                // auto vel = COLLISION_UTILS::flatten(v0); 
                // auto m = eles("m",ei)/(T)4.0;
                // auto C = kd_beta * H + kd_alpha * inertia * m * zs::vec<T,12,12>::identity();
                // auto rdamping = C * vel;  

                gh_buffer.tuple(dim_c<12>,"grad",ei + offset) = gh_buffer.pack(dim_c<12>,"grad",ei + offset) + vf/* - rdamping*/; 
                // gh_buffer.tuple(dim_c<12>,"grad",ei + offset) = gh_buffer.pack(dim_c<12>,"grad",ei + offset) - rdamping; 
                // H += kd_beta*H/dt;

                gh_buffer.template tuple<12*12>("H",ei + offset) = gh_buffer.template pack<12,12>("H",ei + offset) + H/* + C/dt*/;
            });
        // Bone Driven Potential Energy
            // T lambda = model.lam;
            // T mu = model.mu;

            auto nmEmbedVerts = b_verts.size();

            // TILEVEC_OPS::fill_range<4>(cudaPol,gh_buffer,"inds",zs::vec<int,4>::uniform(-1).reinterpret_bits(float_c),eles.size() + offset,b_verts.size());
            // TILEVEC_OPS::fill_range<3>(cudaPol,gh_buffer,"grad",zs::vec<T,3>::zeros(),eles.size() + offset,b_verts.size());
            // TILEVEC_OPS::fill_range<144>(cudaPol,gh_buffer,"H",zs::vec<T,144>::zeros(),eles.size() + offset,b_verts.size());

            // we should neglect the inverted element
            // std::cout << "nmEmbedVerts : " << nmEmbedVerts << std::endl;
            // std::cout << "bcwsize :  " << b_bcws.size() << std::endl;
            // return;
            cudaPol(zs::range(nmEmbedVerts), [
                    gh_buffer = proxy<space>({},gh_buffer),model = model,
                    bcws = proxy<space>({},b_bcws),b_verts = proxy<space>({},b_verts),vtemp = proxy<space>({},vtemp),etemp = proxy<space>({},etemp),
                    eles = proxy<space>({},eles),bone_driven_weight = bone_driven_weight,offset = offset] ZS_LAMBDA(int vi) mutable {
                        auto ei = reinterpret_bits<int>(bcws("inds",vi));
 
                        if(ei < 0){

                            return;
                        }
                        // if(ei >= etemp.size()){
                        //     printf("ei too big for etemp\n");
                        //     return;
                        // }
                        // auto is_inverted = reinterpret_bits<int>(etemp("is_inverted",ei));
                        // if(is_inverted){
                        //     if(vi == 0)
                        //         printf("inverted tet\n");
                        //     return;
                        // }

                        // auto FatID = eles("FatID",ei);
                        // if(FatID > 0)
                        //     return;

                        auto lambda = model.lam;
                        auto mu = model.mu;
                        // if(eles.hasProperty("mu") && eles.hasProperty("lam")) {
                        //     mu = eles("mu",ei);
                        //     lambda = eles("lam",ei);
                        // }

                        auto inds = eles.pack(dim_c<4>,"inds",ei).reinterpret_bits(int_c);
                        // gh_buffer.tuple(dim_c<4>,"inds",vi + offset + eles.size()) = eles.pack(dim_c<4>,"inds",ei);
                        auto w = bcws.pack(dim_c<4>,"w",vi);
                        // if(w[0] < 1e-4 || w[1] < 1e-4 || w[2] < 1e-4 || w[3] < 1e-4){
                        //     // if(vi == 0)
                        //     //     printf("boundary tet\n");
                        //     return;
                        // }
                        auto tpos = vec3::zeros();
                        for(int i = 0;i != 4;++i)
                            tpos += w[i] * vtemp.pack(dim_c<3>,"xn",inds[i]);
                        auto pdiff = tpos - b_verts.pack<3>("x",vi);
                        // auto pdiff = tpos - b_verts[vi];

                        T stiffness = (2.0066 * mu + 1.0122 * lambda) * b_verts("strength",vi);

                        // zs::vec<T,12> elm_grad{};
                        // auto elm_H = zs::vec<T,12,12>::zeros();

                        // if(vi == 0) {
                        //     printf("stiff : %f dw : %f strength : %f cnorm : %f vol : %f bdw : %f\n",
                        //         (float)stiffness,
                        //         (float)bone_driven_weight,
                        //         (float)bcws("strength",vi),
                        //         (float)bcws("cnorm",vi),
                        //         (float)eles("vol",ei),
                        //         (float)eles("bdw",ei));
                        // }

                        auto alpha = stiffness * bone_driven_weight * bcws("strength",vi) * bcws("cnorm",vi) * eles("vol",ei) * eles("bdw",ei);

                        for(size_t i = 0;i != 4;++i){
                            auto tmp = -pdiff * alpha * w[i]; 
                            // if(vi == 0 && i == 0) {
                                // printf("check: %f %f %f\n",(float)tmp[0],(float)tmp[1],(float)tmp[2]);
                            // }
                            for(size_t d = 0;d != 3;++d){
                                atomic_add(exec_cuda,&gh_buffer("grad",i*3 + d,ei),tmp[d]);
                                // elm_grad[i*3 + d] = tmp[d];
                                // atomic_add(exec_cuda,&gh_buffer("grad",i * 3 + d,ei),tmp[d]);
                            }
                        }
                        for(int i = 0;i != 4;++i)
                            for(int j = 0;j != 4;++j){
                                T beta = alpha * w[i] * w[j];
                                if(isnan(beta))
                                    printf("nan H detected at driver : %d\n",vi);
                                for(int d = 0;d != 3;++d){
                                    atomic_add(exec_cuda,&gh_buffer("H",(i*3 + d)*12 + j*3 + d,ei),beta);
                                }
                            }
                        
                        // for(int i = 0;i != 12;++i){
                            // atomic_add(exec_cuda,&gh_buffer("grad",i,ei),elm_grad[i]);
                            // for(int j = 0;j != 12;++j)
                            //     atomic_add(exec_cuda,&gh_buffer("H",i*12 + j,ei),elm_H(i,j));
                        // }
                        // gh_buffer.tuple(dim_c<12>,"grad",vi + eles.size() + offset) = elm_grad;
                        // gh_buffer.tuple(dim_c<12*12>,"H",vi + eles.size() + offset) = elm_H;
            });

            // cudaPol(zs::range(eles.size()), [gh_buffer = proxy<space>({},gh_buffer)] ZS_LAMBDA (int ei) mutable {
            //     auto H = gh_buffer.template pack<12,12>("H",ei);
            //     make_pd(H);
            //     gh_buffer.template tuple<12*12>("H",ei) = H;
            // });

        }

        template <typename ElasticModel>
        void computeElasticBonesEnergy(zs::CudaExecutionPolicy& cudaPol,
                            const ElasticModel& model,    
                            const dtiles_t& vtemp,
                            const dtiles_t& etemp,
                            T& res) {
            using namespace zs;
            constexpr auto space = execspace_e::cuda;

            Vector<T> psi{vtemp.get_allocator(), 1};
            psi.setVal((T)0);
            cudaPol(zs::range(eles.size()), [
                            verts = proxy<space>({},verts),
                            vtemp = proxy<space>({}, vtemp),
                            etemp = proxy<space>({}, etemp),
                            psi = proxy<space>(psi),
                            eles = proxy<space>({}, eles),
                            model = model] ZS_LAMBDA (int ei) mutable {
                auto DmInv = eles.pack(dim_c<3,3>,"IB",ei);
                auto dFdX = dFdXMatrix(DmInv);
                auto inds = eles.pack(dim_c<4>,"inds",ei).reinterpret_bits(int_c);
                vec3 x1[4] = {vtemp.pack(dim_c<3>,"xn", inds[0]),
                                vtemp.pack(dim_c<3>,"xn", inds[1]),
                                vtemp.pack(dim_c<3>,"xn", inds[2]),
                                vtemp.pack(dim_c<3>,"xn", inds[3])};   
                mat3 FAct{};
                {
                    auto x1x0 = x1[1] - x1[0];
                    auto x2x0 = x1[2] - x1[0];
                    auto x3x0 = x1[3] - x1[0];
                    auto Ds = mat3{x1x0[0], x2x0[0], x3x0[0], x1x0[1], x2x0[1],
                                    x3x0[1], x1x0[2], x2x0[2], x3x0[2]};
                    FAct = Ds * DmInv;
                    FAct = FAct * etemp.template pack<3,3>("ActInv",ei);
                } 
                auto inversion_strength = (T)1.0;
                // for(int i = 0;i != 4;++i)
                //     if(vtemp("is_inverted",inds[i]) < (T)0.5)
                //         inversion_strength = (T)1.0;
                auto vole = eles("vol", ei);
                auto epsi = vole * model.psi(FAct) * inversion_strength;                

                atomic_add(exec_cuda,&psi[0],epsi);
            });

            auto nmEmbedVerts = b_verts.size();
            cudaPol(zs::range(nmEmbedVerts), [
                    model = model,psi = proxy<space>(psi),
                    bcws = proxy<space>({},b_bcws),b_verts = proxy<space>({},b_verts),vtemp = proxy<space>({},vtemp),etemp = proxy<space>({},etemp),
                    eles = proxy<space>({},eles),bone_driven_weight = bone_driven_weight] ZS_LAMBDA(int vi) mutable {
                        auto ei = reinterpret_bits<int>(bcws("inds",vi));
 
                        if(ei < 0){
                            return;
                        }

                        auto lambda = model.lam;
                        auto mu = model.mu;

                        auto inds = eles.pack(dim_c<4>,"inds",ei).reinterpret_bits(int_c);
                        // gh_buffer.tuple(dim_c<4>,"inds",vi + offset + eles.size()) = eles.pack(dim_c<4>,"inds",ei);
                        auto w = bcws.pack(dim_c<4>,"w",vi);

                        auto tpos = vec3::zeros();
                        for(int i = 0;i != 4;++i)
                            tpos += w[i] * vtemp.pack(dim_c<3>,"xn",inds[i]);
                        auto pdiff = tpos - b_verts.pack<3>("x",vi);
                        // auto pdiff = tpos - b_verts[vi];

                        T stiffness = (2.0066 * mu + 1.0122 * lambda) * b_verts("strength",vi);

                        auto alpha = stiffness * bone_driven_weight * bcws("strength",vi) * bcws("cnorm",vi) * eles("vol",ei) * eles("bdw",ei);
                        T bpsi = (T)0.5 * pdiff.l2NormSqr() * alpha; 

                        atomic_add(exec_cuda,&psi[0],bpsi);

            });            


            res = psi.getVal();
        }

        FEMDynamicSteppingSystem(const tiles_t &verts, const tiles_t &eles,
                const tiles_t& points,const tiles_t& lines,const tiles_t& tris,
                T in_collisionEps,T out_collisionEps,
                const tiles_t &b_bcws, const tiles_t& b_verts,T bone_driven_weight,
                const vec3& volf,const T& _dt,const T& collisionStiffness,
                const T& kine_in_collisionEps,const T& kine_out_collisionEps,
                const T& kineCollisionStiffness,const T& aniso_strength,const T& binderStiffness,const T& plane_constraint_stiffness)
            : verts{verts}, eles{eles},points{points}, lines{lines}, tris{tris},
                    in_collisionEps{in_collisionEps},out_collisionEps{out_collisionEps},
                    b_bcws{b_bcws}, b_verts{b_verts}, bone_driven_weight{bone_driven_weight},
                    volf{volf},binderStiffness{binderStiffness},plane_constraint_stiffness{plane_constraint_stiffness},
                    kine_in_collisionEps{kine_in_collisionEps},kine_out_collisionEps{kine_out_collisionEps},
                    kineCollisionStiffness{kineCollisionStiffness},aniso_strength{aniso_strength},
                    dt{_dt}, dt2{_dt * _dt},collisionStiffness{collisionStiffness},use_edge_edge_collision{true}, use_vertex_facet_collision{true} {}

        const tiles_t &verts;
        const tiles_t &eles;
        const tiles_t &points;
        const tiles_t &lines;
        const tiles_t &tris;
        const tiles_t &b_bcws;  // the barycentric interpolation of embeded bones 
        const tiles_t &b_verts; // the position of embeded bones

        T bone_driven_weight;
        vec3 volf;
        T dt;
        T dt2;
        T in_collisionEps;
        T out_collisionEps;

        T collisionStiffness;

        bool bvh_initialized;
        bool use_edge_edge_collision;
        bool use_vertex_facet_collision;

        T kine_in_collisionEps;
        T kine_out_collisionEps;
        T kineCollisionStiffness;

        T aniso_strength;


        T binderStiffness;
        // int default_muscle_id;
        // zs::vec<T,3> default_muscle_dir;
        // T default_act;

        // T inset;
        // T outset;

        T plane_constraint_stiffness;
    };




    void apply() override {
        using namespace zs;
        auto zsparticles = get_input<ZenoParticles>("ZSParticles");
        auto gravity = zeno::vec<3,T>(0);
        if(has_input("gravity"))
            gravity = get_input2<zeno::vec<3,T>>("gravity");
        // T armijo = (T)1e-4;
        // T wolfe = (T)0.9;
        // T cg_res = (T)0.01;
        // T cg_res = (T)0.0001;
        // T cg_res = get_param<float>("cg_res");
        T cg_res = get_input2<float>("cg_res");
        T btl_res = (T)0.1;
        auto models = zsparticles->getModel();
        auto& verts = zsparticles->getParticles();
        auto& eles = zsparticles->getQuadraturePoints();

        // zs::Vector<vec3>(MAX_VERTS)
        // TileVec("pos","tag","deleted","")

        if(eles.getPropertySize("inds") != 4)
            throw std::runtime_error("the input zsparticles is not a tetrahedra mesh");
        if(!zsparticles->hasAuxData(ZenoParticles::s_surfTriTag))
            throw std::runtime_error("the input zsparticles has no surface tris");
        if(!zsparticles->hasAuxData(ZenoParticles::s_surfEdgeTag))
            throw std::runtime_error("the input zsparticles has no surface lines");
        if(!zsparticles->hasAuxData(ZenoParticles::s_surfVertTag)) 
            throw std::runtime_error("the input zsparticles has no surface points");
        if(!zsparticles->hasAuxData(ZenoParticles::s_surfHalfEdgeTag))
            throw std::runtime_error("the input zsparticles has no half edge structures");

        auto& tris  = (*zsparticles)[ZenoParticles::s_surfTriTag];
        auto& lines = (*zsparticles)[ZenoParticles::s_surfEdgeTag];
        auto& points = (*zsparticles)[ZenoParticles::s_surfVertTag];
        const auto& halfedges = (*zsparticles)[ZenoParticles::s_surfHalfEdgeTag];

        auto muscle_id_tag = get_input2<std::string>("muscle_id_tag");

        // auto bone_driven_weight = (T)0.02;

        auto newton_res = get_input2<float>("newton_res");

        auto dt = get_input2<float>("dt");

        auto volf = vec3::from_array(gravity * models.density);

        std::vector<zeno::vec2f> act_;    
        std::size_t nm_acts = 0;

        if(has_input("Acts")) {
            act_ = get_input<zeno::ListObject>("Acts")->getLiterial<zeno::vec2f>();
            nm_acts = act_.size();
        }

        std::cout << "nmActs:" << nm_acts << std::endl;

        constexpr auto host_space = zs::execspace_e::openmp;
        auto ompExec = zs::omp_exec();
        auto act_buffer = dtiles_t{{{"act",2}},nm_acts,zs::memsrc_e::host};
        ompExec(zs::range(act_buffer.size()),
            [act_buffer = proxy<host_space>({},act_buffer),act_] (int i) mutable {
                act_buffer.tuple(dim_c<2>,"act",i) = vec2(act_[i][0],act_[i][1]);
        });

        act_buffer = act_buffer.clone({zs::memsrc_e::device, 0});

        auto driven_tag = get_input2<std::string>("driven_tag");
        auto bone_driven_weight = get_input2<float>("driven_weight");


        constexpr auto space = execspace_e::cuda;
        auto cudaPol = cuda_exec();

        auto bbw = typename ZenoParticles::particles_t({
            {"X",3},
            {"inds",1},
            {"w",4},
            {"strength",1},
            {"cnorm",1}},0,zs::memsrc_e::device,0);

        auto bverts = typename ZenoParticles::particles_t({
            {"x",3},
            {"strength",1}},0,zs::memsrc_e::device,0);
        if(has_input<ZenoParticles>("driven_boudary") && zsparticles->hasAuxData(driven_tag)){
            auto zsbones = get_input<ZenoParticles>("driven_boudary");
            const auto& zsbones_verts = zsbones->getParticles();
            bverts.resize(zsbones_verts.size());


            TILEVEC_OPS::copy(cudaPol,zsbones_verts,"x",bverts,"x");
            if(zsbones_verts.hasProperty("strength"))
                TILEVEC_OPS::copy(cudaPol,zsbones_verts,"strength",bverts,"strength");
            else   
                TILEVEC_OPS::fill(cudaPol,bverts,"strength",(T)1.0);

            const auto& inbbw = (*zsparticles)[driven_tag];
            bbw.resize(inbbw.size());
            TILEVEC_OPS::copy(cudaPol,inbbw,"X",bbw,"X");
            TILEVEC_OPS::copy(cudaPol,inbbw,"inds",bbw,"inds");
            TILEVEC_OPS::copy(cudaPol,inbbw,"w",bbw,"w");
            TILEVEC_OPS::copy(cudaPol,inbbw,"strength",bbw,"strength");
            TILEVEC_OPS::copy(cudaPol,inbbw,"cnorm",bbw,"cnorm");

            // if(zsbones_verts.has_attr<float>("drivenStrength"))
            //     ompExec(zs::range(zsbones_verts.size()),
            //         [bverts = proxy<host_space>(bverts),&zsbones_verts] (int i) mutable {
            //             auto v = zsbones_verts[i];
            //             bverts[i] = zs::vec<T,3>{v[0],v[1],v[2]};
            //     });

        }
        // bverts = bverts.clone({zs::memsrc_e::device,0});
        // std::cout << "bverts.size() = " << bverts.size() << std::endl;

        auto kverts = typename ZenoParticles::particles_t({
                {"x",3},
                {"xp",3},
                {"b_fail",1},
                {"binderStiffness",1},
                {"nrm",3},
                {"area",1}},0,zs::memsrc_e::device,0);
        auto ktris = typename ZenoParticles::particles_t({
                {"inds",3},
                {"nrm",3}},0,zs::memsrc_e::device,0);


        dtiles_t surf_tris_buffer{tris.get_allocator(),{
            {"inds",3},
            {"nrm",3},
            {"he_inds",1}
        },tris.size()};

        dtiles_t surf_verts_buffer{points.get_allocator(),{
            {"inds",1},
            {"xn",3}
        },points.size()};
        TILEVEC_OPS::copy(cudaPol,points,"inds",surf_verts_buffer,"inds");
        TILEVEC_OPS::copy(cudaPol,tris,"inds",surf_tris_buffer,"inds");
        TILEVEC_OPS::copy(cudaPol,tris,"he_inds",surf_tris_buffer,"he_inds");
        reorder_topology(cudaPol,points,surf_tris_buffer);
        // zs::Vector<int> nodal_colors{surf_verts_buffer.get_allocator(),surf_verts_buffer.size()};
        dtiles_t gia_res{points.get_allocator(),{
            {"ring_mask",1},
            {"type_mask",1},
            {"color_mask",1}
        },points.size()};
        // zs::Vector<zs::vec<int,2>> instBuffer{surf_verts_buffer.get_allocator(),surf_verts_buffer.size() * 8};
        dtiles_t inst_buffer_info{tris.get_allocator(),{
            {"pair",2},
            {"type",1},
            {"its_edge_mark",6},
            {"int_points",6}
        },tris.size() * 2};


        if(has_input<ZenoParticles>("kinematic_boundary")){
            auto kinematic_boundary = get_input<ZenoParticles>("kinematic_boundary");
            // if (kinematic_boundary.empty())

            // const auto& prim_kverts = kinematic_boundary.verts;
            // auto& prim_kverts_area = kinematic_boundary.attr<float>("area");
            auto& kb_verts = kinematic_boundary->getParticles();
            auto& kb_tris = kinematic_boundary->getQuadraturePoints();

            // auto& kb_tris = kinematic_boundary->getQuadraturePoints();
            // if(kb_tris.getPropertySize("inds") != 3){
            //     fmt::print(fg(fmt::color::red),"the kinematic boundary is not a surface triangulate mesh\n");
            //     throw std::runtime_error("the kinematic boundary is not a surface triangulate mesh");
            // }
            // if(!kb_tris.hasProperty("area")){
            //     fmt::print(fg(fmt::color::red),"the kinematic boundary has no 'area' channel\n");
            //     throw std::runtime_error("the kinematic boundary has no 'area' channel");
            // }     
            kverts.resize(kb_verts.size());
            TILEVEC_OPS::copy<3>(cudaPol,kb_verts,"x",kverts,"x");
            TILEVEC_OPS::copy<3>(cudaPol,kb_verts,"x",kverts,"xp");
            TILEVEC_OPS::copy<3>(cudaPol,kb_verts,"nrm",kverts,"nrm");
            TILEVEC_OPS::fill(cudaPol,kverts,"area",(T)1.0);
            if(kb_verts.hasProperty("b_fail"))
                TILEVEC_OPS::copy(cudaPol,kb_verts,"b_fail",kverts,"b_fail");
            else 
                TILEVEC_OPS::fill(cudaPol,kverts,"b_fail",(T)0.0);
            if(kb_verts.hasProperty("binderStiffness"))
                TILEVEC_OPS::copy(cudaPol,kb_verts,"binderStiffness",kverts,"binderStiffness");
            else 
                TILEVEC_OPS::fill(cudaPol,kverts,"binderStiffness",(T)1.0);

            ktris.resize(kb_tris.size());
            TILEVEC_OPS::copy<3>(cudaPol,kb_tris,"nrm",ktris,"nrm");
            TILEVEC_OPS::copy<3>(cudaPol,kb_tris,"inds",ktris,"inds");            
        }
        // std::cout << "nm_kb_tris : " << kb_tris.size() << " nm_kb_verts : " << kb_verts.size() << std::endl;
        // cudaPol(zs::range(kb_tris.size()),
        //     [kb_verts = proxy<space>({},kb_verts),kb_tris = proxy<space>({},kb_tris),kverts = proxy<space>({},kverts)] ZS_LAMBDA(int ti) mutable {
        //         auto tri = kb_tris.pack(dim_c<3>,"inds",ti).reinterpret_bits(int_c);
        //         for(int i = 0;i != 3;++i)
        //             atomic_add(exec_cuda,&kverts("area",tri[i]),(T)kb_tris("area",ti)/(T)3.0);
        //         if(ti == 0)
        //             printf("tri[0] area : %f\n",(float)kb_tris("area",ti));
        // });

        // the temp buffer only store the data that will change every iterations or every frame
        dtiles_t vtemp{verts.get_allocator(),
                            {
                                {"grad", 3},
                                {"P", 9},
                                {"bou_tag",1},
                                {"dir", 3},
                                {"xn", 3},
                                {"xp",3},
                                {"vn",3},
                                {"vp",3},
                                {"is_inverted",1},
                                {"active",1},
                                {"k_active",1},
                                {"ring_mask",1},
                                {"color_mask",1},
                                {"type_mask",1},
                                {"grad",3},
                                {"H",9},
                                {"inds",1}
                            },verts.size()};
        

        // auto max_collision_pairs = tris.size() / 10; 
        dtiles_t etemp(eles.get_allocator(), {
                // {"H", 12 * 12},
                    {"ActInv",3*3},
                // {"muscle_ID",1},
                    {"is_inverted",1}
                }, eles.size()
        );

                // {{tags}, cnt, memsrc_e::um, 0}
        dtiles_t sttemp(tris.get_allocator(),
            {
                {"nrm",3},
                {"inds",3},
                {"grad",9},
                {"H",9 * 9}
            },tris.size()
        );
        TILEVEC_OPS::copy(cudaPol,tris,"inds",sttemp,"inds");
        dtiles_t setemp(lines.get_allocator(),
            {
                {"nrm",3}
            },lines.size()
        );

        // std::cout << "sttemp.size() << " << sttemp.size() << std::endl;
        // std::cout << "setemp.size() << " << setemp.size() << std::endl;

        bool turn_on_self_collision = get_input2<bool>("use_self_collision");

        // int fp_buffer_size = turn_on_self_collision ? points.size() * MAX_FP_COLLISION_PAIRS : 0;

        #ifdef USE_SPARSE_MATRIX

        dtiles_t fp_buffer(points.get_allocator(),{
            {"inds",4},
            {"grad",12},
            {"H",12 * 12},
        },points.size());

        #else

        int fp_buffer_size = points.size() * MAX_FP_COLLISION_PAIRS;
        // int fp_buffer_size = 0;

        dtiles_t fp_buffer(points.get_allocator(),{
            {"inds",4},
            {"area",1},
            {"inverted",1},
        },fp_buffer_size);

        #endif

        // static dtiles_t ee_buffer(lines.get_allocator(),{
        //     {"inds",4},
        //     {"area",1},
        //     {"inverted",1},
        //     {"abary",2},
        //     {"bbary",2},
        //     {"bary",4}
        // },lines.size());

        // int ee_buffer_size = ee_buffer.size();
        int ee_buffer_size = 0;


        int kc_buffer_size = kverts.size() * MAX_FP_COLLISION_PAIRS;
        // kc_buffer_size = 0;

        dtiles_t kc_buffer(points.get_allocator(),{
            {"inds",2},
            {"area",1},
            {"inverted",1},
        },kc_buffer_size);

        // int kc_buffer_size = kc_buffer.size();
        // int kc_buffer_size = 0;

// change
#ifdef USE_SPARSE_MATRIX
        dtiles_t gh_buffer(eles.get_allocator(),{
            {"inds",4},
            {"H",12*12},
            {"grad",12}
        },eles.size());
#else

        dtiles_t gh_buffer(eles.get_allocator(),{
            {"inds",4},
            {"H",12*12},
            {"grad",12}
        },eles.size() + fp_buffer.size());
#endif

        // dtiles_t tri_gh_buffer(tris.size(),{
        //     {"inds",3},
        //     {"H",9 * 9},
        //     {"grad",9}
        // },tris.size());


        // TILEVEC_OPS::fill<4>(cudaPol,etemp,"inds",zs::vec<int,4>::uniform(-1).template reinterpret_bits<T>())
        // TILEVEC_OPS::copy<4>(cudaPol,eles,"inds",etemp,"inds");
        TILEVEC_OPS::fill<9>(cudaPol,etemp,"ActInv",zs::vec<T,9>{1.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0});
        // TILEVEC_OPS::fill(cudaPol,vtemp,"inertia",(T)1.0);
        // if(verts.hasProperty("inertia"))
        //     TILEVEC_OPS::copy(cudaPol,verts,"inertia",vtemp,"inertia");
        if(verts.hasProperty("is_inverted"))
            TILEVEC_OPS::copy(cudaPol,verts,"is_inverted",vtemp,"is_inverted");
        else
            TILEVEC_OPS::fill(cudaPol,vtemp,"is_inverted",(T)0.0);
        cudaPol(zs::range(vtemp.size()),
            [vtemp = proxy<space>({},vtemp)] ZS_LAMBDA(int vi) mutable {
                vtemp("inds",vi) = reinterpret_bits<T>(vi);
        });
        // apply muscle activation

        if(!eles.hasProperty("Act"))
            eles.append_channels(cudaPol,{{"Act",1}});

        if(!eles.hasProperty(muscle_id_tag) || !eles.hasProperty("fiber"))
            fmt::print(fg(fmt::color::red),"the quadrature has no \"{}\" muscle_id_tag\n",muscle_id_tag);
        if(nm_acts == 0)
            fmt::print(fg(fmt::color::red),"no activation input\n");

        cudaPol(zs::range(eles.size()),
            [etemp = proxy<space>({},etemp),eles = proxy<space>({},eles),
                act_buffer = proxy<space>({},act_buffer),muscle_id_tag = SmallString(muscle_id_tag),nm_acts] ZS_LAMBDA(int ei) mutable {
                // auto act = eles.template pack<3>("act",ei);
                // auto fiber = etemp.template pack<3>("fiber",ei);

                vec3 act{1.0,1.0,1.0};
                vec3 fiber{};
                // float a = 1.0f;
                if(eles.hasProperty("fiber") && eles.hasProperty(muscle_id_tag) && nm_acts > 0 && (int)eles(muscle_id_tag,ei) >= 0 && fabs(eles.template pack<3>("fiber",ei).norm() - 1.0) < 0.001 && (int)eles(muscle_id_tag,ei) < act_buffer.size()){
                    fiber = eles.template pack<3>("fiber",ei);
                    auto ID = (int)eles(muscle_id_tag,ei);
                    auto a = 1. - act_buffer("act",0,ID);
                    auto b = 1. - act_buffer("act",1,ID);
                    // act = vec3{zs::sqrt(a),zs::sqrt(1./a),zs::sqrt(1./a)};
                    // auto aclamp = 
                    // act = vec3{a < 0.7 ? 0.7 : a,zs::sqrt(1./a),zs::sqrt(1./a)};
                    act = vec3{a,zs::sqrt(1./b),zs::sqrt(1./b)};
                    eles("Act",ei) = act_buffer("act",0,ID) + 1e-6;
                }else{
                    fiber = zs::vec<T,3>(1.0,0.0,0.0);
                    act = vec3{1,1,1};
                    eles("Act",ei) = (T)0.0;
                }
                if(fabs(fiber.norm() - 1.0) > 0.1) {
                    printf("invalid fiber[%d] detected : %f %f %f\n",(int)ei,
                        (float)fiber[0],(float)fiber[1],(float)fiber[2]);
                }

                vec3 dir[3];
                dir[0] = fiber;
                auto tmp = vec3{0.0,1.0,0.0};
                dir[1] = dir[0].cross(tmp);
                if(dir[1].length() < 1e-3) {
                    tmp = vec3{0.0,0.0,1.0};
                    dir[1] = dir[0].cross(tmp);
                }

                dir[1] = dir[1] / dir[1].length();
                dir[2] = dir[0].cross(dir[1]);
                dir[2] = dir[2] / dir[2].length();

                auto R = mat3{};
                for(int i = 0;i < 3;++i)
                    for(int j = 0;j < 3;++j)
                        R(i,j) = dir[j][i];

                auto Act = mat3::zeros();
                Act(0,0) = act[0];
                Act(1,1) = act[1];
                Act(2,2) = act[2];

                Act = R * Act * R.transpose();
                etemp.template tuple<9>("ActInv",ei) = zs::inverse(Act);
                // if(a < 1.0f) {
                //     auto ActInv = etemp.template pack<3,3>("ActInv",ei);
                //     printf("ActInv[%d] : \n%f %f %f\n%f %f %f\n%f %f %f\n",ei,
                //         (float)ActInv(0,0),(float)ActInv(0,1),(float)ActInv(0,2),
                //         (float)ActInv(1,0),(float)ActInv(1,1),(float)ActInv(1,2),
                //         (float)ActInv(2,0),(float)ActInv(2,1),(float)ActInv(2,2));
                // }
        });
        auto collisionStiffness = get_input2<float>("cstiffness");
        auto kineCollisionStiffness = get_input2<float>("kineCstiffness");


        // auto inset_ratio = get_input2<float>("collision_inset");
        // auto outset_ratio = get_input2<float>("collision_outset");    

        auto in_collisionEps = get_input2<float>("in_collisionEps");
        auto out_collisionEps = get_input2<float>("out_collisionEps");

        auto kine_in_collisionEps = get_input2<float>("kine_inCollisionEps");
        auto kine_out_collisionEps = get_input2<float>("kine_outCollisionEps");

        auto aniso_strength = get_input2<float>("aniso_strength");

        auto binderStiffness = get_input2<float>("binderStiffness");
        auto binderTag = get_param<std::string>("binderTag");
        auto binderThicknessTag = get_param<std::string>("binderThicknessTag");
        auto binderInversionTag = get_param<std::string>("binderInversionTag");

        auto planeConsPosTag = get_param<std::string>("planeConsPosTag");
        auto planeConsNrmTag = get_param<std::string>("planeConsNrmTag");
        auto planeConsIDTag = get_param<std::string>("planeConsIDTag");
        auto planeConsBaryTag = get_param<std::string>("planeConsBaryTag");

        auto planeConsStiffness = get_input2<float>("planeConsStiffness");

        FEMDynamicSteppingSystem A{
            verts,eles,
            points,lines,tris,
            (T)in_collisionEps,(T)out_collisionEps,
            bbw,bverts,bone_driven_weight,
            volf,dt,collisionStiffness,
            (T)kine_in_collisionEps,(T)kine_out_collisionEps,
            (T)kineCollisionStiffness,(T)aniso_strength,(T)binderStiffness,(T)planeConsStiffness};

        // std::cout << "set initial guess" << std::endl;
        // setup initial guess
        // if(verts.hasProperty("dt")) {
        //     std::cout << "verts has property 'dt'" << std::endl;
        // }

        TILEVEC_OPS::copy<3>(cudaPol,verts,"x",vtemp,"xp");
        TILEVEC_OPS::copy<3>(cudaPol,verts,"v",vtemp,"vp");
        if(verts.hasProperty("active"))
            TILEVEC_OPS::copy(cudaPol,verts,"active",vtemp,"active");
        else
            TILEVEC_OPS::fill(cudaPol,vtemp,"active",(T)1.0);

        if(verts.hasProperty("k_active"))
            TILEVEC_OPS::copy(cudaPol,verts,"k_active",vtemp,"k_active");
        else
            TILEVEC_OPS::fill(cudaPol,vtemp,"k_active",(T)1.0);

        // if there is no init_x as guess, then use the baraff witkin approach
        // if(verts.hasProperty("init_x"))
        //     TILEVEC_OPS::copy<3>(cudaPol,verts,"init_x",vtemp,"xn");   
        // else {
            // TILEVEC_OPS::add<3>(cudaPol,vtemp,"xp",1.0,"vp",dt,"xn");
        TILEVEC_OPS::copy(cudaPol,verts,"v",vtemp,"vn");  
        TILEVEC_OPS::copy(cudaPol,verts,"x",vtemp,"xn");
            // TILEVEC_OPS::add<3>(cudaPol,verts,"x",1.0,"vp",(T)0.0,"xn");  
        // }
        if(verts.hasProperty("bou_tag") && verts.getPropertySize("bou_tag") == 1)
            TILEVEC_OPS::copy(cudaPol,verts,"bou_tag",vtemp,"bou_tag");
        else
            TILEVEC_OPS::fill(cudaPol,vtemp,"bou_tag",(T)0.0);

        int max_newton_iterations = get_input2<int>("max_newton_iters");
        int nm_iters = 0;
        // make sure, at least one baraf simi-implicit step will be taken
        auto res0 = 1e10;

        auto kd_alpha = get_input2<float>("kd_alpha");
        auto kd_beta = get_input2<float>("kd_beta");
        auto kd_theta = get_input2<float>("kd_theta");

        auto max_cg_iters = get_param<int>("max_cg_iters");

        bool use_plane_constraint = get_input2<bool>("use_plane_constraint");
        bool use_binder_constraint = get_input2<bool>("use_binder_constraint");

        bool use_line_search = get_param<bool>("use_line_search");

        zs::CppTimer timer;

        #ifdef USE_SPARSE_MATRIX

        timer.tick();


        spmat_t spmat{};
        zs::Vector<int> is{verts.get_allocator(),verts.size()};
        zs::Vector<int> js{verts.get_allocator(),verts.size()};
        // init diagonal entries
        // cudaPol(zs::range(verts.size()),
        //         [is = proxy<space>(is),js = proxy<space>(js)] ZS_LAMBDA(int vi) mutable {
        //     is[vi] = js[vi] = vi;
        // });
        cudaPol(enumerate(is, js), [] ZS_LAMBDA(int no, int &i, int &j) mutable { i = j = no; });
        auto reserveStorage = [&is, &js](std::size_t n) {
            auto size = is.size();
            is.resize(size + n);
            js.resize(size + n);
            return size;
        };

        // init tet incidents' entries, off-diagonal
        auto tets_entry_offset = reserveStorage(eles.size() * 6);
        cudaPol(zs::range(eles.size()),[offset = tets_entry_offset,
                stride = eles.size(),
                is = proxy<space>(is),
                js = proxy<space>(js),
                eles = proxy<space>({},eles)] ZS_LAMBDA(int ei) mutable {
            auto inds = eles.pack(dim_c<4>,"inds",ei,int_c);
            for (int d = 1; d < 4; ++d)
                for (int k = 0; k < 4 - d; ++k)
                    if (inds[k] > inds[k + 1]) {
                        auto t = inds[k];
                        inds[k] = inds[k + 1];
                        inds[k + 1] = t;
                    }

            // <0, 1>, <0, 2>, <0, 3>, <1, 2>, <1, 3>, <2, 3>
            is[offset + ei] = inds[0];
            is[offset + stride + ei] = inds[0];
            is[offset + stride * 2 + ei] = inds[0];
            is[offset + stride * 3 + ei] = inds[1];
            is[offset + stride * 4 + ei] = inds[1];
            is[offset + stride * 5 + ei] = inds[2];

            js[offset + ei] = inds[1];
            js[offset + stride + ei] = inds[2];
            js[offset + stride * 2 + ei] = inds[3];
            js[offset + stride * 3 + ei] = inds[2];
            js[offset + stride * 4 + ei] = inds[3];
            js[offset + stride * 5 + ei] = inds[3];
        });

        spmat = spmat_t{verts.get_allocator(),(int)verts.size(),(int)verts.size()};
        spmat.build(cudaPol,(int)verts.size(),(int)verts.size(),zs::range(is),zs::range(js),zs::false_c);
        spmat.localOrdering(cudaPol, zs::false_c);
        spmat._vals.resize(spmat.nnz());
        spmat._vals.reset(0);   

        timer.tock("setup spmat");

        #endif

        auto cnorm = compute_average_edge_length(cudaPol,kverts,"x",ktris);

        auto use_sticky_condition = get_input2<bool>("use_sticky_condition");

        zs::Vector<zs::vec<int,4>> csPT{points.get_allocator(),points.size()};
        int nm_csPT = 0;

        while(nm_iters < max_newton_iterations) {
            // break;
            T e0 = (T)0;
            if(use_line_search){
                match([&](auto &elasticModel){
                    A.computeElasticBonesEnergy(cudaPol, elasticModel,vtemp,etemp,e0);
                },[](...) {
                    throw std::runtime_error("unsupported anisotropic elasticity model");
                })(models.getElasticModel());      
            }      

            TILEVEC_OPS::fill(cudaPol,gh_buffer,"grad",(T)0.0);
            TILEVEC_OPS::fill(cudaPol,gh_buffer,"H",(T)0.0);  
            TILEVEC_OPS::fill<4>(cudaPol,gh_buffer,"inds",zs::vec<int,4>::uniform(-1).reinterpret_bits(float_c)); 
            TILEVEC_OPS::fill(cudaPol,vtemp,"grad",(T)0.0);
            TILEVEC_OPS::fill(cudaPol,vtemp,"H",(T)0.0);
            TILEVEC_OPS::fill(cudaPol,sttemp,"grad",(T)0.0);
            TILEVEC_OPS::fill(cudaPol,sttemp,"H",(T)0.0);

            // if(!calculate_facet_normal(cudaPol,vtemp,"xn",tris,sttemp,"nrm")){
            //     throw std::runtime_error("fail updating facet normal");
            // }  

            A.findInversion(cudaPol,vtemp,etemp);  

            // match([&](auto &elasticModel,auto &anisoModel) -> std::enable_if_t<zs::is_same_v<RM_CVREF_T(anisoModel),zs::AnisotropicArap<float>>> {...},[](...) {
            //     A.computeGradientAndHessian(cudaPol, elasticModel,vtemp,etemp,gh_buffer,kd_alpha,kd_beta);
            // })(models.getElasticModel(),models.getAnisoElasticModel());
            timer.tick();
            match([&](auto &elasticModel,zs::AnisotropicArap<float> &anisoModel){
                A.computeGradientAndHessian(cudaPol, elasticModel,anisoModel,vtemp,etemp,gh_buffer,kd_alpha,kd_beta);
            },[](...) {
                throw std::runtime_error("unsupported anisotropic elasticity model");
            })(models.getElasticModel(),models.getAnisoElasticModel());
            // std::cout << "computePositionConstraintGradientAndHessian : " << kverts.size() << std::endl;
            // the binder constraint gradient and hessian
            if(use_binder_constraint) {
                std::cout << "apply binder constraint " << std::endl;
                A.computePositionConstraintGradientAndHessian(cudaPol,
                    vtemp,
                    binderTag,
                    binderThicknessTag,
                    binderInversionTag,
                    kverts,
                    gh_buffer);
            }else {
                std::cout << "apply no binder constraint" << std::endl;
            }
            if(verts.hasProperty(planeConsPosTag) && verts.hasProperty(planeConsNrmTag) && verts.hasProperty(planeConsIDTag) && verts.hasProperty(planeConsBaryTag) && use_plane_constraint){
                std::cout << "apply plane constraint" << std::endl;
                // A.computePlaneConstraintGradientAndHessian(cudaPol,
                
                A.computePlaneConstraintGradientAndHessian2(cudaPol,
                    vtemp,
                    sttemp,
                    kverts,
                    ktris,
                    planeConsBaryTag,
                    planeConsIDTag,
                    vtemp,
                    sttemp,cnorm,use_sticky_condition);
            }
            else{
                std::cout << "apply no plane constraint : " << 
                    verts.hasProperty(planeConsPosTag) << "\t" << 
                    verts.hasProperty(planeConsNrmTag) << "\t" << 
                    verts.hasProperty(planeConsIDTag) << "\t" << use_plane_constraint << std::endl;
            }
            if(!calculate_facet_normal(cudaPol,vtemp,"xn",tris,sttemp,"nrm")){
                throw std::runtime_error("fail updating facet normal");
            }  


            if(turn_on_self_collision) {
                // auto nm_insts = do_
                topological_sample(cudaPol,points,vtemp,"xn",surf_verts_buffer);
                auto nm_insts = do_global_self_intersection_analysis_on_surface_mesh_info(cudaPol,
                    surf_verts_buffer,"xn",surf_tris_buffer,halfedges,inst_buffer_info,gia_res);
                TILEVEC_OPS::fill(cudaPol,vtemp,"ring_mask",zs::reinterpret_bits<T>((int)0));
                TILEVEC_OPS::fill(cudaPol,vtemp,"color_mask",zs::reinterpret_bits<T>((int)0));
                TILEVEC_OPS::fill(cudaPol,vtemp,"type_mask",zs::reinterpret_bits<T>((int)0));
                cudaPol(zs::range(gia_res.size()),[
                    gia_res = proxy<space>({},gia_res),
                    vtemp = proxy<space>({},vtemp),
                    points = proxy<space>({},points)] ZS_LAMBDA(int pi) mutable {
                        auto vi = zs::reinterpret_bits<int>(points("inds",pi));
                        vtemp("ring_mask",vi) = gia_res("ring_mask",pi);
                        vtemp("color_mask",vi) = gia_res("color_mask",pi);
                        vtemp("type_mask",vi) = gia_res("type_mask",pi);
                });


                #ifdef USE_SPARSE_MATRIX
                    COLLISION_UTILS::do_facet_point_collsion_detection_and_compute_surface_normal(
                        cudaPol,
                        vtemp,"xn",
                        points,tris,sttemp,csPT,nm_csPT,(T)in_collisionEps,(T )out_collisionEps);
                    std::cout << "nm_csPT detected : " << nm_csPT << std::endl;

                    match([&](auto &elasticModel) {
                    COLLISION_UTILS::evaluate_fp_collision_grad_and_hessian(
                        cudaPol,
                        vtemp,"xn",
                        csPT,nm_csPT,
                        fp_buffer,
                        (T)in_collisionEps,(T)out_collisionEps,
                        (T)collisionStiffness,
                        elasticModel.mu,elasticModel.lam);
                    })(models.getElasticModel());

                    // auto cHn = TILEVEC_OPS::dot<12 * 12>(cudaPol,fp_buffer,"H","H");
                    // if(std::isnan(cHn)) {
                    //     std::cout << "nan cHn detected : " << std::endl;
                    //     throw std::runtime_error("nan cHn detected");
                    // }
                #else
                    match([&](auto &elasticModel) {
                        A.computeCollisionGradientAndHessian(cudaPol,elasticModel,
                            vtemp,
                            etemp,
                            sttemp,
                            setemp,
                            // ee_buffer,
                            fp_buffer,
                            kverts,
                            kc_buffer,
                            gh_buffer,kd_theta);
                        })(models.getElasticModel());
                #endif
            }

            timer.tock("eval hessian and gradient");
            timer.tick();
            // TILEVEC_OPS::fill(cudaPol,vtemp,"grad",(T)0.0); 
            TILEVEC_OPS::assemble(cudaPol,gh_buffer,"grad","inds",vtemp,"grad");
            TILEVEC_OPS::assemble(cudaPol,sttemp,"grad","inds",vtemp,"grad");

            #ifdef USE_SPARSE_MATRIX
            if(turn_on_self_collision)
                TILEVEC_OPS::assemble(cudaPol,fp_buffer,"grad","inds",vtemp,"grad");
            #endif
            TILEVEC_OPS::fill(cudaPol,vtemp,"P",(T)0.0);

            PCG::prepare_block_diagonal_preconditioner<4,3>(cudaPol,"H",gh_buffer,"P",vtemp,false,true);
            #ifdef USE_SPARSE_MATRIX
            if(turn_on_self_collision)
                PCG::prepare_block_diagonal_preconditioner<4,3>(cudaPol,"H",fp_buffer,"P",vtemp,false,true);
            #endif
            PCG::prepare_block_diagonal_preconditioner<3,3>(cudaPol,"H",sttemp,"P",vtemp,false,true);
            PCG::prepare_block_diagonal_preconditioner<1,3>(cudaPol,"H",vtemp,"P",vtemp,true,true);
            timer.tock("precondition and assemble setup");

            // eval sparse matrix
            #ifdef USE_SPARSE_MATRIX
            timer.tick();
            spmat._vals.reset(0);  

            cudaPol(zs::range(eles.size()),
                [gh_buffer = proxy<space>({},gh_buffer),
                        spmat = view<space>(spmat),
                        verts = proxy<space>({},verts)] ZS_LAMBDA(int ei) mutable {
                    auto inds = gh_buffer.pack(dim_c<4>,"inds",ei).reinterpret_bits(int_c);
                    auto H = gh_buffer.pack(dim_c<12,12>,"H",ei);
                    update_hessian(spmat,inds,H,true);
            });

             cudaPol(zs::range(sttemp.size()),
                [sttemp = proxy<space>({},sttemp),spmat = proxy<space>(spmat)] ZS_LAMBDA(int vi) mutable {
                    auto inds = sttemp.pack(dim_c<3>,"inds",vi,int_c);
                    auto H = sttemp.pack(dim_c<9,9>,"H",vi);
                    update_hessian(spmat,inds,H,true);
            });

            cudaPol(zs::range(vtemp.size()),
                [vtemp = proxy<space>({},vtemp),spmat = proxy<space>(spmat)] ZS_LAMBDA(int vi) mutable {
                    auto inds = vtemp.pack(dim_c<1>,"inds",vi,int_c);
                    auto H = vtemp.pack(dim_c<3,3>,"H",vi);
                    update_hessian(spmat,inds,H,true);
            });

            timer.tock("spmat evaluation");
            #endif
            // PCG::precondition<3>(cudaPol,vtemp,"P","grad","q");
            // T res = TILEVEC_OPS::inf_norm<3>(cudaPol, vtemp, "q");
            // if(res < newton_res){
            //     fmt::print(fg(fmt::color::cyan),"reach desire newton res {} : {}\n",newton_res,res);
            //     break;
            // }
            // auto nP = TILEVEC_OPS::inf_norm<9>(cudaPol,vtemp,"P");
            // std::cout << "nP : " << nP << std::endl;
            // PCG::prepare_block_diagonal_preconditioner<4,3>(cudaPol,"H",etemp,"P",vtemp);
            // if the grad is too small, return the result
            // Solve equation using PCG
            timer.tick();
            TILEVEC_OPS::fill(cudaPol,vtemp,"dir",(T)0.0);
            // std::cout << "solve using pcg" << std::endl;
            // auto Hn = TILEVEC_OPS::dot<12 * 12>(cudaPol,gh_buffer,"H","H");
            // std::cout << "Hn : " << Hn << std::endl;
            int nm_CG_iters = 0;
            #ifdef USE_SPARSE_MATRIX
                if(turn_on_self_collision)
                    nm_CG_iters = PCG::pcg_with_fixed_sol_solve<3>(cudaPol,vtemp,spmat,fp_buffer,"dir","bou_tag","grad","P","inds","H",(T)cg_res,max_cg_iters,100);
                else
                    nm_CG_iters = PCG::pcg_with_fixed_sol_solve<3>(cudaPol,vtemp,spmat,"dir","bou_tag","grad","P","inds","H",(T)cg_res,max_cg_iters,100);

            #else
                nm_CG_iters = PCG::pcg_with_fixed_sol_solve<3,4>(cudaPol,vtemp,gh_buffer,"dir","bou_tag","grad","P","inds","H",(T)cg_res,max_cg_iters,100);
            #endif
            timer.tock("CG SOLVER");
            fmt::print(fg(fmt::color::cyan),"nm_cg_iters : {}\n",nm_CG_iters);
            // T alpha = 1.;

            // auto nxn = TILEVEC_OPS::inf_norm<3>(cudaPol,vtemp,"xn");
            // auto ndir = TILEVEC_OPS::dot<3>(cudaPol,vtemp,"dir","dir");
            // auto nP = TILEVEC_OPS::dot<9>(cudaPol,vtemp,"P","P");

            // std::cout << "vtemp's xn : " << nxn << std::endl;
            // std::cout << "vtemp's dir : " << ndir << std::endl;
            // std::cout << "vtemp's P : " << nP << std::endl;

            if(use_line_search) {
                int search_idx = 0;     
                T alpha = (T)2.0;
                T beta = (T)0.5;
                T c1 = (T)0.0001;

                auto eg0 = (T)-1.0 * TILEVEC_OPS::dot<3>(cudaPol,vtemp,"grad","dir");
                if(eg0 > 0)
                    throw std::runtime_error("invalid searching direction");
                double armijo_condition;
                // int max_line_search = 5;


                do {
                    if(search_idx != 0){
                        TILEVEC_OPS::add<3>(cudaPol,vtemp,"xn",(T)1.0,"dir",-alpha,"xn"); 
                    }
                    alpha *= beta;
                    TILEVEC_OPS::add<3>(cudaPol,vtemp,"xn",(T)1.0,"dir",alpha,"xn"); 
                    T e1;
                    match([&](auto &elasticModel){
                        A.computeElasticBonesEnergy(cudaPol, elasticModel,vtemp,etemp,e1);
                    },[](...) {
                        throw std::runtime_error("unsupported anisotropic elasticity model");
                    })(models.getElasticModel());

                    ++search_idx;            

                    armijo_condition = double(e1) - double(e0) - double(c1) * double(alpha) * double(eg0);   
                }while(armijo_condition > 0.0);

            }else{
                cudaPol(zs::range(vtemp.size()), [vtemp = proxy<space>({}, vtemp),dt] __device__(int i) mutable {
                    vtemp.template tuple<3>("xn", i) =
                        vtemp.template pack<3>("xn", i) + vtemp.template pack<3>("dir", i);
                    vtemp.template tuple<3>("vn",i) = 
                        (vtemp.template pack<3>("xn",i) - vtemp.template pack<3>("xp",i))/dt; 
                });
            }


            // nxn = TILEVEC_OPS::inf_norm<3>(cudaPol,vtemp,"xn");
            // std::cout << "new vtemp's xn : " << nxn << std::endl;


            // res = TILEVEC_OPS::inf_norm<3>(cudaPol, vtemp, "dir");// this norm is independent of descriterization
            // std::cout << "res[" << nm_iters << "] : " << res << std::endl;
            // if(res < newton_res){
            //     fmt::print(fg(fmt::color::cyan),"reach desire newton res {} : {}\n",newton_res,res);
            //     break;
            // }
            nm_iters++;
        }


        cudaPol(zs::range(verts.size()),
                [vtemp = proxy<space>({}, vtemp), verts = proxy<space>({}, verts),dt = dt] __device__(int vi) mutable {
                    // auto newX = vtemp.pack(dim_c<3>,"xn", vi);
                    verts.tuple<3>("x", vi) = vtemp.pack(dim_c<3>,"xn", vi);
                    // if(verts.hasProperty("dt"))
                    //     dt = verts("dt",vi);
                    verts.tuple<3>("v",vi) = vtemp.pack<3>("vn",vi);
                });

        set_output("ZSParticles", zsparticles);
    }
};

ZENDEFNODE(FleshDynamicStepping, {{"ZSParticles","kinematic_boundary",
                                    "gravity","Acts",
                                    "driven_boudary",
                                    {"int","max_newton_iters","5"},
                                    {"float","cg_res","0.0001"},
                                    {"string","driven_tag","bone_bw"},
                                    {"float","driven_weight","0.02"},
                                    {"string","muscle_id_tag","ms_id_tag"},
                                    {"float","cstiffness","0.0"},
                                    {"float","in_collisionEps","0.01"},
                                    {"float","out_collisionEps","0.01"},
                                    {"float","kineCstiffness","1"},
                                    {"float","kine_inCollisionEps","0.01"},
                                    {"float","kine_outCollisionEps","0.02"},
                                    {"float","dt","0.5"},
                                    {"float","newton_res","0.001"},
                                    {"float","kd_alpha","0.01"},
                                    {"float","kd_beta","0.01"},
                                    {"float","kd_theta","0.01"},
                                    {"float","aniso_strength","1.0"},
                                    {"float","binderStiffness","1.0"},
                                    {"float","planeConsStiffness","0.01"},
                                    {"bool","use_plane_constraint","0"},
                                    {"bool","use_binder_constraint","0"},
                                    {"bool","use_self_collision","0"},
                                    {"bool","use_sticky_condition","0"}
                                    },
                                  {"ZSParticles"},
                                  {
                                    {"int","max_cg_iters","1000"}, 
                                    {"string","binderTag","binder_tag"},
                                    {"string","binderThicknessTag","binder_thickness"},
                                    {"string","binderInversionTag","binder_inversion"},
                                    {"string","planeConsPosTag","planeConsPosTag"},
                                    {"string","planeConsNrmTag","planeConsNrmTag"},
                                    {"string","planeConsIDTag","planeConsIDTag"},
                                    {"string","planeConsBaryTag","planeConsBaryTag"},
                                    {"bool","use_line_search","0"}
                                  },
                                  {"FEM"}});

// struct EvaluateElasticForce : zeno::INode {
//     using T = float;
//     using Ti = int;
//     using dtiles_t = zs::TileVector<T,32>;
//     using tiles_t = typename ZenoParticles::particles_t;
//     using vec2 = zs::vec<T,2>;
//     using vec3 = zs::vec<T, 3>;
//     using mat3 = zs::vec<T, 3, 3>;
//     using mat9 = zs::vec<T,9,9>;
//     using mat12 = zs::vec<T,12,12>;

//     using bvh_t = zs::LBvh<3,int,T>;
//     using bv_t = zs::AABBBox<3, T>;

//     using pair3_t = zs::vec<Ti,3>;
//     using pair4_t = zs::vec<Ti,4>;

//     virtual void apply() override {
//         using namespace zs;
//         auto zsparticles = get_input<ZenoParticles>("ZSParticles");
//         auto models = zsparticles->getModel();
//         auto& verts = zsparticles->getParticles();
//         auto& eles = zsparticles->getQuadraturePoints();

//         std::vector<zeno::vec2f> act_;    
//         std::size_t nm_acts = 0;

//         if(has_input("Acts")) {
//             act_ = get_input<zeno::ListObject>("Acts")->getLiterial<zeno::vec2f>();
//             nm_acts = act_.size();
//         }

//         std::cout << "nmActs:" << nm_acts << std::endl;

//         constexpr auto host_space = zs::execspace_e::openmp;
//         auto ompExec = zs::omp_exec();
//         auto act_buffer = dtiles_t{{{"act",2}},nm_acts,zs::memsrc_e::host};
//         ompExec(zs::range(act_buffer.size()),
//             [act_buffer = proxy<host_space>({},act_buffer),act_] (int i) mutable {
//                 act_buffer.tuple(dim_c<2>,"act",i) = vec2(act_[i][0],act_[i][1]);
//         });

//         act_buffer = act_buffer.clone({zs::memsrc_e::device, 0});
//         constexpr auto space = execspace_e::cuda;
//         auto cudaPol = cuda_exec();

//         auto forceTag = get_param<std::string>("forceTag");
//         if(!verts.hasProperty(forceTag)){
//             verts.append_channels(cudaPol,{{forceTag,3}});
//         }
//         TILEVEC_OPS::fill(cudaPol,verts,forceTag,(T)0.0);


//     }
// };

// struct VisualizeBoneDrivenForce : zeno::INode {

// };

// struct VisualizePlaneConstraintForce : zeno::INode {
//     using T = float;
//     using Ti = int;
//     using dtiles_t = zs::TileVector<T,32>;
//     using tiles_t = typename ZenoParticles::particles_t;
//     using vec2 = zs::vec<T,2>;
//     using vec3 = zs::vec<T, 3>;
//     using mat3 = zs::vec<T, 3, 3>;
//     using mat9 = zs::vec<T,9,9>;
//     using mat12 = zs::vec<T,12,12>;

//     using bvh_t = zs::LBvh<3,int,T>;
//     using bv_t = zs::AABBBox<3, T>;

//     using pair3_t = zs::vec<Ti,3>;
//     using pair4_t = zs::vec<Ti,4>;

//     virtual void apply() override {
//         using namespace zs;
//         auto zsparticles = get_input<ZenoParticles>("ZSParticles");
//         auto& verts = zsparticles->getParticles();
//         auto& tris  = (*zsparticles)[ZenoParticles::s_surfTriTag];     

//         auto kinematic_boundary = get_input<ZenoParticles>("kinematic_boundary");   
//         auto& kb_verts = kinematic_boundary->getParticles();
//         auto& kb_tris = kinematic_boundary->getQuadraturePoints();

//         auto planeConsPosTag = get_param<std::string>("planeConsPosTag");
//         auto planeConsNrmTag = get_param<std::string>("planeConsNrmTag");
//         auto planeConsIDTag = get_param<std::string>("planeConsIDTag");
//         auto planeConsBaryTag = get_param<std::string>("planeConsBaryTag");

//         auto planeConsStiffness = get_input2<float>("planeConsStiffness");        


//     }
// }

};