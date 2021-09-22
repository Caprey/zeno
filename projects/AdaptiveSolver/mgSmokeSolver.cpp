#include "mgSmokeSolver.h"
#include <openvdb/tools/Interpolation.h>
#include "tbb/blocked_range3d.h"
#include <openvdb/tools/LevelSetUtil.h>
#include <openvdb/tools/FastSweeping.h>
#include "tbb/atomic.h"
namespace zeno{
    void mgIterStuff::init(std::vector<openvdb::FloatGrid::Ptr> pressField)
    {
        for(int i=0;i<pressField.size();++i)
        {
            rhsGrid[i] = pressField[i]->deepCopy();
            resGrid[i] = pressField[i]->deepCopy();
            r2Grid[i] = pressField[i]->deepCopy();
            pGrid[i] = pressField[i]->deepCopy();
            ApGrid[i] = pressField[i]->deepCopy();
        }
    }
    void mgIterStuff::resize(int levelNum)
    {
        rhsGrid.resize(levelNum);
        resGrid.resize(levelNum);
        r2Grid.resize(levelNum); 
        pGrid.resize(levelNum);
        ApGrid.resize(levelNum);
        for(int i=0;i<levelNum;++i)
        {
            rhsGrid[i] = openvdb::FloatGrid::create(0);
            resGrid[i] = openvdb::FloatGrid::create(0);
            r2Grid[i] = openvdb::FloatGrid::create(0);
            pGrid[i] = openvdb::FloatGrid::create(0);
            ApGrid[i] = openvdb::FloatGrid::create(0);
        }
    }
    void mgSmokeData::resize(int levelNum)
    {
        tag = openvdb::Int32Grid::create(0);
        temperatureField.resize(levelNum);
        volumeField.resize(levelNum);
        pressField.resize(levelNum);
        iterBuffer.resize(levelNum);
        dx.resize(levelNum);
        for(int i=0;i<3;++i)
            velField[i].resize(levelNum);

        for(int i=0;i<levelNum;++i)
        {
            temperatureField[i] = openvdb::FloatGrid::create(0);
            volumeField[i] = openvdb::FloatGrid::create(0);
            pressField[i] = openvdb::FloatGrid::create(0);
            
            for(int xyz=0;xyz<3;++xyz)
                velField[xyz][i] = openvdb::FloatGrid::create(0);
        }

    }
    void mgSmokeData::initData(openvdb::FloatGrid::Ptr sdf, int levelNum, float inputdt)
    {
        dt = inputdt;
        resize(levelNum);
        printf("mgSmoke Data resize over!level Num is %d\n", levelNum);
        
        dx[0] = sdf->voxelSize()[0];
        for(int level = 1;level < levelNum;++level)
            dx[level] = dx[level-1]*2;

        auto tagtrans = openvdb::math::Transform::createLinearTransform(dx[levelNum-1]);
        tagtrans->postTranslate(openvdb::Vec3d{0.5,0.5,0.5}*dx[levelNum-1]);
        tag->setTransform(tagtrans);

        auto inputbbox = openvdb::CoordBBox();
        auto is_valid = sdf->tree().evalLeafBoundingBox(inputbbox);
        if (!is_valid) {
            return;
        }
        worldinputbbox = openvdb::BBoxd(sdf->indexToWorld(inputbbox.min()),
                                       sdf->indexToWorld(inputbbox.max()));
        worldinputbbox.min() += openvdb::Vec3d(-0.2);
        worldinputbbox.max() += openvdb::Vec3d(0.2);
        printf("generate world input box over!\n");

        openvdb::Vec3R loopbegin, loopend;
        tbb::blocked_range3d<int, int, int> pispace_range(0,0,0,0,0,0);

        for(int level = 0;level < levelNum;++level){
            auto transform = openvdb::math::Transform::createLinearTransform(dx[level]);
            transform->postTranslate(openvdb::Vec3d{0.5,0.5,0.5}*dx[level]);
            temperatureField[level]->setTransform(transform);
            volumeField[level]->setTransform(transform);
            
            openvdb::tools::resampleToMatch<openvdb::tools::QuadraticSampler>(*sdf, *volumeField[level]);
            openvdb::tools::sdfToFogVolume(*volumeField[level]);
            temperatureField[level] = volumeField[level]->deepCopy();

            loopbegin =
                openvdb::tools::local_util::floorVec3(volumeField[level]->worldToIndex(worldinputbbox.min()));
            loopend =
                openvdb::tools::local_util::ceilVec3(volumeField[level]->worldToIndex(worldinputbbox.max()));
            pispace_range = tbb::blocked_range3d<int, int, int>(
                loopbegin.x(), loopend.x(), loopbegin.y(), loopend.y(), loopbegin.z(),
                loopend.z());
            {
                auto sdf_axr{sdf->getConstAccessor()};
                auto tag_axr{tag->getAccessor()};
                ConstBoxSample sdfSample(sdf_axr, sdf->transform());
                auto temperature_axr{temperatureField[level]->getAccessor()};
                auto volume_axr{volumeField[level]->getAccessor()};
                for (int i = pispace_range.pages().begin(); i < pispace_range.pages().end(); i += 1) {
                for (int j = pispace_range.rows().begin(); j < pispace_range.rows().end(); j += 1) {
                    for (int k = pispace_range.cols().begin(); k < pispace_range.cols().end(); k += 1) {
                        openvdb::Coord coord(i,j,k);
                        if(!temperature_axr.isValueOn(coord))
                        {
                            temperature_axr.setValue(coord, 0);
                            volume_axr.setValue(coord, 0);
                        }
                    }
                }
                }
            }
            //printf("start to mark active point leaves on level %d, levelNum is %d\n", level, levelNum);
            //tbb::parallel_for(pispace_range, mark_active_point_leaves);
            //tbb::parallel_for(tbb::blocked_range<size_t>(0, leaves.size()), makeSubd);
            //printf("mark active point leaves on level %d, levelNum is %d end!!\n", level, levelNum);
            
            pressField[level]->setTree((std::make_shared<openvdb::FloatTree>(
                        temperatureField[level]->tree(), /*bgval*/ float(0),
                        openvdb::TopologyCopy())));
            for(int i=0;i<3;++i){
                auto velTrans = transform->copy();
                openvdb::Vec3d v(0);
                v[i] = -0.5 * double(dx[level]);
                velTrans->postTranslate(v);
                velField[i][level]->setTree((std::make_shared<openvdb::FloatTree>(
                            temperatureField[level]->tree(), /*bgval*/ float(0),
                            openvdb::TopologyCopy())));
                velField[i][level]->setTransform(velTrans);
                for (openvdb::FloatGrid::ValueOffIter iter = velField[i][level]->beginValueOff(); iter.test(); ++iter) {
                    iter.setValue(0);
                }
            }
            auto pressTrans = transform->copy();
            pressTrans->postTranslate(openvdb::Vec3d{ -0.5,-0.5,-0.5 }*double(dx[level]));
            pressField[level]->setTransform(pressTrans);

            for (openvdb::FloatGrid::ValueOffIter iter = volumeField[level]->beginValueOff(); iter.test(); ++iter) {
                iter.setValue(0);
            }
            for (openvdb::FloatGrid::ValueOffIter iter = temperatureField[level]->beginValueOff(); iter.test(); ++iter) {
                iter.setValue(0);
            }
            for (openvdb::FloatGrid::ValueOffIter iter = pressField[level]->beginValueOff(); iter.test(); ++iter) {
                iter.setValue(0);
            }
        }
        iterBuffer.init(pressField);
    };

