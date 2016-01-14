#include <arpa/inet.h>
#include <limits.h>

#define INDEX(x, y, nnodes)			((x) + (nnodes) * (y))
#define INFINITY					INT_MAX
#define UNDEFINED					(-1)

double timer_now(void);
void timer_start(double when, void (*handler)(void *arg), void *arg);
int timer_check(void);

struct file_info;
struct sockaddr_in;
struct gossip;
struct node_list *nl;
struct gossip *gossip;

void gossip_to_peer(struct file_info *fi);
void gossip_received(struct file_info *fi, char *line);
struct gossip* gossip_next(struct gossip* gossip);
struct sockaddr_in gossip_src(struct gossip* gossip);
char* gossip_latest(struct gossip* gossip);

void file_info_send(struct file_info *fi, char *buf, int size);
void file_broadcast(char *buf, int size, struct file_info *fi);
struct file_info* sockaddr_to_file(struct sockaddr_in dst);

int addr_get(struct sockaddr_in *sin, const char *addr, int port);
int addr_cmp(struct sockaddr_in a1, struct sockaddr_in a2);

struct sockaddr_in string_to_addr(char* string);