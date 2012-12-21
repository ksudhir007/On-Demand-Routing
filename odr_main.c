
#include "myheaders.h"

routing_table *routing_head = NULL;
rreqs_sent *rreqs_head = NULL;
struct hwa_info *hwahead = NULL;
pending_frame_list *pending_head = NULL;
client_server_lookup_table* cliser_lookup_head = NULL;
char my_canonical_ip[INET_ADDRSTRLEN];
unsigned long stale_value;
static int my_odr_broadcast_id;
static int client_server_counter;

char* get_mac_address(int interface_index)
{
	struct hwa_info *hwa;
	hwa = hwahead;
	
	while(hwa != NULL)
	{
		if ((hwa->if_index == interface_index) && ((hwa->ip_alias) != IP_ALIAS))
		{
			return hwa->if_haddr;
		}
		hwa = hwa->hwa_next;
	}
	return NULL;
}

my_ether_hdr* get_ethernet_hdr(void *buffer)
{
	my_ether_hdr *received_hdr;
	received_hdr = NULL;
	
	if (buffer != NULL)
	{
		received_hdr = malloc(sizeof(my_ether_hdr));
		memset(received_hdr, 0, sizeof(my_ether_hdr));
	
		memcpy(received_hdr, buffer, sizeof(my_ether_hdr));
	}
	return received_hdr;
}

payload* get_ethernet_payload(void *buffer)
{
	payload* received_payload;
	received_payload = NULL;
	
	if(buffer != NULL)
	{
		received_payload = malloc(sizeof(payload));
		memset(received_payload, 0, sizeof(payload));
		
		memcpy(received_payload, buffer, sizeof(payload));
	}
	return received_payload;
}

void build_interface_list()
{
	struct hwa_info *hwa;
	struct sockaddr	*sa;
	
	hwa = hwahead = Get_hw_addrs();
	
	
	while(hwa != NULL)
	{
		if ((strcmp(hwa->if_name, "eth0") == 0) && ((hwa->ip_alias) != IP_ALIAS))
		{
			if ((sa = hwa->ip_addr) != NULL)
				sprintf(my_canonical_ip, "%s", (char *)my_sock_ntop(sa, sizeof(*sa)));
			
		}
		hwa = hwa->hwa_next;
	}
}

void broadcastRREQ(int pf_sockfd,RREQ* req)
{
	struct hwa_info *hwa;
	void* eframe;
	struct sockaddr_ll socket_addr;
	
	hwa = hwahead;
	
	while(hwa != NULL)
	{
		if ((strcmp(hwa->if_name, "eth0") == 0) || (strcmp(hwa->if_name, "lo") == 0) || ((hwa->ip_alias) == IP_ALIAS))
			;
		else
		{
			eframe = (void*)buildNewFrame(R_REQUEST, NULL, hwa->if_haddr, htons(MY_PROTO_ID),
				       hwa->if_index, &socket_addr, (union eth_payload*)req, my_true);
			sendFrame(pf_sockfd, eframe, &socket_addr);
		}
		hwa = hwa->hwa_next;
	}
}

void broadcastRREQonAllInterfacesExceptThis(int pf_sockfd, RREQ* req, int leaveThisInterface)
{
	struct hwa_info *hwa;
	void* eframe;
	struct sockaddr_ll socket_addr;
	
	hwa = hwahead;
		
	while(hwa != NULL)
	{
		if ((strcmp(hwa->if_name, "eth0") == 0) || (strcmp(hwa->if_name, "lo") == 0) || ((hwa->ip_alias) == IP_ALIAS) ||
			(hwa->if_index == leaveThisInterface))
			;
		else
		{
			eframe = (void*)buildNewFrame(R_REQUEST, NULL, hwa->if_haddr, htons(MY_PROTO_ID),
				       hwa->if_index, &socket_addr, (union eth_payload*)req, my_true);
			sendFrame(pf_sockfd, eframe, &socket_addr);
		}
		hwa = hwa->hwa_next;
	}	
}