    // temporally using semi-la advection
    void mgSmokeData::advection()
    {
        std::vector<openvdb::FloatTree::LeafNodeType *> leaves;
        for(int level = 0;level < volumeField.size();++level)
        {
            volumeField[level]->tree().getNodes(leaves);
            auto new_temField = temperatureField[level]->deepCopy();
            auto new_volField = volumeField[level]->deepCopy();
            // advect the vertex attribute, temperature and volume
            auto semiLangAdvection = [&](const tbb::blocked_range<size_t> &r) 
            {
                openvdb::tree::ValueAccessor<const openvdb::FloatTree, true> vel_axr[3]=
                    {velField[0][level]->getConstAccessor(),
                    velField[1][level]->getConstAccessor(),
                    velField[2][level]->getConstAccessor()};
                
                auto tem_axr = temperatureField[level]->getConstAccessor();
                auto vol_axr = volumeField[level]->getConstAccessor();
                auto new_tem_axr{new_temField->getAccessor()};
                auto new_vol_axr{new_volField->getAccessor()};
                ConstBoxSample velSampler[3] = {
                    ConstBoxSample(vel_axr[0], velField[0][level]->transform()),
                    ConstBoxSample(vel_axr[1], velField[1][level]->transform()),
                    ConstBoxSample(vel_axr[2], velField[2][level]->transform())
                };
                ConstBoxSample volSample(vol_axr, volumeField[level]->transform());
                ConstBoxSample temSample(tem_axr, temperatureField[level]->transform());
                // leaf iter
                for (auto liter = r.begin(); liter != r.end(); ++liter) {
                    auto &leaf = *leaves[liter];
                    for (auto offset = 0; offset < leaf.SIZE; ++offset) {
                        openvdb::Coord coord = leaf.offsetToGlobalCoord(offset);
                        if(!vol_axr.isValueOn(coord))
                            continue;
                        auto oldvol = vol_axr.getValue(coord);
                        auto wpos = temperatureField[level]->indexToWorld(coord);

                        openvdb::Vec3f vel, midvel;
                        for(int i=0;i<3;++i)
                        {
                            vel[i] = velSampler[i].wsSample(wpos);
                        }
                        //printf("vel is (%f,%f,%f)\n", vel[0], vel[1], vel[2]);
                        auto midwpos = wpos - vel * 0.5 * dt;
                        for(int i=0;i<3;++i)
                        {    
                            midvel[i]  = velSampler[i].wsSample(midwpos);
                        }
                        
                        auto pwpos = wpos - midvel * dt;
                        auto volume = volSample.wsSample(pwpos);
                        auto tem = temSample.wsSample(pwpos);
                        new_tem_axr.setValue(coord, tem);
                        new_vol_axr.setValue(coord, volume);
                    }
                }
            };
        
            tbb::parallel_for(tbb::blocked_range<size_t>(0, leaves.size()), semiLangAdvection);
            

            temperatureField[level]->clear();volumeField[level]->clear();
            temperatureField[level] = new_temField;
            volumeField[level] = new_volField;

            openvdb::FloatGrid::Ptr new_vel[3] = {velField[0][level]->deepCopy(),
                velField[1][level]->deepCopy(),velField[2][level]->deepCopy()};
            
            auto velAdvection = [&](const tbb::blocked_range<size_t> &r){
                openvdb::tree::ValueAccessor<const openvdb::FloatTree, true> 
                    vel_axr[3]=
                    {velField[0][level]->getConstAccessor(),velField[1][level]->getConstAccessor(),velField[2][level]->getConstAccessor()};
                openvdb::tree::ValueAccessor<openvdb::FloatTree, true> 
                    new_vel_axr[3]=
                    {new_vel[0]->getAccessor(),new_vel[1]->getAccessor(),new_vel[2]->getAccessor()};
                ConstBoxSample velSampler[3] = {
                    ConstBoxSample(vel_axr[0], velField[0][level]->transform()),
                    ConstBoxSample(vel_axr[1], velField[1][level]->transform()),
                    ConstBoxSample(vel_axr[2], velField[2][level]->transform())
                    };
                // leaf iter
                for (auto liter = r.begin(); liter != r.end(); ++liter) {
                    auto &leaf = *leaves[liter];
                    for (auto offset = 0; offset < leaf.SIZE; ++offset) {
                        openvdb::Coord coord = leaf.offsetToGlobalCoord(offset);
                        for(int i=0;i<3;++i)
                        {
                            if(!vel_axr[i].isValueOn(coord))
                                continue;
                            auto wpos = velField[i][level]->indexToWorld(coord);

                            openvdb::Vec3f vel, midvel;
                            for(int i=0;i<3;++i)
                            {
                                vel[i] = velSampler[i].wsSample(wpos);
                            }
                            
                            auto midwpos = wpos - vel * 0.5 * dt;
                            for(int i=0;i<3;++i)
                            {    
                                midvel[i]  = velSampler[i].wsSample(midwpos);
                            }
                            auto pwpos = wpos - midvel * dt;
                            auto pvel = velSampler[i].wsSample(pwpos);
                            new_vel_axr[i].setValue(coord, pvel);
                        }
                    }
                }

            };
            
            tbb::parallel_for(tbb::blocked_range<size_t>(0, leaves.size()), velAdvection);
            
            velField[0][level]->clear();velField[1][level]->clear();velField[2][level]->clear();
            velField[0][level] = new_vel[0];
            velField[1][level] = new_vel[1];
            velField[2][level] = new_vel[2];
            
            leaves.clear();
        }
    }

