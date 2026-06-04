#ifndef N720V5_H
#define N720V5_H

#include <stddef.h>
#include <stdint.h>
#include "config/app_config.h"
#include "config/app_types.h"

int n720v5_get_cimi(char *imsi, size_t imsi_size);
int n720v5_get_ccid(char *ccid, size_t ccid_size);
int n720v5_get_csq(int *out_rssi);
int n720v5_get_creg(int *out_stat);
int n720v5_get_cpin(int *out_ready);
int n720v5_set_cgdcont(const char *apn);
int n720v5_set_sim(sim_slot_t sim);
int n720v5_set_netact(uint8_t channel, uint8_t action);
int n720v5_netcreate(uint8_t channel, uint8_t mode, uint8_t socket_id,
                     const char *ip, uint16_t port, uint16_t local_port);
int n720v5_get_mynetack(uint8_t socket_id, int *out_unacked, int *out_rest);
int n720v5_mynetread(uint8_t socket_id, uint8_t *data, size_t max_len, size_t *out_len);
int n720v5_mynetwrite(uint8_t socket_id, const uint8_t *data, size_t len);

#endif
