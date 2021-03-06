#include <iostream>
#include <MCoreApplication>

#include "mrtmpserver.hpp"
#include "BlsConf.hpp"
#include "mrtmpplayer.hpp"
#include "BlsMasterChannel.hpp"
#include "BlsChildChannel.hpp"
#include "BlsUtils.hpp"
#include "BlsStatistics.hpp"
#include "BlsHttpService.hpp"

#include <unistd.h>
#include <getopt.h>

#include "../../config.h"

#include "BlsRtmpPublisher.hpp"

static int childRun(MCoreApplication &app);
static void printVersion();
static void printHelp(int argc, char *argv[]);
static int getOpt(int argc ,char* argv[]);

static MString gs_confPath;
#if 1
int main(int argc, char *argv[])
{
    if (getOpt(argc, argv) != E_SUCCESS) {
        return -1;
    }

    if (gs_confPath.empty()) {
        printf("you must specify conf file.\n");
        return -1;
    }

    MCoreApplication app(argc, argv);

    BlsConf::instance(gs_confPath);

    // create back source
    vector<BlsHostInfo> back_source_infos = BlsConf::instance()->getBackSourceInfo();
    for (int i = 0; i < back_source_infos.size(); ++i) {
        BlsHostInfo &info = back_source_infos.at(i);

        pid_t pid = fork();
        if (pid > 0) {
            continue;
        } else if (pid == 0) {
            MRtmpServer *s = new MRtmpServer;
            if (!s->listen(info.addr, info.port)) {
                log_error("listen %s:%d failed.", info.addr.c_str(), info.port);
                return -1;
            }
            MString appName = MString().sprintf("bls_backsource:%d", info.port);
            app.setProcTitle(appName);

            log_trace("Bls(%d) back source listen %s:%d success.", (int)getpid(), info.addr.c_str(), info.port);
            BlsConf::instance()->m_processRole = Process_Role_BackSource;

            return app.exec();
        } else {
            log_error("fork error");
        }
    }

    // master process
    vector<MRtmpServer *> rtmpServer;
    vector<BlsHostInfo> infos = BlsConf::instance()->getRtmpListenInfo();
    for (int i = 0; i < infos.size(); ++i) {
        BlsHostInfo &info = infos.at(i);
        MRtmpServer *s = new MRtmpServer;
        if (!s->listen(info.addr, info.port)) {
            log_error("listen %s:%d failed.", info.addr.c_str(), info.port);
            return -1;
        }

        log_trace("Bls(%d) Rtmp Server listen %s:%d success.", (int)getpid(), info.addr.c_str(), info.port);

        rtmpServer.push_back(s);
    }

    // http service
    vector<BlsHttpService *> httpServers;
    vector<BlsHostInfo> httpInfos = BlsConf::instance()->getHttpLiveFlvListenInfo();

    for (int i = 0; i < httpInfos.size(); ++i) {
        BlsHostInfo &info = httpInfos.at(i);

        BlsHttpService *httpService = new BlsHttpService;
        if (!httpService->listen(info.addr, info.port)) {
            log_error("listen %s:%d failed.", info.addr.c_str(), info.port);
            return -1;
        }
        log_trace("Bls(%d) Http Server listen %s:%d success.", (int)getpid(), info.addr.c_str(), info.port);

        httpServers.push_back(httpService);
    }

    int childProcessCount = BlsConf::instance()->getWorkerCount();
    for (int i = 0; i < childProcessCount; ++i) {
        pid_t pid = fork();

        if (pid > 0) {
            continue;
        } else if (pid == 0) {
            MString appName = "bls_worker";
            app.setProcTitle(appName);

           return childRun(app);
        }
    }

    // crate master channle
//    BlsMasterChannel masterChannel;
//    if (!masterChannel.listen("127.0.0.1", 1940)) {
//        log_error("BlsMasterChannel listen failed.");
//        return -1;
//    }

    BlsConf::instance()->m_processRole = Process_Role_Master;
    app.setProcTitle("bls_master");

    // close rtmp channel in master
    for (int i = 0; i < rtmpServer.size(); ++i) {
        MRtmpServer *s = rtmpServer.at(i);
        s->close();
        // TODO check if we delete it ,app will crash.
        //s->deleteLater();
    }

    // close http service in master
    for (unsigned int i = 0; i < httpServers.size(); ++i) {
        BlsHttpService *server = httpServers.at(i);
        server->close();
    }

    int ret = app.exec();

    return ret;
}
#else
int main(int argc, char *argv[])
{
    MCoreApplication app(argc, argv);

    BlsRtmpPublisher publisher;
    publisher.setUrl("rtmp://ble.ossrs.net/live/ble");
    publisher.setHost("127.0.0.1", 1935);
    publisher.start();

    return app.exec();
}
#endif

int childRun(MCoreApplication &app)
{
//    BlsChildChannel childChannel;
//    if (childChannel.start() != E_SUCCESS) {
//        return -1;
//    }

    BlsConf::instance()->m_processRole = Process_Role_Child;

    return app.exec();
}

void printVersion()
{
    printf("%s\n", BLS_VERSION);
}

void printHelp(int argc, char *argv[])
{
    M_UNUSED(argc);

    char buf[2048];
    sprintf(buf, "Usage: %s [OPTION]... [FILE]...\n"
            "A Common Media Server for streaming rtmp, http flv live, HDS, HLS and so on.\n"
            "\n"
            "Mandatory arguments to long options are mandatory for short options too.\n"
            "  -f, --file           the configure file\n"
            "  -h, --help           display this help and exit\n"
            "  -v, --version        display version string and exit\n"
            "\n"
            "Exit status:\n"
            " 0     if OK,\n"
            " -1    if serious trouble\n"
            "\n"
            "Report %s bugs to 740936897@qq.com\n"
            ,argv[0]
            ,argv[0]);

    printf("%s", buf);
}

static struct option longOptions[] =
{
    {"file",        required_argument,  0, 'f'},
    {"help",        no_argument,        0, 'h'},
    {"version",     no_argument,        0, 'v'},
};

static const char* shortOptions = "f:hv";

int getOpt(int argc ,char* argv[])
{
    int c = -1;
    while ((c = getopt_long (argc, argv, shortOptions, longOptions, NULL)) != -1) {
        switch (c) {
        case 'f':
            if (optarg) {
                gs_confPath = optarg;
            }
            break;
        case 'v':
            printVersion();
            _exit(0);
        case 'h':
            printHelp(argc, argv);
            _exit(0);
        default:
            printf("see --help or -h\n");
            _exit(0);
        }
    }

    return E_SUCCESS;
}