void processDomainSocketData(int pf_sockfd, int socket_fd, char* canonical_ip, int dest_port, char* message, my_bool flag, struct sockaddr_un *socket_address)
{
	struct in_addr addr;
	routing_table *table_entry;
	RREQ* new_req;
	RDATA* new_data;
	void* eframe;
	int source_port;
	struct sockaddr_ll socket_addr;
	client_server_lookup_table *entry;
	
	memset(&socket_addr, 0, sizeof(struct sockaddr_ll));
	
	// remove stale entries
	// lookup if the path already exits, 
	//	if exists, get source port and send it here
	//	else, create an entry, get entry port number and send it here
	//printf("before adding client data......\n");
	cliser_lookup_head = (client_server_lookup_table*)remove_stale_entries_from_table(cliser_lookup_head);
	entry = (client_server_lookup_table*)get_path_from_client_server_lookup_table(cliser_lookup_head, socket_address->sun_path);
	
	if(entry == NULL)
	{
		source_port = client_server_counter++;
		cliser_lookup_head = (client_server_lookup_table*)add_to_client_server_lookup_table(cliser_lookup_head,
											    source_port, socket_address->sun_path, my_false);
	}
	else
	{
		source_port = entry->dest_port;
	}
	
	//print_client_server_table(cliser_lookup_head);
	//printf("after adding client data......\n");
	new_data = (RDATA*)buildRDATA(inet_addr(my_canonical_ip),inet_addr(canonical_ip), source_port, dest_port, 0, strlen(message), message); // source port 0, hop count 1??
	
	if(!inet_pton(AF_INET, canonical_ip, &addr))
	{
                fprintf(stderr, "Could not convert address\n");
                return;
        }
        
        
        
        /* If same destination ?*/
	/*Lookup the routing table
		Found and not staled -> build RDATA, send
		Not found or staled or flag set -> add to a list(capable of storing data or rrep) and broadcast rreq on all interfaces
	*/
	table_entry = (routing_table*)getRoutingEntry(routing_head, addr.s_addr);
	
	if ((table_entry == NULL) || (table_entry->timestamp + stale_value < get_current_time_millis())) 
	{
		//printf("it should enter here only \n");
		pending_head = (pending_frame_list*)addFrameToPendingList(pending_head, DATA, inet_addr(canonical_ip), (union eth_payload*)new_data);
		//printf("dereferencing %x, %x, %x\n", pending_head, (pending_head->dest_ip), pending_head->dest_ip.s_addr);
		//printf("added frame to pending list\n");
		//printRDATA(new_data);
		printPendingList(pending_head);
		new_req = (RREQ*)buildRREQ(inet_addr(my_canonical_ip),inet_addr(canonical_ip),0,my_odr_broadcast_id, flag, my_false);
		broadcastRREQ(pf_sockfd,new_req);
		rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
		my_odr_broadcast_id++;
	}
	else if ((flag == my_true)) // stale value in table ??
	{
		// Remove entry from routing table, send rreq ??
		//printf("flag true\n");
		pending_head = (pending_frame_list*)addFrameToPendingList(pending_head, DATA, inet_addr(canonical_ip),(union eth_payload*)new_data);
		new_req = (RREQ*)buildRREQ(inet_addr(my_canonical_ip),inet_addr(canonical_ip),0,my_odr_broadcast_id, flag, my_false);
		broadcastRREQ(pf_sockfd,new_req);
		rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
		my_odr_broadcast_id++;
	}
	else
	{
		//printf("direct sending\n");
		// We have the route in table, send it.
		//printf("we have the route in table, sending it in processDomainSocketData()\n");
		eframe = (void*)buildNewFrame(DATA, table_entry->neighbour_mac, get_mac_address(table_entry->interface_index), htons(MY_PROTO_ID),
				       table_entry->interface_index, &socket_addr, (union eth_payload*)new_data, my_false); 
		sendFrame(pf_sockfd, eframe, &socket_addr);
		//printf("^^^^^^^^^^^^^^^^^^^^^^^^^^ after sending frame \n");
	}
	
}

