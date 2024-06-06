#ifndef __CORE_REFERMANAGER_H__
#define __CORE_REFERMANAGER_H__

#include <map>
#include <set>
#include <string>
#include <memory>
#include "common.h"
#include <zeno/utils/api.h>

namespace zeno {
    struct INode;
    struct IParam;
    struct Graph;

    struct ReferManager
    {
    public:
        ReferManager();
        ~ReferManager();
        ZENO_API void init(const std::shared_ptr<Graph>& pGraph);
        void checkReference(const ObjPath& uuid_path, const std::string& param);
        void removeReference(const std::string& path, const std::string& uuid_path, const std::string& param = "");
        //�������õĽڵ������޸ĺ���Ҫ��������
        void updateReferParam(const std::string& oldPath, const std::string& newPath, const std::string& uuid_path, const std::string& param = "");
        bool isReferSelf(const std::string& uuid_path, const std::string& param) const;//�Ƿ�ѭ������

    private:
        void addReferInfo(const std::set<std::pair<std::string, std::string>>& referedParams, const std::string& referPath);
        //�����õĲ����޸ĺ���Ҫ��������
        void updateReferedInfo(const std::string& uuid_path, const std::string& param, const std::set<std::pair<std::string, std::string>>& referedParams);
        //�����õĲ�������ʱ��Ҫ�����õĽڵ����
        void updateDirty(const std::string& uuid_path, const std::string& param);
        std::set<std::pair<std::string, std::string>> getAllReferedParams(const std::string& uuid_param) const;

        std::set<std::pair<std::string, std::string>> referPaths(const std::string& currPath, const zvariant& val) const;
        bool updateParamValue(const std::string& oldVal, const std::string& newVal, const std::string& currentPath, zvariant& arg);

        //<�����ò���uuidpath, <�����ò���, ���ò���params>>
        std::map <std::string, std::map<std::string, std::set<std::string> > > m_referInfos; 
        bool m_bModify;
    };
}
#endif