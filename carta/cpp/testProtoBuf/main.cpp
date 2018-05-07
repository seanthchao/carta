/*
 * This is the test for percentile algorithms and protocol buffer
 *
 * Usage: $./testProtoBuf [the path and name of the image file]
 *
 * for example: $./testProtoBuf.app/Contents/MacOS/testProtoBuf ../../../carta/scriptedClient/tests/data/aH.fits
 *
 */
#include "core/MyQApp.h"
#include "core/CmdLine.h"
#include "core/MainConfig.h"
#include "core/Globals.h"
#include "CartaLib/Hooks/LoadAstroImage.h"
#include "CartaLib/Hooks/Initialize.h"
#include "CartaLib/Hooks/PercentileToPixelHook.h"
#include <QDebug>
#include <cmath>
#include <QJsonObject>

#include <ctime>
#include <fstream>
#include <google/protobuf/util/time_util.h>
#include <iostream>
#include <string>

#include "databook.pb.h"

#include "ProtoBufHelper.h"
#include "CartaHelper.h"

using namespace std;

namespace testProtoBuff {

typedef Carta::Lib::AxisInfo AxisInfo;

static int coreMainCPP(QString platformString, int argc, char* argv[]) {
    MyQApp qapp(argc, argv);

    QString appName = "carta-" + platformString;
    appName += "-verbose";

    if (CARTA_RUNTIME_CHECKS) {
        appName += "-runtimeChecks";
    }

    MyQApp::setApplicationName(appName);
    qDebug() << "Starting" << qapp.applicationName() << qapp.applicationVersion();

    // alias globals
    auto & globals = * Globals::instance();

    // parse command line arguments & environment variables
    auto cmdLineInfo = CmdLine::parse(MyQApp::arguments());
    globals.setCmdLineInfo(& cmdLineInfo);

    int file_num = cmdLineInfo.fileList().size();
    if (file_num < 1) {
        qFatal("Usage: ./testestProtoBuff [the path and name of the image file] ...");
    }

    // load the config file
    QString configFilePath = cmdLineInfo.configFilePath();
    MainConfig::ParsedInfo mainConfig = MainConfig::parse(configFilePath);
    globals.setMainConfig(& mainConfig);
    qDebug() << "plugin directories:\n - " + mainConfig.pluginDirectories().join( "\n - " );

    // initialize plugin manager
    globals.setPluginManager(std::make_shared<PluginManager>());
    auto pm = globals.pluginManager();

    // tell plugin manager where to find plugins
    pm -> setPluginSearchPaths(globals.mainConfig()->pluginDirectories());

    // find and load plugins
    pm -> loadPlugins();

    qDebug() << "Loading plugins...";
    auto infoList = pm -> getInfoList();

    qDebug() << "List of loaded plugins: [" << infoList.size() << "]";
    for (const auto & entry : infoList) {
        qDebug() << "  path:" << entry.json.name;
    }
    
    // tell all plugins that the core has initialized
    pm -> prepare<Carta::Lib::Hooks::Initialize>().executeAll();
    
    auto imgRes = pm -> prepare<Carta::Lib::Hooks::LoadAstroImage>(cmdLineInfo.fileList()[0]).first();

    if (imgRes.isNull()) {
        throw "Could not find any plugin to load astroImage";
    }

    std::shared_ptr<Carta::Lib::Image::ImageInterface> astroImage = imgRes.val();

    if (!astroImage) {
        throw "Image read was a nullptr";
    }

    // extract the raw data
    std::vector<double> rawdata = extractRawData(astroImage);

    // Verify that the version of the library that we linked against is
    // compatible with the version of the headers we compiled against.
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    CartaMessage::DataBook DataBook;

    char const *test = "DataBook.dat";
    /*{
      // Read the existing databook.
      fstream input(test, ios::in | ios::binary);
      if (!input) {
        cout << test << ": File not found.  Creating a new file." << endl;
      } else if (!DataBook.ParseFromIstream(&input)) {
        cerr << "Failed to parse databook." << endl;
        return -1;
      }
    }*/

    // Add an databook.
    PromptForRasterImageData(DataBook.add_raster_image(), rawdata);

    {
      // Write the new databook book back to disk.
      fstream output(test, ios::out | ios::trunc | ios::binary);
      if (!DataBook.SerializeToOstream(&output)) {
        cerr << "Failed to write databook book." << endl;
        return -1;
      }
    }

    // list the DataBook
    ListData(DataBook);

    // Optional:  Delete all global objects allocated by libprotobuf.
    google::protobuf::ShutdownProtobufLibrary();

    return 0;
} // coreMainCPP

} // namespace testProtoBuff

int main(int argc, char* argv[]) {
    try {
        return testProtoBuff::coreMainCPP( "testProtoBuff", argc, argv );
    }
    catch ( const char * err ) {
        qCritical() << "Exception(char*):" << err;
    }
    catch ( const std::string & err ) {
        qCritical() << "Exception(std::string &):" << err.c_str();
    }
    catch ( const QString & err ) {
        qCritical() << "Exception(QString &):" << err;
    }
    catch ( ... ) {
        qCritical() << "Exception(unknown type)!";
    }
    qFatal( "%s", "...caught in main()" );
    return - 1;
} // main
