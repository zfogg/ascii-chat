#ifndef TEST_UPNP_GATEWAY_H
#define TEST_UPNP_GATEWAY_H
#include <stdint.h>
struct UPNPDev {
  int unused;
};
struct UPNPUrls {
  char *controlURL;
};
struct IGDdatas {
  struct {
    char servicetype[128];
  } first;
};
#define UPNPCOMMAND_SUCCESS 0
struct UPNPDev *upnpDiscover(int, const char *, const char *, int, int, unsigned char, int *);
#ifdef MINIUPNPC_GETVALIDIGD_7ARG
#define UPNP_PRIVATEIP_IGD 2
int UPNP_GetValidIGD(struct UPNPDev *, struct UPNPUrls *, struct IGDdatas *, char *, int, char *, int);
#else
int UPNP_GetValidIGD(struct UPNPDev *, struct UPNPUrls *, struct IGDdatas *, char *, int);
#endif
int UPNP_GetExternalIPAddress(const char *, const char *, char *);
int UPNP_AddPortMapping(const char *, const char *, const char *, const char *, const char *, const char *,
                        const char *, const char *, const char *);
int UPNP_DeletePortMapping(const char *, const char *, const char *, const char *, const char *);
int UPNP_GetSpecificPortMappingEntry(const char *, const char *, const char *, const char *, const char *, char *,
                                     char *, char *, char *, char *);
void freeUPNPDevlist(struct UPNPDev *);
void FreeUPNPUrls(struct UPNPUrls *);
const char *strupnperror(int);
#define NATPMP_TRYAGAIN -100
#define NATPMP_RESPTYPE_PUBLICADDRESS 0
#define NATPMP_RESPTYPE_TCPPORTMAPPING 2
#define NATPMP_RESPTYPE_UDPPORTMAPPING 1
#define NATPMP_PROTOCOL_TCP 2
#define NATPMP_PROTOCOL_UDP 1
typedef struct {
  uint32_t gateway;
} natpmp_t;
typedef struct {
  uint16_t type, resultcode;
  union {
    struct {
      uint32_t addr;
    } publicaddress;
    struct {
      uint16_t privateport, mappedpublicport;
      uint32_t lifetime;
    } newportmapping;
  } pnu;
} natpmpresp_t;
int initnatpmp(natpmp_t *, int, uint32_t);
int closenatpmp(natpmp_t *);
int sendpublicaddressrequest(natpmp_t *);
int sendnewportmappingrequest(natpmp_t *, int, uint16_t, uint16_t, uint32_t);
int readnatpmpresponseorretry(natpmp_t *, natpmpresp_t *);

#endif
