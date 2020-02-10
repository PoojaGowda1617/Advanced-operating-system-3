#include "server.h"

int myid,process_th_exit = 0;

map_info current_map_info;

pthread_t proccess_thread;

FILE *log_file;

void sendResponse(int fd, message_server response){
    printf("Send responce\n");
    send(fd,&response,sizeof(message_server),0);
}

void *processthread(void *val) {
    
    int fd = (uintptr_t)val;
    message_server rcv_msg = { 0 };
    

    while(1) {
        if( recv(fd , &rcv_msg, sizeof(message_server), MSG_WAITALL) <= 0){
            close(fd);
            return NULL;
        }
        log_file = fopen ( PATH_TO_LOG_FILE, "a+" );
        if (!log_file) {
            perror( PATH_TO_LOG_FILE);
            exit(1);
        }
        switch(rcv_msg.type) {
                
            case SERVER_MESSAGE_TYPE_REQUEST: {
                switch(rcv_msg.subtype.request.type) {
                        
                    case SERVER_REQUEST_WRITE_ACCESS: {
                        int len = 0;
                        printf("Received write access req from client %d\n",rcv_msg.id);
                        message_server snd_msg = { 0 };
                        
                        snd_msg.type = SERVER_MESSAGE_TYPE_RESPONSE;
                        snd_msg.id = myid;
                        snd_msg.subtype.response.type = SERVER_RESPONSE_GRANTED;
                        snd_msg.version = current_map_info.version_num;
                        snd_msg.numberofnodes_ingroup = current_map_info.Replicas_updated;
                        snd_msg.site = current_map_info.Distinguished_site;
                        fseek(log_file, 0, SEEK_END);
                        len = ftell(log_file);
                        fseek(log_file, 0, 0);
                        printf("length read %d\n",len);
                        fread(snd_msg.buff, sizeof(char), len, log_file);
                        printf("write access file data responce %s\n",snd_msg.buff);
                        sendResponse(fd,snd_msg);
                        memset(&rcv_msg,0,sizeof(message_server));
                    }
                        
                        break;
                    case SERVER_REQUEST_WRITE: {
                        
                        printf("Received WRITE req from client %d with data %s\n",rcv_msg.id,rcv_msg.buff);
                       current_map_info.version_num = rcv_msg.version;
                       current_map_info.Replicas_updated = rcv_msg.numberofnodes_ingroup;
                        current_map_info.Distinguished_site = rcv_msg.site;
                        message_server snd_msg = { 0 };
                        snd_msg.type = SERVER_MESSAGE_TYPE_RESPONSE;
                        snd_msg.id = myid;
                        snd_msg.subtype.response.type = SERVER_RESPONSE_SUCCESS;
                        printf("sending write responce after appending\n");
                        //update the file
                        fseek(log_file, 0, SEEK_END);
                        fprintf(log_file,"%s\n", rcv_msg.buff);
                        sendResponse(fd,snd_msg);
                        memset(&rcv_msg,0,sizeof(message_server));

                    }
                        break;
                    case SERVER_REQUEST_CLOSE: {
                        return NULL;
                    }
                    case SERVER_REQUEST_UPDATE_FILE: {
                        printf("Received Update with data %s\n",rcv_msg.buff);
                        fclose(log_file);
                        fclose(fopen(PATH_TO_LOG_FILE, "w"));
                        log_file = fopen (PATH_TO_LOG_FILE, "a+" );
                        current_map_info.version_num = rcv_msg.version;
                        current_map_info.Replicas_updated = rcv_msg.numberofnodes_ingroup;
                        current_map_info.Distinguished_site = rcv_msg.site;
                        message_server snd_msg = { 0 };
                        fseek(log_file, 0, 0);
                        fprintf(log_file,"%s\n", rcv_msg.buff);
                        snd_msg.type = SERVER_MESSAGE_TYPE_RESPONSE;
                        snd_msg.id = myid;
                        snd_msg.subtype.response.type = SERVER_RESPONSE_SUCCESS;                        //update the file
                        sendResponse(fd,snd_msg);
                        memset(&rcv_msg,0,sizeof(message_server));
                    }
                        
                    default:
                        printf("Invalid OP %d\n",rcv_msg.id);
                        break;
                }
            }
                break;
            default :
                printf("Server received response message, send invalid reply");
                break;
        }
        fclose(log_file);
        log_file = NULL;
    }
}

int main(int argc, char **argv) {
    
    struct ifaddrs *ifaddr, *ifa;
    char ip[NI_MAXHOST];
    FILE *configfile;
    int listensockfd = -1,newfd = -1 , set = 1,port,currentconnections=0,i = 0;
    struct sockaddr_in addr,cliaddr;
    struct hostent *host;
    socklen_t len = sizeof (struct sockaddr_in);


    if(argc != 3) {
        printf("please provide arguments (serverID , PORT) \n");
        return 1;
    }
    myid = atoi(argv[1]);
    port = atoi(argv[2]);
    if (myid < 0 || myid > SERVER_COUNT) {
        printf("server id not supported\n");
        return 1;
    }
    
    //listen for connections
    if (getifaddrs(&ifaddr) == -1) {
        perror("ERROR: getifaddrs\n");
        exit(1);
    }
    
    for(ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if((ifa->ifa_addr->sa_family == AF_INET) && strncmp(ifa->ifa_name, "lo", 2)) {
           int s = getnameinfo(ifa->ifa_addr,  sizeof(struct sockaddr_in) , ip, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            printf("IP address of this system is :%s\n", ip);
            break;
        }
    }
    freeifaddrs(ifaddr);
    if ((host=gethostbyname(ip)) == NULL)
    {
        perror("gethostbyname failed");
        exit(1);
    }
    configfile = fopen ( PATH_TO_SERVER_CONFIG, "a+" );
    if (!configfile) {
        perror( PATH_TO_SERVER_CONFIG);
        exit(1);
    }
    fprintf(configfile, "%d ", myid);
    fprintf(configfile, "%s ", ip);
    fprintf(configfile, "%d\n", port);

    fclose(configfile);
    current_map_info.version_num = 1;
    current_map_info.Replicas_updated = 8;
    current_map_info.Distinguished_site = 'A';

    if((listensockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("FAULT: socket create failed");
        exit(1);
    }
    
    if (setsockopt(listensockfd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(int)) == -1) {
        perror("setsockopt() failed");
        exit(1);
    }
    
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr =  *((struct in_addr *)host->h_addr);
    memset(addr.sin_zero, '\0', sizeof (addr.sin_zero));
    
    if( bind(listensockfd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind() failed");
        close(listensockfd);
        exit(1);
    }
    
    if (listen(listensockfd,5) == -1) {
        perror("listen failed");
        close(listensockfd);
        exit(1);
    }

    while(1) {
        if(( newfd =  accept(listensockfd, (struct sockaddr *)&cliaddr, &len)) == -1) {
            perror("accept failed");
        }
        current_map_info.version_num = 1;
        current_map_info.Replicas_updated = 8;
        current_map_info.Distinguished_site = 'A';
        fclose(fopen(PATH_TO_LOG_FILE, "w"));
        pthread_create( &proccess_thread, NULL, &processthread, (void *)(uintptr_t)newfd);     //Creating server threas

    }
    

    pthread_join(proccess_thread, NULL);
   
}
