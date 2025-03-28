/**
 * @file    test_mdReceive.c
 * @brief   TRDP MD receiving application
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(POSIX)
#include <unistd.h>
#elif defined(WIN32) || defined(WIN64)
#include "getopt.h"
#endif
#include "../../SDTv2/api/sdt_api.h"
#include "trdp_if_light.h"
#include "vos_thread.h"
#include "vos_sock.h"
#include "vos_utils.h"

/* Constants */
#define APP_VERSION         "1.5"
#define DATA_MAX            1000u
#define DEFAULT_COMID       1001u
#define RESERVED_MEMORY     2000000u
#define MAX_IF              10u
#define POLL_INTERVAL_US    100000u  /* 100ms */

/* Data Structures */
typedef struct {
    BOOL8               responder;
    BOOL8               confirmRequested;
    BOOL8               sdt;
    UINT32              comId;
    TRDP_APP_SESSION_T  appHandle;
    TRDP_LIS_T          listenUDP;
    TRDP_LIS_T          listenTCP;
    BOOL8               blockingMode;
    UINT32              ownIP;
} AppContext;

/* Function Prototypes */
static void printDebug(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime,
                      const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr);
static void printUsage(const char *appName);
static UINT32 parseIP(const char *ipStr);
static TRDP_ERR_T initializeTRDP(AppContext *context);
static TRDP_ERR_T setupListeners(AppContext *context);
static int processCommandLine(AppContext *context, int argc, char *argv[]);
static void mdCallback(void *pRefCon, TRDP_APP_SESSION_T appHandle,
                      const TRDP_MD_INFO_T *pMsg, UINT8 *pData, UINT32 dataSize);
static void mainLoop(AppContext *context);
static void cleanup(AppContext *context);

/* Debug Output Callback */
static void printDebug(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime,
                      const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr) {
    static const char *catStr[] = {"**Error:", "Warning:", "   Info:", "  Debug:", "   User:"};
    if (category != VOS_LOG_DBG && (category != VOS_LOG_INFO || strstr(pFile, "vos_sock.c") == NULL)) {
        printf("%s %s %s", strrchr(pTime, '-') + 1, catStr[category], pMsgStr);
    }
}

/* Usage Information */
static void printUsage(const char *appName) {
    printf("%s: Version %s (%s - %s)\n", appName, APP_VERSION, __DATE__, __TIME__);
    printf("Usage of %s\n", appName);
    printf("Receives and responds to MD messages with following arguments:\n"
           "-o <own IP>       : Local IP address\n"
           "-s                : SDTv2\n"
           "-c                : Respond with confirmation\n"
           "-b <0|1>          : Blocking mode (default: 1)\n"
           "-v                : Print version and quit\n");
}

/* IP Address Parser */
static UINT32 parseIP(const char *ipStr) {
    unsigned int ip[4];
    if (sscanf(ipStr, "%u.%u.%u.%u", &ip[3], &ip[2], &ip[1], &ip[0]) != 4) {
        return 0;
    }
    return (ip[3] << 24) | (ip[2] << 16) | (ip[1] << 8) | ip[0];
}

/* TRDP Initialization */
static TRDP_ERR_T initializeTRDP(AppContext *context) {
    TRDP_MEM_CONFIG_T dynamicConfig = {NULL, RESERVED_MEMORY, {0}};
    TRDP_MD_CONFIG_T mdConfig = {mdCallback, context, {0u, 64u, 0u, 0, 0u},
                                TRDP_FLAGS_CALLBACK, 1000000u, 1000000u, 1000000u,
                                1000000u, 17225u, 17225u, 10};
    TRDP_PROCESS_CONFIG_T processConfig = {"MD_Receiver", "", "", 0, 0,
                                         context->blockingMode ? TRDP_OPTION_BLOCK : 0};

    TRDP_ERR_T err = tlc_init(printDebug, NULL, &dynamicConfig);
    if (err != TRDP_NO_ERR) return err;

    return tlc_openSession(&context->appHandle, context->ownIP, 0,
                          NULL, NULL, &mdConfig, &processConfig);
}

