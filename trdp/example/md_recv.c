/**
 * @file            test_mdReceive.c
 * @brief           Test application for TRDP MD receiving
 */

/***********************************************************************************************************************
 * INCLUDES
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined (POSIX)
#include <unistd.h>
#elif (defined (WIN32) || defined (WIN64))
#include "getopt.h"
#endif
#include "trdp_if_light.h"
#include "vos_thread.h"
#include "vos_sock.h"
#include "vos_utils.h"

/***********************************************************************************************************************
 * DEFINITIONS
 */
#define APP_VERSION         "1.5"
#define MD_COMID1           1001
#define RESERVED_MEMORY     2000000
#define MAX_IF              10

typedef struct sSessionData
{
    BOOL8               sResponder;
    BOOL8               sConfirmRequested;
    UINT32              sComID;
    TRDP_APP_SESSION_T  appHandle;
    TRDP_LIS_T          listenUDP;
    TRDP_LIS_T          listenTCP;
    BOOL8               sBlockingMode;
} SESSION_DATA_T;

SESSION_DATA_T  sSessionData = {TRUE, FALSE, MD_COMID1, NULL, NULL, NULL, TRUE};
UINT32          ownIP = 0u;

/***********************************************************************************************************************
 * PROTOTYPES
 */
void dbgOut(void *, TRDP_LOG_T, const CHAR8 *, const CHAR8 *, UINT16, const CHAR8 *);
void usage(const char *);
void mdCallback(void *, TRDP_APP_SESSION_T, const TRDP_MD_INFO_T *, UINT8 *, UINT32);