void processRREQFrame(int pf_sockfd, my_ether_hdr *received_hdr, payload* received_payload, struct sockaddr_ll *socket_address)
{
	// immediately increment the hop-count. This guy took one hop to reach you.
	// check if it is old by looking up in RREQS sent list
	//	if it is not old i.e it came from unknown source address
	//		add this to rreqs_sent list
	//		add entry in the routing table
	//	if it is old, i.e we knew about the source before also
	//		take source of received RREQ and look in the routing table
	//			if not NULL (it should definetely be not null since its previous source, but for pointer safety)
	//				get the entry
	//				if rediscover flag is set, update the entry of the timestamp by adding stale value amount
	//				if not stale
	//					received payload hop count <= entry hop count?
	//						update routing entry
	//					received payload hop count > entry hop count ?
	//						return;
	//				if stale
	//					update routing entry
	// check the destination address of the received payload with my canonical ip
	//	if i am the destination
	//		if dont_reply == false
	//			send RREP with me as source and source from received payload as destination
	//		if rediscover_flag == false
	//			broadcast the received payload on all interfaces with the dont_reply flag set
	//	if i am not the destination
	//		check dest address of received payload in the routing table
	//		if entry not found in routing table
	//			broadcast as it is on all interfaces except the one on which it is received.
	//		if entry is found
	//			if rediscover flag is set, update the entry of the timestamp by adding stale value amount
	//			if stale
	//				broadcast as it is on all interfaces except the one on which it is received.
	//			if not stale
	//				if dont_reply == false
	//					send RREP with me as source and source from received payload as destination
	//				broadcast the received payload on all interfaces with the dont_reply flag set
	

	// Declarations start here
	my_bool is_old = -1;
	routing_table* new_entry;
	routing_table* to_compare_source_entry;
	routing_table* to_compare_dest_entry;
	my_bool bool_return_value;
	RREP* new_rep;
	void* eframe;
	
	// Declarations end here
	
	received_payload->actual_payload_content.request.hop_count = htonl(ntohl(received_payload->actual_payload_content.request.hop_count) + 1);
	
	is_old = is_old_rreq(rreqs_head, ntohl(received_payload->actual_payload_content.request.src.s_addr),
							      ntohl(received_payload->actual_payload_content.request.broadcast_id));
	
	if(is_old == my_false)
	{
		// add this RREQ to the list so that I dont accept duplicate RREQs for this source again.
		rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,ntohl(received_payload->actual_payload_content.request.src.s_addr),
								     ntohl(received_payload->actual_payload_content.request.broadcast_id));
		// Add entry
		new_entry = (routing_table*)createRoutingEntry(ntohl(received_payload->actual_payload_content.request.src.s_addr),
								socket_address->sll_ifindex, received_hdr->src_mac,
								ntohl(received_payload->actual_payload_content.request.hop_count));
		routing_head = (routing_table*)addRoutingEntry(routing_head, new_entry);
		
	}
	else if (is_old == my_true)
	{
		//printf("******************************************************************************************************\n");
		if (ntohl(received_payload->actual_payload_content.request.rediscover) == my_true)
		{
			//received_payload->actual_payload_content.request.rediscover == htonl(my_false);
			return;
		}
		if(inet_addr(my_canonical_ip) == ntohl(received_payload->actual_payload_content.request.src.s_addr))
		{
			//free(received_hdr);
			//free(received_payload);
			//printf("********** old mine returning\n");
			return;
		}
		
		if(ntohl(received_payload->actual_payload_content.request.rediscover) == my_true)
		{
			bool_return_value = update_time_stamp_to_make_stale(routing_head,
										ntohl(received_payload->actual_payload_content.request.src.s_addr),
										0);
		}
		to_compare_source_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.request.src.s_addr));
			
		if (to_compare_source_entry != NULL)
		{
			if(to_compare_source_entry->timestamp + stale_value < get_current_time_millis())
			{
				// stale
				bool_return_value = updateRoutingEntry(routing_head,ntohl(received_payload->actual_payload_content.request.src.s_addr),
									socket_address->sll_ifindex, received_hdr->src_mac,
									ntohl(received_payload->actual_payload_content.request.hop_count));
				//printf("Routing table update status : %d\n", bool_return_value);	
				received_payload->actual_payload_content.request.dont_reply = htonl(my_true);
				broadcastRREQonAllInterfacesExceptThis(pf_sockfd, &(received_payload->actual_payload_content.request), socket_address->sll_ifindex);
				rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
				my_odr_broadcast_id++;
				return;
			}
			else
			{
				if(ntohl(received_payload->actual_payload_content.request.hop_count) <= to_compare_source_entry->hop_count)
				{
					bool_return_value = updateRoutingEntry(routing_head,ntohl(received_payload->actual_payload_content.request.src.s_addr),
										socket_address->sll_ifindex, received_hdr->src_mac,
										ntohl(received_payload->actual_payload_content.request.hop_count));
					//printf("Routing table update status : %d\n", bool_return_value);
				}
				else
				{
					return;
				}
			}
		}
	}
	//
	if (inet_addr(my_canonical_ip) == ntohl(received_payload->actual_payload_content.request.dest.s_addr))
	{
		if(ntohl(received_payload->actual_payload_content.request.dont_reply) == my_false)
		{
			new_rep = (RREP*)buildRREP(inet_addr(my_canonical_ip),htonl(received_payload->actual_payload_content.request.src.s_addr),0,
						   ntohl(received_payload->actual_payload_content.request.rediscover),inet_addr(my_canonical_ip));
			eframe = (void*)buildNewFrame(R_REPLY, received_hdr->src_mac, get_mac_address(socket_address->sll_ifindex),
						      htons(MY_PROTO_ID), socket_address->sll_ifindex, socket_address,(union eth_payload*)new_rep, my_false);
			sendFrame(pf_sockfd, eframe, socket_address);
		}
		if(ntohl(received_payload->actual_payload_content.request.rediscover) == my_true)
		{
			received_payload->actual_payload_content.request.dont_reply = htonl(my_true);
			broadcastRREQonAllInterfacesExceptThis(pf_sockfd, &(received_payload->actual_payload_content.request), socket_address->sll_ifindex);
			rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
			my_odr_broadcast_id++;			
		}
	}
	else
	{
		// not the destination
		to_compare_dest_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.request.dest.s_addr));
		
		if(to_compare_dest_entry == NULL)
		{
			broadcastRREQonAllInterfacesExceptThis(pf_sockfd, &(received_payload->actual_payload_content.request), socket_address->sll_ifindex);
			rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
			my_odr_broadcast_id++;			
		}
		else
		{
			if(ntohl(received_payload->actual_payload_content.request.rediscover) == my_true)
			{
				bool_return_value = update_time_stamp_to_make_stale(routing_head,
							ntohl(received_payload->actual_payload_content.request.dest.s_addr),
							0);
			}
			to_compare_dest_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.request.dest.s_addr));
			if (to_compare_dest_entry != NULL)
			{
				if(to_compare_dest_entry->timestamp + stale_value < get_current_time_millis())
				{ //stale
					broadcastRREQonAllInterfacesExceptThis(pf_sockfd, &(received_payload->actual_payload_content.request), socket_address->sll_ifindex);
					rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
					my_odr_broadcast_id++;					
				}
				else
				{
					if(ntohl(received_payload->actual_payload_content.request.dont_reply) == my_false)
					{ 
						// send using the route in the routing table.
						new_rep = (RREP*)buildRREP(inet_addr(my_canonical_ip),
									   htonl(received_payload->actual_payload_content.request.src.s_addr),0,
									   ntohl(received_payload->actual_payload_content.request.rediscover),
									   ntohl(received_payload->actual_payload_content.request.dest.s_addr));
						eframe = (void*)buildNewFrame(R_REPLY, received_hdr->src_mac, get_mac_address(socket_address->sll_ifindex), 
								htons(MY_PROTO_ID), socket_address->sll_ifindex, socket_address,(union eth_payload*)new_rep, my_false);
						sendFrame(pf_sockfd, eframe, socket_address);
					}
					received_payload->actual_payload_content.request.dont_reply = htonl(my_true);
					broadcastRREQonAllInterfacesExceptThis(pf_sockfd, &(received_payload->actual_payload_content.request), socket_address->sll_ifindex);
					rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
					my_odr_broadcast_id++;					
				}
			}
		}
	}	
}