/* Setup Listeners */
static TRDP_ERR_T setupListeners(AppContext *context) {
    TRDP_ERR_T err;
    
    err = tlm_addListener(context->appHandle, &context->listenUDP, context,
                         mdCallback, TRUE, context->comId, 0u, 0u,
                         VOS_INADDR_ANY, VOS_INADDR_ANY, 0,
                         TRDP_FLAGS_CALLBACK, NULL, NULL);
    if (err != TRDP_NO_ERR) return err;

    err = tlm_addListener(context->appHandle, &context->listenTCP, context,
                         mdCallback, TRUE, context->comId, 0u, 0u,
                         VOS_INADDR_ANY, VOS_INADDR_ANY, 0,
                         TRDP_FLAGS_TCP | TRDP_FLAGS_CALLBACK, NULL, NULL);
    return err;
}

/* Command Line Parser */
static int processCommandLine(AppContext *context, int argc, char *argv[]) {
    context->responder = TRUE;
    context->comId = DEFAULT_COMID;
    context->blockingMode = TRUE;

    int ch;
    while ((ch = getopt(argc, argv, "o:b:cvhs?")) != -1) {
        switch (ch) {
            case 'o': context->ownIP = parseIP(optarg); break;
            case 'c': context->confirmRequested = TRUE; break;
            case 'b': context->blockingMode = atoi(optarg); break;
            case 's': context->sdt = TRUE; break;
            case 'v':
                printf("%s: Version %s (%s - %s)\n", argv[0], APP_VERSION, __DATE__, __TIME__);
                exit(0);
            default: printUsage(argv[0]); return 1;
        }
    }
    return 0;
}

/* SDTv2 Handle */
static void addSDTInfo(UINT8 *data, UINT32 *data_size) {
    // input parameters
    uint32_t sid = 0x12345678U;
    uint16_t ver = 2; // SDT version
    unsigned int ssc = 0xFFFFFFFF; // SSC is fixed for MD

    sdt_result_t result;
    UINT16 len = *data_size;
    UINT16 padding = (4 - len % 4) + 16;
    *data_size = len + padding;
    memset(data + len, 0, padding);

    result = sdt_ipt_secure_pd(data, 
                            *data_size, 
                            sid, 
                            ver, 
                            &ssc);
    if (result != SDT_OK) {
        fprintf(stderr, "sdt_ipt_secure_pd() failed with %d\n", result);
    }
}

#define RESULTS(NAME)	case NAME: return #NAME;
#define DEF_UNKNOWN()	default:   return "UNKNOWN";   

const char* result_string(sdt_result_t r)
{
    switch (r)
    {
        RESULTS(SDT_OK)
        RESULTS(SDT_ERR_SIZE)
        RESULTS(SDT_ERR_VERSION)
        RESULTS(SDT_ERR_HANDLE)
        RESULTS(SDT_ERR_CRC)
        RESULTS(SDT_ERR_DUP)
        RESULTS(SDT_ERR_LOSS)
        RESULTS(SDT_ERR_SID)
        RESULTS(SDT_ERR_PARAM)
        RESULTS(SDT_ERR_REDUNDANCY)
        RESULTS(SDT_ERR_SYS)
        RESULTS(SDT_ERR_LTM)
        RESULTS(SDT_ERR_INIT)
        RESULTS(SDT_ERR_CMTHR)
        DEF_UNKNOWN()       
    }
}

static void validateSDTMessage(UINT8 *data, UINT32 data_size) {
    // handler fix
    sdt_handle_t hnd;

    // validateor input
    uint32_t      sid1 = 0x12345678U; // TODO: add sid counting process
    uint32_t      sid2 = 0;
    uint8_t       sid2red = 0;
    uint16_t      ver = 2; // SDT version

    // result parameters
    sdt_result_t        result;
    sdt_result_t        sdt_error;

    sdt_get_validator(SDT_IPT,  sid1, sid2, sid2red, ver, &hnd);

    result = sdt_validate_md(hnd, data, data_size);
    sdt_get_errno(hnd, &sdt_error);
    printf("sdt_validate_md errno=%s\n", result_string(sdt_error));
    printf("SDT result %i\n",result);
}

