#include <zeno/core/INode.h>
#include <zeno/core/defNode.h>
#include <zeno/types/ListObject.h>
#include <zeno/types/NumericObject.h>
#include <zeno/types/PrimitiveObject.h>

#include "zensim/zpc_tpls/fmt/color.h"
#include "zensim/zpc_tpls/fmt/format.h"
#include "zensim/ZpcBuiltin.hpp"
#include <cstdlib>
#include <zeno/utils/log.h>
#include <filesystem>

namespace fs = std::filesystem;

namespace zeno {

struct PyZpcLite : INode {
    void apply() override {
        char *p;
        p = getenv("PATH");
        std::set<std::string> pathLocations;
        auto processTags = [](std::string tags, std::set<std::string> &res, std::string sep) {
            using Ti = RM_CVREF_T(std::string::npos);
            Ti st = tags.find_first_not_of(sep, 0);
            for (auto ed = tags.find_first_of(sep, st + 1); ed != std::string::npos; ed = tags.find_first_of(sep, st + 1)) {
                res.insert(tags.substr(st, ed - st));
                st = tags.find_first_not_of(sep, ed);
                if (st == std::string::npos)
                    break;
            }
            if (st != std::string::npos && st < tags.size()) {
                res.insert(tags.substr(st));
            }
        };
#ifdef ZS_PLATFORM_WINDOWS
        processTags(p, pathLocations, ";");
        const std::string target = "python.exe";
#else
        processTags(p, pathLocations, ":");
        const std::string target = "python";
#endif
        for (const auto& path : pathLocations) {
            fmt::print("iterate path: {}\n", path);
            fs::path loc = path + "/" + target;
            bool ifExist = false;
            try {
                ifExist = fs::exists(loc);
            }
            catch (const std::exception& e) {
                fmt::print("\tskipping path {} due to exception (e.g. inaccessibility).\n", path);
                continue;
            }
            if (ifExist) {
                fmt::print("\tfound {} at {}\n", target, path);
            }
        }
    }
};

ZENDEFNODE(PyZpcLite, {/* inputs: */
                       {},
                       /* outputs: */
                       {},
                       /* params: */
                       {},
                       /* category: */
                       {"PyZfx"}});

} // namespace zeno