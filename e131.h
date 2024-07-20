#ifndef _E131_H
#define _E131_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <sys/types.h>

/* E1.31 Public Constants */
extern const uint16_t E131_DEFAULT_PORT;
extern const uint32_t E131_MULTICAST_GROUP;
#define E131_UNIVERSE_LEN 512


typedef uint16_t e131_universe_t;

typedef struct dmx_s {
  uint8_t startcode;
  uint8_t data[E131_UNIVERSE_LEN];
} dmx_t;

typedef struct e131_uni_container_s {
  dmx_t *active;
  dmx_t *inactive;
  e131_universe_t sync_addr;
  uint8_t data_seq;
  uint8_t sync_seq;
  // TODO: sources
} e131_uni_container_t;

typedef struct e131_s {
  e131_universe_t first_addr;
  e131_universe_t num_universes;
  e131_uni_container_t *universes;
} e131_t;

int e131_parse_packet(e131_t *o, uint8_t *buf, size_t len);

#ifdef __cplusplus
}
#endif
#endif
