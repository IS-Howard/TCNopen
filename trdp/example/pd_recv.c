/**
 * @file    pd_receive.c
 * @brief   TRDP PD receive application
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(POSIX)
#include <unistd.h>
#elif defined(WIN32) || defined(WIN64)
#include "getopt.h"
#endif

#include "trdp_if_light.h"
#include "tau_marshall.h"
#include "vos_utils.h"

/* Constants */
#define APP_VERSION         "1.4"
#define DATA_MAX            1432u
#define DEFAULT_COMID       0u
#define DEFAULT_CYCLE_TIME  1000000u    /* 1 second in microseconds */
#define RESERVED_MEMORY     1000000u
#define MAX_TIMEOUT_SEC     1u          /* 1 second */
#define BUFFER_SIZE         32u

/* Data Structures */
typedef struct {
    TRDP_APP_SESSION_T  appHandle;
    TRDP_SUB_T          subHandle;
    UINT32              comId;
    UINT32              ownIP;
    UINT32              dstIP;
    UINT8               buffer[BUFFER_SIZE];
} AppContext;

/* Function Prototypes */
static void printDebug(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime,
                      const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr);
static void printUsage(const char *appName);
static UINT32 parseIP(const char *ipStr);
static TRDP_ERR_T initializeTRDP(AppContext *context);
static TRDP_ERR_T setupSubscriber(AppContext *context);
static int processCommandLine(AppContext *context, int argc, char *argv[]);
static void mainLoop(AppContext *context);
static void cleanup(AppContext *context);
static void printReceivedData(const TRDP_PD_INFO_T *pdInfo, const UINT8 *data, UINT32 size);

/* Debug Output Callback */
static void printDebug(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime,
                      const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr) {
    static const char *catStr[] = {"**Error:", "Warning:", "   Info:", "  Debug:", "   User:"};
    CHAR8 *pF = strrchr(pFile, VOS_DIR_SEP);
    
    if (category != VOS_LOG_DBG) {
        printf("%s %s %s:%d %s",
               pTime,
               catStr[category],
               (pF == NULL) ? "" : pF + 1,
               LineNumber,
               pMsgStr);
    }
}

/* Usage Information */
static void printUsage(const char *appName) {
    printf("Usage of %s\n", appName);
    printf("Receives PD messages from an ED with following arguments:\n"
           "-o <own IP>       : Local IP address (default: default interface)\n"
           "-m <multicast IP> : Multicast group IP (default: none)\n"
           "-c <comId>        : Communication ID (default: %u)\n"
           "-v                : Print version and quit\n",
           DEFAULT_COMID);
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
    TRDP_PD_CONFIG_T pdConfig = {NULL, NULL, TRDP_PD_DEFAULT_SEND_PARAM,
                                TRDP_FLAGS_NONE, DEFAULT_CYCLE_TIME,
                                TRDP_TO_SET_TO_ZERO, 0u};
    TRDP_PROCESS_CONFIG_T processConfig = {"PD_Receiver", "", "",
                                         TRDP_PROCESS_DEFAULT_CYCLE_TIME,
                                         0, TRDP_OPTION_NONE};

    TRDP_ERR_T err = tlc_init(printDebug, NULL, &dynamicConfig);
    if (err != TRDP_NO_ERR) return err;

    return tlc_openSession(&context->appHandle, context->ownIP, 0,
                          NULL, &pdConfig, NULL, &processConfig);
}

/* Subscriber Setup */
static TRDP_ERR_T setupSubscriber(AppContext *context) {
    TRDP_ERR_T err = tlp_subscribe(context->appHandle, &context->subHandle,
                                 NULL, NULL, 0u, context->comId,
                                 0u, 0u, VOS_INADDR_ANY, VOS_INADDR_ANY,
                                 context->dstIP, TRDP_FLAGS_DEFAULT,
                                 NULL, DEFAULT_CYCLE_TIME * 3,
                                 TRDP_TO_SET_TO_ZERO);

    if (err != TRDP_NO_ERR) return err;
    return tlc_updateSession(context->appHandle);
}

