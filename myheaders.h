#include <sys/socket.h>
#include <sys/un.h>
#include <netdb.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <linux/if_ether.h>
#include <linux/if_arp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <limits.h>
#include <errno.h>

#ifndef MAX
	#define MAX( a, b ) ( ((a) > (b)) ? (a) : (b) )
#endif

#define ODR_SUN_PATH "my_odr_path"
#define SERVER_SUN_PATH "my_server_path"
#define SERVER_PORT 45500

#define MAX_BUFFER_LENGTH 1024

#define MY_PROTO_ID	0x8543

#define CLIENT_SERVER_TIMEOUT_STALENESS 6000*1000

#define my_packet_type int
#define R_REQUEST 0
#define R_REPLY 1
#define DATA 2

#define my_bool int
#define my_true 1
#define my_false 0

#define TIMEOUT_RETURN_CODE -2

#define REDISCOVERY_TIMEOUT 30

#define	IF_NAME		16	/* same as IFNAMSIZ    in <net/if.h> */
#define	IF_HADDR	 6	/* same as IFHWADDRLEN in <net/if.h> */

#define	IP_ALIAS  	 1	/* hwa_addr is an alias */

#define MAX_PF_SOCKFD	10 

struct hwa_info {
  char    if_name[IF_NAME];	/* interface name, null terminated */
  char    if_haddr[IF_HADDR];	/* hardware address */
  int     if_index;		/* interface index */
  short   ip_alias;		/* 1 if hwa_addr is an alias IP address */
  struct  sockaddr  *ip_addr;	/* IP address */
  struct  hwa_info  *hwa_next;	/* next of these structures */
};


/* function prototypes */
struct hwa_info	*get_hw_addrs();
struct hwa_info	*Get_hw_addrs();
void	free_hwa_info(struct hwa_info *);

/* api function prototypes */
int msg_send(int socket_fd, char* canonical_ip, int dest_port, char* message, int flag);
int msg_recv(int socket_fd, char* canonical_ip, int* src_port, char* message);

typedef struct msg_send_serialized_data
{
	int socket_fd;
	char canonical_ip[MAX_BUFFER_LENGTH];
	int dest_port;
	char message[MAX_BUFFER_LENGTH];
	my_bool flag;
}msg_send_serialized_data;

typedef struct RREQ
{
	struct in_addr src;
	struct in_addr dest;
	int hop_count;
	int broadcast_id;
	my_bool rediscover;
	my_bool dont_reply;         
}RREQ;

typedef struct RREP
{
	struct in_addr src;
	struct in_addr dest;
	int hop_count;	
	struct in_addr final_dest;
	my_bool rediscover;
}RREP;

typedef struct RDATA
{
	struct in_addr source_ip;
	struct in_addr dest_ip;
	int source_port;
	int dest_port;
	int hop_count;
	int msg_length;
	char msg[MAX_BUFFER_LENGTH];
}RDATA;

union eth_payload
{
	RREQ request;
	RREP reply;
	RDATA actual_data;
};

typedef struct payload
{
	my_packet_type payload_type; 
	union eth_payload actual_payload_content;
}payload;

typedef struct my_ether_hdr 
{
	unsigned char dest_mac[6];
	unsigned char src_mac[6];
	short proto;
}my_ether_hdr;

typedef struct ethernet_frame
{
	my_ether_hdr eth_header;
	payload eth_data;
}ethernet_frame;

typedef struct pending_frame_list
{
	payload payload_to_send;
	struct in_addr dest_ip;
	struct pending_frame_list *next;
}pending_frame_list;

typedef struct routing_table
{
	struct in_addr source_ip;
	struct in_addr dest_ip;
	int interface_index;
	unsigned char neighbour_mac[6];
	int hop_count;
	unsigned long timestamp;
	struct routing_table *next;
}routing_table;

typedef struct rreqs_sent
{
	struct in_addr source_ip;
	int broadcast_id;
	struct rreqs_sent *next;
}rreqs_sent;

typedef struct client_server_lookup_table{
	int dest_port;
	char path[MAX_BUFFER_LENGTH];
	unsigned int timestamp;
	struct client_server_lookup_table *next;
}client_server_lookup_table;

unsigned long get_current_time_millis();
RREQ* buildRREQ(unsigned long src,unsigned long dest,int hop_count,int broadcast_id, my_bool rediscover, my_bool dont_reply);
RREP* buildRREP(unsigned long src,unsigned long dest,int hop_count, my_bool rediscover, unsigned long final_dest);
RDATA* buildRDATA(unsigned long src, unsigned long dest, int source_port, int dest_port, int hop_count, int msg_length, char* msg);
void* buildNewFrame(int type, unsigned char* dest_mac, unsigned char* src_mac, short proto ,int interface_index, struct sockaddr_ll* socket_addr,union eth_payload* data, my_bool broadcast_this);
int sendFrame(int sockfd, void* eframe, struct sockaddr_ll* socket_address);
client_server_lookup_table* get_from_client_server_lookup_table(client_server_lookup_table* head, int destination_port);
client_server_lookup_table* get_path_from_client_server_lookup_table(client_server_lookup_table* head, char* path);
client_server_lookup_table* remove_stale_entries_from_table(client_server_lookup_table* head);
client_server_lookup_table* add_to_client_server_lookup_table(client_server_lookup_table* head, int port, char* path, my_bool permanent);
void print_client_server_table(client_server_lookup_table* head);
pending_frame_list* addFrameToPendingList(pending_frame_list* head, my_packet_type type, unsigned long dest_ip, union eth_payload* data);
pending_frame_list* deleteFrameFromPendingList(pending_frame_list* head, unsigned long dest_ip);
my_bool is_old_rreq(rreqs_sent* head, unsigned long source_ip, int broadcast_id);
rreqs_sent* add_or_update_rreqs_sent(rreqs_sent* head, unsigned long source_ip, int broadcast_id);
routing_table* getRoutingEntry(routing_table* head, unsigned long dest_ip);
my_bool update_time_stamp_to_make_stale(routing_table* head, unsigned long dest_ip, unsigned long to_update);
routing_table* createRoutingEntry(unsigned long dest_ip, int interface_index, unsigned char* neighbour_mac, int hop_count);
routing_table* addRoutingEntry(routing_table* head, routing_table* nodeToAdd);
my_bool updateRoutingEntry(routing_table* head, unsigned long dest_ip, int interface_index, unsigned char* neighbour_mac, int hop_count);
int getLengthofRoutingTable(routing_table* head);
char * my_sock_ntop(const struct sockaddr *sa, socklen_t salen);
void printRREQ(RREQ* req);
void printRREP(RREP* rep);
void printRDATA(RDATA* data);
char* print_mac_address(unsigned char* mac_addr);
void printEthernetHeader(my_ether_hdr* hdr);
void printEthernetPayload(payload* pld);
void printRoutingEntry(routing_table *node);
void printRoutingTable(routing_table* head);
void printPendingList(pending_frame_list *head);
void print_rreqs_sent(rreqs_sent* head);
