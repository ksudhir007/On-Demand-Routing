#include "myheaders.h"

void printRREQ(RREQ* req);
void printRREP(RREP* rep);
void printRDATA(RDATA* data);

unsigned long get_current_time_millis() 
{
	struct timeval tim;
	gettimeofday(&tim, NULL);
	
	return ((tim.tv_sec * 1000) + (tim.tv_usec / 1000));
}

RREQ* buildRREQ(unsigned long src,unsigned long dest,int hop_count,int broadcast_id, my_bool rediscover, my_bool dont_reply)
{
	RREQ * req;
	req = malloc(sizeof(RREQ));
	memset(req, 0, sizeof(RREQ));
	req->src.s_addr = htonl(src);
	req->dest.s_addr = htonl(dest);
	req->hop_count = htonl(hop_count);
	req->broadcast_id = htonl(broadcast_id);
	req->rediscover = htonl(rediscover);
	req->dont_reply = htonl(dont_reply);
	return req;
}

RREP* buildRREP(unsigned long src,unsigned long dest,int hop_count, my_bool rediscover, unsigned long final_dest)
{
	RREP *rep;
	rep = malloc(sizeof(RREP));
	memset(rep, 0, sizeof(RREP));
	
	rep->src.s_addr = htonl(src);
	rep->dest.s_addr = htonl(dest);
	rep->hop_count = htonl(hop_count);
	rep->rediscover = htonl(rediscover);
	rep->final_dest.s_addr = htonl(final_dest);
	return rep;
}

RDATA* buildRDATA(unsigned long src, unsigned long dest, int source_port, int dest_port, int hop_count, int msg_length, char* msg)
{

	RDATA *data;
	data = malloc(sizeof(RDATA));
	memset(data, 0, sizeof(RDATA));
	
	data->source_ip.s_addr = htonl(src);
	data->dest_ip.s_addr = htonl(dest);
	data->source_port = htonl(source_port);
	data->dest_port = htonl(dest_port);
	data->hop_count = htonl(hop_count);
	data->msg_length = htonl(msg_length);
	
	memcpy(data->msg, msg, msg_length);
	
	return data;
}