    void mgSmokeData::applyOuterforce(){
        float alpha =-0.1, beta = 0.2;
        int levelNum = dx.size();
        std::vector<openvdb::FloatTree::LeafNodeType *> leaves;
        for(int level = 0;level < levelNum; ++ level){
            auto applyOuterForce = [&](const tbb::blocked_range<size_t> &r) {
                openvdb::tree::ValueAccessor<openvdb::FloatTree, true> 
                    vel_axr[3]=
                    {velField[0][level]->getAccessor(),velField[1][level]->getAccessor(),velField[2][level]->getAccessor()};
                auto vol_axr = volumeField[level]->getConstAccessor();
                auto tem_axr = temperatureField[level]->getConstAccessor();
                ConstBoxSample volSample(vol_axr, volumeField[level]->transform());
                ConstBoxSample temSample(tem_axr, temperatureField[level]->transform());
                
                for (auto liter = r.begin(); liter != r.end(); ++liter) {
                    auto &leaf = *leaves[liter];
                    for (auto offset = 0; offset < leaf.SIZE; ++offset) {
                        openvdb::Coord coord = leaf.offsetToGlobalCoord(offset);
                        if(!vel_axr[1].isValueOn(coord))
                            continue;
                        auto wpos = velField[1][level]->indexToWorld(coord);
                        float volume =  volSample.wsSample(wpos);
                        float temperature = temSample.wsSample(wpos);
                        float dens = (alpha * volume -beta * temperature);

                        auto vel = vel_axr[1].getValue(coord);
                        auto deltaV = vel - dens * 9.8 * dt;
                        
                        vel_axr[1].setValue(coord, deltaV);
                        
                    }
                }
            };

            velField[1][level]->tree().getNodes(leaves);
            tbb::parallel_for(tbb::blocked_range<size_t>(0, leaves.size()), applyOuterForce);
            leaves.clear();
        }
    }
    void mgSmokeData::step(){
        //printf("begin to step\n");
        advection();
        //printf("advection over\n");
        applyOuterforce();
        //printf("apply outer force over\n");
        //solvePress();
    }

