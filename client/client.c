#include "client.h"

clientnodes currentnodeinfo; //self node information

map_info server_data_info[SERVER_COUNT];

sync_data server_sync[SERVER_COUNT];
pthread_cond_t server_intn_done;

pthread_t server_threads[SERVER_COUNT]; //threads to interact with server
pthread_t request_generator;
char global_buf[2048];
static int serverresponsecount = 0,current_req_count=0,numberofvotes_received = 0,number;
static int serverrespawaitingcount = 0,max_version_index = 0;

pthread_mutex_t serverresponsemutex;

message_server serverindividualmessages[SERVER_COUNT];

quorum_st quorums[] = {
    {8,{'A','B','C','D','E','F','G','H'}},
    {4,{'A','B','C','D'}},
    {4,{'E','F','G','H'}},
    {1,{'A'}},
    {3,{'B','C','D'}},
    {3,{'E','F','G'}},
    {1,{'H'}},
    {6,{'B','C','D','E','F','G'}}
};

int parseConfigFiles(int myid) {
    
    FILE *file;
    int i = 0;
    
    file = fopen(PATH_TO_SERVER_CONFIG, "r");
    
    if(file==NULL) {
        printf("Error: can't open server config file\n");
        return -1;
    } else {
        printf("server config file opened successfully\n");
    }
    
    for(i=0;i<SERVER_COUNT;i++)
    {
        fscanf(file,"%d",&servers[i].serverid);//Reading server info from config file
        fscanf(file,"%s",servers[i].ip);
        fscanf(file,"%d",&servers[i].port);
        servers[i].socketfd = -1;
    }
    
    printf("====================================SERVERS================================================\n\n");
    
    for(i = 0 ; i < SERVER_COUNT ; i++)
        printf("id %d ip %s and port %d\n",servers[i].serverid,servers[i].ip,servers[i].port);
    
    printf("====================================================================================\n\n");
    return 0;
}

