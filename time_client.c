#include "myheaders.h"

int main(int argc, char** argv)
{
	char input[1024];
	int vm_number = 0;
	int input_valid = -1;
	int sockfd = -1;
	int fd = -1;
	struct sockaddr_un cli_name;
	int cli_port, dest_port;
	struct hostent *hp = NULL;
	char server_ip[MAX_BUFFER_LENGTH];
	char *temp_string;
	char my_hostname[MAX_BUFFER_LENGTH];
	char server_message[MAX_BUFFER_LENGTH];
	int server_port;
	char server_canonical_ip[MAX_BUFFER_LENGTH];
	long addr;
	int repeat_times = 1;

	memset(server_ip, 0, MAX_BUFFER_LENGTH);
	memset(&cli_name, 0, sizeof(cli_name));
	memset(server_canonical_ip,0,MAX_BUFFER_LENGTH);
	memset(server_message,0,MAX_BUFFER_LENGTH);
	cli_port = 0;
	dest_port = SERVER_PORT;

	if ((sockfd = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0)
	{
		perror("On socket() for Unix socket ");
		exit(0);
	}
	
	cli_name.sun_family = AF_UNIX;
	strcpy(cli_name.sun_path, "my_client_XXXXXX");
	
	fd = mkstemp(cli_name.sun_path);
	if (fd == -1)
	{
		printf("Error creating temporary file.\n");
	}
	unlink(cli_name.sun_path);
	close(fd);
	
	
	if (bind(sockfd, (struct sockaddr *) &cli_name, sizeof(struct sockaddr_un))) 
	{
        	perror("On bind() for unix socket ");
		goto cleanup;
    	}

	while(1)
    	{ 
repeat:
        	printf("Select a server node from [vm1-vm10]:\n");
        	if(gets(input) != NULL)
		{
			if((vm_number = atoi(input+2)) != 0)
			{
				if ((input[0] == 'v') && (input[1] == 'm'))
				{
					//printf("%d\n", vm_number);
					if (vm_number <= 10 && vm_number >=1)
						input_valid = 0; 
				}
			}
			if (input[0] == 'q')
			{
				goto cleanup;
			}
			if (input_valid != 0)
			{
				printf("Invalid input. \n");
			}
			else
			{
				
				//printf("Selected %s. \n", input);
				hp = gethostbyname(input);
				if (hp == NULL)
				{
					printf("gethostbyname error.\n");
					exit(-1);
				}
				if (gethostname(my_hostname, sizeof(my_hostname)) == -1)
				{
					printf("gethostname error. \n");
					exit(-1);
				}
				
				printf("Client at node \"%s\" sending request to the server at \"%s\".\n", my_hostname,input);
				if (hp->h_addr_list[0] != NULL)
				{
					temp_string = inet_ntoa( *( struct in_addr*)( hp -> h_addr_list[0]));
					memcpy(server_ip, temp_string, strlen(temp_string));
					// this should call api - msg_send for the selected machine.
					msg_send(sockfd, server_ip, dest_port, "Time please!", my_false);
					printf("Waiting for the reply from server....\n");

					if (msg_recv(sockfd, server_canonical_ip, &server_port, server_message) == TIMEOUT_RETURN_CODE)
					{
						printf("Client at node \"%s\": timeout on response from \"%s\".\n", my_hostname,input);
						msg_send(sockfd, server_ip, dest_port, "Time please!", my_true);
						if (msg_recv(sockfd, server_canonical_ip, &server_port, server_message) == TIMEOUT_RETURN_CODE)
							goto repeat;
					}
					else
					{
						addr = inet_addr(server_canonical_ip);
						if (hp = gethostbyaddr((char *) &addr, sizeof(addr), AF_INET)) 
						{
						 	printf("Client at node \"%s\": received from \"%s\" <%s>", my_hostname, input,server_message);
						}
					}
				}
			}
			input[0] = '\0';
			vm_number = 0;
			input_valid = -1;
		}
	}


cleanup:
	close(sockfd);
	unlink(cli_name.sun_path);
	return 0;
}
