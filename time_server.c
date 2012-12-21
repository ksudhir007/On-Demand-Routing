#include "myheaders.h"

int main(int argc, char** argv)
{
	int sockfd;
	const int optval = 1;
	struct sockaddr_un server_addr, odr_addr;
	char client_message[MAX_BUFFER_LENGTH];
	int client_port;
	char client_canonical_ip[MAX_BUFFER_LENGTH];
	char time_buffer[MAX_BUFFER_LENGTH];
	time_t curtime;
	struct tm *loctime;
	struct hostent *hp;
	long    addr;
	char my_hostname[MAX_BUFFER_LENGTH];
	

	memset(&server_addr, 0, sizeof(server_addr));
	memset(&odr_addr, 0, sizeof(odr_addr));
	memset(time_buffer, 0, MAX_BUFFER_LENGTH);
	memset(client_canonical_ip, 0, MAX_BUFFER_LENGTH);
	memset(client_message, 0, MAX_BUFFER_LENGTH);
	memset(my_hostname,0,MAX_BUFFER_LENGTH);

	unlink(SERVER_SUN_PATH);

	server_addr.sun_family = odr_addr.sun_family = AF_UNIX;
	strcpy(server_addr.sun_path, SERVER_SUN_PATH);
	strcpy(odr_addr.sun_path, ODR_SUN_PATH);

	if ((sockfd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
	{
		perror("On socket() for Unix socket ");
		exit(0);
	}
	if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0)
	{
		perror("On setsockopt() for Unix socket ");
		exit(0);
	}

	if((bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr))) < 0)
	{
		perror("On bind() for Unix socket ");
		exit(0);
	}

	if(connect(sockfd, (struct sockaddr *)&odr_addr, sizeof(odr_addr)) < 0)
	{
		perror("On connect() with ODR unix socket ");
		unlink(SERVER_SUN_PATH);
		exit(0);
	}
	if (gethostname(my_hostname, sizeof(my_hostname)) == -1)
	{
		printf("gethostname error. \n");
		exit(-1);
	}	

#if 1
	while(1)
	{
		//printf("Waiting for the clients....\n");
		if (msg_recv(sockfd, client_canonical_ip, &client_port, client_message) == TIMEOUT_RETURN_CODE)
			continue;
		addr = inet_addr(client_canonical_ip);
		//printf("after msg_recv() (%s:%d) and message is %s\n", client_canonical_ip, client_port, client_message);
#if 1
		if (hp = gethostbyaddr((char *) &addr, sizeof(addr), AF_INET)) 
		{
		
			//printf("Received message from client %s (%s:%d) and message is %s\n", hp->h_name, client_canonical_ip, client_port, client_message);

			/* Get the current time.  */
			curtime = time(NULL);

			/* Convert it to local time representation.  */
			loctime = (struct tm *)localtime(&curtime);

			/* Print out the date and time in the standard format.  */
			memcpy(time_buffer, (char*)asctime(loctime), strlen((char*)asctime(loctime)) - 1);		
			msg_send(sockfd, client_canonical_ip, client_port, time_buffer, 0);

			printf("Server at node \"%s\" responding to request from %s.\n", my_hostname,hp->h_name);
			//printf("Sent time %s to client at %s (%s:%d).\n", time_buffer, hp->h_name, client_canonical_ip, client_port);
		}
#endif

	}
#endif
	printf("Ending main\n");
	return 0;
}
