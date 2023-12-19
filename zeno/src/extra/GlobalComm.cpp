#include <zeno/extra/GlobalComm.h>
#include <zeno/extra/GlobalState.h>
#include <zeno/funcs/ObjectCodec.h>
#include <zeno/utils/log.h>
#include <algorithm>
#include <fstream>
#include <cassert>
#include <zeno/types/UserData.h>
#include <unordered_set>
#include <zeno/types/MaterialObject.h>
#include <zeno/types/CameraObject.h>
#include <zeno/types/ListObject.h>
#include <zeno/types/PrimitiveObject.h>
#include "zeno/core/Session.h"
#include "zeno/utils/vec.h"
#ifdef __linux__
    #include<unistd.h>
    #include <sys/statfs.h>
#endif
#define MIN_DISKSPACE_MB 1024

namespace zeno {

std::set<std::string> lightCameraNodes({
"CameraEval", "CameraNode", "CihouMayaCameraFov", "ExtractCameraData", "GetAlembicCamera","MakeCamera",
"LightNode", "BindLight", "ProceduralSky", "HDRSky",
    });
std::string matlNode = "ShaderFinalize";

static void markListIndex(const std::string& root, std::shared_ptr<ListObject> lstObj)
{
    for (int i = 0; i < lstObj->arr.size(); i++) {
        auto& obj = lstObj->arr[i];
        const std::string& objpath = root + "/" + std::to_string(i);
        obj->userData().set2("list-index", objpath);
        if (std::shared_ptr<ListObject> spList = std::dynamic_pointer_cast<ListObject>(obj)) {
            markListIndex(objpath, spList);
        }
    }
}

void GlobalComm::toDisk(std::string cachedir, int frameid, GlobalComm::ViewObjects &objs, std::string key, bool dumpCacheVersionInfo) {
    if (cachedir.empty()) return;

    std::filesystem::path dir = std::filesystem::u8path(cachedir + "/" + std::to_string(1000000 + frameid).substr(1));
    if (!std::filesystem::exists(dir) && !std::filesystem::create_directories(dir))
    {
        log_critical("can not create path: {}", dir);
    }
    if (dumpCacheVersionInfo)
    {
        std::map<std::string, std::string> toViewNodesInfo;

        std::filesystem::path toViewInfoPath = dir / "toViewInofs.zencache";
        if (std::filesystem::exists(toViewInfoPath))
        {
            auto szBuffer = std::filesystem::file_size(toViewInfoPath);
            if (szBuffer != 0)
            {
                std::vector<char> dat(szBuffer);
                FILE* fp = fopen(toViewInfoPath.string().c_str(), "rb");
                if (!fp) {
                    log_error("zeno cache file does not exist");
                    return;
                }
                size_t ret = fread(&dat[0], 1, szBuffer, fp);
                assert(ret == szBuffer);
                fclose(fp);
                fp = nullptr;

                size_t beginpos = 0;
                size_t keyLen = 0;
                std::vector<char>::iterator beginIterator = dat.begin();
                for (auto i = dat.begin(); i != dat.end(); i++)
                {
                    if (*i == '\a')
                    {
                        keyLen = i - beginIterator;
                        std::string key(dat.data() + beginpos, keyLen);
                        toViewNodesInfo.insert(std::make_pair(std::move(key.substr(0, key.find(":"))), std::move(key)));
                        beginpos += i - beginIterator + 1;
                        beginIterator = i + 1;
                    }
                }
            }
        }
        for (auto const& [key, obj] : objs) {
            if (toViewNodesInfo.count(key.substr(0, key.find(":")))) {
                toViewNodesInfo[key.substr(0, key.find(":"))] = key;
            }
            toViewNodesInfo.insert(std::make_pair(key.substr(0, key.find(":")), key));
        }
        std::string keys;
        for (auto const& [id, key] : toViewNodesInfo) {
            keys.append(key);
            keys.push_back('\a');
        }
        std::ofstream ofs(toViewInfoPath, std::ios::binary);
        std::ostreambuf_iterator<char> oit(ofs);
        std::copy(keys.begin(), keys.end(), oit);
    }
    else {
        std::filesystem::path cachepath = dir / (key + ".zencache");
        std::vector<char> bufCaches;
        std::vector<size_t> poses;
        std::string keys;
        for (auto const& [key, obj] : objs) {
            size_t bufsize = bufCaches.size();

            std::back_insert_iterator<std::vector<char>> it(bufCaches);
            if (encodeObject(obj.get(), bufCaches))
            {
                keys.push_back('\a');
                keys.append(key);
                poses.push_back(bufsize);
            }
        }
        keys.push_back('\a');
        keys = "ZENCACHE" + std::to_string(poses.size()) + keys;
        poses.push_back(bufCaches.size());
        size_t currentFrameSize = keys.size() + poses.size() * sizeof(size_t) + bufCaches.size();

        size_t freeSpace = 0;
#ifdef __linux__
        struct statfs diskInfo;
        statfs(std::filesystem::u8path(cachedir).c_str(), &diskInfo);
        freeSpace = diskInfo.f_bsize * diskInfo.f_bavail;
#else
        freeSpace = std::filesystem::space(std::filesystem::u8path(cachedir)).free;
#endif
        //wait in two case: 1. available space minus current frame size less than 1024MB, 2. available space less or equal than 1024MB
        while (((freeSpace >> 20) - MIN_DISKSPACE_MB) < (currentFrameSize >> 20) || (freeSpace >> 20) <= MIN_DISKSPACE_MB)
        {
#ifdef __linux__
            zeno::log_critical("Disk space almost full on {}, wait for zencache remove", std::filesystem::u8path(cachedir).string());
            sleep(2);
            statfs(std::filesystem::u8path(cachedir).c_str(), &diskInfo);
            freeSpace = diskInfo.f_bsize * diskInfo.f_bavail;

#else
            zeno::log_critical("Disk space almost full on {}, wait for zencache remove", std::filesystem::u8path(cachedir).root_path().string());
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));
            freeSpace = std::filesystem::space(std::filesystem::u8path(cachedir)).free;
#endif
        }

