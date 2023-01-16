#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<signal.h>
#include<unistd.h>
#include<sys/socket.h>
#include<poll.h>
#include<netdb.h>
#include<netinet/in.h>

// https://www.example.com

#define PORT 8080
#define POLL_SIZE 10
#define REQUEST_BUFFER_LENGTH 1024
#define CLIENT_BUFFER_LENGTH 256
#define RESPONSE_BUFFER_LENGTH 2048
#define ERROR_BUFFER_LENGTH 50
#define DATA_BUFFER_LENGTH 4096

int finished = 0;

typedef struct requestDetails{
    char* host;
    char* resource;
}requestDetails;

typedef struct cacheInfo{
    char* host_name;
    char* resource;
    char* data;
}cacheInfo;

cacheInfo cache[500];
int position_in_cache = 0;

struct pollfd poll_set[POLL_SIZE];
volatile int numfds = 0; 

void write_data_to_cache(int serv, int client)
{
    printf("Writing response to cache....\n");

    char buffer[RESPONSE_BUFFER_LENGTH];
    memset(buffer, 0, RESPONSE_BUFFER_LENGTH);
    size_t total_bytes = 0;
    int cur_bytes = 0;
    char* start_point;

    cache[position_in_cache].data = (char*)malloc(DATA_BUFFER_LENGTH * sizeof(char));

    while((cur_bytes = read(serv, buffer, RESPONSE_BUFFER_LENGTH)) > 0)
    {
        total_bytes += cur_bytes;
        write(client, buffer, cur_bytes);
        write(1, buffer, cur_bytes);
        if(total_bytes > sizeof(cache[position_in_cache].data))
        {
            cache[position_in_cache].data = (char*)realloc(cache[position_in_cache].data, DATA_BUFFER_LENGTH + total_bytes);
            
            start_point = &(cache[position_in_cache].data[total_bytes]);
            memcpy(start_point, buffer, cur_bytes);
        }
        else
        {
            start_point = &(cache[position_in_cache].data[total_bytes]);
            memcpy(start_point, buffer, cur_bytes);
        }
        memset(buffer, 0, RESPONSE_BUFFER_LENGTH);
    }

    position_in_cache++;
    printf("Finished writing!\n");
}

requestDetails parse_request(char* request)
{
    char* host = (char*)malloc((strlen(request)-8)*sizeof(char));
    memcpy(host, request + 8, strlen(request));
    
    requestDetails det;
    det.resource = "-";
    det.host = host;

    return det;
}

int check_cache(char* request)
{
    requestDetails details = parse_request(request);
    int pos = -1;

    for(int i = 0; i < position_in_cache; i++)
    {
        if(strcmp(cache[i].host_name, details.host) == 0) 
        {
            pos = i;
            break;
        }
    }

    // 0 means cache was found, 1 means cache wasnt found
    if(pos >= 0)
    {
        return pos;
    }
    else
    {
        return -1;
    }
}

void sendResponseFromCache(int pos_in_cache, int client)
{
    write(1, cache[pos_in_cache].data, strlen(cache[pos_in_cache].data));
    printf("Resonse was send from cache\n");
}

void sendResponse(int server, int client)
{
    /*char response_buffer[RESPONSE_BUFFER_LENGTH];
    size_t total_bytes = 0;
    int cur_bytes = 0;
    char* start_point;
    while((cur_bytes = read(server, response_buffer, RESPONSE_BUFFER_LENGTH)) > 0)
    {
        write(client, response_buffer, bytes);
        write_data_to_cache(response_buffer, cur_bytes);
    }*/
}