void processRREPFrame(int pf_sockfd, my_ether_hdr *received_hdr, payload* received_payload, struct sockaddr_ll *socket_address)
{
	// immediately increment the hop-count. This guy took one hop to reach you.
	// Get the source address of the received frame and lookup in the routing table 
	//	if routing table has entry
	//		if the entry is not stale
	//			if received hop_count <= table entry hop count
	//				update path in routing table
	//				relay RREP in that path
	//			if received hop  > table entry hop count
	//				free, kill here itself.
	//		if entry is stale
	//			update with new entry
	//			relay rrep
	//	if routing table does not have entry
	//		create an entry with source ip in frame as destination ip in table
	//		check the destination address of the received packet with my canonical ip
	//		if i am the destination
	//			for every frame in the pending frame list
	//				get destination and look in table
	//				if entry is there, send the frame 
	//				delete the frame from the list
	//		if i am not the destination
	//			check if the routing table has an entry for the destination of received packet
	//			if routing table has entry
	//				if rediscover flag is set, update the entry of the timestamp by adding stale value amount
	//				if stale
	//					add RREP to the pending list, 
	//					create RREQ with source as my canonical ip, increment broadcast id, add to rreqs_sent list
	//					send this RREQ on all interfaces except the one which you have received from
	//				if not stale
	//					relay rrep using that route
	//			if routing table doesnt have entry
	//				add RREP to the pending list, 
	//				create RREQ with source as my canonical ip, increment broadcast id, add to rreqs_sent list
	//				send this RREQ on all interfaces except the one which you have received from

	routing_table* to_compare_entry;
	routing_table* new_entry;
	routing_table* to_send_entry;
	routing_table* to_compare_dest_entry;
	pending_frame_list *traverse_node;
	RREQ* new_req;
	RREP* new_rep;
	void* eframe;
	int to_send_hop_count;
	my_bool bool_return_value;
	unsigned long to_delete_from_pending_list;
	
	traverse_node = pending_head;
	
	received_payload->actual_payload_content.reply.hop_count = htonl(ntohl(received_payload->actual_payload_content.reply.hop_count) + 1);

	if(ntohl(received_payload->actual_payload_content.reply.rediscover) == my_true)
	{
		bool_return_value = update_time_stamp_to_make_stale(routing_head,
									ntohl(received_payload->actual_payload_content.reply.src.s_addr),
									0);
	}	
	to_compare_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.reply.src.s_addr));
	
	if (to_compare_entry != NULL) 
	{
		if(to_compare_entry->timestamp + stale_value < get_current_time_millis())
		{ //stale
			bool_return_value = updateRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.reply.src.s_addr),
								socket_address->sll_ifindex,received_hdr->src_mac,
								ntohl(received_payload->actual_payload_content.reply.hop_count));
			goto after_stale_relay_rrep;
		}
		else
		{ //not stale
		
			if(ntohl(received_payload->actual_payload_content.reply.hop_count) <= to_compare_entry->hop_count)
			{
				bool_return_value = updateRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.reply.src.s_addr),
									socket_address->sll_ifindex,received_hdr->src_mac,
									ntohl(received_payload->actual_payload_content.reply.hop_count));
				goto after_stale_relay_rrep;
				/*
				// relay rrep here
				// send using the route in the routing table.
				new_rep = (RREP*)buildRREP(ntohl(received_payload->actual_payload_content.reply.src.s_addr),
							   ntohl(received_payload->actual_payload_content.reply.dest.s_addr),
							   ntohl(received_payload->actual_payload_content.reply.hop_count),
							   ntohl(received_payload->actual_payload_content.reply.rediscover));
				eframe = (void*)buildNewFrame(R_REPLY, to_compare_entry->neighbour_mac, get_mac_address(to_compare_entry->interface_index), 
						htons(MY_PROTO_ID), to_compare_entry->interface_index, socket_address,(union eth_payload*)new_rep, my_false);
				sendFrame(pf_sockfd, eframe, socket_address);				
				*/
			}
			else
			{
				//free(received_hdr);
				//free(received_payload);
				return;
			}
		}
	}
	else
	{
		new_entry = (routing_table*)createRoutingEntry(ntohl(received_payload->actual_payload_content.reply.src.s_addr),
				socket_address->sll_ifindex, received_hdr->src_mac,
				ntohl(received_payload->actual_payload_content.reply.hop_count));
		routing_head = (routing_table*)addRoutingEntry(routing_head, new_entry);
after_stale_relay_rrep:
		if(inet_addr(my_canonical_ip) == ntohl(received_payload->actual_payload_content.reply.dest.s_addr))
		{
			while(traverse_node != NULL)
			{
				// for every node in the list, check id destination is received rrep final_dest, if it is, send that packet.
				if (traverse_node->dest_ip.s_addr == ntohl(received_payload->actual_payload_content.reply.final_dest.s_addr))
				{
					eframe = (void*)buildNewFrame(traverse_node->payload_to_send.payload_type,
								      received_hdr->src_mac,get_mac_address(socket_address->sll_ifindex),
								      htons(MY_PROTO_ID),socket_address->sll_ifindex,socket_address,
								      (union eth_payload*)&(traverse_node->payload_to_send.actual_payload_content), my_false);
					sendFrame(pf_sockfd, eframe, socket_address);
					to_delete_from_pending_list = traverse_node->dest_ip.s_addr; 
					
				}
				traverse_node= traverse_node->next;
			}
			pending_head = (pending_frame_list*)deleteFrameFromPendingList(pending_head, 
											to_delete_from_pending_list);	
		}
		else
		{
			// not destination
			to_compare_dest_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.reply.dest.s_addr));
			if (to_compare_dest_entry == NULL)
			{
				pending_head = (pending_frame_list*)addFrameToPendingList(pending_head, R_REPLY,
							ntohl(received_payload->actual_payload_content.reply.dest.s_addr),
							(union eth_payload*)&(received_payload->actual_payload_content));
				new_req = (RREQ*)buildRREQ(inet_addr(my_canonical_ip),ntohl(received_payload->actual_payload_content.reply.dest.s_addr),
							0,my_odr_broadcast_id, my_false, my_false);
				broadcastRREQonAllInterfacesExceptThis(pf_sockfd, new_req, socket_address->sll_ifindex);
				rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
				my_odr_broadcast_id++;				
			}
			else
			{
				if(ntohl(received_payload->actual_payload_content.reply.rediscover) == my_true)
				{
					bool_return_value = update_time_stamp_to_make_stale(routing_head,
								ntohl(received_payload->actual_payload_content.reply.dest.s_addr),
								0);
				}
				to_compare_dest_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.reply.dest.s_addr));
			
				if(to_compare_dest_entry->timestamp + stale_value < get_current_time_millis())
				{
					pending_head = (pending_frame_list*)addFrameToPendingList(pending_head, R_REPLY,
								ntohl(received_payload->actual_payload_content.reply.dest.s_addr),
								(union eth_payload*)&(received_payload->actual_payload_content));
					new_req = (RREQ*)buildRREQ(inet_addr(my_canonical_ip),ntohl(received_payload->actual_payload_content.reply.dest.s_addr),
								0,my_odr_broadcast_id, my_false, my_false);
					broadcastRREQonAllInterfacesExceptThis(pf_sockfd, new_req, socket_address->sll_ifindex);
					rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
					my_odr_broadcast_id++;
				}
				else
				{
					// relay rrep using the route
					// send using the route in the routing table.
					new_rep = (RREP*)buildRREP(ntohl(received_payload->actual_payload_content.reply.src.s_addr),
								   ntohl(received_payload->actual_payload_content.reply.dest.s_addr),
								   ntohl(received_payload->actual_payload_content.reply.hop_count),
								   ntohl(received_payload->actual_payload_content.reply.rediscover),
								   ntohl(received_payload->actual_payload_content.reply.final_dest.s_addr));
					eframe = (void*)buildNewFrame(R_REPLY, to_compare_dest_entry->neighbour_mac, get_mac_address(to_compare_dest_entry->interface_index), 
							htons(MY_PROTO_ID), to_compare_dest_entry->interface_index, socket_address,(union eth_payload*)new_rep, my_false);
					sendFrame(pf_sockfd, eframe, socket_address);	
				}
			}
		}
	}
}
		
