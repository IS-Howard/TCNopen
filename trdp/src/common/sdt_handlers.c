/**
 * @file    sdt_handlers.c
 * @brief   SDTv2 handler function implementations
 */

#include "sdt_handlers.h"
#include <string.h>
#include <stdio.h>

#define RESULTS(NAME)	case NAME: return #NAME;
#define DEF_UNKNOWN()	default:   return "UNKNOWN";   

const char* validity_string(sdt_validity_t v)
{
    switch (v)
    {
        RESULTS(SDT_FRESH)
        RESULTS(SDT_INVALID)
        RESULTS(SDT_ERROR)
        DEF_UNKNOWN()       
    }
}

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
void addSDTInfo(UINT8 *data, UINT32 *data_size, unsigned int *ssc) {
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

void validateSDTMD(UINT8 *data, UINT32 data_size) {
    // new handler for each message
    sdt_handle_t hnd;

    // validateor input
    uint32_t      sid1 = 0x12345678U; // TODO: add sid counting process
    uint32_t      sid2 = 0;
    uint8_t       sid2red = 0;
    uint16_t      udv = 2;

    // result parameters
    sdt_result_t        result;
    sdt_result_t        sdt_error;

    sdt_get_validator(SDT_IPT,  sid1, sid2, sid2red, udv, &hnd);

    result = sdt_validate_md(hnd, data, data_size);
    sdt_get_errno(hnd, &sdt_error);
    if (result != 0) {
        printf("sdt validataion with error:%s\n", result_string(sdt_error));
    }
}

void validateSDTPD(sdt_handle_t *hnd, int *init, UINT8 *data, UINT32 data_size) {
    // validateor input
    uint32_t      sid1 = 0x12345678U; // TODO: add sid counting process
    uint32_t      sid2 = 0;
    uint8_t       sid2red = 0;
    uint16_t      ver = 2; // SDT version

    // sink parameters
    uint16_t      rx_period = 120;
    uint16_t      tx_period = 100;
    uint8_t       n_rxsafe  = 100;
    uint16_t      n_guard   = 2;
    uint32_t      cmthr     = 1000;
    uint16_t      lmi_max   = 200;

    // result parameters
    sdt_result_t        result;
    sdt_result_t        sdt_error;
    sdt_counters_t      counters;
    uint32_t            ssc_l;


    if (*init == 1)
    {
        *init = 0;
        sdt_get_validator(SDT_IPT,  sid1, sid2, sid2red, ver, hnd);
        sdt_set_sdsink_parameters(*hnd, rx_period, tx_period, n_rxsafe, n_guard, cmthr, lmi_max);
    }
    result = sdt_validate_pd(*hnd, data, data_size);
    sdt_get_errno(*hnd, &sdt_error);
    sdt_get_ssc(*hnd, &ssc_l);
    printf("SDT: ssc=%u, valid=%s\n", ssc_l, validity_string(result));
    if (result != 0) {
        printf("validataion with error:%s\n", result_string(sdt_error));
    }
    sdt_get_counters(*hnd, &counters);
    printf("sdt_counters: rx(%u) err(%u) sid(%u) oos(%u) dpl(%u) udv(%u) lmg(%u)\n", 
        counters.rx_count,
        counters.err_count,
        counters.sid_count,
        counters.oos_count,
        counters.dpl_count,
        counters.udv_count,
        counters.lmg_count);
}