void *server_thread(void *serverid) {
    
    int id = (uintptr_t)serverid,set = 1;
    servernodes *serverinfo = &servers[id];
    struct sockaddr_in server_addr;
    struct hostent *host;
    message_server server_msg = {0};
    message_server server_resp = {0};
    
    printf("Establishing connection to server id %d, ip %s and port %d\n",serverinfo->serverid,serverinfo->ip,serverinfo->port);
    
    if ((serverinfo->socketfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("FAULT: Server socket create failed");
        exit(1);
    }
    
    if (setsockopt(serverinfo->socketfd, SOL_SOCKET, SO_REUSEADDR, &set, sizeof(int)) == -1) {
        perror("setsockopt() failed");
        exit(1);
    }
    
    if ((host=gethostbyname(serverinfo->ip)) == NULL)
    {
        perror("gethostbyname");
        exit(1);
    }
    
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(serverinfo->port);
    server_addr.sin_addr = *((struct in_addr *)host->h_addr);
    memset(&(server_addr.sin_zero), '\0', 8);
    
    if (connect(serverinfo->socketfd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("FAULT: Server socket connect failed");
        exit(1);
    }
    
    while(1) {
        pthread_mutex_lock(&server_sync[id].lock);
        pthread_cond_wait(&server_sync[id].wait,&server_sync[id].lock);
        pthread_mutex_unlock(&server_sync[id].lock);
        server_msg = serverindividualmessages[id]; //Read the request from the server specific slot and send req
        send(serverinfo->socketfd,&server_msg,sizeof(message_server),0); //sending message
        int ret = recv(serverinfo->socketfd,(void *)&server_resp,sizeof(message_server),MSG_WAITALL);
        if(ret <= 0) {
            printf("server request failed for op %d for serv id %d\n",server_msg.subtype.request.type,id);
        }
        pthread_mutex_lock(&serverresponsemutex);
        //Maintain votes;
        if(server_resp.subtype.response.type == SERVER_RESPONSE_GRANTED) {
            printf("     %c %d  %d  %c \n",65+id,server_resp.version,server_resp.numberofnodes_ingroup,server_resp.site);
            numberofvotes_received++;
            server_data_info[id].version_num = server_resp.version;
            server_data_info[id].Replicas_updated = server_resp.numberofnodes_ingroup;
            server_data_info[id].Distinguished_site = server_resp.site;
            if(max_version_index == -1 || server_data_info[max_version_index].version_num < server_resp.version) {
                max_version_index = id;
                memset(global_buf,0,2048);
                strcpy(global_buf,server_resp.buff);
            }
        }
        
        serverresponsecount++; //Keep track of how many servers have responded.

        if(serverresponsecount == serverrespawaitingcount) {
            pthread_cond_signal(&server_intn_done);
        }
        pthread_mutex_unlock(&serverresponsemutex);
        
    }
    exit(0);
}

void *request_generator_function(void *param) {
    
    int i = 0,j=0,required_votes=0,is_write_allowed = 0;
    while(current_req_count <  MAX_ITTERATIONS) {
        int size = sizeof(quorums)/sizeof(quorum_st);
        char timebuf[30] = {0};
        struct timeval currentTime;

            for(i = 0; i<size ; i++) {

            quorum_st quorum_single = quorums[i];
                required_votes = 0;
                is_write_allowed = 0;
            printf("==============================================\n\n\n");
            printf("Request no: %d\n",(current_req_count * size)+ i+1);
                for(j=0; j<quorum_single.srv_count;j++) {
                    printf(" %c",quorum_single.srv_id[j]);
                }
                printf("\n");
            
            pthread_mutex_lock(&serverresponsemutex);
            serverresponsecount = 0;
            serverrespawaitingcount = 0;
            numberofvotes_received = 0;
            max_version_index = -1;
            printf("the values in replies\n");
            printf("Server VN RU DS\n");
            for ( j = 0;j < quorum_single.srv_count ; j++) {
                int index = quorum_single.srv_id[j] - 65;
                message_server *msg = &serverindividualmessages[index];
                msg->id = currentnodeinfo.clientid;
                msg->type = SERVER_MESSAGE_TYPE_REQUEST;
                msg->subtype.request.type = SERVER_REQUEST_WRITE_ACCESS;
                ++serverrespawaitingcount; //Number of servers we are requesting. Response should match this count
                pthread_mutex_lock(&server_sync[index].lock);
                pthread_cond_signal(&server_sync[index].wait);
                pthread_mutex_unlock(&server_sync[index].lock);
            }
            
            pthread_cond_wait(&server_intn_done,&serverresponsemutex);
                required_votes = server_data_info[max_version_index].Replicas_updated/2;
          //      printf("The number of votes received : %d and required Votes are %d\n",numberofvotes_received,required_votes);
                printf("\n");
            if(numberofvotes_received > required_votes) {
               // printf("\n"serverrespawaitingcount);
                printf("The number of votes received  greater than votes required so operation is successful.\n\n");

                is_write_allowed = 1;


            } else if(numberofvotes_received == required_votes) {
                      
           //     printf("Number of Votes equals the received votes. So Tie breaking\n");
                
                if(server_data_info[max_version_index].Distinguished_site == quorum_single.srv_id[0]) {
               //     printf("As least server id matching the Distinguished site id allowing the write DS: %c and least id %c\n ",server_data_info[max_version_index].Distinguished_site,quorum_single.srv_id[0]);
                    printf("The number of votes required is %d and since DS is %c, operation can be successfully performed\n\n",numberofvotes_received,server_data_info[max_version_index].Distinguished_site);
                    is_write_allowed = 1;
                } else {
                 //   printf("Not allowing to write as mismatch in DS and the least server id DS: %c and least id %c\n",server_data_info[max_version_index].Distinguished_site,quorum_single.srv_id[0]);
                    printf("The number of votes required is %d and since DS is %c, operation cannot be successfully performed. so the values remain same\n\n",numberofvotes_received,server_data_info[max_version_index].Distinguished_site);

                    is_write_allowed = 0;
                }
                
            } else {
                printf("The number of votes required is %d , which is lesser than the required numberof votes, so operation fails and values remains same\n\n",numberofvotes_received);
                is_write_allowed = 0;

            }
                      //TODO: File update;
                      
            serverresponsecount = 0;
            serverrespawaitingcount = 0;
            gettimeofday(&currentTime, NULL);
            snprintf (timebuf, 30, "%jd",currentTime.tv_sec * (int)1e6 + currentTime.tv_usec);
            if(is_write_allowed == 1) {
                printf("After write table looks like this\n\n");
                printf("Server VN RU DS\n");
                strcat(global_buf,timebuf);

                for ( j = 0;j < quorum_single.srv_count ; j++) {
                    int index = quorum_single.srv_id[j] - 65;
                    message_server *msg = &serverindividualmessages[index];
                    msg->id = currentnodeinfo.clientid;
                    msg->type = SERVER_MESSAGE_TYPE_REQUEST;
                    msg->numberofnodes_ingroup = quorum_single.srv_count;
                    if(server_data_info[index].version_num < server_data_info[max_version_index].version_num) {
                        msg->subtype.request.type = SERVER_REQUEST_UPDATE_FILE;
                        strcpy(msg->buff,global_buf);

                    } else {
                        msg->subtype.request.type = SERVER_REQUEST_WRITE;
                        strcpy(msg->buff,timebuf);

                    }
                    if(quorum_single.srv_count %2 == 0) {
                        msg->site = quorum_single.srv_id[0];
                    } else {
                        msg->site = server_data_info[max_version_index].Distinguished_site;
                    }
                    msg->version = server_data_info[max_version_index].version_num + 1;
                    printf("     %c %d  %d  %c \n",65+index,server_data_info[max_version_index].version_num + 1,quorum_single.srv_count,msg->site);
                    pthread_mutex_lock(&server_sync[index].lock);
                    pthread_cond_signal(&server_sync[index].wait);
                    pthread_mutex_unlock(&server_sync[index].lock);
                    ++serverrespawaitingcount; //Number of servers we are requesting. Response should match this count

                }
                pthread_cond_wait(&server_intn_done,&serverresponsemutex);
            } else {
                printf("Server VN RU DS\n");
                for ( j = 0;j < quorum_single.srv_count ; j++) {
                    int index = quorum_single.srv_id[j] - 65;

                    printf("     %c %d  %d  %c \n",65+index,server_data_info[index].version_num,server_data_info[index].Replicas_updated,server_data_info[index].Distinguished_site);

                }
            }
                pthread_mutex_unlock(&serverresponsemutex);

            printf("==============================================\n\n\n");

        }
        current_req_count++;
    }
    return NULL;
}

int main(int argc, char **argv) {
    
    struct ifaddrs *ifaddr, *ifa;
    char ip[NI_MAXHOST];
    int myid=-1,port=-1,i = 0;
    struct hostent *host;

    pthread_cond_init(&server_intn_done,NULL);
    pthread_mutex_init(&serverresponsemutex,NULL);

    myid = 0;
    port = 0;//atoi(argv[2]);
    
    
    if (getifaddrs(&ifaddr) == -1) {
        perror("ERROR: getifaddrs\n");
        exit(1);
    }
    
    for(ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if((ifa->ifa_addr->sa_family == AF_INET) && strncmp(ifa->ifa_name, "lo", 2)) {
            int s = getnameinfo(ifa->ifa_addr,  sizeof(struct sockaddr_in) , ip, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
            printf("IP address is :%s\n", ip);
            break;
        }
    }
    freeifaddrs(ifaddr);
    if ((host=gethostbyname(ip)) == NULL)
    {
        perror("gethostbyname failed\n");
        exit(1);
    }
    
    strncpy(currentnodeinfo.ip,ip,25);
    currentnodeinfo.port = port;
    currentnodeinfo.clientid = myid;
    
    if(parseConfigFiles(myid) < 0) {
        printf("Error in config parse.. abort\n");
        exit(1);
    }

    // Establish connections with all servers.
    for(i = 0 ; i < SERVER_COUNT ; i++) {
        printf("Server %d thread started\n",i);
        pthread_mutex_init(&(server_sync[i].lock),NULL);
        pthread_cond_init(&(server_sync[i].wait),NULL);
        pthread_create( &server_threads[i], NULL, &server_thread, (void *)(uintptr_t)i);     //Creating server threads
    }
    
    sleep(1);
    pthread_create( &request_generator, NULL, &request_generator_function, NULL);     //request generating thread
    
    for(i=0;i<SERVER_COUNT;i++) {
        pthread_join( server_threads[i], NULL);               //Join all server threads
    }
    
    pthread_join(request_generator,NULL);  //Join request gen thread

}

