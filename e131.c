#include <string.h>
#include <inttypes.h>
#include "e131.h"

/* E1.31 Public Constants */
const uint16_t E131_DEFAULT_PORT = 5568;
const uint8_t E131_DEFAULT_PRIORITY = 0x64;

const size_t E131_CID_LEN = 16;
const size_t E131_SOURCENAME_LEN = 64;

/* E1.31 Private Constants */
const uint16_t _E131_PREAMBLE_SIZE = 0x0010;
const uint16_t _E131_POSTAMBLE_SIZE = 0x0000;
const uint8_t _E131_ACN_PID[] = {0x41, 0x53, 0x43, 0x2d, 0x45, 0x31, 0x2e, 0x31, 0x37, 0x00, 0x00, 0x00};

/* E1.31 private enums */
enum E131_VECTOR_ROOT {
  VECTOR_ROOT_E131_DATA = 0x00000004,
  VECTOR_ROOT_E131_EXTENDED = 0x00000008,
};

enum E131_VECTOR_E131_DATA {
  VECTOR_E131_DATA_PACKET = 0x00000002,
};
enum E131_VECTOR_E131_EXTENDED {
  VECTOR_E131_EXTENDED_SYNCHRONIZATION = 0x00000001,
  VECTOR_E131_EXTENDED_DISCOVERY = 0x00000002,
};

enum E131_VECTOR_UNIVERSE_DISCOVERY {
  VECTOR_UNIVERSE_DISCOVERY_UNIVERSE_LIST = 0x00000001,
};

const uint32_t E131_UNIVERSE_DISCOVERY_INTERVAL = 10000; // 10s
const uint32_t E131_NETWORK_DATA_LOSS_TIMEOUT = 2500; // 2.5s


typedef uint32_t acn_vector_t;
typedef struct acn_root_layer_s {
  acn_vector_t vector;      // Vector (VECTOR_ROOT_E131_DATA or VECTOR_ROOT_E131_EXTENDED)
  uint8_t *cid_p; // Sender's CID (unique ID)
} acn_root_layer_t;

typedef struct e131_framing_layer_data_s {
  acn_vector_t vector;      // Vector (VECTOR_E131_DATA_PACKET)
  uint8_t *source_name_p;   // Pointer to User Assigned name of Source (UTF-8, null-terminated)
  uint8_t priority;         // Data priority if multiple sources (0-200, default of 100)
  e131_universe_t sync_addr; // Universe address on which sync packets will be sent
  uint8_t seq;              // Sequence Number
  uint8_t options;          // Options Flags
  e131_universe_t universe; // Universe Number
} e131_framing_layer_data_t;

typedef struct e131_framing_layer_sync_s {
  acn_vector_t vector;      // Vector (VECTOR_E131_EXTENDED_SYNCHRONIZATION)
  uint8_t seq;              // Sequence Number
  e131_universe_t sync_addr; // Universe address on which sync packets will be sent
} e131_framing_layer_sync_t;

typedef struct e131_framing_layer_discovery_s {
  acn_vector_t vector;      // Vector (VECTOR_E131_EXTENDED_DISCOVERY)
  uint8_t *source_name_p;  // User Assigned name of Source (UTF-8, null-terminated)
} e131_framing_layer_discovery_t;

typedef struct e131_dmp_layer_data_s {
  uint16_t value_count;      // Number of channels + 1
  uint8_t *value_ptr;        // Pointer to the data
} e131_dmp_layer_data_t;

typedef struct e131_data_packet_s {
  acn_root_layer_t root;
  e131_framing_layer_data_t framing;
  e131_dmp_layer_data_t dmp;
} e131_data_packet_t;

typedef struct e131_sync_packet_s {
  acn_root_layer_t root;
  e131_framing_layer_sync_t framing;
} e131_sync_packet_t;


uint32_t _be32toh(uint8_t *ptr){
  return (ptr[0] << 24) | (ptr[1] << 16) | (ptr[2] << 8) | (ptr[3] << 0);
}

uint16_t _be16toh(uint8_t *ptr){
  return (ptr[0] << 8) | (ptr[1] << 0);
}

void e131_swap(e131_t *o, size_t u) {
    // swap active - inactive
    dmx_t *tmp = o->universes[u].active;
    o->universes[u].active = o->universes[u].inactive;
    o->universes[u].inactive = tmp;
}