void* buildNewFrame(int type, unsigned char* dest_mac, unsigned char* src_mac, short proto ,int interface_index, struct sockaddr_ll* socket_addr,union eth_payload* data, my_bool broadcast_this)
{
	unsigned char bcast_mac[6]; 
	void* buffer;
	int toCopy, i;
	RREQ* req;
	RREP* rep;
	RDATA* act_data = NULL;
	char my_hostname[MAX_BUFFER_LENGTH];
	struct hostent *hp1 = NULL;
	struct hostent *hp2 = NULL;
	long addr1,addr2;
	char src_name[MAX_BUFFER_LENGTH], dest_name[MAX_BUFFER_LENGTH];
	
	memset(my_hostname,0,MAX_BUFFER_LENGTH);
	memset(src_name,0,MAX_BUFFER_LENGTH);
	memset(dest_name,0,MAX_BUFFER_LENGTH);
	
	if (gethostname(my_hostname, sizeof(my_hostname)) == -1)
	{
		printf("gethostname error. \n");
		exit(-1);
	}	
	
	for(i = 0; i < 6; i++)
		bcast_mac[i] = 0xff;
	
	/*prepare sockaddr_ll*/

	/*RAW communication*/
	socket_addr->sll_family   = PF_PACKET;	

	/*index of the network device
	see full code later how to retrieve it*/
	
	socket_addr->sll_ifindex  = interface_index;

	/*ARP hardware identifier is ethernet*/
	socket_addr->sll_hatype   = ARPHRD_ETHER;

	if (broadcast_this == my_true)
	{
		socket_addr->sll_pkttype = PACKET_BROADCAST;
		/*address length*/
		socket_addr->sll_halen    = ETH_ALEN;		
		/*MAC - begin*/
		socket_addr->sll_addr[0]  = bcast_mac[0];		
		socket_addr->sll_addr[1]  = bcast_mac[1];		
		socket_addr->sll_addr[2]  = bcast_mac[2];
		socket_addr->sll_addr[3]  = bcast_mac[3];
		socket_addr->sll_addr[4]  = bcast_mac[4];
		socket_addr->sll_addr[5]  = bcast_mac[5];		
	}
	else if (broadcast_this == my_false)
	{
		/*target is another host*/
		socket_addr->sll_pkttype  = PACKET_OTHERHOST;

		/*address length*/
		socket_addr->sll_halen    = ETH_ALEN;		
		/*MAC - begin*/
		socket_addr->sll_addr[0]  = dest_mac[0];		
		socket_addr->sll_addr[1]  = dest_mac[1];		
		socket_addr->sll_addr[2]  = dest_mac[2];
		socket_addr->sll_addr[3]  = dest_mac[3];
		socket_addr->sll_addr[4]  = dest_mac[4];
		socket_addr->sll_addr[5]  = dest_mac[5];
	}
	/*MAC - end*/
	socket_addr->sll_addr[6]  = 0x00;/*not used*/
	socket_addr->sll_addr[7]  = 0x00;/*not used*/
	

	toCopy = 0;
	if (type == R_REQUEST)
	{
		toCopy = sizeof(RREQ);
	}
	else if (type == R_REPLY) 
	{
		toCopy = sizeof(RREP);
	}
	else if (type == DATA) 
	{
		toCopy = sizeof(RDATA);
	}
	else
	{
		printf("Some other packet type.\n");
	}

	buffer = (void*)malloc(ETH_FRAME_LEN);
	memset(buffer, 0, ETH_FRAME_LEN);
	
	if (broadcast_this == my_true)
		memcpy((void*)buffer, (void*)bcast_mac, ETH_ALEN);
	else if (broadcast_this == my_false)
		memcpy((void*)buffer, (void*)dest_mac, ETH_ALEN);
	
	memcpy((void*)(buffer+ETH_ALEN), (void*)src_mac, ETH_ALEN);
	memcpy((void*)(buffer+ETH_ALEN+ETH_ALEN), &proto, sizeof(short));

	memcpy((void*)(buffer+sizeof(struct ethhdr)), &type, sizeof(type));
	memcpy((void*)(buffer+sizeof(struct ethhdr)+ sizeof(type)), data, toCopy); 

	if(type == R_REQUEST)
	{
		req = (RREQ *)&(data->request);
		addr1 = ntohl(req->src.s_addr);
		hp1 = gethostbyaddr((char *) &addr1, sizeof(addr1), AF_INET);
		memcpy(src_name, hp1->h_name, strlen(hp1->h_name));
		addr2 = ntohl(req->dest.s_addr);
		hp2 = gethostbyaddr((char *) &addr2, sizeof(addr2), AF_INET);
		memcpy(dest_name, hp2->h_name, strlen(hp2->h_name));
		//printRREQ(req);
	}
	else if (type == R_REPLY)
	{
		rep = (RREP *)&(data->reply);
		addr1 = ntohl(rep->src.s_addr);
		hp1 = gethostbyaddr((char *) &addr1, sizeof(addr1), AF_INET);
		memcpy(src_name, hp1->h_name, strlen(hp1->h_name));
		addr2 = ntohl(rep->dest.s_addr);
		hp2 = gethostbyaddr((char *) &addr2, sizeof(addr2), AF_INET);
		memcpy(dest_name, hp2->h_name, strlen(hp2->h_name));
		//printRREP(rep);		
	}
	else if (type == DATA)
	{
		act_data = (RDATA *)&(data->actual_data);
		addr1 = ntohl(act_data->source_ip.s_addr);
		hp1 = gethostbyaddr((char *) &addr1, sizeof(addr1), AF_INET);
		memcpy(src_name, hp1->h_name, strlen(hp1->h_name));
		addr2 = ntohl(act_data->dest_ip.s_addr);
		hp2 = gethostbyaddr((char *) &addr2, sizeof(addr2), AF_INET);
		memcpy(dest_name, hp2->h_name, strlen(hp2->h_name));
		//printRDATA(act_data);
	}



	
	if (hp1 != NULL && hp2 != NULL)
	{
		if (type == DATA)
		{
			if (act_data != NULL)
			{
				printf("ODR at node %s : sending frame hdr src %s dest %s\n\t\t\t ODR msg type %d src %s dest %s  Message: <%s>.\n",
				my_hostname, print_mac_address(src_mac), print_mac_address(socket_addr->sll_addr), type, src_name, dest_name, act_data->msg);
			}
		}
		else if (type == R_REQUEST || type == R_REPLY)
		{
			printf("ODR at node %s : sending frame hdr src %s dest %s\n\t\t\t ODR msg type %d src %s dest %s.\n",
			my_hostname, print_mac_address(src_mac), print_mac_address(socket_addr->sll_addr), type, src_name, dest_name); 
		}
	}	
	
	return buffer;
}