        log_debug("dump cache to disk {}", cachepath);
        std::ofstream ofs(cachepath, std::ios::binary);
        std::ostreambuf_iterator<char> oit(ofs);
        std::copy(keys.begin(), keys.end(), oit);
        std::copy_n((const char*)poses.data(), poses.size() * sizeof(size_t), oit);
        std::copy(bufCaches.begin(), bufCaches.end(), oit);
    }

    objs.clear();
}

bool GlobalComm::fromDiskByObjsManager(std::string cachedir, int frameid, GlobalComm::ViewObjects& objs, std::vector<std::string>& nodesToLoad)
{
    if (cachedir.empty())
        return false;
    objs.clear();
    auto dir = std::filesystem::u8path(cachedir) / std::to_string(1000000 + frameid).substr(1);
    if (!std::filesystem::exists(dir))
        return false;

    std::map<std::string, std::string> toViewNodesInfo;

    std::filesystem::path filePath = dir / "toViewInofs.zencache";
    if (!std::filesystem::is_directory(filePath) && std::filesystem::exists(filePath)) {
        auto szBuffer = std::filesystem::file_size(filePath);
        if (szBuffer == 0)
            return true;

        std::vector<char> dat(szBuffer);
        FILE* fp = fopen(filePath.string().c_str(), "rb");
        if (!fp) {
            log_error("zeno cache file does not exist");
            return false;
        }
        size_t ret = fread(&dat[0], 1, szBuffer, fp);
        assert(ret == szBuffer);
        fclose(fp);
        fp = nullptr;

        size_t beginpos = 0;
        size_t keyLen = 0;
        std::vector<char>::iterator beginIterator = dat.begin();
        for (auto i = dat.begin(); i != dat.end(); i++)
        {
            if (*i == '\a')
            {
                keyLen = i - beginIterator;
                std::string key(dat.data() + beginpos, keyLen);
                toViewNodesInfo.insert(std::make_pair(std::move(key.substr(0, key.find(":"))), std::move(key)));
                beginpos += i - beginIterator + 1;
                beginIterator = i + 1;
            }
        }
    }

    std::function<void(zany const&, std::string, std::string)> convertToView = [&](zany const& p, std::string postfix, std::string name) -> void {
        if (ListObject* lst = dynamic_cast<ListObject*>(p.get())) {
            log_info("ToView got ListObject (size={}), expanding", lst->arr.size());
            for (size_t i = 0; i < lst->arr.size(); i++) {
                zany const& lp = lst->arr[i];
                std::string id(name);
                convertToView(lp, postfix + ":LIST" + std::to_string(i), id.insert(id.find(":"), postfix + ":LIST:" + std::to_string(i)));
            }
            return;
        }
        if (!p) {
            log_error("ToView: given object is nullptr");
        }
        else {
            objs.try_emplace(name, std::move(p));
        }
    };
    for (auto& cache : nodesToLoad)
    {
        if (toViewNodesInfo.find(cache) == toViewNodesInfo.end())
            continue;
        std::string toViewObjInfo = toViewNodesInfo[cache];

        std::filesystem::path cachePath = dir / (cache + ".zencache");
        if (!std::filesystem::is_directory(cachePath) && std::filesystem::exists(cachePath)) {
            auto szBuffer = std::filesystem::file_size(cachePath);
            if (szBuffer == 0)
                return true;

            log_debug("load cache from disk {}", cachePath);

            std::vector<char> dat(szBuffer);
            FILE* fp = fopen(cachePath.string().c_str(), "rb");
            if (!fp) {
                log_error("zeno cache file does not exist");
                return false;
            }
            size_t ret = fread(&dat[0], 1, szBuffer, fp);
            assert(ret == szBuffer);
            fclose(fp);
            fp = nullptr;

            if (dat.size() <= 8 || std::string(dat.data(), 8) != "ZENCACHE") {
                log_error("zeno cache file broken (1)");
                return false;
            }
            size_t pos = std::find(dat.begin() + 8, dat.end(), '\a') - dat.begin();
            if (pos == dat.size()) {
                log_error("zeno cache file broken (2)");
                return false;
            }
            size_t keyscount = std::stoi(std::string(dat.data() + 8, pos - 8));
            pos = pos + 1;
            std::vector<std::string> keys;
            for (int k = 0; k < keyscount; k++) {
                size_t newpos = std::find(dat.begin() + pos, dat.end(), '\a') - dat.begin();
                if (newpos == dat.size()) {
                    log_error("zeno cache file broken (3.{})", k);
                    return false;
                }
                keys.emplace_back(dat.data() + pos, newpos - pos);
                pos = newpos + 1;
            }
            std::vector<size_t> poses(keyscount + 1);
            std::copy_n(dat.data() + pos, (keyscount + 1) * sizeof(size_t), (char*)poses.data());
            pos += (keyscount + 1) * sizeof(size_t);

            int lastObjIdx = keyscount - 1; //now only first output is needed to view this obj.
            if (poses[lastObjIdx] > dat.size() - pos || poses[lastObjIdx + 1] < poses[lastObjIdx]) {
                log_error("zeno cache file broken (4.{})", lastObjIdx);
            }
            const char* p = dat.data() + pos + poses[lastObjIdx];

            zeno::zany decodedObj = decodeObject(p, poses[lastObjIdx + 1] - poses[lastObjIdx]);
            if (std::shared_ptr<ListObject> spListObj = std::dynamic_pointer_cast<ListObject>(decodedObj))
            {
                std::string objid = toViewObjInfo.substr(0, toViewObjInfo.find(":"));
                markListIndex(objid, spListObj);
            }

            convertToView(decodedObj, {}, toViewObjInfo.insert(toViewObjInfo.find(":"), ":TOVIEW"));
        }
    }
    return true;
}