/**********************************************************************************************************************/
/** callback routine for receiving TRDP traffic */
void mdCallback(void *pRefCon, TRDP_APP_SESSION_T appHandle, const TRDP_MD_INFO_T *pMsg, 
                UINT8 *pData, UINT32 dataSize)
{
    TRDP_ERR_T      err = TRDP_NO_ERR;
    SESSION_DATA_T  *myGlobals = (SESSION_DATA_T *)pMsg->pUserRef;

    switch (pMsg->resultCode)
    {
        case TRDP_NO_ERR:
            switch (pMsg->msgType)
            {
                case TRDP_MSG_MN:
                    vos_printLog(VOS_LOG_USR, "<- MD Notification %u\n", pMsg->comId);
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    break;
                case TRDP_MSG_MR:
                    vos_printLog(VOS_LOG_USR, "<- MR Request with reply %u\n", pMsg->comId);
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    
                    if (sSessionData.sConfirmRequested)
                    {
                        vos_printLogStr(VOS_LOG_USR, "-> sending reply with query\n");
                        err = tlm_replyQuery(myGlobals->appHandle, &pMsg->sessionId, pMsg->comId, 0u,
                                           10000000u, NULL, (UINT8 *)"I'm fine, how are you?", 23u, 
                                           "test_mdReceive");
                    }
                    else
                    {
                        vos_printLogStr(VOS_LOG_USR, "-> sending reply\n");
                        err = tlm_reply(myGlobals->appHandle, &pMsg->sessionId, pMsg->comId, 0,
                                      NULL, (UINT8 *)"I'm fine, thanx!", 17, "test_mdReceive");
                    }
                    if (err != TRDP_NO_ERR)
                        vos_printLog(VOS_LOG_USR, "tlm_reply/Query returned error %d\n", err);
                    break;
                default:
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
            break;
    }
}

/**********************************************************************************************************************/
/** callback routine for TRDP logging/error output */
void dbgOut(void *pRefCon, TRDP_LOG_T category, const CHAR8 *pTime, const CHAR8 *pFile,
            UINT16 LineNumber, const CHAR8 *pMsgStr)
{
    const char *catStr[] = {"**Error:", "Warning:", "   Info:", "  Debug:", "   User:"};
    if (category != VOS_LOG_DBG)
    {
        if ((category != VOS_LOG_INFO) || (strstr(pFile, "vos_sock.c") == NULL))
        {
            printf("%s %s %s", strrchr(pTime, '-') + 1, catStr[category], pMsgStr);
        }
    }
}

/**********************************************************************************************************************/
/** Print a sensible usage message */
void usage(const char *appName)
{
    printf("%s: Version %s\t(%s - %s)\n", appName, APP_VERSION, __DATE__, __TIME__);
    printf("Usage of %s\n", appName);
    printf("This tool receives and responds to MD messages.\n"
           "Arguments are:\n"
           "-o <own IP address>    in dotted decimal\n"
           "-c                     respond with confirmation\n"
           "-b <0|1>               blocking mode (default = 1, blocking)\n"
           "-v                     print version and quit\n");
}

/**********************************************************************************************************************/
/** main entry */
int main(int argc, char *argv[])
{
    unsigned int ip[4];
    TRDP_MD_CONFIG_T mdConfiguration = {mdCallback, &sSessionData, {0u, 64u, 0u, 0, 0u},
                                      TRDP_FLAGS_CALLBACK, 1000000u, 1000000u, 1000000u,
                                      1000000u, 17225u, 17225u, 10};
    TRDP_MEM_CONFIG_T dynamicConfig = {NULL, RESERVED_MEMORY, {0}};
    TRDP_PROCESS_CONFIG_T processConfig = {"Me", "", "", 0, 0, TRDP_OPTION_BLOCK};
    VOS_IF_REC_T interfaces[MAX_IF];
    int rv = 0, ch;

    if (argc <= 1)
    {
        usage(argv[0]);
        return 1;
    }

    while ((ch = getopt(argc, argv, "o:b:cvh?")) != -1)
    {
        switch (ch)
        {
            case 'o':
                if (sscanf(optarg, "%u.%u.%u.%u", &ip[3], &ip[2], &ip[1], &ip[0]) < 4)
                {
                    usage(argv[0]);
                    return 1;
                }
                ownIP = (ip[3] << 24) | (ip[2] << 16) | (ip[1] << 8) | ip[0];
                break;
            case 'c':
                sSessionData.sConfirmRequested = TRUE;
                break;
            case 'b':
                if (sscanf(optarg, "%hhd", &sSessionData.sBlockingMode) < 1)
                {
                    usage(argv[0]);
                    return 1;
                }
                processConfig.options = sSessionData.sBlockingMode ? 
                                      TRDP_OPTION_BLOCK : 0;
                break;
            case 'v':
                printf("%s: Version %s\t(%s - %s)\n", argv[0], APP_VERSION, __DATE__, __TIME__);
                return 0;
            case 'h':
            case '?':
            default:
                usage(argv[0]);
                return 1;
        }
    }

    if (tlc_init(dbgOut, NULL, &dynamicConfig) != TRDP_NO_ERR)
    {
        fprintf(stderr, "tlc_init error\n");
        return 1;
    }

    if (tlc_openSession(&sSessionData.appHandle, ownIP, 0, NULL, NULL, 
                       &mdConfiguration, &processConfig) != TRDP_NO_ERR)
    {
        vos_printLogStr(VOS_LOG_ERROR, "tlc_openSession error\n");
        return 1;
    }

    /* Set up listeners */
    if (tlm_addListener(sSessionData.appHandle, &sSessionData.listenUDP, &sSessionData, 
                       mdCallback, TRUE, sSessionData.sComID, 0u, 0u, VOS_INADDR_ANY, 
                       VOS_INADDR_ANY, 0, TRDP_FLAGS_CALLBACK, NULL, NULL) != TRDP_NO_ERR)
    {
        vos_printLogStr(VOS_LOG_ERROR, "tlm_addListener error (UDP)\n");
        return 1;
    }
    if (tlm_addListener(sSessionData.appHandle, &sSessionData.listenTCP, &sSessionData, 
                       mdCallback, TRUE, sSessionData.sComID, 0u, 0u, VOS_INADDR_ANY, 
                       VOS_INADDR_ANY, 0, TRDP_FLAGS_TCP | TRDP_FLAGS_CALLBACK, NULL, NULL) != TRDP_NO_ERR)
    {
        vos_printLogStr(VOS_LOG_ERROR, "tlm_addListener error (TCP)\n");
        return 1;
    }

    /* Main processing loop */
    while (1)
    {
        fd_set rfds;
        TRDP_SOCK_T noDesc = 0;
        TRDP_TIME_T tv = {0, 0}, max_tv = {0, 100000};

        if (sSessionData.sBlockingMode)
        {
            FD_ZERO(&rfds);
            tlm_getInterval(sSessionData.appHandle, &tv, (TRDP_FDS_T *)&rfds, &noDesc);
        }
        
        if (vos_cmpTime(&tv, &max_tv) > 0)
            tv = max_tv;

        if (sSessionData.sBlockingMode)
        {
            rv = vos_select((int)noDesc, &rfds, NULL, NULL, &tv);
            tlm_process(sSessionData.appHandle, (TRDP_FDS_T *)&rfds, &rv);
        }
        else
        {
            vos_threadDelay((UINT32)(tv.tv_sec * 1000000) + (UINT32)tv.tv_usec);
            tlm_process(sSessionData.appHandle, NULL, NULL);
        }
    }

    /* Cleanup */
    tlm_delListener(sSessionData.appHandle, sSessionData.listenUDP);
    tlm_delListener(sSessionData.appHandle, sSessionData.listenTCP);
    tlc_closeSession(sSessionData.appHandle);
    tlc_terminate();
    return rv;
}