    struct mgSmokeToSDF : zeno::INode{
        virtual void apply() override {
            auto data = get_input<mgSmokeData>("mgSmokeData");
            std::cout<<"data pointer: "<<data<<" vole pointer: "<<data->volumeField[0]<<std::endl;
            
            auto result = zeno::IObject::make<VDBFloatGrid>();
            int levelNum = data->volumeField.size();
            //result->m_grid = openvdb::FloatGrid::create(0);
            // result->m_grid->setTransform(data->volumeField[levelNum-1]->transformPtr());
            //result->m_grid->setTransform(data->volumeField[0]->transformPtr());
            
            result->m_grid = openvdb::tools::fogToSdf(*(data->volumeField[0]), 0);
            // std::vector<openvdb::FloatTree::LeafNodeType *> leaves;
            // for(int level = levelNum-1;level < levelNum;++level)
            // {
            //     float dx = data->dx[level];
            //     auto volumeField = data->volumeField[level];
            //     volumeField->tree().getNodes(leaves);

            //     auto sampleValue = [&](const tbb::blocked_range<size_t> &r){
            //         auto volume_axr = volumeField->getConstAccessor();
            //         auto result_axr = result->m_grid->getAccessor();
            //         for (auto liter = r.begin(); liter != r.end(); ++liter) {
            //             auto &leaf = *leaves[liter];
            //             for (auto offset = 0; offset < leaf.SIZE; ++offset) {
            //                 openvdb::Coord coord = leaf.offsetToGlobalCoord(offset);
            //                 if(!volume_axr.isValueOn(coord))
            //                     continue;
            //                 auto vol = volume_axr.getValue(coord);
            //                 if(vol <= 0.01)
            //                     continue;
            //                 auto wpos = volumeField->indexToWorld(coord);
            //                 auto fine = result->m_grid->worldToIndex(wpos);
            //                 auto finepos = openvdb::Coord(round(fine[0]), round(fine[1]), round(fine[2]));
            //                 result_axr.setValue(finepos, vol);
            //                 //printf("vol is %f, wpos is (%f,%f,%f),fineindex is (%d,%d,%d)\n",
            //                 //    vol, wpos[0],wpos[1],wpos[2], finepos[0],finepos[1],finepos[2]);
            //             }
            //         }
            //     };
                
            //     tbb::parallel_for(tbb::blocked_range<size_t>(0, leaves.size()), sampleValue);
            //     leaves.clear();
            // }
            
            printf("mgsmoke to vdb is done. result nodes num is %d\n",
                result->m_grid->tree().activeVoxelCount());
            set_output("volumeGrid", result);
        }
    };
    ZENDEFNODE(mgSmokeToSDF,
    {
        {"mgSmokeData"},
        {"volumeGrid"},
        {},
        {"AdaptiveSolver"},
    }
    );