int sendFrame(int sockfd, void* eframe, struct sockaddr_ll* socket_address)
{
	int send_result;
	send_result = 0;
	
	if(eframe != NULL)
	{

		send_result = sendto(sockfd,eframe, ETH_FRAME_LEN, 0,(struct sockaddr*)socket_address, sizeof(struct sockaddr_ll));
		//free(eframe);
		if (send_result == -1) 
		{
		printf("Error sending ethernet frame.\n");
		exit(-1);
		}
	}
	else
		printf("Frame is empty!!\n");
}

client_server_lookup_table* get_from_client_server_lookup_table(client_server_lookup_table* head, int destination_port)
{
	client_server_lookup_table* traverse_node;
	traverse_node = head;
	
	while(traverse_node != NULL)
	{
		if( traverse_node->dest_port == destination_port)
			return traverse_node;
		traverse_node= traverse_node->next;
	}
	return NULL;	
}

client_server_lookup_table* get_path_from_client_server_lookup_table(client_server_lookup_table* head, char* path)
{
	client_server_lookup_table* traverse_node;
	traverse_node = head;
	
	while(traverse_node != NULL)
	{
		if(strcmp(path, traverse_node->path) == 0)
			return traverse_node;
		traverse_node= traverse_node->next;
	}
	return NULL;	
}

client_server_lookup_table* remove_stale_entries_from_table(client_server_lookup_table* head)
{
	struct client_server_lookup_table *to_be_deleted;
	struct client_server_lookup_table *traverse_node = head;
	struct client_server_lookup_table *previous_node = NULL;
	unsigned long int curtime = get_current_time_millis(); //time(NULL);
	while((traverse_node!=NULL) && (traverse_node->timestamp != 0) &&((traverse_node->timestamp + CLIENT_SERVER_TIMEOUT_STALENESS) < curtime))
	{
		to_be_deleted = traverse_node;
		traverse_node = traverse_node->next;
		previous_node = traverse_node;
		//free(to_be_deleted);
	}
	if(traverse_node == NULL)
	{
		head = NULL;
		return NULL;
	}
	previous_node = traverse_node;
	head = previous_node;
	traverse_node = traverse_node->next;
	while(traverse_node != NULL)
	{
		if((traverse_node->timestamp != 0) && ((traverse_node->timestamp + CLIENT_SERVER_TIMEOUT_STALENESS) < curtime))
		{
			to_be_deleted = traverse_node;
			previous_node->next = traverse_node->next;
			//free(to_be_deleted);
			traverse_node = previous_node->next;
		}
		else
		{
			traverse_node = traverse_node->next;
			previous_node = previous_node->next;
		}
	}	

	return head;
}

client_server_lookup_table* add_to_client_server_lookup_table(client_server_lookup_table* head, int port, char* path, my_bool permanent)
{
	client_server_lookup_table* new_entry;
	client_server_lookup_table* traverse_node;

	traverse_node = head;
	new_entry = malloc(sizeof(client_server_lookup_table));
	memset(new_entry, 0, sizeof(client_server_lookup_table));

	memcpy(new_entry->path, path, strlen(path));
	new_entry->dest_port = port;
	
	if (permanent == my_true)
		new_entry->timestamp = 0;
	else
		new_entry->timestamp = get_current_time_millis();//time(NULL);

	if (head != NULL)
	{
		while(traverse_node->next != NULL)
		{
			traverse_node = traverse_node->next;
		}
		traverse_node->next = new_entry;
	}
	else
	{
		head = new_entry;
	}
	
	return head;	
}