bool GlobalComm::fromDiskByRunner(std::string cachedir, int frameid, GlobalComm::ViewObjects &objs, std::string filename) {
    if (cachedir.empty())
        return false;
    objs.clear();
    auto dir = std::filesystem::u8path(cachedir) / std::to_string(1000000 + frameid).substr(1);
    if (!std::filesystem::exists(dir))
        return false;

    std::filesystem::path filePath = dir / (filename + ".zencache");
    if (!std::filesystem::is_directory(filePath) && std::filesystem::exists(filePath)) {

        auto szBuffer = std::filesystem::file_size(filePath);
        if (szBuffer == 0)
            return true;

        log_debug("load cache from disk {}", filePath);

        std::vector<char> dat(szBuffer);
        FILE* fp = fopen(filePath.string().c_str(), "rb");
        if (!fp) {
            log_error("zeno cache file does not exist");
            return false;
        }
        size_t ret = fread(&dat[0], 1, szBuffer, fp);
        assert(ret == szBuffer);
        fclose(fp);
        fp = nullptr;

        if (dat.size() <= 8 || std::string(dat.data(), 8) != "ZENCACHE") {
            log_error("zeno cache file broken (1)");
            return false;
        }
        size_t pos = std::find(dat.begin() + 8, dat.end(), '\a') - dat.begin();
        if (pos == dat.size()) {
            log_error("zeno cache file broken (2)");
            return false;
        }
        size_t keyscount = std::stoi(std::string(dat.data() + 8, pos - 8));
        pos = pos + 1;
        std::vector<std::string> keys;
        for (int k = 0; k < keyscount; k++) {
            size_t newpos = std::find(dat.begin() + pos, dat.end(), '\a') - dat.begin();
            if (newpos == dat.size()) {
                log_error("zeno cache file broken (3.{})", k);
                return false;
            }
            keys.emplace_back(dat.data() + pos, newpos - pos);
            pos = newpos + 1;
        }
        std::vector<size_t> poses(keyscount + 1);
        std::copy_n(dat.data() + pos, (keyscount + 1) * sizeof(size_t), (char*)poses.data());
        pos += (keyscount + 1) * sizeof(size_t);
        for (int k = 0; k < keyscount; k++) {
            if (poses[k] > dat.size() - pos || poses[k + 1] < poses[k]) {
                log_error("zeno cache file broken (4.{})", k);
            }
            const char* p = dat.data() + pos + poses[k];
            objs.try_emplace(keys[k], decodeObject(p, poses[k + 1] - poses[k]));
        }
    }
    return true;
}

ZENO_API void GlobalComm::newFrame() {
    std::lock_guard lck(m_recur_mutex);
    log_debug("GlobalComm::newFrame {}", m_frames.size());
    m_frames.emplace_back().frame_state = FRAME_UNFINISH;
}

ZENO_API void GlobalComm::finishFrame() {
    std::lock_guard lck(m_recur_mutex);
    log_debug("GlobalComm::finishFrame {}", m_maxPlayFrame);
    if (m_maxPlayFrame >= 0 && m_maxPlayFrame < m_frames.size())
        m_frames[m_maxPlayFrame].frame_state = FRAME_COMPLETED;
    m_maxPlayFrame += 1;
}

ZENO_API void GlobalComm::dumpFrameCache(int frameid) {
    std::lock_guard lck(m_recur_mutex);
    int frameIdx = frameid - beginFrameNumber;
    if (frameIdx >= 0 && frameIdx < m_frames.size()) {
        log_debug("dumping frame {}", frameid);
        toDisk(cacheFramePath, frameid, m_frames[frameIdx].view_objects, "", true);
    }
}

ZENO_API void GlobalComm::addViewObject(std::string const &key, std::shared_ptr<IObject> object) {
    std::lock_guard lck(m_recur_mutex);
    log_debug("GlobalComm::addViewObject {}", m_frames.size());
    if (m_frames.empty()) throw makeError("empty frame cache");
    m_frames.back().view_objects.try_emplace(key, std::move(object));
}

