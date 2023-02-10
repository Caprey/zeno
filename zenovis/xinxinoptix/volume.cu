#include "volume.h"

#include "TraceStuff.h"

#include "DisneyBRDF.h"
#include "DisneyBSDF.h"

#include <nanovdb/NanoVDB.h>
#include <nanovdb/util/Ray.h>
#include <nanovdb/util/HDDA.h>
#include <nanovdb/util/SampleFromVoxels.h>

//PLACEHOLDER
static const float _vol_absorption = 1.0f;
static const float _vol_scattering = 1.0f;
//PLACEHOLDER

//COMMON_CODE

inline __device__ float _LERP_(float t, float s1, float s2)
{
    //return (1 - t) * s1 + t * s2;
    return fma(t, s2, fma(-t, s1, s1));
}

template <typename Acc>
inline __device__ float linearSampling(Acc& acc, nanovdb::Vec3f& point_indexd) {

        // using Sampler = nanovdb::SampleFromVoxels<nanovdb::FloatGrid::AccessorType, 1, false>;
        // float d = Sampler(acc)(test_point_indexd);

        //nanovdb::BaseStencil<typename DerivedType, int SIZE, typename GridT>
        //auto bs = nanovdb::BoxStencil<nanovdb::FloatGrid*>(grid);

    auto point_floor = nanovdb::RoundDown<nanovdb::Vec3f>(point_indexd); 
    auto point_a = nanovdb::Coord(point_floor[0], point_floor[1], point_floor[2]);

        auto value_000 = acc.getValue(point_a);
        auto value_100 = acc.getValue(point_a + nanovdb::Coord(1, 0, 0));

        auto value_010 = acc.getValue(point_a + nanovdb::Coord(0, 1, 0));
        auto value_110 = acc.getValue(point_a + nanovdb::Coord(1, 1, 0));

        auto value_001 = acc.getValue(point_a + nanovdb::Coord(0, 0, 1));
        auto value_101 = acc.getValue(point_a + nanovdb::Coord(1, 0, 1));

        auto value_011 = acc.getValue(point_a + nanovdb::Coord(0, 1, 1));
        auto value_111 = acc.getValue(point_a + nanovdb::Coord(1, 1, 1));

    auto delta = point_indexd - point_floor; 

        auto value_00 = _LERP_(delta[0], value_000, value_100);
        auto value_10 = _LERP_(delta[0], value_010, value_110);
        auto value_01 = _LERP_(delta[0], value_001, value_101);
        auto value_11 = _LERP_(delta[0], value_011, value_111);
        
        auto value_0 = _LERP_(delta[1], value_00, value_10);
        auto value_1 = _LERP_(delta[1], value_01, value_11);

    return _LERP_(delta[2], value_0, value_1);
}

struct VolumeIn {
    vec3 pos;
};

struct VolumeOut {
    float density;
    float temp;

    float anisotropy;

    vec3 emission;
};

// static __inlin__ __device__ VolMatOutput evalVolumeMat(
// _CUDA_TEXTURE_OBJECT_LIST_32,
// float4* uniforms,
// MatInput const &attrs
// )
// {
//     auto att_pos = attrs.pos;
//     auto att_clr = attrs.clr;
//     auto att_uv = attrs.uv;
//     auto att_nrm = attrs.nrm;
//     auto att_tang = attrs.tang;
    
//     //VOLUME_GENERATED_BEGIN
//     float mat_density = 0;
//     float mat_temperature = 0;
//     vec3  mat_emission = vec3(0,0,0);
//     vec3  mat_scatter = vec3(1,1,1);
//     vec3  mat_absorb = vec3(1,1,1);
//     //VOLUME_GENERATED_END

//     VolMatOutput v;
//     v.density = mat_density;
//     v.temperature = 
// }

#define USING_VDB 1

