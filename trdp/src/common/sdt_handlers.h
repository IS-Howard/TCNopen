/**
 * @file    sdt_handlers.h
 * @brief   SDTv2 handler functions for TRDP application
 */

#ifndef SDT_HANDLERS_H
#define SDT_HANDLERS_H

#include "../../../SDTv2/api/sdt_api.h"
#include "vos_utils.h"

void addSDTInfo(UINT8 *data, UINT32 *data_size, unsigned int *ssc);
void validateSDTMD(UINT8 *data, UINT32 data_size);
void validateSDTPD(sdt_handle_t *hnd, int *init, UINT8 *data, UINT32 data_size);

#endif /* SDT_HANDLERS_H */