    struct SDFtoMGSmoke : zeno::INode{
        virtual void apply() override {
            auto sdf = get_input<zeno::VDBFloatGrid>("sdfgrid");
            auto data = zeno::IObject::make<mgSmokeData>();
            float dt = get_input("dt")->as<zeno::NumericObject>()->get<float>();
            int levelNum = get_input("levelNum")->as<zeno::NumericObject>()->get<int>();
            
            data->initData(sdf->m_grid, levelNum, dt);
            printf("sdf to mg smoke done\n");
            set_output("mgSmokeData", data);
        }
    };
    ZENDEFNODE(SDFtoMGSmoke,
    {
        {"sdfgrid", "dt", "levelNum"},
        {"mgSmokeData"},
        {},
        {"AdaptiveSolver"},
    }
    );

    struct mgSmokeSolver : zeno::INode{
        virtual void apply() override {
            auto data = get_input<mgSmokeData>("mgSmokeData");
            data->step();
            //printf("volume num is %d\n", data->volumeField->tree().activeVoxelCount());
            set_output("mgSmokeData", data);
        }
    };
    ZENDEFNODE(mgSmokeSolver,
    {
        {"mgSmokeData"},
        {"mgSmokeData"},
        {},
        {"AdaptiveSolver"},
    }
    );
}