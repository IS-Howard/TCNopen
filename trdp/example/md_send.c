/**
 * @file    test_mdSend.c
 * @brief   TRDP MD sending application
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
#define DEFAULT_TIMEOUT     2000000u  /* 2 seconds in microseconds */
#define BUFFER_SIZE         (64 * 1024)
#define POLL_INTERVAL_US    100000u   /* 100ms */

/* Static Data */
static const UINT8 DEMO_DATA[] = 
    "Far out in the uncharted backwaters of the unfashionable end of the western spiral arm of the Galaxy lies a small unregarded yellow sun. Orbiting this at a distance of roughly ninety-two million miles is an utterly insignificant little blue green planet whose ape-descended life forms are so amazingly primitive that they still think digital watches are a pretty neat idea.\n";

/* Data Structures */
typedef struct {
    BOOL8               notifyOnly;
    BOOL8               onlyOnce;
    BOOL8               noData;
    BOOL8               loop;
    BOOL8               sdt;
    UINT32              comId;
    TRDP_APP_SESSION_T  appHandle;
    BOOL8               blockingMode;
    UINT32              dataSize;
    UINT32              ownIP;
    UINT32              destIP;
    UINT32              expReplies;
    UINT32              timeout;
    TRDP_FLAGS_T        flags;
    UINT8               buffer[BUFFER_SIZE];
} AppContext;

/* Function Prototypes */
static void printDebug(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime,
                      const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr);
static void printUsage(const char *appName);
static UINT32 parseIP(const char *ipStr);
static TRDP_ERR_T initializeTRDP(AppContext *context);
static int processCommandLine(AppContext *context, int argc, char *argv[]);
static void mdCallback(void *pRefCon, TRDP_APP_SESSION_T appHandle,
                      const TRDP_MD_INFO_T *pMsg, UINT8 *pData, UINT32 dataSize);
static TRDP_ERR_T sendMessage(AppContext *context);
static void processResponses(AppContext *context);
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
    printf("Sends MD messages with following arguments:\n"
           "-o <own IP>       : Source IP address\n"
           "-t <target IP>    : Destination IP address (required)\n"
           "-p <TCP|UDP>      : Protocol (default: UDP)\n"
           "-d <timeout>      : Timeout in us (default: %u)\n"
           "-e <replies>      : Expected replies (default: 1)\n"
           "-n                : Notify only\n"
           "-l <size>         : Send large message (up to 65420 Bytes)\n"
           "-0                : Send no data\n"
           "-1                : Send only one request\n"
           "-b <0|1>          : Blocking mode (default: 1)\n"
           "-s                : SDTv2\n"
           "-v                : Print version and quit\n",
           DEFAULT_TIMEOUT);
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
    TRDP_PROCESS_CONFIG_T processConfig = {"MD_Sender", "", "", 0, 0,
                                         context->blockingMode ? TRDP_OPTION_BLOCK : 0};

    TRDP_ERR_T err = tlc_init(printDebug, NULL, &dynamicConfig);
    if (err != TRDP_NO_ERR) return err;

    return tlc_openSession(&context->appHandle, context->ownIP, 0,
                          NULL, NULL, &mdConfig, &processConfig);
}