void print_client_server_table(client_server_lookup_table* head)
{
	client_server_lookup_table* traverse_node;
	
	traverse_node = head;

	printf("************** PORT DEMULTIPLEX TABLE **************\n");
	printf("port\tsun-path\ttimestamp(ms)\n");
	while(traverse_node != NULL)
	{
		printf("%d\t%s\t%ld\n",traverse_node->dest_port, traverse_node->path, traverse_node->timestamp);
		traverse_node = traverse_node->next;
	}
	printf("************** PORT DEMULTIPLEX TABLE **************\n");
}

pending_frame_list* addFrameToPendingList(pending_frame_list* head, my_packet_type type, unsigned long dest_ip, union eth_payload* data)
{
	pending_frame_list* new_entry;
	pending_frame_list* traverse_node;
	
	//printf("In addFrameToPendingList()**********************\n");

	traverse_node = head;
	new_entry = malloc(sizeof(pending_frame_list));
	memset(new_entry, 0, sizeof(pending_frame_list));

	new_entry->dest_ip.s_addr = dest_ip;
	new_entry->payload_to_send.payload_type = type;
	memcpy(&(new_entry->payload_to_send.actual_payload_content), data, sizeof(union eth_payload));
	
	/*if (type == R_REPLY)
	{
		printf("rrep destip %s\n", inet_ntoa(new_entry->dest_ip));
		printRREP(&(new_entry->payload_to_send.actual_payload_content.reply));
	}
	if (type == DATA)
	{
		printf("data destip %s\n", inet_ntoa(new_entry->dest_ip));
		printRDATA(&(new_entry->payload_to_send.actual_payload_content.actual_data));
	}*/

	if (head != NULL)
	{
		while(traverse_node->next != NULL)
		{
			traverse_node = traverse_node->next;
		}
		traverse_node->next = new_entry;
	}
	else
	{
		head = new_entry;
	}
	
	return head;
}

pending_frame_list* deleteFrameFromPendingList(pending_frame_list* head, unsigned long dest_ip)
{
	pending_frame_list* traverse_node;
	pending_frame_list* toDelete = NULL;
	
	traverse_node = head;
	
	//printf("in deleteFrmeFromPendingList\n");
	if (head != NULL)
	{
		if(traverse_node->dest_ip.s_addr == dest_ip)
		{
			if (traverse_node->next != NULL)
				head = traverse_node->next;
			else
				head = NULL;
			traverse_node->next = NULL;
			free(traverse_node);
			return head;
		}
		while(traverse_node->next != NULL)
		{
			if (traverse_node->dest_ip.s_addr == dest_ip)
			{
				toDelete = traverse_node->next;
				traverse_node->next = toDelete->next;
				toDelete->next = NULL;
				free(toDelete);
			}
			traverse_node = traverse_node->next;
		}
	}
	return head;
}

my_bool is_old_rreq(rreqs_sent* head, unsigned long source_ip, int broadcast_id)
{
	rreqs_sent* traverse_node;
	traverse_node = head;
	while(traverse_node != NULL)
	{
		if (traverse_node->source_ip.s_addr == source_ip)
			if (broadcast_id <= traverse_node->broadcast_id )
				return my_true;
		traverse_node= traverse_node->next;
	}
	return my_false;
	
}

rreqs_sent* add_or_update_rreqs_sent(rreqs_sent* head, unsigned long source_ip, int broadcast_id)
{
	rreqs_sent* traverse_node;
	int found;
	
	traverse_node = head;
	found = -1;
	while(traverse_node != NULL)
	{
		if( traverse_node->source_ip.s_addr == source_ip)
		{
			traverse_node->broadcast_id = broadcast_id;
			found = 1;
		}
		traverse_node= traverse_node->next;
	}
	
	if (found == 1)
		return head;
	else
	{
		traverse_node = head;
		if(traverse_node == NULL)
		{
			head = malloc(sizeof(rreqs_sent));
			memset(head, 0, sizeof(rreqs_sent));
			
			head->source_ip.s_addr = source_ip;
			head->broadcast_id = broadcast_id;
			
			return head;
		}
		else
		{
			while(traverse_node->next != NULL)
			{
				traverse_node = traverse_node->next;
			}
			
			traverse_node->next = malloc(sizeof(rreqs_sent));
			memset(traverse_node->next, 0, sizeof(rreqs_sent));
			
			traverse_node->next->source_ip.s_addr = source_ip;
			traverse_node->next->broadcast_id = broadcast_id;
			
			return head;
		}
	}
}