ZENO_API void GlobalComm::clearState() {
    std::lock_guard lck(m_recur_mutex);
    m_frames.clear();
    m_inCacheFrames.clear();
    m_maxPlayFrame = 0;
    maxCachedFrames = 1;
    cacheFramePath = {};
}

ZENO_API void GlobalComm::clearFrameState()
{
    std::lock_guard lck(m_recur_mutex);
    m_frames.clear();
    m_inCacheFrames.clear();
    m_maxPlayFrame = 0;
}

ZENO_API void GlobalComm::frameCache(std::string const &path, int gcmax) {
    std::lock_guard lck(m_recur_mutex);
    cacheFramePath = path;
    maxCachedFrames = gcmax;
}

ZENO_API void GlobalComm::initFrameRange(int beg, int end) {
    std::lock_guard lck(m_recur_mutex);
    beginFrameNumber = beg;
    endFrameNumber = end;
}

ZENO_API int GlobalComm::maxPlayFrames() {
    std::lock_guard lck(m_recur_mutex);
    return m_maxPlayFrame + beginFrameNumber; // m_frames.size();
}

ZENO_API int GlobalComm::numOfFinishedFrame() {
    std::lock_guard lck(m_recur_mutex);
    return m_maxPlayFrame;
}

ZENO_API int GlobalComm::numOfInitializedFrame()
{
    std::lock_guard lck(m_recur_mutex);
    return m_frames.size();
}

ZENO_API std::pair<int, int> GlobalComm::frameRange() {
    std::lock_guard lck(m_recur_mutex);
    return std::pair<int, int>(beginFrameNumber, endFrameNumber);
}

ZENO_API GlobalComm::ViewObjects const *GlobalComm::getViewObjects(const int frameid) {
    std::lock_guard lck(m_recur_mutex);
    return _getViewObjects(frameid);
}

GlobalComm::ViewObjects const* GlobalComm::_getViewObjects(const int frameid) {
    int frameIdx = frameid - beginFrameNumber;
    if (frameIdx < 0 || frameIdx >= m_frames.size())
        return nullptr;
    if (maxCachedFrames != 0) {
        // load back one gc:
        if (!m_inCacheFrames.count(frameid)) {  // notinmem then cacheit
            bool ret = fromDiskByObjsManager(cacheFramePath, frameid, m_frames[frameIdx].view_objects, toViewNodesId);
            if (!ret)
                return nullptr;

            m_inCacheFrames.insert(frameid);
            // and dump one as balance:
            if (m_inCacheFrames.size() && m_inCacheFrames.size() > maxCachedFrames) { // notindisk then dumpit
                for (int i: m_inCacheFrames) {
                    if (i != frameid) {
                        // seems that objs will not be modified when load_objects called later.
                        // so, there is no need to dump.
                        //toDisk(cacheFramePath, i, m_frames[i - beginFrameNumber].view_objects);
                        m_frames[i - beginFrameNumber].view_objects.clear();
                        m_inCacheFrames.erase(i);
                        break;
                    }
                }
            }
        }
    }
    return &m_frames[frameIdx].view_objects;
}

ZENO_API GlobalComm::ViewObjects const &GlobalComm::getViewObjects() {
    std::lock_guard lck(m_recur_mutex);
    return m_frames.back().view_objects;
}

ZENO_API bool GlobalComm::load_objects(
        const int frameid,
        bool& isFrameValid)
{
    std::lock_guard lck(m_recur_mutex);

    int frame = frameid;
    frame -= beginFrameNumber;
    if (frame < 0 || frame >= m_frames.size() || m_frames[frame].frame_state != FRAME_COMPLETED)
    {
        isFrameValid = false;
        return false;
    }

    isFrameValid = true;
    auto const* viewObjs = _getViewObjects(frameid);

    bool inserted = false;
    for (auto const& [key, obj] : viewObjs->m_curr)
        if (lastToViewNodesType.find(key) == lastToViewNodesType.end() && key.find_last_of("#") == std::string::npos) {     //key modified && key contains "#"(modified by viewport)
            inserted = true;
            break;
        }
    m_currentFrame = frameid - beginFrameNumber;

    if (viewObjs) {
        zeno::log_trace("load_objects: {} objects at frame {}", viewObjs->size(), frameid);
        prepareForOptix(inserted, viewObjs->m_curr);
    }
    else {
        zeno::log_trace("load_objects: no objects at frame {}", frameid);
        prepareForOptix(inserted, {});
    }
    return inserted;
}

ZENO_API bool GlobalComm::isFrameCompleted(int frameid) const {
    std::lock_guard lck(m_recur_mutex);
    frameid -= beginFrameNumber;
    if (frameid < 0 || frameid >= m_frames.size())
        return false;
    return m_frames[frameid].frame_state == FRAME_COMPLETED;
}

ZENO_API GlobalComm::FRAME_STATE GlobalComm::getFrameState(int frameid) const
{
    std::lock_guard lck(m_recur_mutex);
    frameid -= beginFrameNumber;
    if (frameid < 0 || frameid >= m_frames.size())
        return FRAME_UNFINISH;
    return m_frames[frameid].frame_state;
}

