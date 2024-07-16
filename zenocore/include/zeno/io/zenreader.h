#ifndef __ZEN_READER_H__
#define __ZEN_READER_H__

#include "zsgreader.h"
#include <zeno/core/Assets.h>

namespace zenoio
{
    class ZenReader : public ZsgReader
    {
    public:
        ZENO_API ZenReader();
        ZENO_API bool importNodes(const std::string& fn, zeno::NodesData& nodes, zeno::LinksData &links);
    protected:
        bool _parseMainGraph(const rapidjson::Document& doc, zeno::GraphData& ret) override;

        void _parseSocket(
            const bool bInput,
            const bool bSubnetNode,
            const bool bObjectParam,
            const std::string& id,
            const std::string& nodeCls,
            const std::string& inSock,
            const rapidjson::Value& sockObj,
            zeno::NodeData& ret,
            zeno::LinksData& links);

        void _parseInputs(
            const bool bObjectParam,
            const std::string& id,
            const std::string& nodeName,
            const rapidjson::Value& inputs,
            zeno::NodeData& ret,
            zeno::LinksData& links);

        void _parseOutputs(
            const bool bObjectParam,
            const std::string& id,
            const std::string& nodeName,
            const rapidjson::Value& jsonParams,
            zeno::NodeData& ret,
            zeno::LinksData& links);

        bool _parseGraph(
            const rapidjson::Value& graph,
            const zeno::AssetsData& assets,
            zeno::GraphData& subgData);

        zeno::NodeData _parseNode(
            const std::string& subgPath,    //Ҳ�������ˣ���Ϊ����Ϣ������path�ķ�ʽ���棨�����鷳�����ȱ�����
            const std::string& nodeid,
            const rapidjson::Value& nodeObj,
            const zeno::AssetsData& subgraphDatas,
            zeno::LinksData& links);    //��parse�ڵ��ʱ��˳���ѽڵ��ϵı���ϢҲ�����¼������

        zeno::CustomUI _parseCustomUI(const std::string& id, const rapidjson::Value& customuiObj, zeno::LinksData& links);
        zeno::CustomUI _parseCustomUI(const rapidjson::Value& customuiObj);
    };
}

#endif