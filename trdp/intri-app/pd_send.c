/**
 * @file    pd_send.c
 * @brief   TRDP PD sending application
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
#include "vos_utils.h"

/* Constants */
#define APP_VERSION         "1.4"
#define DATA_MAX            800u
#define DEFAULT_COMID       0u
#define DEFAULT_CYCLE_TIME  1000000u    /* 1 second in microseconds */
#define RESERVED_MEMORY     160000u
#define MAX_TIMEOUT         1000000u    /* 1 second */
#define MIN_TIMEOUT         TRDP_PROCESS_DEFAULT_CYCLE_TIME

/* Data Structures */
typedef struct {
    TRDP_APP_SESSION_T  appHandle;
    TRDP_PUB_T          pubHandle;
    UINT32              comId;
    UINT32              interval;
    UINT32              ownIP;
    UINT32              destIP;
    BOOL8               sdt;
    UINT8*              outputBuffer;
    UINT32              outputBufferSize;
} AppContext;

/* Function Prototypes */
static void printDebug(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime,
                      const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr);
static void printUsage(const char *appName);
static UINT32 parseIP(const char *ipStr);
static TRDP_ERR_T initializeTRDP(AppContext *context);
static TRDP_ERR_T setupPublisher(AppContext *context);
static int processCommandLine(AppContext *context, int argc, char *argv[]);
static void mainLoop(AppContext *context);
static void cleanup(AppContext *context);

/* Debug Output Callback */
static void printDebug(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime,
                      const CHAR8 *pFile, UINT16 LineNumber, const CHAR8 *pMsgStr) {
    static const char *catStr[] = {"**Error:", "Warning:", "   Info:", "  Debug:", "   User:"};
    CHAR8 *pF = strrchr(pFile, VOS_DIR_SEP);
    printf("%s %s %s:%d %s",
           strrchr(pTime, '-') + 1,
           catStr[category],
           (pF == NULL) ? "" : pF + 1,
           LineNumber,
           pMsgStr);
}

/* Usage Information */
static void printUsage(const char *appName) {
    printf("Usage of %s\n", appName);
    printf("Sends PD messages to an ED with following arguments:\n"
           "-o <own IP>       : Source IP address (default: INADDR_ANY)\n"
           "-t <target IP>    : Destination IP address (required)\n"
           "-c <comId>        : Communication ID (default: %u)\n"
           "-p <cycle period> : Cycle period in us (default: %u)\n"
           "-s                : SDTv2\n"
           "-e                : Send empty request\n"
           "-d <string>       : Custom string to send (default: 'Hello World')\n"
           "-v                : Print version and quit\n",
           DEFAULT_COMID, DEFAULT_CYCLE_TIME);
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
                                TRDP_TO_SET_TO_ZERO, 0};
    TRDP_PROCESS_CONFIG_T processConfig = {"PD_Sender", "", "",
                                         TRDP_PROCESS_DEFAULT_CYCLE_TIME,
                                         0u, TRDP_OPTION_BLOCK};

    TRDP_ERR_T err = tlc_init(printDebug, NULL, &dynamicConfig);
    if (err != TRDP_NO_ERR) return err;

    return tlc_openSession(&context->appHandle, context->ownIP, 0,
                          NULL, &pdConfig, NULL, &processConfig);
}

/* Publisher Setup */
static TRDP_ERR_T setupPublisher(AppContext *context) {
    TRDP_ERR_T err = tlp_publish(context->appHandle, &context->pubHandle,
                                NULL, NULL, 0u, context->comId,
                                0u, 0u, context->ownIP, context->destIP,
                                context->interval, 0u, TRDP_FLAGS_NONE,
                                NULL, context->outputBuffer,
                                context->outputBufferSize);

    if (err != TRDP_NO_ERR) return err;
    return tlc_updateSession(context->appHandle);
}