void getHTTPresponse(char* request, int client, int length)
{
    requestDetails details = parse_request(request);

    printf("res: %s\n", details.resource);
    printf("host: %s\n", details.host);    

    int err;
    char errbuf[ERROR_BUFFER_LENGTH];

    int servfd;
    struct addrinfo hints;
    struct addrinfo *result;

    memset(&hints, 0, sizeof(struct addrinfo));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if((err = getaddrinfo(details.host, "80", &hints, &result)) != 0)
    {
        strerror_r(err, errbuf, ERROR_BUFFER_LENGTH);
        printf("Couldn't get info about host: %s\n", errbuf);
    }

    if((servfd = socket(result->ai_family, result->ai_socktype, result->ai_protocol)) == -1)
    {
        perror("Couldn't create socket... ");
    }

    if(connect(servfd, result->ai_addr, result->ai_addrlen) == -1)
    {
        perror("Error while connection to host... ");
    }

    char request_buf[REQUEST_BUFFER_LENGTH] = {0};
    char request_template[] = "GET / HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n";
    sprintf(request_buf, request_template, details.host);

    cache[position_in_cache].host_name = details.host;
    cache[position_in_cache].resource = details.resource;

    write(servfd, request, REQUEST_BUFFER_LENGTH);
    //sendResponse(servfd, client);
    write_data_to_cache(servfd, client);
}

void handler(int signum)
{
    for(int i = 0; i < numfds; i++)
	{
		close(poll_set[i].fd);
	}
}

int main(int argc, char* argv[])
{
    char client_buffer[CLIENT_BUFFER_LENGTH];

    int listen_socket, 
        client_socket;

    struct sockaddr_in client_addr;
    struct sockaddr_in proxy_addr;

    struct sigaction sig;
    sig.sa_handler = handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = SA_RESTART;

    signal(SIGINT, handler);

    memset(&proxy_addr, 0, sizeof(struct sockaddr));
    proxy_addr.sin_family = AF_INET;
    proxy_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    proxy_addr.sin_port = htons(PORT);

    listen_socket = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_socket == -1)
    {
        perror("Couldn't create listen socket... :\n");
        return -1;
    }

    if((bind(listen_socket, (struct sockaddr*)&proxy_addr, sizeof(proxy_addr))) != 0)
    {
        perror("Socket bind failed... :\n");
        return -1;
    }

    if(listen(listen_socket, 10) != 0)
    {
        perror("Error while listening... :\n");
        return -1;
    }

    memset(poll_set, '\0', sizeof(poll_set));
	poll_set[0].fd = listen_socket;
	poll_set[0].events = POLLIN;
	numfds += 1;

    int read_bytes = 0;
    int idx = 0;
    int cur_fds = 0;
    while(1)
    {
        int res = poll(poll_set, numfds, 1000*1000); // 100 sek
        if(res == 0)
            break;
        if(res == -1)
        {
            perror("Poll error... :\n");
            return -1;
        }    

        cur_fds = numfds;
        for(idx = 0; idx < cur_fds; idx++)
        {
            if(poll_set[idx].revents == 0)
			{
				continue;
			}
            if(poll_set[idx].revents & POLLIN)
            {
                if(poll_set[idx].fd == listen_socket)
                {
                    socklen_t len = sizeof(client_addr);
                    client_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &len);

                    poll_set[numfds].fd = client_socket;
				    poll_set[numfds].events = POLLIN;
				    numfds++;
				    write(1, "Client was connected...\n", 25);
                }
                else
                {
                    read_bytes = 0;
				    if((read_bytes = read(poll_set[idx].fd, client_buffer, CLIENT_BUFFER_LENGTH)) > 0)
				    {
					    write(1, "Request is : ", 11);
				        write(1, client_buffer, read_bytes);
                        write(1, "\n", 2);
                        
                        client_buffer[read_bytes-1] = '\0';
                        int check = check_cache(client_buffer);
                        if(check >= 0)
                        {
                            sendResponseFromCache(check, poll_set[idx].fd);
                        }
                        else
                        {
                            getHTTPresponse(client_buffer, poll_set[idx].fd, read_bytes);
                        }
				    }
                    else
                    {
                        write(1, "Client removed!\n", 20);
				        close(poll_set[idx].fd);
				        poll_set[idx].events = 0;
				        poll_set[idx] = poll_set[idx + 1];
				        numfds--;
                    }
                }
            }
        }
    }

    for(int i = 0; i < numfds; i++)
	{
		close(poll_set[i].fd);
	}

    return 0;
}