static __inline__ __device__ VolumeOut evalVolume(void* density_ptr, void* temp_ptr, float4* uniforms, VolumeIn const &attrs) {

    auto att_pos = attrs.pos;
    auto att_clr = vec3(0);
    auto att_uv = vec3(0);;
    auto att_nrm = vec3(0);
    auto att_tang = vec3(0);

    HitGroupData* sbt_data = (HitGroupData*)optixGetSbtDataPointer();
    auto zenotex = sbt_data->textures;

    //GENERATED_BEGIN_MARK
        auto BlackbodyTempOffset = 0.f;
        auto BlackbodyTempScale = 100.f;
        auto BlackbodyIntensity = 100.f;
        
        auto vol_anisotropy = 0.0f;
    //GENERATED_END_MARK

    auto att_max_density = sbt_data->density_max;
    auto att_max_temp = sbt_data->temperature_max; 

#if USING_VDB

    const auto* density_grid = reinterpret_cast<const nanovdb::FloatGrid*>(density_ptr);
    const auto& density_acc = density_grid->tree().getAccessor();

    auto pos_indexd = density_grid->worldToIndexF(reinterpret_cast<const nanovdb::Vec3f&>(att_pos));

    using Sampler = nanovdb::SampleFromVoxels<nanovdb::FloatGrid::AccessorType, 1, false>;
    float density = Sampler(density_acc)(pos_indexd);
    float temp = CUDART_INF_F;
    float3 emission {};

    if (temp_ptr != nullptr) {
        const auto* temp_grid = reinterpret_cast<const nanovdb::FloatGrid*>(temp_ptr);
        const auto& temp_acc = temp_grid->tree().getAccessor();
        temp = Sampler(temp_acc)(pos_indexd);

        float scale = temp / att_max_temp;
        scale = powf(scale, 4);

        float kelvin = temp * BlackbodyTempScale;

        emission = fakeBlackBody(kelvin); // Normalized color;
        emission *= scale * BlackbodyIntensity;
    } 

    VolumeOut output;
    output.density = density;
    output.temp = temp;
    output.anisotropy = vol_anisotropy;

    output.emission = emission;

    return output;
#else
    //USING 3D ARRAY
    //USING 3D Noise 
#endif
}

// ----------------------------------------------------------------------------
// Volume programs
// ----------------------------------------------------------------------------

inline __device__ void confine( const nanovdb::BBox<nanovdb::Coord> &bbox, nanovdb::Vec3f &iVec )
{
    // NanoVDB's voxels and tiles are formed from half-open intervals, i.e.
    // voxel[0, 0, 0] spans the set [0, 1) x [0, 1) x [0, 1). To find a point's voxel,
    // its coordinates are simply truncated to integer. Ray-box intersections yield
    // pairs of points that, because of numerical errors, fall randomly on either side
    // of the voxel boundaries.
    // This confine method, given a point and a (integer-based/Coord-based) bounding
    // box, moves points outside the bbox into it. That means coordinates at lower
    // boundaries are snapped to the integer boundary, and in case of the point being
    // close to an upper boundary, it is move one EPS below that bound and into the volume.

    // get the tighter box around active values
    auto iMin = nanovdb::Vec3f( bbox.min() );
    auto iMax = nanovdb::Vec3f( bbox.max() ) + nanovdb::Vec3f( 1.0f );

    // move the start and end points into the bbox
    float eps = 1e-7f;
    if( iVec[0] < iMin[0] ) iVec[0] = iMin[0];
    if( iVec[1] < iMin[1] ) iVec[1] = iMin[1];
    if( iVec[2] < iMin[2] ) iVec[2] = iMin[2];
    if( iVec[0] >= iMax[0] ) iVec[0] = iMax[0] - fmaxf( 1.0f, fabsf( iVec[0] ) ) * eps;
    if( iVec[1] >= iMax[1] ) iVec[1] = iMax[1] - fmaxf( 1.0f, fabsf( iVec[1] ) ) * eps;
    if( iVec[2] >= iMax[2] ) iVec[2] = iMax[2] - fmaxf( 1.0f, fabsf( iVec[2] ) ) * eps;
}

