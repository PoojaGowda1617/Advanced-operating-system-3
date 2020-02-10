#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/times.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <signal.h>
#include <netdb.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <ifaddrs.h>

#define CLIENT_NODE_COUNT (5 + 1) // 5 clients + 1 server 0 as client
#define SERVER_COUNT 8
#define PATH_TO_SERVER_CONFIG "../config/serverconfig.txt" //PATH to global file
#define PATH_TO_LOG_FILE "./File/log_file.txt" //PATH to global file

typedef enum SERVER_MESSAGE_TYPE {
    SERVER_MESSAGE_TYPE_REQUEST,
    SERVER_MESSAGE_TYPE_RESPONSE
}SERVER_MESSAGE_TYPE;


typedef enum SERVER_REQUEST_RESPONSE {
    SERVER_REQUEST_WRITE_ACCESS,
    SERVER_REQUEST_WRITE,
    SERVER_REQUEST_UPDATE_FILE,
    SERVER_REQUEST_CLOSE,
    SERVER_RESPONSE_GRANTED,
    SERVER_RESPONSE_RESTRICTED,
    SERVER_RESPONSE_INVALID,
    SERVER_RESPONSE_SUCCESS,
}SERVER_REQUEST_RESPONSE;


typedef struct map_info {
    int version_num;
    int Replicas_updated;
    char Distinguished_site;
}map_info;


typedef struct message_server {
    SERVER_MESSAGE_TYPE type;
    int id;
    int version;
    int numberofnodes_ingroup;
    char site;
    char buff[2048]; //1KB MAX FILE TRANSFER
    union subtype{
        struct request{
            SERVER_REQUEST_RESPONSE type;
            map_info info;
        }request;
        struct response {
            SERVER_REQUEST_RESPONSE type;
        }response;
    }subtype;
}message_server;