/* Command Line Parser */
static int processCommandLine(AppContext *context, int argc, char *argv[]) {
    context->comId = DEFAULT_COMID;
    memset(context->buffer, 0, BUFFER_SIZE);

    int ch;
    while ((ch = getopt(argc, argv, "o:m:h?vc:")) != -1) {
        switch (ch) {
            case 'o': context->ownIP = parseIP(optarg); break;
            case 'm': context->dstIP = parseIP(optarg); break;
            case 'c': context->comId = atoi(optarg); break;
            case 'v':
                printf("%s: Version %s (%s - %s)\n",
                       argv[0], APP_VERSION, __DATE__, __TIME__);
                exit(0);
            default: printUsage(argv[0]); return 1;
        }
    }
    return 0;
}

/* Data Printing Helper */
static void printReceivedData(const TRDP_PD_INFO_T *pdInfo, const UINT8 *data, UINT32 size) {
    vos_printLogStr(VOS_LOG_USR, "\nMessage received:\n");
    vos_printLog(VOS_LOG_USR, "Type = %c%c, ", pdInfo->msgType >> 8, pdInfo->msgType & 0xFF);
    vos_printLog(VOS_LOG_USR, "Seq  = %u, ", pdInfo->seqCount);
    
    if (size > 0) {
        vos_printLog(VOS_LOG_USR, "with %d Bytes:\n", size);
        vos_printLog(VOS_LOG_USR, "   %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx\n",
                    data[0], data[1], data[2], data[3],
                    data[4], data[5], data[6], data[7]);
        vos_printLog(VOS_LOG_USR, "   %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx\n",
                    data[8], data[9], data[10], data[11],
                    data[12], data[13], data[14], data[15]);
        vos_printLog(VOS_LOG_USR, "%s\n", data);
    } else {
        vos_printLog(VOS_LOG_USR, "\n");
    }
}

/* Main Processing Loop */
static void mainLoop(AppContext *context) {
    while (1) {
        TRDP_FDS_T rfds;
        INT32 noDesc;
        TRDP_TIME_T tv = {0, 0};
        const TRDP_TIME_T max_tv = {MAX_TIMEOUT_SEC, 0};
        const TRDP_TIME_T min_tv = {0, TRDP_PROCESS_DEFAULT_CYCLE_TIME};
        
        FD_ZERO(&rfds);
        tlc_getInterval(context->appHandle, &tv, &rfds, &noDesc);
        
        if (vos_cmpTime(&tv, &max_tv) > 0) tv = max_tv;
        if (vos_cmpTime(&tv, &min_tv) < 0) tv = min_tv;

        INT32 rv = vos_select(noDesc, &rfds, NULL, NULL, &tv);
        tlc_process(context->appHandle, &rfds, &rv);

        if (rv > 0) {
            vos_printLogStr(VOS_LOG_USR, "Other descriptors ready\n");
        }

        TRDP_PD_INFO_T pdInfo;
        UINT32 receivedSize = BUFFER_SIZE;
        TRDP_ERR_T err = tlp_get(context->appHandle, context->subHandle,
                               &pdInfo, context->buffer, &receivedSize);

        switch (err) {
            case TRDP_NO_ERR:
                printReceivedData(&pdInfo, context->buffer, receivedSize);
                break;
            case TRDP_TIMEOUT_ERR:
                vos_printLogStr(VOS_LOG_INFO, "Packet timed out\n");
                break;
            case TRDP_NODATA_ERR:
                vos_printLogStr(VOS_LOG_INFO, "No data yet\n");
                break;
            default:
                vos_printLog(VOS_LOG_ERROR, "PD GET ERROR: %d\n", err);
        }
    }
}

/* Cleanup */
static void cleanup(AppContext *context) {
    tlp_unsubscribe(context->appHandle, context->subHandle);
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
    
    TRDP_ERR_T err = initializeTRDP(&context);
    if (err != TRDP_NO_ERR) {
        printf("TRDP initialization failed\n");
        return 1;
    }

    err = setupSubscriber(&context);
    if (err != TRDP_NO_ERR) {
        vos_printLogStr(VOS_LOG_ERROR, "Subscriber setup failed\n");
        cleanup(&context);
        return 1;
    }

    mainLoop(&context);
    cleanup(&context);
    return 0;
}