inline __hostdev__ void confine( const nanovdb::BBox<nanovdb::Coord> &bbox, nanovdb::Vec3f &iStart, nanovdb::Vec3f &iEnd )
{
    confine( bbox, iStart );
    confine( bbox, iEnd );
}

template<typename AccT>
inline __device__ float transmittanceHDDA(
    const nanovdb::Vec3f& start,
    const nanovdb::Vec3f& end,
    AccT& acc, const float opacity )
{

    // transmittance along a ray through the volume is computed by
    // taking the negative exponential of volume's density integrated
    // along the ray.
    float transmittance = 1.f;
    auto dir = end - start;
    auto len = dir.length();
    nanovdb::Ray<float> ray( start, dir / len, 0.0f, len );
    nanovdb::Coord ijk = nanovdb::RoundDown<nanovdb::Coord>( ray.start() ); // first hit of bbox

    // Use NanoVDB's HDDA line digitization for fast integration.
    // This algorithm (http://www.museth.org/Ken/Publications_files/Museth_SIG14.pdf)
    // can skip over sparse parts of the data structure.
    //
    nanovdb::HDDA<nanovdb::Ray<float> > hdda( ray, acc.getDim( ijk, ray ) );

    float t = 0.0f;
    float density = acc.getValue( ijk ) * opacity;
    while( hdda.step())
    {
        float dt = hdda.time() - t; // compute length of ray-segment intersecting current voxel/tile
        transmittance *= expf( -density * dt );
        t = hdda.time();
        ijk = hdda.voxel();

        density = acc.getValue( ijk ) * opacity;
        hdda.update( ray, acc.getDim( ijk, ray ) ); // if necessary adjust DDA step size
    }

    return transmittance;
}


extern "C" __global__ void __intersection__volume()
{
    RadiancePRD* prd = getPRD();
    //auto mask = optixGetRayVisibilityMask();
    {
        const auto* sbt_data = reinterpret_cast<const HitGroupData*>( optixGetSbtDataPointer() );
        const auto* grid = reinterpret_cast<const nanovdb::FloatGrid*>( sbt_data->density_grid );
        assert( grid );

        // compute intersection points with the volume's bounds in index (object) space.
        const float3 ray_orig = optixGetWorldRayOrigin(); //optixGetObjectRayOrigin();
        const float3 ray_dir  = optixGetWorldRayDirection(); //optixGetObjectRayDirection();

        auto bbox = grid->worldBBox(); //grid->indexBBox();
        float t0 = optixGetRayTmin();
        float t1 = optixGetRayTmax();

        auto iRay = nanovdb::Ray<float>( reinterpret_cast<const nanovdb::Vec3f&>( ray_orig ),
            reinterpret_cast<const nanovdb::Vec3f&>( ray_dir ), t0, t1 );
        
        //bbox.isInside(nanovdb::Coord(ray_orig.x, ray_orig.y, ray_orig.z));
    
        if( iRay.intersects( bbox, t0, t1 )) // t0 >= 0
        {
            // report the entry-point as hit-point
            //auto kind = optixGetHitKind();
            t0 = fmaxf(t0, optixGetRayTmin());
            prd->vol_t0 = t0; prd->vol_t1 = t1;
            prd->inside_volume = (t0 == 0);

            optixReportIntersection( t0, 0);
        }
    } 
}

