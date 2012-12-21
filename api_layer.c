#include "myheaders.h"

int msg_send(int socket_fd, char* canonical_ip, int dest_port, char* message, int flag)
{
	socklen_t length;
	struct sockaddr_un cli_verify;
	struct sockaddr_un odr_addr;
	msg_send_serialized_data *data_to_send;
	
	data_to_send = malloc(sizeof(msg_send_serialized_data));
	memset(&cli_verify, 0, sizeof(struct sockaddr_un));
	memset(&odr_addr, 0, sizeof(struct sockaddr_un));
	memset(data_to_send, 0, sizeof(msg_send_serialized_data));
	
	odr_addr.sun_family = AF_UNIX;
	strcpy(odr_addr.sun_path, ODR_SUN_PATH);	

	length = sizeof(cli_verify);
	if (0 != getsockname(socket_fd, (struct sockaddr *) &cli_verify, &length))
	{
		perror("On getsockname for unix socket ");
		return -1;
	}

	data_to_send->socket_fd = socket_fd;
	data_to_send->dest_port = dest_port;
	data_to_send->flag = flag;
	memcpy(data_to_send->message, message, strlen(message));
	memcpy(data_to_send->canonical_ip, canonical_ip, strlen(canonical_ip));
	
	//printf("sending from api to odr %d, %s, %d, %s, %d\n", data_to_send->socket_fd, data_to_send->canonical_ip, 
	  //     data_to_send->dest_port, data_to_send->message, data_to_send->flag);
	
	if(sendto(socket_fd, (void*)data_to_send, sizeof(msg_send_serialized_data), 0, (struct sockaddr*)&odr_addr, sizeof(struct sockaddr_un)) == -1)
	{
		printf("sendto() error\n");
		free(data_to_send);
		return -1;
	}
	//printf("message sent from api to odr.\n");
	free(data_to_send);
	
	return 0;
}


int msg_recv(int socket_fd, char* canonical_ip, int* src_port, char* message)
{
	struct sockaddr_un client_socket_address;
	int client_address_length;
	msg_send_serialized_data *client_recvd_data;
	int length;
	fd_set setallfd;
	struct timeval tv;
	int n;

	tv.tv_sec = REDISCOVERY_TIMEOUT;
	tv.tv_usec = 0;	
	
	//printf("Waiting for message in msg_recv() ....\n");
	
	client_recvd_data = (void*)malloc(sizeof(msg_send_serialized_data));
	length = 0;
	client_address_length = sizeof(client_socket_address);
	
	memset(client_recvd_data, 0, sizeof(msg_send_serialized_data));
	
	FD_ZERO(&setallfd);
	FD_SET(socket_fd, &setallfd);

	n = select(socket_fd + 1, &setallfd, NULL, NULL, &tv);
	if (n < 0) 
	{
		printf("select() error");
		return -1; 
	}
	
	if (FD_ISSET(socket_fd, &setallfd)) 
	{
		/* The socket_fd has data available to be read */
		length = recvfrom(socket_fd, (void*)client_recvd_data, sizeof(msg_send_serialized_data), 0, 
					(struct sockaddr*)&client_socket_address, &client_address_length);
		if (length == -1) 
		{
			printf("recvfrom() error in msg_recv\n");
			exit(-1);
		}
	}
	else
	{
		return TIMEOUT_RETURN_CODE;
	}
	
	//printf("%d ...sizeof(client_recvd_data) %d\n", length, sizeof(msg_send_serialized_data));
	//printf("received in msg_recv() %s, %s, %d\n", client_recvd_data->canonical_ip, client_recvd_data->message, client_recvd_data->dest_port);
	
	memset(message, 0, MAX_BUFFER_LENGTH);
	memset(canonical_ip, 0, MAX_BUFFER_LENGTH);
	
	strcpy(message, client_recvd_data->message);
	*src_port = client_recvd_data->dest_port;
	strcpy(canonical_ip, client_recvd_data->canonical_ip);
	
	//printf("after copying in msg_recv() %s, %s, %d\n", canonical_ip, message, *src_port);
	
	//free(client_recvd_data);
	return length;
}