ZENO_API bool GlobalComm::isFrameBroken(int frameid) const
{
    std::lock_guard lck(m_recur_mutex);
    frameid -= beginFrameNumber;
    if (frameid < 0 || frameid >= m_frames.size())
        return false;
    return m_frames[frameid].frame_state == FRAME_BROKEN;
}

ZENO_API int GlobalComm::maxCachedFramesNum()
{
    std::lock_guard lck(m_recur_mutex);
    return maxCachedFrames;
}

ZENO_API std::string GlobalComm::cachePath()
{
    std::lock_guard lck(m_recur_mutex);
    return cacheFramePath;
}

ZENO_API bool GlobalComm::removeCache(int frame)
{
    std::lock_guard lck(m_recur_mutex);
    bool hasZencacheOnly = true;
    std::filesystem::path dirToRemove = std::filesystem::u8path(cacheFramePath + "/" + std::to_string(1000000 + frame).substr(1));
    if (std::filesystem::exists(dirToRemove))
    {
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(dirToRemove))
        {
            std::string filePath = entry.path().string();
            if (std::filesystem::is_directory(entry.path()) || filePath.substr(filePath.size() - 9) != ".zencache")
            {
                hasZencacheOnly = false;
                break;
            }
        }
        if (hasZencacheOnly)
        {
            m_frames[frame - beginFrameNumber].frame_state = FRAME_BROKEN;
            std::filesystem::remove_all(dirToRemove);
            zeno::log_info("remove dir: {}", dirToRemove);
        }
    }
    if (frame == endFrameNumber && std::filesystem::exists(std::filesystem::u8path(cacheFramePath)) && std::filesystem::is_empty(std::filesystem::u8path(cacheFramePath)))
    {
        std::filesystem::remove(std::filesystem::u8path(cacheFramePath));
        zeno::log_info("remove dir: {}", std::filesystem::u8path(cacheFramePath).string());
    }
    return true;
}

ZENO_API void GlobalComm::removeCachePath()
{
    std::lock_guard lck(m_recur_mutex);
    std::filesystem::path dirToRemove = std::filesystem::u8path(cacheFramePath);
    if (std::filesystem::exists(dirToRemove) && cacheFramePath.find(".") == std::string::npos)
    {
        std::filesystem::remove_all(dirToRemove);
        zeno::log_info("remove dir: {}", dirToRemove);
    }
}

ZENO_API void GlobalComm::setToViewNodes(std::vector<std::string>&nodes)
{
    std::lock_guard lck(m_recur_mutex);
    toViewNodesId = std::move(nodes);
}

//-----ObjectsManager-----
void GlobalComm::prepareForOptix(bool inserted, std::map<std::string, std::shared_ptr<zeno::IObject>> const& objs) {
    if (inserted || objs.size() < lastToViewNodesType.size()) {
        std::vector<size_t> count(3, 0);
        for (auto it = lastToViewNodesType.begin(); it != lastToViewNodesType.end();)
            if (objs.find(it->first) == objs.end())
            {
                count[it->second]++;
                lastToViewNodesType.erase(it++);
            }
            else {
                it++;
            }
        for (auto& [key, obj] : objs)
            if (lastToViewNodesType.find(key) == lastToViewNodesType.end())
            {
                std::string nodeName = key.substr(key.find("-") + 1, key.find(":") - key.find("-") - 1);
                if (lightCameraNodes.count(nodeName) || obj->userData().get2<int>("isL", 0) || std::dynamic_pointer_cast<zeno::CameraObject>(obj)) {
                    lastToViewNodesType.insert({ key, LIGHT_CAMERA });
                    count[LIGHT_CAMERA]++;
                }
                else if (matlNode == nodeName || std::dynamic_pointer_cast<zeno::MaterialObject>(obj)) {
                    lastToViewNodesType.insert({ key, MATERIAL });
                    count[MATERIAL]++;
                }
                else {
                    lastToViewNodesType.insert({ key, NORMAL });
                    count[NORMAL]++;
                }
            }
        renderType = count[NORMAL] == 0 && count[MATERIAL] == 0 ? LIGHT_CAMERA : count[NORMAL] == 0 && count[LIGHT_CAMERA] == 0 ? MATERIAL : NORMAL;

        lightObjects.clear();
        for (auto const& [key, obj] : objs) {
            if (auto prim_in = dynamic_cast<zeno::PrimitiveObject*>(obj.get())) {
                auto isRealTimeObject = prim_in->userData().get2<int>("isRealTimeObject", 0);
                if (isRealTimeObject) {
                    //printf("loading light object %s\n", key.c_str());
                    lightObjects[key] = obj;
                }
            }
        }
    }
}

ZENO_API void GlobalComm::clear_objects() {
    std::lock_guard lck(m_recur_mutex);
    if (m_frames.size() != 0)
    {
        if (m_currentFrame < 0 && m_currentFrame >= m_frames.size()) {
            return;
        }
        m_frames[m_currentFrame].view_objects.clear();
    }
}

ZENO_API void GlobalComm::clear_lightObjects()
{
    std::lock_guard lck(m_recur_mutex);
    lightObjects.clear();
}