/* Command Line Parser */
static int processCommandLine(AppContext *context, int argc, char *argv[]) {
    context->comId = DEFAULT_COMID;
    context->loop = TRUE;
    context->blockingMode = TRUE;
    context->timeout = DEFAULT_TIMEOUT;
    context->expReplies = 1;
    context->flags = TRDP_FLAGS_CALLBACK;

    int ch;
    while ((ch = getopt(argc, argv, "t:o:p:d:l:e:b:n01vhs?")) != -1) {
        switch (ch) {
            case 'o': context->ownIP = parseIP(optarg); break;
            case 't': context->destIP = parseIP(optarg); break;
            case 'p':
                if (strcmp(optarg, "TCP") == 0) context->flags |= TRDP_FLAGS_TCP;
                else if (strcmp(optarg, "UDP") != 0) return 1;
                break;
            case 'd': context->timeout = atoi(optarg); break;
            case 'l': context->dataSize = atoi(optarg); break;
            case 'e': context->expReplies = atoi(optarg); break;
            case 'n': context->notifyOnly = TRUE; break;
            case '0': context->noData = TRUE; break;
            case 's': context->sdt = TRUE; break;
            case '1': context->onlyOnce = TRUE; context->loop = FALSE; break;
            case 'b': context->blockingMode = atoi(optarg); break;
            case 'v':
                printf("%s: Version %s (%s - %s)\n", argv[0], APP_VERSION, __DATE__, __TIME__);
                exit(0);
            default: printUsage(argv[0]); return 1;
        }
    }
    if (context->destIP == 0) {
        fprintf(stderr, "Destination IP address required\n");
        return 1;
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

    switch (pMsg->resultCode) {
        case TRDP_NO_ERR:
            switch (pMsg->msgType) {
                case TRDP_MSG_MP:
                    vos_printLog(VOS_LOG_USR, "<- MR Reply received %u\n", pMsg->comId);
                    vos_printLog(VOS_LOG_USR, "   from userURI: %.32s\n", pMsg->srcUserURI);
                    if (context->sdt) {
                        validateSDTMessage(pData, dataSize);
                    }
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    context->loop = FALSE;
                    break;
                case TRDP_MSG_MQ:
                    vos_printLog(VOS_LOG_USR, "<- MR Reply with confirmation received %u\n", pMsg->comId);
                    vos_printLog(VOS_LOG_USR, "   from userURI: %.32s\n", pMsg->srcUserURI);
                    if (context->sdt) {
                        validateSDTMessage(pData, dataSize);
                    }
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    vos_printLogStr(VOS_LOG_USR, "-> sending confirmation\n");
                    if (tlm_confirm(appHandle, (const TRDP_UUID_T *)&pMsg->sessionId, 0, NULL) != TRDP_NO_ERR)
                        vos_printLogStr(VOS_LOG_USR, "tlm_confirm failed\n");
                    context->loop = FALSE;
                    break;
                case TRDP_MSG_MC:
                    vos_printLog(VOS_LOG_USR, "<- MR Confirmation received %u\n", pMsg->comId);
                    context->loop = FALSE;
                    break;
                case TRDP_MSG_ME:
                    vos_printLog(VOS_LOG_USR, "<- ME received %u\n", pMsg->comId);
                    context->loop = FALSE;
                    break;
            }
            break;
        case TRDP_REPLYTO_ERR:
        case TRDP_CONFIRMTO_ERR:
        case TRDP_REQCONFIRMTO_ERR:
            vos_printLog(VOS_LOG_USR, "### Timeout for ComID %d, destIP: %s\n",
                        pMsg->comId, vos_ipDotted(pMsg->destIpAddr));
            context->loop = FALSE;
            break;
        default:
            vos_printLog(VOS_LOG_USR, "### Error on packet received (ComID %d), err = %d\n",
                        pMsg->comId, pMsg->resultCode);
            context->loop = FALSE;
    }
}

/* Send MD Message */
static TRDP_ERR_T sendMessage(AppContext *context) {
    TRDP_ERR_T err;
    TRDP_UUID_T sessionId;
    UINT8 data[DATA_MAX];
    UINT32 dataSize = 0;

    if (!context->noData) {
        if (context->dataSize > 0) {
            for (UINT32 i = 0, j = 0; i < context->dataSize; i++) {
                context->buffer[i] = DEMO_DATA[j++];
                if (j >= sizeof(DEMO_DATA)) j = 0;
            }
            memcpy(data, context->buffer, DATA_MAX);
            dataSize = context->dataSize;
        } else {
            strncpy((char*)data, context->notifyOnly ? "Hello, World" : "How are you?", DATA_MAX - 1);
            dataSize = strlen((const char *)data) + 1;
        }
    }

    if (context->sdt) {
        addSDTInfo(data, &dataSize);
    }

    if (context->notifyOnly) {
        vos_printLog(VOS_LOG_USR, "-> sending MR Notification %u\n", context->comId);
        err = tlm_notify(context->appHandle, context, mdCallback, context->comId,
                        0, 0, context->ownIP, context->destIP, context->flags,
                        NULL, data, dataSize, 0, 0);
    } else {
        vos_printLog(VOS_LOG_USR, "-> sending MR Request with reply %u\n", context->comId);
        err = tlm_request(context->appHandle, context, mdCallback, &sessionId,
                         context->comId, 0, 0, context->ownIP, context->destIP,
                         context->flags, context->expReplies, context->timeout,
                         NULL, data, dataSize, NULL, NULL);
    }
    return err;
}

/* Process Responses */
static void processResponses(AppContext *context) {
    if (context->notifyOnly) return;

    vos_printLogStr(VOS_LOG_USR, "waiting for an answer...\n");
    while (context->loop) {
        if (context->blockingMode) {
            TRDP_TIME_T tv = {0, POLL_INTERVAL_US};
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
    tlm_process(context->appHandle, NULL, NULL);
    vos_printLogStr(VOS_LOG_USR, "-> finishing.\n");
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

    err = sendMessage(&context);
    if (err != TRDP_NO_ERR) {
        vos_printLog(VOS_LOG_USR, "Message send failed (err = %d)\n", err);
        cleanup(&context);
        return 1;
    }

    processResponses(&context);
    cleanup(&context);
    return 0;
}