void processDATAFrame(int pf_sockfd, int domain_sockfd, my_ether_hdr *received_hdr, payload* received_payload, struct sockaddr_ll *socket_address)
{
	// immediately increment the hop-count. This guy took one hop to reach you.
	
	// Check if you are destination
	//	if you are destination
	//		serialize the data to a structure to pass to the api (port, ip string, message)
	//		remove all stale entries from client-server lookup table
	//		lookup with the port received from the packet in the lookup table
	//		if found 
	//			get the path and write to the domain socket
	//		if not found
	//			client is dead unable to deliver to application
	//	if you are not destination
	//		check the destination address of the received frame in the routing table
	//			if the routing table has entry
	//				increment the hop count
	//				use that route and forward the packet
	//			if the routing table does not have the entry
	//				add the frame to the pending frame list
	//				create RREQ with source as my canonical ip, increment broadcast id, add to rreqs_sent list
	//				send this RREQ on all interfaces except the one which you have received from.
	
	routing_table* to_compare_entry;
	routing_table* free_rreps_feature;
	RREQ* new_req;
	void *eframe; 
	int length;
	struct in_addr source_ip;
	struct sockaddr_un unix_sockaddr;
	msg_send_serialized_data *data_to_send;
	client_server_lookup_table *entry; 
	my_bool bool_return_value;
	
	data_to_send = malloc(sizeof(msg_send_serialized_data));
	memset(data_to_send, 0, sizeof(msg_send_serialized_data));
	memset(&unix_sockaddr,0,sizeof(struct sockaddr_un));
	unix_sockaddr.sun_family = AF_UNIX;	
	
	received_payload->actual_payload_content.actual_data.hop_count = htonl(ntohl(received_payload->actual_payload_content.actual_data.hop_count) + 1);
//////// FREE RREPs behavior
	to_compare_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.actual_data.source_ip.s_addr));
	
	if (to_compare_entry != NULL) 
	{
		if(to_compare_entry->timestamp + stale_value < get_current_time_millis())
		{ //stale
		//printf(" free rreps stale %p %d\n", routing_head,__LINE__);
			bool_return_value = updateRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.actual_data.source_ip.s_addr),
								socket_address->sll_ifindex,received_hdr->src_mac,
								ntohl(received_payload->actual_payload_content.reply.hop_count));
		//printf(" free rreps after updating %p %d\n", routing_head,__LINE__);
		}
		else
		{ //not stale
		
			if(ntohl(received_payload->actual_payload_content.actual_data.hop_count) <= to_compare_entry->hop_count)
			{
				//printf(" free rreps not stale %p %d\n", routing_head,__LINE__);
				bool_return_value = updateRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.actual_data.source_ip.s_addr),
									socket_address->sll_ifindex,received_hdr->src_mac,
									ntohl(received_payload->actual_payload_content.actual_data.hop_count));
			}
		}
	}
	else
	{
		free_rreps_feature = (routing_table*)createRoutingEntry(ntohl(received_payload->actual_payload_content.actual_data.source_ip.s_addr),
				socket_address->sll_ifindex, received_hdr->src_mac,
				ntohl(received_payload->actual_payload_content.actual_data.hop_count));
		//printf("free rreps adding src %p %d\n", routing_head,__LINE__);
		routing_head = (routing_table*)addRoutingEntry(routing_head, free_rreps_feature);
	}	
