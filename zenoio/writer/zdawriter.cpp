#include "zdawriter.h"
#include <zeno/utils/logger.h>
#include <zeno/funcs/ParseObjectFromUi.h>
#include <zeno/utils/helper.h>
#include <zenoio/include/iohelper.h>
#include <format>

using namespace zeno::iotags;


namespace zenoio
{
    ZdaWriter::ZdaWriter()
    {
    }

    std::string ZdaWriter::dumpAsset(zeno::ZenoAsset asset)
    {
        std::string strJson;

        rapidjson::StringBuffer s;
        RAPIDJSON_WRITER writer(s);

        {
            JsonObjScope batch(writer);

            writer.Key("name");
            writer.String(asset.info.name.c_str());

            writer.Key("version");
            std::string ver = zeno::format("{}.{}", asset.info.majorVer, asset.info.minorVer);
            writer.String(ver.c_str());

            writer.Key("graph");
            dumpGraph(asset.graph, writer);

            writer.Key("Parameters");
            {
                JsonObjScope batch(writer);
            }
        }

        strJson = s.GetString();
        return strJson;
    }
}