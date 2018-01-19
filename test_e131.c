#include "e131.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

char buf[1024];

void die(const char *s) {
  perror(s);
  exit(1);

}

// initialize e131 structures
e131_uni_container_t universes[1];

dmx_t a;
dmx_t b;


e131_t e131_p;


const short port = 5568;
const unsigned long universe = 1;

int main (int argc, char **argv) {

  universes[0].active = &a;
  universes[0].inactive = &b;

  e131_p.universes = universes;
  e131_p.num_universes = 1;
  e131_p.first_addr = 1;


  int s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  if (s < 0) {
    die("socket");
  }

  struct sockaddr_in si_me;
  memset((char *) &si_me, 0, sizeof(si_me));
  si_me.sin_family = AF_INET;
  si_me.sin_port = htons(port);
  si_me.sin_addr.s_addr = htonl(INADDR_ANY);

  if ( bind(s, (struct sockaddr*) &si_me, sizeof(si_me) ) < 0) {
    die("bind");
  }

  // join multicast
  struct ip_mreq mreq;

  // 239.255.0.0 + universe
  mreq.imr_multiaddr.s_addr = htonl(0xefff0000 | universe);
  mreq.imr_interface.s_addr=htonl(INADDR_ANY);
  if (setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0 ) {
    die("setsockopt");
  }

  while(1) {

    struct sockaddr_in si_other;
    size_t recvlen;
    socklen_t slen;

    struct sockaddr * si_other_p = (struct sockaddr *) &si_other;

    printf("Waiting for data\n");
    fflush(stdout);

    recvlen = recvfrom(s, buf, sizeof(buf), 0, si_other_p, &slen);
    printf("Packet received\n");

    if (recvlen > 0) {
      e131_parse_packet(&e131_p, buf, recvlen);
    }
  }

  close(s);
  return 0;

}
