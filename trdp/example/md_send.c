/**
 * @file            test_mdSend.c
 * @brief           Test application for TRDP MD sending
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
#define DATA_MAX            1000
#define MD_COMID1           1001
#define RESERVED_MEMORY     2000000
#define MAX_IF              10

const UINT8 cDemoData[] = " " /* Same demo data as original */;

UINT8 gBuffer[64 * 1024];

typedef struct sSessionData
{
    BOOL8               sNotifyOnly;
    BOOL8               sOnlyOnce;
    BOOL8               sNoData;
    UINT32              sComID;
    TRDP_APP_SESSION_T  appHandle;
    BOOL8               sBlockingMode;
    UINT32              sDataSize;
} SESSION_DATA_T;

SESSION_DATA_T sSessionData = {FALSE, FALSE, FALSE, MD_COMID1, NULL, TRUE, 0u};
UINT32 ownIP = 0u;

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
    switch (pMsg->resultCode)
    {
        case TRDP_NO_ERR:
            switch (pMsg->msgType)
            {
                case TRDP_MSG_MP:
                    vos_printLog(VOS_LOG_USR, "<- MR Reply received %u\n", pMsg->comId);
                    vos_printLog(VOS_LOG_USR, "   from userURI: %.32s \n", pMsg->srcUserURI);
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    break;
                case TRDP_MSG_MQ:
                    vos_printLog(VOS_LOG_USR, "<- MR Reply with confirmation received %u\n", pMsg->comId);
                    vos_printLog(VOS_LOG_USR, "   from userURI: %.32s \n", pMsg->srcUserURI);
                    if (pData && dataSize > 0)
                        vos_printLog(VOS_LOG_USR, "   Data[%uB]: %.80s...\n", dataSize, pData);
                    tlm_confirm(appHandle, (const TRDP_UUID_T *)&pMsg->sessionId, 0, NULL);
                    break;
                case TRDP_MSG_MC:
                    vos_printLog(VOS_LOG_USR, "<- MR Confirmation received %u\n", pMsg->comId);
                    break;
                case TRDP_MSG_ME:
                    vos_printLog(VOS_LOG_USR, "<- ME received %u\n", pMsg->comId);
                    break;
            }
            break;
        case TRDP_REPLYTO_ERR:
            vos_printLog(VOS_LOG_USR, "### No Reply within time out for ComID %d, destIP: %s\n",
                        pMsg->comId, vos_ipDotted(pMsg->destIpAddr));
            break;
        case TRDP_CONFIRMTO_ERR:
        case TRDP_REQCONFIRMTO_ERR:
            vos_printLog(VOS_LOG_USR, "### No Confirmation within time out for ComID %d, destIP: %s\n",
                        pMsg->comId, vos_ipDotted(pMsg->destIpAddr));
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
    printf("This tool sends MD messages.\n"
           "Arguments are:\n"
           "-o <own IP address>    in dotted decimal\n"
           "-t <target IP address> in dotted decimal\n"
           "-p <TCP|UDP>           protocol (default UDP)\n"
           "-d <n>                 timeout in us (default 2s)\n"
           "-e <n>                 expected replies\n"
           "-n                     notify only\n"
           "-l <n>                 send large message (up to 65420 Bytes)\n"
           "-0                     send no data\n"
           "-1                     send only one request\n"
           "-b <0|1>               blocking mode (default = 1)\n"
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
    UINT32 destIP = 0u, expReplies = 1u, delay = 2000000u;
    TRDP_FLAGS_T flags = TRDP_FLAGS_CALLBACK;
    int rv = 0, ch;

    if (argc <= 1)
    {
        usage(argv[0]);
        return 1;
    }

    while ((ch = getopt(argc, argv, "t:o:p:d:l:e:b:n01vh?")) != -1)
    {
        switch (ch)
        {
            case 'o':
                if (sscanf(optarg, "%u.%u.%u.%u", &ip[3], &ip[2], &ip[1], &ip[0]) < 4)
                    return 1;
                ownIP = (ip[3] << 24) | (ip[2] << 16) | (ip[1] << 8) | ip[0];
                break;
            case 't':
                if (sscanf(optarg, "%u.%u.%u.%u", &ip[3], &ip[2], &ip[1], &ip[0]) < 4)
                    return 1;
                destIP = (ip[3] << 24) | (ip[2] << 16) | (ip[1] << 8) | ip[0];
                break;
            case 'p':
                if (strcmp(optarg, "TCP") == 0)
                    flags |= TRDP_FLAGS_TCP;
                else if (strcmp(optarg, "UDP") != 0)
                    return 1;
                break;
            case 'd':
                if (sscanf(optarg, "%u", &delay) < 1)
                    return 1;
                break;
            case 'l':
                if (sscanf(optarg, "%u", &sSessionData.sDataSize) < 1)
                    return 1;
                break;
            case 'e':
                if (sscanf(optarg, "%u", &expReplies) < 1)
                    return 1;
                break;
            case 'n':
                sSessionData.sNotifyOnly = TRUE;
                break;
            case '0':
                sSessionData.sNoData = TRUE;
                break;
            case '1':
                sSessionData.sOnlyOnce = TRUE;
                break;
            case 'b':
                if (sscanf(optarg, "%hhd", &sSessionData.sBlockingMode) < 1)
                    return 1;
                processConfig.options = sSessionData.sBlockingMode ? TRDP_OPTION_BLOCK : 0;
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

    if (destIP == 0)
    {
        fprintf(stderr, "No destination address given!\n");
        return 1;
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

    /* Send message */
    TRDP_ERR_T err;
    TRDP_UUID_T sessionId;
    if (sSessionData.sNotifyOnly)
    {
        vos_printLog(VOS_LOG_USR, "-> sending MR Notification %u\n", sSessionData.sComID);
        if (sSessionData.sNoData)
            err = tlm_notify(sSessionData.appHandle, &sSessionData, NULL, sSessionData.sComID,
                           0, 0, ownIP, destIP, flags, NULL, NULL, 0, 0, 0);
        else if (sSessionData.sDataSize > 0)
        {
            for (UINT32 i = 0, j = 0; i < sSessionData.sDataSize; i++)
            {
                gBuffer[i] = cDemoData[j++];
                if (j >= sizeof(cDemoData)) j = 0;
            }
            err = tlm_notify(sSessionData.appHandle, &sSessionData, mdCallback, sSessionData.sComID,
                           0, 0, ownIP, destIP, flags, NULL, (const UINT8 *)gBuffer,
                           sSessionData.sDataSize, 0, 0);
        }
        else
            err = tlm_notify(sSessionData.appHandle, &sSessionData, mdCallback, sSessionData.sComID,
                           0, 0, ownIP, destIP, flags, NULL, (const UINT8 *)"Hello, World", 13, 0, 0);
    }
    else
    {
        vos_printLog(VOS_LOG_USR, "-> sending MR Request with reply %u\n", sSessionData.sComID);
        if (sSessionData.sNoData)
            err = tlm_request(sSessionData.appHandle, &sSessionData, mdCallback, &sessionId,
                            sSessionData.sComID, 0u, 0u, ownIP, destIP, flags, expReplies,
                            delay, NULL, NULL, 0u, NULL, NULL);
        else if (sSessionData.sDataSize > 0)
        {
            for (UINT32 i = 0, j = 0; i < sSessionData.sDataSize; i++)
            {
                gBuffer[i] = cDemoData[j++];
                if (j >= sizeof(cDemoData)) j = 0;
            }
            err = tlm_request(sSessionData.appHandle, &sSessionData, mdCallback, &sessionId,
                            sSessionData.sComID, 0, 0, ownIP, destIP, flags, expReplies,
                            delay, NULL, (const UINT8 *)gBuffer, sSessionData.sDataSize, 0, 0);
        }
        else
            err = tlm_request(sSessionData.appHandle, &sSessionData, mdCallback, &sessionId,
                            sSessionData.sComID, 0, 0, ownIP, destIP, flags, expReplies,
                            delay, NULL, (const UINT8 *)"How are you?", 13, 0, 0);
    }

    if (err != TRDP_NO_ERR)
        vos_printLog(VOS_LOG_USR, "tlm_%s failed (err = %d)\n", 
                    sSessionData.sNotifyOnly ? "notify" : "request", err);

    /* Wait for responses if not notify only */
    if (!sSessionData.sNotifyOnly)
    {
        vos_printLogStr(VOS_LOG_USR, "waiting for an answer...\n");
        UINT32 timeout = delay / 1000000 + 1; // Convert to seconds plus buffer
        while (timeout--)
        {
            TRDP_TIME_T tv = {0, 1000000}; // 1 second intervals
            if (sSessionData.sBlockingMode)
            {
                fd_set rfds;
                TRDP_SOCK_T noDesc = 0;
                FD_ZERO(&rfds);
                tlm_getInterval(sSessionData.appHandle, &tv, (TRDP_FDS_T *)&rfds, &noDesc);
                vos_select((int)noDesc, &rfds, NULL, NULL, &tv);
                tlm_process(sSessionData.appHandle, (TRDP_FDS_T *)&rfds, &rv);
            }
            else
            {
                vos_threadDelay(1000000); // 1 second
                tlm_process(sSessionData.appHandle, NULL, NULL);
            }
        }
    }

    /* Cleanup */
    tlc_closeSession(sSessionData.appHandle);
    tlc_terminate();
    return rv;
}