void e131_handle_sync(e131_t *o, e131_sync_packet_t *pkt) {
  e131_universe_t universe = pkt->framing.sync_addr;

  if (universe == 0)
    return;

  if (universe >= o->first_addr && universe < o->first_addr+o->num_universes) {
    size_t i = universe - o->first_addr;

    // check if packet is in order
    uint8_t seq = pkt->framing.seq;
    if (seq > o->universes[i].sync_seq || o->universes[i].sync_seq - seq > 20) {
      o->universes[i].sync_seq = seq;

      for (size_t u = 0; u < o->num_universes; u++) {
        if (o->universes[u].sync_addr == universe) {
          e131_swap(o, u);
        }
      }
    }
  }
}

void e131_handle_data(e131_t *o, e131_data_packet_t *pkt) {
  e131_universe_t universe = pkt->framing.universe;

  if (universe >= o->first_addr && universe < o->first_addr+o->num_universes) {
    e131_universe_t i = universe - o->first_addr;

    // check if packet is in order
    uint8_t seq = pkt->framing.seq;

    if (seq > o->universes[i].data_seq || o->universes[i].data_seq - seq > 20) {
      o->universes[i].data_seq = seq;

      // TODO: check options

      // copy data
      if (pkt->dmp.value_count > 1) {
        size_t num = (pkt->dmp.value_count-1 <= E131_UNIVERSE_LEN ?
          pkt->dmp.value_count-1 : 
          E131_UNIVERSE_LEN); 
        o->universes[i].inactive->startcode = pkt->dmp.value_ptr[0];
        memcpy(o->universes[i].inactive->data, &(pkt->dmp.value_ptr[1]), num);
      }

      // update sync address
      o->universes[i].sync_addr = pkt->framing.sync_addr;

      if (o->universes[i].sync_addr == 0) {
        e131_swap(o, i);
      }
    }

  }

}

int e131_parse_packet(e131_t *o, uint8_t *buf, size_t len) {
  int ret;

  acn_root_layer_t root;

  // parse root layer
  if (len < 38) {
    // too short to contain the ACN root layer
    return -1;
  }
  if (_be16toh(buf) != _E131_PREAMBLE_SIZE ) {
    // invalid preable size
    return -1;
  }
  if (_be16toh(buf+2) != _E131_POSTAMBLE_SIZE ) {
    // invalid post-amble size
    return -1;
  }
  if (memcmp(buf+4, _E131_ACN_PID, sizeof(_E131_ACN_PID) ) != 0) {
    //invalid ACN PACKET ID
    return -1;
  }
  uint16_t root_length = ((0x0f & buf[16]) << 8) | buf[17];

  if (root_length + 16 > len) {
    return -1;
  }

  acn_vector_t root_vector = _be32toh(buf+18);

  uint8_t *root_cid_p = buf+22;

  if(root_vector == VECTOR_ROOT_E131_DATA) {

    e131_data_packet_t pkt;
    pkt.root.vector = root_vector;
    pkt.root.cid_p  = root_cid_p;

    // DATA packet
    if (len < 124) {
      // too short
      return -1;
    }

    uint16_t framing_length = ((0x0f & buf[38]) << 8) | buf[39];

    if (framing_length + 38 > len) {
      return -1;
    }

    pkt.framing.vector = _be32toh(buf+40);

    if (pkt.framing.vector != VECTOR_E131_DATA_PACKET) {
      return -1;
    }
    pkt.framing.source_name_p = buf+44;
    pkt.framing.priority = buf[108];
    pkt.framing.sync_addr = _be16toh(buf+109);
    pkt.framing.seq = buf[111];
    pkt.framing.options = buf[112];
    pkt.framing.universe = _be16toh(buf+113);

    pkt.dmp.value_count = _be16toh(buf+123);
    if (len < 124+pkt.dmp.value_count) {
      return -1;
    }
    pkt.dmp.value_ptr = buf+125;

    // call "data recv callback"
    e131_handle_data(o, &pkt);

  } else if (root_vector == VECTOR_ROOT_E131_EXTENDED ) {
    // EXTENDED packet (sync or discovery)

    if (len < 44) {
      // too short
      return -1;
    }

    uint16_t framing_length = ((0x0f & buf[38]) << 8) | buf[39];

    if (framing_length + 38 > len) {
      return -1;
    }

    acn_vector_t framing_vector = _be32toh(buf+40);


    if (framing_vector != VECTOR_E131_EXTENDED_SYNCHRONIZATION) {
      return -1;
    }

    // from here: this is a sync packet

    e131_sync_packet_t pkt;
    pkt.root.vector = root_vector;
    pkt.root.cid_p  = root_cid_p;
    pkt.framing.vector = framing_vector;

    if (len < 48) {
      // too short
      return -1;
    }

    pkt.framing.seq = buf[44];
    pkt.framing.sync_addr = _be16toh(buf+45);

    // call "sync recv callback"
    e131_handle_sync(o, &pkt);
  }
}

const uint32_t E131_MULTICAST_GROUP = 0xefff0000;