routing_table* getRoutingEntry(routing_table* head, unsigned long dest_ip)
{
	routing_table* traverse_node;
	traverse_node = head;
	
	while(traverse_node != NULL)
	{
		if( traverse_node->dest_ip.s_addr == dest_ip)
			return traverse_node;
		traverse_node= traverse_node->next;
	}
	return NULL;
}

my_bool update_time_stamp_to_make_stale(routing_table* head, unsigned long dest_ip, unsigned long to_update)
{
	routing_table* traverse_node;
	traverse_node = head;
	
	while(traverse_node != NULL)
	{
		if( traverse_node->dest_ip.s_addr == dest_ip)
		{
			traverse_node->timestamp = to_update;
			return my_true;
		}
		traverse_node= traverse_node->next;
	}
	return my_false;	
}

routing_table* createRoutingEntry(unsigned long dest_ip, int interface_index, unsigned char* neighbour_mac, int hop_count)
{
	routing_table *new_entry;
	new_entry = malloc(sizeof(routing_table));
	memset(new_entry, 0, sizeof(routing_table));
	
	new_entry->dest_ip.s_addr = dest_ip;
	new_entry->interface_index = interface_index;
	new_entry->hop_count = hop_count;
	memcpy(new_entry->neighbour_mac, neighbour_mac, ETH_ALEN);
	new_entry->timestamp = get_current_time_millis(); //time(NULL);
	new_entry->next = NULL;
	
	return new_entry;
}

routing_table* addRoutingEntry(routing_table* head, routing_table* nodeToAdd)
{
	routing_table* traverse_node;
	int found;
	
	traverse_node = head;
	found = -1;
	while(traverse_node != NULL)
	{
		if( traverse_node->dest_ip.s_addr == nodeToAdd->dest_ip.s_addr)
		{
			traverse_node->interface_index = nodeToAdd->interface_index;
			traverse_node->hop_count = nodeToAdd->hop_count;
			memcpy(traverse_node->neighbour_mac, nodeToAdd->neighbour_mac, ETH_ALEN);
			traverse_node->timestamp = get_current_time_millis(); //time(NULL);
			found = 1;
		}
		traverse_node= traverse_node->next;
	}
	
	if (found == 1)
		return head;
	else
	{
		traverse_node = head;
		if(traverse_node == NULL)
		{
			head = nodeToAdd;
			printRoutingTable(head);
			return head;
		}
		else
		{
			while(traverse_node->next != NULL)
			{
				traverse_node = traverse_node->next;
			}
			
			traverse_node->next = nodeToAdd;
			printRoutingTable(head);
			return head;
		}
	}
}

#if 0
routing_table* addRoutingEntry(routing_table* head, routing_table* nodeToAdd)
{
	routing_table* traverse_node;
	//my_bool updated = my_false;
	
	traverse_node = head;
	
	if (getLengthofRoutingTable(head) != 0)
	{
		while(traverse_node->next != NULL)
		{
			/*if (traverse_node->dest_ip.s_addr == nodeToAdd->dest_ip.s_addr)
			{
				updated = my_true;
				traverse_node->interface_index = nodeToAdd->interface_index;
				traverse_node->hop_count = nodeToAdd->hop_count;
				memcpy(traverse_node->neighbour_mac, nodeToAdd->neighbour_mac, ETH_ALEN);
				traverse_node->timestamp = get_current_time_millis(); //time(NULL);				
				
			}*/
			traverse_node = traverse_node->next;
		}
		//if (updated != my_true)
			traverse_node->next = nodeToAdd;
	}
	else
	{
		head = nodeToAdd;
	}
	
	return head;
}