//////// FREE RREPs behavior
	if (inet_addr(my_canonical_ip) == ntohl(received_payload->actual_payload_content.actual_data.dest_ip.s_addr))
	{
		data_to_send->dest_port = ntohl(received_payload->actual_payload_content.actual_data.source_port);
		memcpy(data_to_send->message,received_payload->actual_payload_content.actual_data.msg,
		       strlen(received_payload->actual_payload_content.actual_data.msg));
		source_ip.s_addr = ntohl(received_payload->actual_payload_content.actual_data.source_ip.s_addr);
		memcpy(data_to_send->canonical_ip, inet_ntoa(source_ip) , strlen(inet_ntoa(source_ip)));
		
		//printf("before removing entries\n");
		//print_client_server_table(cliser_lookup_head);
		cliser_lookup_head = (client_server_lookup_table*)remove_stale_entries_from_table(cliser_lookup_head);
		//printf("lookup for %d................\n", ntohl(received_payload->actual_payload_content.actual_data.dest_port));
		entry = (client_server_lookup_table*)get_from_client_server_lookup_table(cliser_lookup_head, 
											 ntohl(received_payload->actual_payload_content.actual_data.dest_port));
		//print_client_server_table(cliser_lookup_head);
		//printf("looking for port %d$$$$$$$$$$$$$$$$$$$$\n", ntohl(received_payload->actual_payload_content.actual_data.dest_port));
		if(entry != NULL)
		{
			memcpy(unix_sockaddr.sun_path, entry->path, strlen(entry->path));
		
			if(ntohl(received_payload->actual_payload_content.actual_data.dest_port) == SERVER_PORT)
				printf("Received DATA for server.\n");
			else
				printf("Received DATA for client.\n");
			
			length = sendto(domain_sockfd, (void*)data_to_send, sizeof(msg_send_serialized_data),0, (struct sockaddr *)&unix_sockaddr, sizeof(unix_sockaddr));
			//free(data_to_send);
			if (length == -1) 
			{
				printf("sendto() error in sending data to application process.\n");
				//exit(-1);
			} 
		}
		else
		{
			if(ntohl(received_payload->actual_payload_content.actual_data.dest_port) == SERVER_PORT)
				printf("Server dead!!\n");
			else
				printf("Client dead!!\n");			
		}
	}
	else
	{
		 to_compare_entry = (routing_table*)getRoutingEntry(routing_head, ntohl(received_payload->actual_payload_content.actual_data.dest_ip.s_addr));
		 if((to_compare_entry == NULL) || (to_compare_entry->timestamp + stale_value < get_current_time_millis()))
		 {
			pending_head = (pending_frame_list*)addFrameToPendingList(pending_head, DATA,
						     ntohl(received_payload->actual_payload_content.actual_data.dest_ip.s_addr),
						     (union eth_payload*)&(received_payload->actual_payload_content));
			new_req = (RREQ*)buildRREQ(inet_addr(my_canonical_ip),ntohl(received_payload->actual_payload_content.actual_data.dest_ip.s_addr),
						0,my_odr_broadcast_id, my_false, my_false);
			broadcastRREQonAllInterfacesExceptThis(pf_sockfd, new_req, socket_address->sll_ifindex);
			rreqs_head = (rreqs_sent *)add_or_update_rreqs_sent(rreqs_head,inet_addr(my_canonical_ip),my_odr_broadcast_id);
			my_odr_broadcast_id++;	
		 }
 		 else
		 {
			//printf("&&&&&&&&&&&& %s\n", print_mac_address(to_compare_entry->neighbour_mac));
			eframe = (void*)buildNewFrame(DATA, to_compare_entry->neighbour_mac, get_mac_address(to_compare_entry->interface_index), 
					htons(MY_PROTO_ID), to_compare_entry->interface_index, socket_address,
					(union eth_payload*)&(received_payload->actual_payload_content.actual_data), my_false);
			sendFrame(pf_sockfd, eframe, socket_address);				 
		 }
	}
}