ZENO_API std::optional<zeno::IObject* > GlobalComm::get(std::string nid) {
    std::lock_guard lck(m_recur_mutex);
    if (m_currentFrame < 0 && m_currentFrame >= m_frames.size()) {
        return std::nullopt;
    }

    for (auto& [key, ptr] : m_frames[m_currentFrame].view_objects) {
        if (key != nid) {
            continue;
        }
        return std::optional<zeno::IObject*>(ptr.get());;
    }

    return std::nullopt;
}

ZENO_API std::vector<std::pair<std::string, IObject*>> GlobalComm::pairs() const
{
    std::lock_guard lck(m_recur_mutex);
    if (m_frames.size() != 0) {
        if (m_currentFrame < 0 && m_currentFrame >= m_frames.size()) {
            return std::vector<std::pair<std::string, IObject*>>();
        }
        return m_frames[m_currentFrame].view_objects.pairs();
    }
    else
        return std::vector<std::pair<std::string, IObject*>>();
}

ZENO_API std::vector<std::pair<std::string, std::shared_ptr<IObject>>> GlobalComm::pairsShared() const
{
    std::lock_guard lck(m_recur_mutex);
    if (m_frames.size() != 0) {
        if (m_currentFrame < 0 && m_currentFrame >= m_frames.size()) {
            return std::vector<std::pair<std::string, std::shared_ptr<IObject>>>();
        }
        return m_frames[m_currentFrame].view_objects.pairsShared();
    }
    else
        return std::vector<std::pair<std::string, std::shared_ptr<IObject>>>();
}

//------new change------

void GlobalComm::mutexCallback(const std::function<void()>& callback)
{
    std::lock_guard lck(m_recur_mutex);
    if (callback)
        callback();
}

ZENO_API bool GlobalComm::lightObjsCount(std::string& id)
{
    std::lock_guard lck(m_recur_mutex);
    for (auto const& [key, ptr] : lightObjects) {
        if (key.find(id) == 0) {
            return true;
        }
    }
    return false;
}

ZENO_API bool GlobalComm::objsCount(std::string& id)
{
    std::lock_guard lck(m_recur_mutex);
    if (m_currentFrame < 0 && m_currentFrame >= m_frames.size()) {
        return 0;
    }

    for (auto const& [key, ptr] : m_frames[m_currentFrame].view_objects) {
        if (key.find(id) == 0) {
            return true;
        }
    }
    return false;
}

ZENO_API const std::string GlobalComm::getLightObjKeyByLightObjID(std::string id)
{
    std::lock_guard lck(m_recur_mutex);
    for (auto const& [key, ptr] : lightObjects) {
        if (key.find(id) != std::string::npos) {
            return key;
        }
    }
    return "";
}

ZENO_API const std::string GlobalComm::getObjKeyByObjID(std::string& id)
{
    std::lock_guard lck(m_recur_mutex);
    if (m_currentFrame >= 0 && m_currentFrame < m_frames.size()) {
        for (auto const& [key, ptr] : m_frames[m_currentFrame].view_objects) {
            if (id == key.substr(0, key.find_first_of(':'))) {
                return key;
            }
        }
    }
    return "";
}

ZENO_API const std::string GlobalComm::getObjKey1(std::string& id, int frame)
{
    std::lock_guard lck(m_recur_mutex);
    if (m_currentFrame >= 0 && m_currentFrame < m_frames.size()) {
        for (auto const& [key, ptr] : m_frames[m_currentFrame].view_objects) {
            if (key.find(id) == 0 && key.find(zeno::format(":{}:", frame)) != std::string::npos) {
                return key;
            }
        }
    }
    return "";
}

ZENO_API GlobalComm::RenderType GlobalComm::getRenderTypeByObjects(std::map<std::string, zeno::IObject*> objs)
{
    std::vector<size_t> count(3, 0);
    for (auto& [key, obj] : objs) {
        std::string nodeName = key.substr(key.find("-") + 1);
        if (lightCameraNodes.count(nodeName) || obj->userData().get2<int>("isL", 0) || dynamic_cast<zeno::CameraObject*>(obj))
            count[LIGHT_CAMERA]++;
        else if (matlNode == nodeName || dynamic_cast<zeno::MaterialObject*>(obj))
            count[MATERIAL]++;
        else
            count[NORMAL]++;
    }
    return count[NORMAL] == 0 && count[MATERIAL] == 0 ? LIGHT_CAMERA : count[NORMAL] == 0 && count[LIGHT_CAMERA] == 0 ? MATERIAL : NORMAL;
}

ZENO_API void GlobalComm::updateObjsIdByViewport(std::map<std::string, zeno::IObject*>& objsToBeUpdate)
{
    std::lock_guard lck(m_recur_mutex);
    static size_t changedNumber = 0;
    for (auto& [key, obj] : objsToBeUpdate)
    {
        std::shared_ptr<IObject> oldObj;
        std::string oldKey;

        if (m_currentFrame < 0 && m_currentFrame >= m_frames.size()) {
            continue;
        }

        for (auto& [k, o] : m_frames[m_currentFrame].view_objects)
            if (k.find(key) != std::string::npos) {
                oldKey = k;
                oldObj = m_frames[m_currentFrame].view_objects.m_curr[k];
                break;
            }
        m_frames[m_currentFrame].view_objects.m_curr.erase(oldKey);
        std::string& newKey = oldKey.replace(oldKey.find_last_of(":") + 1, std::to_string(changedNumber).size() + 1, std::to_string(changedNumber) + "#");
        m_frames[m_currentFrame].view_objects.try_emplace(newKey,std::move(oldObj));
        if (lightCameraNodes.count(key) || obj->userData().get2<int>("isL", 0))
        {
            std::shared_ptr<IObject> oldLightObj;
            std::string oldLightKey;
            for (auto& [k, o] : lightObjects)
                if (k.find(key) != std::string::npos) {
                    oldLightKey = k;
                    oldLightObj = lightObjects[k];
                    break;
                }
            lightObjects.erase(oldLightKey);
            std::string& newKey = oldLightKey.replace(oldLightKey.find_last_of(":") + 1, std::to_string(changedNumber).size() + 1, std::to_string(changedNumber) + "#");
            lightObjects.insert(std::make_pair(newKey, std::move(oldLightObj)));
        }
    }
    changedNumber++;
    updateOptixByViewport = true;
}