extern "C" __global__ void __closesthit__radiance_volume()
{
    RadiancePRD* prd = getPRD();
    if(prd->test_distance==true)
        return;
    
    prd->countEmitted = false;
    prd->radiance = make_float3(0);

    prd->trace_tmin = 0;
    
    const HitGroupData* sbt_data = reinterpret_cast<HitGroupData*>( optixGetSbtDataPointer() );

    float3 ray_orig = optixGetWorldRayOrigin();
    float3 ray_dir  = optixGetWorldRayDirection();

    const float t0 = prd->vol_t0; // world space
    float t1 = prd->vol_t1; // world space

    RadiancePRD testPRD;
    testPRD.vol_t1 = CUDART_INF_F;
    testPRD.test_distance = true;
    traceRadianceMasked(
        params.handle,
        ray_orig,
        ray_dir,
        t0,
        t1,
        DefaultMatMask,
        &testPRD);

    bool surface_inside_volume = false;
    if(testPRD.vol_t1 < t1)
    {
        t1 = testPRD.vol_t1;
        surface_inside_volume = true;
    }

    const float t_max = max(0.f, t1 - t0); // world space
    float t_ele = 0;

    auto test_point = ray_orig; 
    float3 emitting = make_float3(0.0);
    float3 scattering = make_float3(1.0);
   
    float v_density = 0.0;

    float sigma_a = _vol_absorption;
    float sigma_s = _vol_scattering;

    float sigma_t = sigma_a + sigma_s;

#if (!_DELTA_TRACKING_) 

    t_ele -= log(1 - rnd(prd->seed)) / 0.1;
    float test_t = t0 + t_ele;
    if(test_t > t1)
    {
        test_t = t1;
        if(surface_inside_volume){
            test_t = t1 - 2e-5;
            prd->volumeHitSurface = true;
        }
    }

    test_point = ray_orig + test_t * ray_dir;
    auto test_point_indexd = grid->worldToIndexF(reinterpret_cast<const nanovdb::Vec3f&>(test_point));
    v_density = linearSampling(acc, test_point_indexd);
    
    vec3 new_dir = ray_dir;
    
    if(v_density > 0){
        const auto ray = nanovdb::Ray<float>( reinterpret_cast<const nanovdb::Vec3f&>( ray_orig ),
                                              reinterpret_cast<const nanovdb::Vec3f&>( ray_dir ) );
        auto start = grid->worldToIndexF( ray( t0 ) );
        auto end   = grid->worldToIndexF( ray( test_t ) );

        auto bbox = grid->indexBBox();
        confine( bbox, start, end );

        const float opacity = sbt_data->opacityHDDA;
        
        //scattering *= transmittanceHDDA( start, end, acc, 0.01 );;
        new_dir = DisneyBSDF::SampleScatterDirection(prd->seed);

        pbrt::HenyeyGreenstein hg {sbt_data->greenstein};
        float3 new_dir; float2 uu = {rnd(prd->seed), rnd(prd->seed)};
        auto pdf = hg.Sample_p(-ray_dir,             new_dir, uu);
        // //scattering *= pdf;

        scattering *= sbt_data->colorVDB;        
        ray_dir = (prd->volumeHitSurface )? ray_dir : float3(new_dir);
    }

#else

    while(true) {

        auto prob = rnd(prd->seed);
        t_ele -= log(1 - prob) / (sigma_t * sbt_data->density_max);

        if (t_ele >= t_max) {

            if (surface_inside_volume) { // Hit default material

                prd->_mask_ = DefaultMatMask;
                prd->trace_tmin = t0;

                test_point = ray_orig;

            } else { // Volume edge

                prd->_mask_ = EverythingMask;
                prd->trace_tmin = 1e-5;

                test_point = ray_orig + t1 * ray_dir;
            }

            ray_orig = test_point;
            prd->origin = ray_orig;
            prd->direction = ray_dir;
            
            return;
        } // over shoot, outside of volume

        test_point = ray_orig + (t0+t_ele) * ray_dir;

        VolumeIn vol_in; vol_in.pos = test_point;
        VolumeOut vol_out = evalVolume(sbt_data->density_grid, 
                                          sbt_data->temp_grid, 
                                          nullptr, vol_in);

        v_density = vol_out.density;

        if (rnd(prd->seed) < v_density/sbt_data->density_max) {

            pbrt::HenyeyGreenstein hg { vol_out.anisotropy }; 
            
            float3 new_dir; 
            float2 uu = {rnd(prd->seed), rnd(prd->seed)};
            auto pdf = hg.Sample_p(ray_dir, new_dir, uu);

            scattering = make_float3(sigma_s / sigma_t);
            // scattering *= sbt_data->colorVDB;
                float3 le = vol_out.emission;
                emitting = le * scattering;
            ray_dir = new_dir;
            break;

        } else { v_density = 0; } 
    }

#endif // _DELTA_TRACKING_

    prd->updateAttenuation(scattering);

    ray_orig = test_point;
    prd->origin = ray_orig;
    prd->direction = ray_dir;
    
    prd->emission = emitting;
    prd->radiance = make_float3(0.0f);
    float3 light_attenuation = make_float3(1.0f);
    float pl = rnd(prd->seed);
    float sum = 0.0f;
    for(int lidx=0;lidx<params.num_lights;lidx++)
    {
            ParallelogramLight light = params.lights[lidx];
            float3 light_pos = light.corner + light.v1 * 0.5 + light.v2 * 0.5;

            // Calculate properties of light sample (for area based pdf)
            float Ldist = length(light_pos - test_point);
            float3 L = normalize(light_pos - test_point);
            float nDl = 1.0f;//clamp(dot(N, L), 0.0f, 1.0f);
            float LnDl = clamp(-dot(light.normal, L), 0.000001f, 1.0f);
            float A = length(cross(params.lights[lidx].v1, params.lights[lidx].v2));
            sum += length(light.emission)  * nDl * LnDl * A / (M_PIf * Ldist * Ldist);
    }
    
    if(rnd(prd->seed)<=0.5f) {
        bool computed = false;
        float ppl = 0;
        for (int lidx = 0; lidx < params.num_lights && computed == false; lidx++) {
            ParallelogramLight light = params.lights[lidx];
            float2 z = sobolRnd2(prd->seed);
            const float z1 = z.x;
            const float z2 = z.y;
            float3 light_tpos = light.corner + light.v1 * 0.5 + light.v2 * 0.5;
            float3 light_pos = light.corner + light.v1 * z1 + light.v2 * z2;

            // Calculate properties of light sample (for area based pdf)
            float tLdist = length(light_tpos - test_point);
            float3 tL = normalize(light_tpos - test_point);
            float tnDl = 1.0f; //clamp(dot(N, tL), 0.0f, 1.0f);
            float tLnDl = clamp(-dot(light.normal, tL), 0.000001f, 1.0f);
            float tA = length(cross(params.lights[lidx].v1, params.lights[lidx].v2));
            ppl += length(light.emission) * tnDl * tLnDl * tA / (M_PIf * tLdist * tLdist) / sum;
            if (ppl > pl) {
                float Ldist = length(light_pos - test_point) + 1e-6;
                float3 L = normalize(light_pos - test_point);
                float nDl = 1.0f; //clamp(dot(N, L), 0.0f, 1.0f);
                float LnDl = clamp(-dot(light.normal, L), 0.0f, 1.0f);
                float A = length(cross(params.lights[lidx].v1, params.lights[lidx].v2));
                float weight = 0.0f;
                if (nDl > 0.0f && LnDl > 0.0f) {

                        weight = sum * nDl / tnDl * LnDl / tLnDl * (tLdist * tLdist) / (Ldist * Ldist) /
                                 (length(light.emission)+1e-6f);
                }
                prd->LP = test_point;
                prd->Ldir = L;
                prd->nonThinTransHit = 1;
                prd->Lweight = weight;

                float3 lbrdf = make_float3(1.0) ;

                prd->radiance = 2.0 * light.emission * lbrdf;
                computed = true;
            }
        }
    } else {
        RadiancePRD shadow_prd2;
        float3 lbrdf;
        vec3 env_dir;
        bool inside = false;

        vec3 sunLightDir = vec3(params.sunLightDirX, params.sunLightDirY, params.sunLightDirZ);
        auto sun_dir = BRDFBasics::halfPlaneSample(prd->seed, sunLightDir,
                                                   params.sunSoftness * 0.2); //perturb the sun to have some softness
        sun_dir = normalize(sun_dir);
        prd->LP = test_point;
        prd->Ldir = sun_dir;
        prd->nonThinTransHit = 1;
        prd->Lweight = 1.0;

        lbrdf = make_float3(1.0) ;
        prd->radiance = 2.0 * float3(envSky(sun_dir, sunLightDir, make_float3(0., 0., 1.),
                                       10, // be careful
                                       .45, 15., 1.030725 * 0.3, params.elapsedTime)) * lbrdf;
    }

    prd->CH = 1.0;
    prd->depth += v_density>0? 1:0;
    prd->radiance *= v_density>0? 1:0; 

    return;
}