/* Command Line Parser */
static int processCommandLine(AppContext *context, int argc, char *argv[]) {
    UINT8 defaultData[DATA_MAX] = "Hello World";
    context->outputBuffer = defaultData;
    context->outputBufferSize = strlen((char*)defaultData) + 1;
    context->comId = DEFAULT_COMID;
    context->interval = DEFAULT_CYCLE_TIME;

    int ch;
    while ((ch = getopt(argc, argv, "t:o:d:p:h?vec:s")) != -1) {
        switch (ch) {
            case 'o': context->ownIP = parseIP(optarg); break;
            case 't': context->destIP = parseIP(optarg); break;
            case 'c': context->comId = atoi(optarg); break;
            case 'p': context->interval = atoi(optarg); break;
            case 's': context->sdt = TRUE; break;
            case 'e': context->outputBuffer = NULL; context->outputBufferSize = 0; break;
            case 'd':
                if (strlen(optarg) >= DATA_MAX) {
                    fprintf(stderr, "Data too long\n");
                    return 1;
                }
                context->outputBuffer = (UINT8*)optarg;
                context->outputBufferSize = strlen(optarg) + 1;
                break;
            case 'v':
                printf("%s: Version %s (%s - %s)\n",
                       argv[0], APP_VERSION, __DATE__, __TIME__);
                exit(0);
            default: printUsage(argv[0]); return 1;
        }
    }
    if (context->destIP == 0) {
        fprintf(stderr, "Destination IP required\n");
        printUsage(argv[0]);
        return 1;
    }
    return 0;
}

/* SDTv2 Handle */
static void addSDTInfo(UINT8 *data, UINT32 *data_size, unsigned int *ssc) {
    // input parameters
    uint32_t sid = 0x12345678U; // TODO: add sid counting process
    uint16_t ver = 2; // SDT version

    sdt_result_t result;
    UINT16 len = *data_size;
    UINT16 padding = (4 - len % 4) + 16;
    *data_size = len + padding;
    memset(data + len, 0, padding);

    result = sdt_ipt_secure_pd(data, 
                            *data_size, 
                            sid, 
                            ver, 
                            ssc);
    if (result != SDT_OK) {
        fprintf(stderr, "sdt_ipt_secure_pd() failed with %d\n", result);
    }
}


/* Main Processing Loop */
static void mainLoop(AppContext *context) {
    INT32 counter = 0;
    UINT8 counterBuffer[DATA_MAX];
    unsigned int ssc = 0;
    while (1) {
        TRDP_FDS_T rfds;
        INT32 noDesc;
        TRDP_TIME_T tv, max_tv = {0, MAX_TIMEOUT}, min_tv = {0, MIN_TIMEOUT};
        
        FD_ZERO(&rfds);
        tlc_getInterval(context->appHandle, &tv, &rfds, &noDesc);
        
        if (vos_cmpTime(&tv, &max_tv) > 0) tv = max_tv;
        else if (vos_cmpTime(&tv, &min_tv) < 0) tv = min_tv;

        INT32 rv = vos_select(noDesc, &rfds, NULL, NULL, &tv);
        tlc_process(context->appHandle, &rfds, &rv);

        if (rv > 0) {
            vos_printLogStr(VOS_LOG_USR, "Other descriptors ready\n");
        } else {
            fprintf(stdout, "."); fflush(stdout);
        }

        if (context->outputBuffer) {
            sprintf((char*)counterBuffer, "Just a Counter: %08d", counter++); //data assignment
            UINT32 data_size = strlen((char*)counterBuffer) + 1;
            if (context->sdt) {
                addSDTInfo(counterBuffer, &data_size, &ssc);
            }
            context->outputBufferSize = data_size;
            context->outputBuffer = counterBuffer;

            TRDP_ERR_T err = tlp_put(context->appHandle, 
                                    context->pubHandle,
                                    context->outputBuffer, 
                                    context->outputBufferSize);
            if (err != TRDP_NO_ERR) {
                vos_printLogStr(VOS_LOG_ERROR, "PD put error\n");
                break;
            }
        }
    }
}

/* Cleanup */
static void cleanup(AppContext *context) {
    tlp_unpublish(context->appHandle, context->pubHandle);
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

    err = setupPublisher(&context);
    if (err != TRDP_NO_ERR) {
        cleanup(&context);
        return 1;
    }

    mainLoop(&context);
    cleanup(&context);
    return 0;
}