ZENO_API bool GlobalComm::getLightObjData(std::string& id, zeno::vec3f& pos, zeno::vec3f& scale, zeno::vec3f& rotate, zeno::vec3f& clr, float& intensity)
{
    std::lock_guard lck(m_recur_mutex);
    if (lightObjects.find(id) != lightObjects.end())
    {
        std::shared_ptr<zeno::IObject> ptr = lightObjects[id];
        if (ptr->userData().get2<int>("isL", 0)) {
            if (auto prim_in = dynamic_cast<zeno::PrimitiveObject*>(ptr.get())) {
                pos = ptr->userData().getLiterial<zeno::vec3f>("pos", zeno::vec3f(0.0f));
                scale = ptr->userData().getLiterial<zeno::vec3f>("scale", zeno::vec3f(0.0f));
                rotate = ptr->userData().getLiterial<zeno::vec3f>("rotate", zeno::vec3f(0.0f));
                if (ptr->userData().has("color")) {
                    clr = ptr->userData().getLiterial<zeno::vec3f>("color");
                }
                else {
                    if (prim_in->verts.has_attr("clr")) {
                        clr = prim_in->verts.attr<zeno::vec3f>("clr")[0];
                    }
                    else {
                        clr = zeno::vec3f(30000.0f, 30000.0f, 30000.0f);
                    }
                }
                intensity = ptr->userData().getLiterial<float>("intensity", 1);
                return true;
            }
        }
        else {
            zeno::log_info("connect item not found {}", id);
        }
    }
    return false;
}

ZENO_API bool GlobalComm::setLightObjData(std::string& id, zeno::vec3f& pos, zeno::vec3f& scale, zeno::vec3f& rotate, zeno::vec3f& rgb, float& intensity, std::vector<zeno::vec3f>& verts)
{
    std::lock_guard lck(m_recur_mutex);
    if (lightObjects.find(id) != lightObjects.end())
    {
        std::shared_ptr<zeno::IObject> obj = lightObjects[id];
        auto prim_in = dynamic_cast<zeno::PrimitiveObject*>(obj.get());

        if (prim_in) {
            auto& prim_verts = prim_in->verts;
            prim_verts[0] = verts[0];
            prim_verts[1] = verts[1];
            prim_verts[2] = verts[2];
            prim_verts[3] = verts[3];
            prim_in->verts.attr<zeno::vec3f>("clr")[0] = rgb *intensity;

            prim_in->userData().setLiterial<zeno::vec3f>("pos", zeno::vec3f(pos));
            prim_in->userData().setLiterial<zeno::vec3f>("scale", zeno::vec3f(scale));
            prim_in->userData().setLiterial<zeno::vec3f>("rotate", zeno::vec3f(rotate));
            if (prim_in->userData().has("intensity")) {
                prim_in->userData().setLiterial<zeno::vec3f>("color", zeno::vec3f(rgb));
                prim_in->userData().setLiterial<float>("intensity", std::move(intensity));
            }
            needUpdateLight = true;
            return true;
        }
        else {
            zeno::log_info("modifyLightData not found {}", id);
        }
    }
    return false;
}