#endif 

my_bool updateRoutingEntry(routing_table* head, unsigned long dest_ip, int interface_index, unsigned char* neighbour_mac, int hop_count)
{
	routing_table* traverse_node;
	traverse_node = head;
	
	while(traverse_node != NULL)
	{
		if( traverse_node->dest_ip.s_addr == dest_ip)
		{
			traverse_node->hop_count = hop_count;
			traverse_node->interface_index = interface_index;
			memcpy(traverse_node->neighbour_mac, neighbour_mac, ETH_ALEN);
			traverse_node->timestamp = get_current_time_millis(); //time(NULL);
			printRoutingTable(head);
			return my_true;
		}
		traverse_node= traverse_node->next;
	}
	//printRoutingTable(head);
	return my_false;	
}

int getLengthofRoutingTable(routing_table* head)
{
	int count;
	routing_table* traverse_node;
	
	count = 0;
	traverse_node = head;
	
	if (traverse_node == NULL)
		count = 0;
	else
	{
		while(traverse_node != NULL)
		{
			count++;
			traverse_node = traverse_node->next;
		}
	}
	
	return count;
}

char * my_sock_ntop(const struct sockaddr *sa, socklen_t salen)
{
    char portstr[8];
    static char str[128];
    struct sockaddr_in *sin;

        switch (sa->sa_family) 
        {
                case AF_INET: 
                {
                        sin = (struct sockaddr_in *) sa;

                        if (inet_ntop(AF_INET, &sin->sin_addr, str, sizeof(str)) == NULL)
                                return(NULL);
                        if (ntohs(sin->sin_port) != 0) 
                        {
                                snprintf(portstr, sizeof(portstr), ":%d", ntohs(sin->sin_port));
                                strcat(str, portstr);
                        }
                        return(str);
                }
        }
}

/* All functions needed for debugging are below, basically prints the contents of packets and tables. */
void printRREQ(RREQ* req)
{
	struct in_addr src;
	struct in_addr dest;
	
	src.s_addr = htonl(req->src.s_addr);
	dest.s_addr = htonl(req->dest.s_addr);
	
	printf("-------RREQ--------\n");
	printf("source ip: %s,", inet_ntoa(src));
	printf(" destination ip: %s,", inet_ntoa(dest));
	printf(" hop count: %d,", ntohl(req->hop_count));
	printf(" broadcast id: %d,", ntohl(req->broadcast_id));
	printf(" rediscover: %s,", ntohl(req->rediscover) == 0 ? "false" : "true");
	printf(" dont_reply: %s.", ntohl(req->dont_reply) == 0 ? "false" : "true");
	printf("\n-------------------\n");
}

void printRREP(RREP* rep)
{
	struct in_addr src;
	struct in_addr dest;
	struct in_addr final_dest;
	
	src.s_addr = htonl(rep->src.s_addr);
	dest.s_addr = htonl(rep->dest.s_addr);
	final_dest.s_addr = htonl(rep->final_dest.s_addr);
	
	printf("-------RREP--------\n");
	printf("source ip: %s,", inet_ntoa(src));
	printf(" destination ip: %s,", inet_ntoa(dest));	
	printf(" hop count: %d,", ntohl(rep->hop_count));
	printf(" rediscover: %s,", ntohl(rep->rediscover) == 0 ? "false" : "true");
	printf(" final_dest: %s.", inet_ntoa(final_dest));
	printf("\n-------------------\n");
}

void printRDATA(RDATA* data)
{
	struct in_addr src;
	struct in_addr dest;
	
	src.s_addr = htonl(data->source_ip.s_addr);
	dest.s_addr = htonl(data->dest_ip.s_addr);
	
	printf("-------DATA--------\n");
	printf("source ip: %s,", inet_ntoa(src));
	printf(" destination ip: %s,", inet_ntoa(dest));
	printf(" source port: %d,", ntohl(data->source_port));
	printf(" dest port: %d,", ntohl(data->dest_port));
	printf(" hop count: %d,", ntohl(data->hop_count));
	//printf(" message length: %d,", ntohl(data->msg_length));
	printf(" message : %s.", data->msg);
	printf("\n-------------------\n");
}