/* MD Callback */
static void mdCallback(void *pRefCon, TRDP_APP_SESSION_T appHandle,
                      const TRDP_MD_INFO_T *pMsg, UINT8 *pData, UINT32 dataSize) {
    AppContext *context = (AppContext *)pRefCon;
    UINT8 data[DATA_MAX];
    UINT32 dataSize2 = 0;
    TRDP_ERR_T err;

    switch (pMsg->resultCode) {
        case TRDP_NO_ERR:
            switch (pMsg->msgType) {
                case TRDP_MSG_MN:
                    vos_printLog(VOS_LOG_USR, "<- MD Notification %u\n", pMsg->comId);
                    if (context->sdt) {
                        validateSDTMessage(pData, dataSize);
                    }
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    break;
                case TRDP_MSG_MR:
                    vos_printLog(VOS_LOG_USR, "<- MR Request with reply %u\n", pMsg->comId);
                    if (context->sdt) {
                        validateSDTMessage(pData, dataSize);
                    }
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    
                    if (context->confirmRequested) {
                        vos_printLogStr(VOS_LOG_USR, "-> sending reply with query\n");
                        strncpy((char*)data, "I'm fine, how are you?", DATA_MAX - 1);
                        dataSize2 = strlen((const char *)data) + 1;

                        if (context->sdt) {
                            addSDTInfo(data, &dataSize2);
                        }

                        err = tlm_replyQuery(appHandle, &pMsg->sessionId, pMsg->comId, 0u,
                                           10000000u, NULL, data, dataSize2,
                                           "test_mdReceive");
                    } else {
                        vos_printLogStr(VOS_LOG_USR, "-> sending reply\n");
                        strncpy((char*)data, "I'm fine, thanx!", DATA_MAX - 1);
                        dataSize2 = strlen((const char *)data) + 1;
                        if (context->sdt) {
                            addSDTInfo(data, &dataSize2);
                        }
                        err = tlm_reply(appHandle, &pMsg->sessionId, pMsg->comId, 0,
                                      NULL, data, dataSize2, "test_mdReceive");
                    }
                    if (err != TRDP_NO_ERR)
                        vos_printLog(VOS_LOG_USR, "tlm_reply/Query returned error %d\n", err);
                    break;
            }
            break;
        case TRDP_TIMEOUT_ERR:
            vos_printLog(VOS_LOG_USR, "### Packet timed out (ComID %d, SrcIP: %s)\n",
                        pMsg->comId, vos_ipDotted(pMsg->srcIpAddr));
            break;
        default:
            vos_printLog(VOS_LOG_USR, "### Error on packet received (ComID %d), err = %d\n",
                        pMsg->comId, pMsg->resultCode);
    }
}

/* Main Processing Loop */
static void mainLoop(AppContext *context) {
    while (1) {
        if (context->blockingMode) {
            TRDP_TIME_T tv = {0, 0};
            TRDP_TIME_T max_tv = {0, POLL_INTERVAL_US};
            fd_set rfds;
            TRDP_SOCK_T noDesc = 0;

            FD_ZERO(&rfds);
            tlm_getInterval(context->appHandle, &tv, (TRDP_FDS_T *)&rfds, &noDesc);
            if (vos_cmpTime(&tv, &max_tv) > 0) tv = max_tv;
            vos_select((int)noDesc, &rfds, NULL, NULL, &tv);
            tlm_process(context->appHandle, (TRDP_FDS_T *)&rfds, NULL);
        } else {
            vos_threadDelay(POLL_INTERVAL_US);
            tlm_process(context->appHandle, NULL, NULL);
        }
    }
}

/* Cleanup */
static void cleanup(AppContext *context) {
    tlm_delListener(context->appHandle, context->listenUDP);
    tlm_delListener(context->appHandle, context->listenTCP);
    tlc_closeSession(context->appHandle);
    tlc_terminate();
}

/* Main Entry Point */
int main(int argc, char *argv[]) {
    AppContext context = {0};

    if (argc <= 1) {
        printUsage(argv[0]);
        return 1;
    }

    if (processCommandLine(&context, argc, argv) != 0) return 1;
    printf("%s: Version %s (%s - %s)\n", argv[0], APP_VERSION, __DATE__, __TIME__);

    TRDP_ERR_T err = initializeTRDP(&context);
    if (err != TRDP_NO_ERR) {
        fprintf(stderr, "TRDP initialization failed\n");
        return 1;
    }

    err = setupListeners(&context);
    if (err != TRDP_NO_ERR) {
        vos_printLogStr(VOS_LOG_ERROR, "Listener setup failed\n");
        cleanup(&context);
        return 1;
    }

    mainLoop(&context);
    cleanup(&context);
    return 0;
}