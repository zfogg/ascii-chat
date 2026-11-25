#ifdef __APPLE__
#include <dns_sd.h>
#include <libplatform/socket.h>
#include <stdlib.h>
#include <string.h>
typedef struct { int ready; int closed; void *context; } mdns_fixture_ref_t;
mdns_fixture_ref_t mdns_fixture_refs[3];
DNSServiceBrowseReply mdns_fixture_browse_reply; DNSServiceResolveReply mdns_fixture_resolve_reply; DNSServiceGetAddrInfoReply mdns_fixture_address_reply;
unsigned mdns_fixture_fault, mdns_fixture_callbacks, mdns_fixture_closed_refs;
static DNSServiceErrorType mdns_fixture_browse(DNSServiceRef *out,DNSServiceFlags f,uint32_t i,const char*t,const char*d,DNSServiceBrowseReply r,void*c){(void)f;(void)i;(void)t;(void)d;if(mdns_fixture_fault==1)return kDNSServiceErr_ServiceNotRunning;mdns_fixture_refs[0]=(mdns_fixture_ref_t){1,0,c};*out=(DNSServiceRef)&mdns_fixture_refs[0];mdns_fixture_browse_reply=r;return kDNSServiceErr_NoError;}
static DNSServiceErrorType mdns_fixture_resolve(DNSServiceRef *out,DNSServiceFlags f,uint32_t i,const char*n,const char*t,const char*d,DNSServiceResolveReply r,void*c){(void)f;(void)i;(void)n;(void)t;(void)d;if(mdns_fixture_fault==2||mdns_fixture_fault==13)return mdns_fixture_fault==13?kDNSServiceErr_NoSuchName:kDNSServiceErr_BadParam;mdns_fixture_refs[1]=(mdns_fixture_ref_t){1,0,c};*out=(DNSServiceRef)&mdns_fixture_refs[1];mdns_fixture_resolve_reply=r;return kDNSServiceErr_NoError;}
static DNSServiceErrorType mdns_fixture_address(DNSServiceRef *out,DNSServiceFlags f,uint32_t i,DNSServiceProtocol p,const char*h,DNSServiceGetAddrInfoReply r,void*c){(void)f;(void)i;(void)p;(void)h;if(mdns_fixture_fault==3||mdns_fixture_fault==12)return mdns_fixture_fault==12?kDNSServiceErr_NoSuchName:kDNSServiceErr_BadParam;mdns_fixture_refs[2]=(mdns_fixture_ref_t){1,0,c};*out=(DNSServiceRef)&mdns_fixture_refs[2];mdns_fixture_address_reply=r;return kDNSServiceErr_NoError;}
static int mdns_fixture_descriptor(DNSServiceRef r){return 10+(int)((mdns_fixture_ref_t*)r-mdns_fixture_refs);}
static void mdns_fixture_deallocate(DNSServiceRef r){mdns_fixture_ref_t*s=(mdns_fixture_ref_t*)r;if(!s->closed){s->closed=1;++mdns_fixture_closed_refs;}}
static int mdns_fixture_poll(struct pollfd*f,nfds_t n,int64_t t){int ready=0;(void)t;for(nfds_t i=0;i<n;++i){f[i].revents=mdns_fixture_refs[f[i].fd-10].ready?(mdns_fixture_fault==8?POLLHUP:mdns_fixture_fault==9?POLLNVAL:POLLIN):0;ready+=f[i].revents!=0;}return ready;}
static DNSServiceErrorType mdns_fixture_process(DNSServiceRef r){mdns_fixture_ref_t*s=(mdns_fixture_ref_t*)r;if(s->closed)return kDNSServiceErr_BadReference;s->ready=0;if(mdns_fixture_fault==4)return kDNSServiceErr_BadReference;if(s==&mdns_fixture_refs[0])mdns_fixture_browse_reply(r,kDNSServiceFlagsAdd,1,mdns_fixture_fault==5?kDNSServiceErr_PolicyDenied:kDNSServiceErr_NoError,"fixture-27224","_ascii-chat._tcp.","local.",s->context);else if(s==&mdns_fixture_refs[1]){TXTRecordRef x;TXTRecordCreate(&x,0,NULL);TXTRecordSetValue(&x,"session_string",13,"one-two-three");mdns_fixture_resolve_reply(r,0,1,mdns_fixture_fault==6?kDNSServiceErr_ServiceNotRunning:mdns_fixture_fault==11?kDNSServiceErr_NoSuchName:kDNSServiceErr_NoError,"fixture-27224._ascii-chat._tcp.local.","fixture.local.",htons(27224),TXTRecordGetLength(&x),TXTRecordGetBytesPtr(&x),s->context);TXTRecordDeallocate(&x);}else{struct sockaddr_in a={.sin_family=AF_INET,.sin_addr.s_addr=htonl(0x7f000001)};mdns_fixture_address_reply(r,kDNSServiceFlagsAdd,1,mdns_fixture_fault==7?kDNSServiceErr_PolicyDenied:mdns_fixture_fault==10?kDNSServiceErr_NoSuchRecord:kDNSServiceErr_NoError,"fixture.local.",(const struct sockaddr*)&a,120,s->context);}return kDNSServiceErr_NoError;}
#define DNSServiceBrowse mdns_fixture_browse
#define DNSServiceResolve mdns_fixture_resolve
#define DNSServiceGetAddrInfo mdns_fixture_address
#define DNSServiceProcessResult mdns_fixture_process
#define DNSServiceRefSockFD mdns_fixture_descriptor
#define DNSServiceRefDeallocate mdns_fixture_deallocate
#define socket_poll mdns_fixture_poll
#define asciichat_mdns_init mdns_fixture_init
#define asciichat_mdns_destroy mdns_fixture_destroy
#define asciichat_mdns_advertise mdns_fixture_advertise
#define asciichat_mdns_unadvertise mdns_fixture_unadvertise
#define asciichat_mdns_query mdns_fixture_query
#define asciichat_mdns_update mdns_fixture_update
#define asciichat_mdns_get_socket mdns_fixture_get_socket
#include "../../../lib/network/mdns/mdns.c"
#undef socket_poll
void *mdns_fixture_context_create(void){asciichat_mdns_t*c=calloc(1,sizeof(*c));if(c)c->socket_fd=-1;return c;}
void mdns_fixture_reset(void){memset(mdns_fixture_refs,0,sizeof(mdns_fixture_refs));mdns_fixture_callbacks=mdns_fixture_closed_refs=0;}
#endif