void processReceivedEthernetFrame(int pf_sockfd,int domain_sockfd, my_ether_hdr *received_hdr, payload* received_payload, struct sockaddr_ll *socket_address)
{
	char my_hostname[MAX_BUFFER_LENGTH];
	struct hostent *hp1 = NULL;
	struct hostent *hp2 = NULL;
	long addr1, addr2;
	char src_name[MAX_BUFFER_LENGTH], dest_name[MAX_BUFFER_LENGTH];
	
	memset(my_hostname,0,MAX_BUFFER_LENGTH);
	memset(src_name,0,MAX_BUFFER_LENGTH);
	memset(dest_name,0,MAX_BUFFER_LENGTH);	
	
	if (gethostname(my_hostname, sizeof(my_hostname)) == -1)
	{
		printf("gethostname error. \n");
		exit(-1);
	}
	
	switch(received_payload->payload_type)
	{
		case R_REQUEST:
			addr1 = ntohl(received_payload->actual_payload_content.request.src.s_addr);
			hp1 = gethostbyaddr((char *) &addr1, sizeof(addr1), AF_INET);
			addr2 = ntohl(received_payload->actual_payload_content.request.dest.s_addr);
			memcpy(src_name, hp1->h_name, strlen(hp1->h_name));
			hp2 = gethostbyaddr((char *) &addr2, sizeof(addr2), AF_INET);	
			memcpy(dest_name, hp2->h_name, strlen(hp2->h_name));
			printf("ODR at node %s: received frame hdr src %s dest %s\n\t\t\t ODR msg type %d src %s dest %s.\n",
				my_hostname, print_mac_address(received_hdr->src_mac), print_mac_address(received_hdr->dest_mac),
				received_payload->payload_type, src_name, dest_name); 
			processRREQFrame(pf_sockfd,received_hdr,received_payload,socket_address);
			break;
		case R_REPLY:
			addr1 = ntohl(received_payload->actual_payload_content.reply.src.s_addr);
			hp1 = gethostbyaddr((char *) &addr1, sizeof(addr1), AF_INET);
			memcpy(src_name, hp1->h_name, strlen(hp1->h_name));
			addr2 = ntohl(received_payload->actual_payload_content.reply.dest.s_addr);
			hp2 = gethostbyaddr((char *) &addr2, sizeof(addr2), AF_INET);		
			memcpy(dest_name, hp2->h_name, strlen(hp2->h_name));
			printf("ODR at node %s: received frame hdr src %s dest %s\n\t\t\t ODR msg type %d src %s dest %s.\n",
				my_hostname, print_mac_address(received_hdr->src_mac), print_mac_address(received_hdr->dest_mac),
				received_payload->payload_type, src_name, dest_name); 			
			processRREPFrame(pf_sockfd,received_hdr,received_payload,socket_address);
			break;
		case DATA:
			addr1 = ntohl(received_payload->actual_payload_content.actual_data.source_ip.s_addr);
			hp1 = gethostbyaddr((char *) &addr1, sizeof(addr1), AF_INET);
			memcpy(src_name, hp1->h_name, strlen(hp1->h_name));
			addr2 = ntohl(received_payload->actual_payload_content.actual_data.dest_ip.s_addr);
			hp2 = gethostbyaddr((char *) &addr2, sizeof(addr2), AF_INET);
			memcpy(dest_name, hp2->h_name, strlen(hp2->h_name));
			printf("ODR at node %s: received frame hdr src %s dest %s\n\t\t\t ODR msg type %d src %s dest %s Message: <%s>.\n",
				my_hostname, print_mac_address(received_hdr->src_mac), print_mac_address(received_hdr->dest_mac),
				received_payload->payload_type, src_name, dest_name, received_payload->actual_payload_content.actual_data.msg); 			
			processDATAFrame(pf_sockfd,domain_sockfd, received_hdr,received_payload,socket_address);
			break;
		default:
			printf("Unsupported packet type received.\n");
	}	
}