extern "C" __global__ void __anyhit__occlusion_volume()
{
    const HitGroupData* sbt_data = ( HitGroupData* )optixGetSbtDataPointer();

    const auto* grid = reinterpret_cast<const nanovdb::FloatGrid*>( sbt_data->density_grid );
    const auto& acc = grid->tree().getAccessor();

    const float3 ray_orig = optixGetWorldRayOrigin();
    const float3 ray_dir  = optixGetWorldRayDirection();

    RadiancePRD* prd = getPRD();

    const float t0 = prd->vol_t0;
    const float t1 = prd->vol_t1;

    const float t_max = t1 - t0; // world space
          float t_ele = 0;

    auto test_point = ray_orig; 
    float3 transmittance = make_float3(1.0f);

    float sigma_a = _vol_absorption;
    float sigma_s = _vol_scattering;
    float sigma_t = sigma_a + sigma_s;

#if (!_DELTA_TRACKING_) 

    const auto ray = nanovdb::Ray<float>( reinterpret_cast<const nanovdb::Vec3f&>( ray_orig ),
                                              reinterpret_cast<const nanovdb::Vec3f&>( ray_dir ) );
    auto start = grid->worldToIndexF( ray( t0 ) );
    auto end   = grid->worldToIndexF( ray( t1 ) );

    auto bbox = grid->indexBBox();
    confine( bbox, start, end );

    const float opacity = sbt_data->opacityHDDA;
    float transHDDA = transmittanceHDDA( start, end, acc, sbt_data->opacityHDDA );
    if (transHDDA < 1.0) {
        transmittance *= transHDDA;
        transmittance *= sbt_data->colorVDB;
    }

#else
    int32_t level = 0;
    while(++level<16) {

        auto prob = rnd(prd->seed);
        t_ele -= log(1 - prob) / (sigma_t * sbt_data->density_max);

        test_point = ray_orig + (t0+t_ele) * ray_dir;

        if (t_ele >= t_max) {
            break;
        } // over shoot, outside of volume

        //ray_orig = test_point;

        auto test_point_indexd = grid->worldToIndexF(reinterpret_cast<const nanovdb::Vec3f&>(test_point));
        auto v_density = linearSampling(acc, test_point_indexd);

        transmittance *= 1 - max(0.0, v_density/sbt_data->density_max);

        // if (transmittance < 0.1) {
        //     float q = max(0.05, 1 - transmittance);
        //     if (rnd(prd->seed) < q) { transmittance = 0; }
        //     transmittance /= 1-q;
        // }
        if (v_density > 0) {
            transmittance *= sbt_data->colorVDB;
        }
    }
#endif

    prd->shadowAttanuation *= transmittance;
    optixIgnoreIntersection();
    //prd->origin = ray_orig;
    //prd->direction = ray_dir;
    return;
}