ZENO_API bool GlobalComm::setProceduralSkyData(std::string id, zeno::vec2f& sunLightDir, float& sunSoftnessValue, zeno::vec2f& windDir, float& timeStartValue, float& timeSpeedValue, float& sunLightIntensityValue, float& colorTemperatureMixValue, float& colorTemperatureValue)
{
    std::lock_guard lck(m_recur_mutex);
    auto& setFunc = [&](zeno::PrimitiveObject* prim_in) {
        prim_in->userData().set2("sunLightDir", std::move(sunLightDir));
        prim_in->userData().set2("sunLightSoftness", std::move(sunSoftnessValue));
        prim_in->userData().set2("windDir", std::move(windDir));
        prim_in->userData().set2("timeStart", std::move(timeStartValue));
        prim_in->userData().set2("timeSpeed", std::move(timeSpeedValue));
        prim_in->userData().set2("sunLightIntensity", std::move(sunLightIntensityValue));
        prim_in->userData().set2("colorTemperatureMix", std::move(colorTemperatureMixValue));
        prim_in->userData().set2("colorTemperature", std::move(colorTemperatureValue));
    };
    bool found = false;
    if (id == "")
    {
        for (auto const& [key, obj] : lightObjects) {
            if (key.find("ProceduralSky") != std::string::npos) {
                found = true;
                if (auto prim_in = dynamic_cast<zeno::PrimitiveObject*>(obj.get())) {
                    setFunc(prim_in);
                }
            }
        }
    }
    else {
        if (lightObjects.find(id) != lightObjects.end())
        {
            found = true;
            std::shared_ptr<zeno::IObject> obj = lightObjects[id];
            if (auto prim_in = dynamic_cast<zeno::PrimitiveObject*>(obj.get())) {
                setFunc(prim_in);
            }
        }
    }
    auto& ud = zeno::getSession().userData();
    if (found) {
        ud.erase("sunLightDir");
        ud.erase("sunLightSoftness");
        ud.erase("windDir");
        ud.erase("timeStart");
        ud.erase("timeSpeed");
        ud.erase("sunLightIntensity");
        ud.erase("colorTemperatureMix");
        ud.erase("colorTemperature");
    }
    else {
        ud.set2("sunLightDir", std::move(sunLightDir));
        ud.set2("sunLightSoftness", std::move(sunSoftnessValue));
        ud.set2("windDir", std::move(windDir));
        ud.set2("timeStart", std::move(timeStartValue));
        ud.set2("timeSpeed", std::move(timeSpeedValue));
        ud.set2("sunLightIntensity", std::move(sunLightIntensityValue));
        ud.set2("colorTemperatureMix", std::move(colorTemperatureMixValue));
        ud.set2("colorTemperature", std::move(colorTemperatureValue));
    }
    needUpdateLight = true;
    return found;
}

ZENO_API bool GlobalComm::getProceduralSkyData(std::string& id, zeno::vec2f& sunLightDir, float& sunSoftnessValue, zeno::vec2f& windDir, float& timeStartValue, float& timeSpeedValue, float& sunLightIntensityValue, float& colorTemperatureMixValue, float& colorTemperatureValue)
{
    std::lock_guard lck(m_recur_mutex);
    auto& getFunc = [&](zeno::PrimitiveObject* prim_in) {
        sunLightDir = prim_in->userData().get2<zeno::vec2f>("sunLightDir");
        windDir = prim_in->userData().get2<zeno::vec2f>("windDir");
        sunSoftnessValue = prim_in->userData().get2<float>("sunLightSoftness");
        timeStartValue = prim_in->userData().get2<float>("timeStart");
        timeSpeedValue = prim_in->userData().get2<float>("timeSpeed");
        sunLightIntensityValue = prim_in->userData().get2<float>("sunLightIntensity");
        colorTemperatureMixValue = prim_in->userData().get2<float>("colorTemperatureMix");
        colorTemperatureValue = prim_in->userData().get2<float>("colorTemperature");
    };
    if (id == "")
    {
        for (auto const& [key, obj] : lightObjects) {
            if (key.find("ProceduralSky") != std::string::npos) {
                if (auto prim_in = dynamic_cast<zeno::PrimitiveObject*>(obj.get())) {
                    getFunc(prim_in);
                    return true;
                }
            }
        }
    }
    else {
        if (lightObjects.find(id) != lightObjects.end())
        {
            std::shared_ptr<zeno::IObject> obj = lightObjects[id];
            if (auto prim_in = dynamic_cast<zeno::PrimitiveObject*>(obj.get())) {
                getFunc(prim_in);
                return true;
            }
        }
    }
    return false;
}

ZENO_API void GlobalComm::getAllLightsKey(std::vector<std::string>& keys)
{
    std::lock_guard lck(m_recur_mutex);
    for (auto const& [key, ptr] : lightObjects) {
        if (ptr->userData().get2<int>("isL", 0)) {
            keys.push_back(key);
        }
    }
}

ZENO_API std::string GlobalComm::getObjMatId(std::string& id)
{
    std::lock_guard lck(m_recur_mutex);

    if (m_currentFrame < 0 && m_currentFrame >= m_frames.size()) {
        return "";
    }

    for (auto const& [key, ptr] : m_frames[m_currentFrame].view_objects) {
        if (id == key) {
            auto& ud = ptr->userData();
            return ud.get2<std::string>("mtlid", "Default");
        }
    }
    return "";
}

ZENO_API void GlobalComm::setRenderType(GlobalComm::RenderType type)
{
    std::lock_guard lck(m_recur_mutex);
    renderType = type;
    if (updateOptixByViewport && renderType == UNDEFINED)
        updateOptixByViewport = false;
}

ZENO_API GlobalComm::RenderType GlobalComm::getRenderType()
{
    std::lock_guard lck(m_recur_mutex);
    return renderType;
}

ZENO_API std::map<std::string, std::shared_ptr<zeno::IObject>>& GlobalComm::getLightObjs()
{
    std::lock_guard lck(m_recur_mutex);
    return lightObjects;
}

ZENO_API int GlobalComm::getLightObjsSize()
{
    std::lock_guard lck(m_recur_mutex);
    return lightObjects.size();
}

ZENO_API bool GlobalComm::getNeedUpdateLight()
{
    std::lock_guard lck(m_recur_mutex);
    return needUpdateLight;
}

ZENO_API void GlobalComm::setNeedUpdateLight(bool update)
{
    std::lock_guard lck(m_recur_mutex);
    needUpdateLight = update;
}

}