int main(int argc, char** argv)
{
	int sockfd, socket_addr_length, length, packets; 
	struct sockaddr_ll socket_address;
	struct sockaddr_un client_socket_address;
	int client_address_length;
	void* buffer;
	int i;
	my_ether_hdr *received_hdr;
	payload* received_payload;
	int base, domain_sockfd;
	char* endptr, *str;
	int optval;
	struct sockaddr_un server_addr, odr_addr;
	fd_set readset;
	msg_send_serialized_data *client_recvd_data;
	unsigned char* debug_purposes;
	int pf_socket_listenfd[MAX_PF_SOCKFD];
	int temp_counter;

	optval = 1;
	stale_value = 0;
	base = 10;
	memset(&odr_addr, 0, sizeof(odr_addr));

	unlink(ODR_SUN_PATH);

	odr_addr.sun_family = AF_UNIX;
	strcpy(odr_addr.sun_path, ODR_SUN_PATH);
	
	client_server_counter = 1;
	
	memset(&socket_address,0,sizeof(struct sockaddr_ll));
	socket_addr_length = sizeof(socket_address);
	memset(&client_socket_address, 0, sizeof(struct sockaddr_un));
	client_address_length = sizeof(client_socket_address);
	
	if(argc != 2)
	{
		printf("Usage: ./odr <staleness>\n");
		printf("staleness: integer value indicating validity of entry in routing table\n");
		exit(0);
	}

	str = argv[1];
	stale_value = strtol(argv[1], &endptr, base);
 	if((errno == ERANGE && (stale_value == LONG_MAX || stale_value == LONG_MIN)) || (errno != 0 && stale_value == 0))
	{
		perror("Invalid arguments passed. ");
		exit(0);
	}
	if(endptr == str)
	{
		printf("Enter valid values.\n");
		printf("Usage: ./odr <staleness>\n");
		printf("staleness: integer value indicating validity of entry in routing table\n");
		exit(0);
	}
	
	//printf("stale_value is %ld\n", stale_value);
	stale_value = stale_value * 1000; // stalevalue in seconds buddy.

	if ((domain_sockfd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
	{
		perror("On socket() for Unix socket ");
		exit(0);
	}

	if((bind(domain_sockfd, (struct sockaddr *)&odr_addr, sizeof(odr_addr))) < 0)
	{
		perror("On bind() for Unix socket ");
		exit(0);
	}	
	
	/* get hardware interface details */
	build_interface_list();
	//printf("My canonical ip is %s\n", my_canonical_ip);
  
	sockfd = socket(PF_PACKET, SOCK_RAW, htons(MY_PROTO_ID));
	if (sockfd == -1) 
	{
		printf("socket() error\n");
		exit(-1);
	}
  
	buffer = (void*)malloc(ETH_FRAME_LEN); /*Buffer for ethernet frame*/
	length = 0; /*length of the received frame*/ 
	client_recvd_data = (void*)malloc(sizeof(msg_send_serialized_data));
	
	/* Add server as permanent entry to the table*/
	cliser_lookup_head = (client_server_lookup_table*)add_to_client_server_lookup_table(cliser_lookup_head,
											    SERVER_PORT, SERVER_SUN_PATH, my_true);
	print_client_server_table(cliser_lookup_head);

	/* Call select() */
	for(;;)
	{
		FD_ZERO(&readset);
		FD_SET(domain_sockfd, &readset);
		FD_SET(sockfd, &readset);
		
		if(select(MAX(domain_sockfd, sockfd) + 1, &readset, NULL, NULL, NULL) < 0)
		{
			if(errno == EINTR)
			{
				printf("EINTR: Continuing with normal operation - %s\n", strerror(errno));
				continue;
			}
		}

		if (FD_ISSET(domain_sockfd, &readset)) 
		{
			memset(client_recvd_data, 0, sizeof(msg_send_serialized_data));
			/* The socket_fd has data available to be read */
			length = recvfrom(domain_sockfd, (void*)client_recvd_data, sizeof(msg_send_serialized_data), 0, 
					  (struct sockaddr*)&client_socket_address, &client_address_length);
			if (length == -1) 
			{
				printf("recvfrom() error\n");
				exit(-1);
			}
			
			processDomainSocketData(sockfd, client_recvd_data->socket_fd, 
				client_recvd_data->canonical_ip, client_recvd_data->dest_port, 
				client_recvd_data->message, client_recvd_data->flag, &client_socket_address);

		}
		if (FD_ISSET(sockfd, &readset)) 
		{
			memset(buffer, 0, ETH_FRAME_LEN);
			length = recvfrom(sockfd, buffer, ETH_FRAME_LEN, 0, (struct sockaddr*)&socket_address, &socket_addr_length);
			if (length == -1) 
			{
				printf("recvfrom() error\n");
				exit(-1);
			}
			
			received_hdr = get_ethernet_hdr(buffer);
			received_payload = get_ethernet_payload(buffer + sizeof(my_ether_hdr));
			
			//printEthernetHeader(received_hdr);
			//printEthernetPayload(received_payload);
			
			processReceivedEthernetFrame(sockfd, domain_sockfd,received_hdr, received_payload, &socket_address);
		}
	}

	
	//free_hwa_info(hwahead);
	unlink(ODR_SUN_PATH);
	exit(0);	
}