char* print_mac_address(unsigned char* mac_addr)
{
	char *address;
	int i,j;
	
	address = malloc(19);
	memset(address, 0, 19);
	
	for(i = 0, j = 0; i < 6; i++, j=j+3)
	{
		if (i == 5)
			sprintf(address+j, "%02x", mac_addr[i]);
		else
			sprintf(address+j, "%02x:", mac_addr[i]);
	}
	
	return address;
}

void printEthernetHeader(my_ether_hdr* hdr)
{
	printf("Source MAC: %s\n", print_mac_address(hdr->src_mac));
	printf("Dest MAC: %s\n", print_mac_address(hdr->dest_mac));
	printf("Protocol: %x\n", htons(hdr->proto));
}

void printEthernetPayload(payload* pld)
{
	switch(pld->payload_type)
	{
		case R_REQUEST:
			printRREQ(&(pld->actual_payload_content.request));
			break;
		case R_REPLY:
			printRREP(&(pld->actual_payload_content.reply));
			break;
		case DATA:
			printRDATA(&(pld->actual_payload_content.actual_data));
			break;
		default:
			printf("Unsupported packet type.\n");
	}
}

void printRoutingEntry(routing_table *node)
{
	if(node != NULL)
	{
		printf("(dest_ip %s, interface %d, hop_count %d, neighbour_mac %s, timestamp %ld\n", 
		       inet_ntoa(node->dest_ip), node->interface_index,
		       node->hop_count, print_mac_address(node->neighbour_mac),node->timestamp);
	}
}

void printRoutingTable(routing_table* head)
{
	routing_table* traverse_node;
	struct hostent *hp;
	
	traverse_node = head;
	
	if(getLengthofRoutingTable(head) != 0)
	{
		printf("-------------- ROUTING TABLE -------------- \n");
		printf("IP-Address\tInterface\tHop count\tNeighbour HWaddr\tTimestamp(ms)\n");
		while(traverse_node != NULL)
		{
			hp = gethostbyaddr((char *) &(traverse_node->dest_ip.s_addr), sizeof(traverse_node->dest_ip.s_addr), AF_INET);
			if(hp != NULL)
			{
				printf("%s\t\t%d\t\t%d\t\t%s\t%ld\n", 
				hp->h_name, traverse_node->interface_index, 
				traverse_node->hop_count, print_mac_address(traverse_node->neighbour_mac),traverse_node->timestamp);
			}
			traverse_node = traverse_node->next;
		}
		printf("-------------- ROUTING TABLE -------------- \n");
	}
	else
	{
		printf("Routing table empty\n");
	}
}

void printPendingList(pending_frame_list *head)
{
	pending_frame_list *traverse_node;
	
	traverse_node = head;
	
	if(traverse_node != NULL)
	{
		printf("############## PENDING FRAMES ##############\n");
		while(traverse_node != NULL)
		{
			if (traverse_node->payload_to_send.payload_type == R_REPLY)
			{
				printRREP(&(traverse_node->payload_to_send.actual_payload_content.reply));
			}
			if (traverse_node->payload_to_send.payload_type == DATA)
			{
				printRDATA(&(traverse_node->payload_to_send.actual_payload_content.actual_data));
			}
			traverse_node = traverse_node->next;
		}
		printf("############## PENDING FRAMES ##############\n");
	}
	else
	{
		printf("Pending frame list is empty\n");
	}
}

void print_rreqs_sent(rreqs_sent* head)
{
	rreqs_sent* traverse_node;
	traverse_node = head;
	
	if(traverse_node != NULL)
	{
		printf("############## PREVIOUSLY SEEN RREQs LIST ##############\n");
		while(traverse_node != NULL)
		{
			printf("(source_ip %s, broadcast_id %d)\n", inet_ntoa(traverse_node->source_ip), traverse_node->broadcast_id); 
			traverse_node = traverse_node->next;
		}
		printf("############## PREVIOUSLY SEEN RREQs LIST ##############\n");
	}
	else
	{
		printf("rreqs_sent empty\n");
	}	
}
