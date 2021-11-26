#include "server.h"
#include "protocol.h"
#include <pthread.h>
#include <signal.h>

typedef struct job{
    int client_fd;
    petr_header* job_header;
    char* messageBuffer;

} job_pack;

typedef struct room{
    char * room_name;
    int fd_list[4]; //It's 4 bc the owner will always stay in the room
    int owner;

} room_pack;

typedef struct user {
    int client_fd;
    char * user;
} user_pack;

static char buffer[BUFFER_SIZE];
static pthread_mutex_t buffer_lock;

static List_t jobsBuffer;
static pthread_mutex_t jbuffer_lock;

static List_t users;
static pthread_mutex_t user_lock;

static List_t room_list;
static pthread_mutex_t room_lock;

//The following are just tests to see if there's a better way to test concurrency
static pthread_mutex_t fd_list_lock;




int listen_fd;
int nJobs = 2;
int Mylog;

void sigint_handler(int sig) {
    printf("shutting down server\n");
    close(listen_fd);
    exit(0);
}

int server_init(int server_port) {
    int sockfd;
    struct sockaddr_in servaddr;

    // socket create and verification
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1) {
        printf("socket creation failed...\n");
        exit(EXIT_FAILURE);
    } else
        printf("Socket successfully created\n");

    bzero(&servaddr, sizeof(servaddr));

    // assign IP, PORT
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(server_port);

    int opt = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, (char *)&opt, sizeof(opt)) < 0) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Binding newly created socket to given IP and verification
    if ((bind(sockfd, (SA *)&servaddr, sizeof(servaddr))) != 0) {
        printf("socket bind failed\n");
        exit(EXIT_FAILURE);
    } else
        printf("Socket successfully binded\n");

    // Now server is ready to listen and verification
    if ((listen(sockfd, 1)) != 0) {
        printf("Listen failed\n");
        exit(EXIT_FAILURE);
    } else
        printf("Server listening on port: %d.. Waiting for connection\n", server_port);

    return sockfd;
}

void *job_thread() { //MUTEX ERRORS EVERY ONCE IN AWHILE

    while (1) {
        // take head job of the list
        pthread_mutex_lock(&jbuffer_lock);
        if (jobsBuffer.head != NULL) {
            // delete job from job buffer
            printf("New job picked up\n");
            
            node_t* job = jobsBuffer.head;
            petr_header * client = calloc(1, sizeof(petr_header));
            client = ((job_pack *)jobsBuffer.head->value)->job_header;
            int client_fd = ((job_pack *)job->value)->client_fd;
            char * clientbuffer = calloc(sizeof(((job_pack *)jobsBuffer.head->value)->messageBuffer), sizeof(char));
            strcpy(clientbuffer, ((job_pack *)jobsBuffer.head->value)->messageBuffer);
        
            removeFront(&jobsBuffer);

            pthread_mutex_unlock(&jbuffer_lock);

            printf("JOB INFO:\n Job type: %d\n Job length: %d\n Job message: %s\n Client Fd: %d\n", client->msg_type, client->msg_len, clientbuffer, client_fd);

            if (client->msg_type == RMCREATE) { //DONE
                printf("USER WANTS TO CREATE ROOM\n");
                bool create_room = true;
                
                pthread_mutex_lock(&room_lock);
                
                node_t * node_ptr = room_list.head;
                
                
                while (node_ptr != NULL) {
                    if (strcmp(((room_pack *)node_ptr->value)->room_name, clientbuffer) == 0) {
                        printf("ROOM EXISTS\n");
                        petr_header * p = calloc(1, sizeof(petr_header));
                        p->msg_len = 0;
                        p->msg_type = ERMEXISTS;
                        wr_msg(client_fd, p, "room repeats");
                        create_room = false;
                        free(p);
                        //close(client_fd);
                        break;
                    }
                    node_ptr = node_ptr->next;
                }
                
                if (create_room) {
                    printf("ROOM TO BE CREATED\n");
                    pthread_mutex_lock(&fd_list_lock);
                    room_pack * r = calloc(1, sizeof(room_pack));
                    r->room_name = clientbuffer;
                    r->owner = client_fd;

                    insertRear(&room_list, (void *) r);

                    pthread_mutex_unlock(&fd_list_lock);

                    printf("Room name: %s\n", r->room_name);
                    printf("Only user inside: %d\n", r->owner);

                    printf("Roomlist head room: %s-%d\n", ((room_pack *)room_list.head->value)->room_name, ((room_pack *)room_list.head->value)->owner);
                    
                    printf("New job Done\n");
                    // send OK message to client
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = 0;
                    wr_msg(client_fd, p, "job done"); 
                
                }
                pthread_mutex_unlock(&room_lock);
            }

            if (client->msg_type == RMDELETE) { //// ALMOST DONE HAS MEMORY LEAKS
                printf("USER WANTS TO DELETE A ROOM\n");
                // find if room exist 
                pthread_mutex_lock(&room_lock);
                node_t * node_ptr = room_list.head;
                room_pack * room;
                int roomExist = 0, roomIndex = 0, UserAccess = 0;
                while (node_ptr != NULL) {
                    if (strcmp( ((room_pack *)node_ptr->value)->room_name, clientbuffer) == 0) {
                        printf("ROOM EXISTS\n");
                        roomExist = 1;
                        // is the user the owner of room?
                        room = node_ptr->value;
                        if (client_fd == room->owner)
                        {
                            printf("REMOVE ROOM\n");
                            UserAccess = 1;
                            // taking room out of the list
                            removeByIndex(&room_list, roomIndex);
                        }
                        break;
                    }
                    roomIndex++;
                    node_ptr = node_ptr->next;
                }
                pthread_mutex_unlock(&room_lock);

                if (roomExist == 0) // room doesnt exist
                {
                    // room does not exist send error message 
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = ERMNOTFOUND;
                    wr_msg(client_fd, p, "room not found");
                    free(p);
                } else if(UserAccess == 1){ // user access
                    // send Room close to all members (except owner)
                    int fd_ind = 0;
                    while (fd_ind < 4)
                    {
                        if (room->fd_list[fd_ind] != 0) {
                            petr_header * p = calloc(1, sizeof(petr_header));
                            p->msg_len = strlen(clientbuffer)+1;
                            p->msg_type = RMCLOSED;
                            wr_msg(room->fd_list[fd_ind], p, clientbuffer);
                            free(p);
                        }
                        fd_ind++;
                    }


                    // free memory for room and fd_list


                    printf("New job Done\n");
                    // send OK message to client
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = 0;
                    wr_msg(client_fd, p, "job done");
                    free(p);
                } else { // no access
                    // user does not own the room
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = ERMDENIED;
                    wr_msg(client_fd, p, "Access Denied");
                    free(p);
                }                 
            }

            if (client->msg_type == RMJOIN) { //Done

                printf("USER WANTS TO JOIN A ROOM\n");
                pthread_mutex_lock(&room_lock);
                node_t * node_ptr = room_list.head;
                int* ClientAdded = &client_fd;
                int roomExist = 0;
                bool room_full = true;
                // find the room
                while (node_ptr != NULL) {
                    if (strcmp(((room_pack *)node_ptr->value)->room_name, clientbuffer) == 0) {
                        printf("ROOM EXISTS: CHECK IF FULL\n");
                        // room does not have a cap
                        // adding client to room
                        pthread_mutex_lock(&fd_list_lock);
                        int fd_ind = 0;
                        room_pack * room = (room_pack *)node_ptr->value; 
                        while (fd_ind < 4) {
                            printf("%d\n", room->fd_list[fd_ind]);
                            if (room->fd_list[fd_ind] == 0) {
                                room->fd_list[fd_ind] = client_fd;
                                room_full = false;
                                break;
                            }
                            fd_ind++;
                        }
                        pthread_mutex_unlock(&fd_list_lock);
                        roomExist = 1;
                        break;
                    }
                    node_ptr = node_ptr->next;
                }
                pthread_mutex_unlock(&room_lock);
                if (roomExist == 0)
                {
                    // room was not found
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = ERMNOTFOUND;
                    wr_msg(client_fd, p, "room not found");
                    free(p);
                } else if (room_full)  {
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = ERMFULL;
                    wr_msg(client_fd, p, "room full");
                    free(p);
                }
                else {
                    printf("New job Done\n");
                    // send OK message to client
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = 0;
                    wr_msg(client_fd, p, "job done");
                    free(p);
                }

                
            }

            if (client->msg_type == RMLEAVE) { //Done
                printf("USER WANTS TO LEAVE A ROOM\n");
                // find if room exist 
                pthread_mutex_lock(&room_lock);
                node_t * node_ptr = room_list.head;
                room_pack * room = NULL;
                bool user_owner = false;
                int roomIndex = 0, UserAccess = 0;
                while (node_ptr != NULL) {
                    if (strcmp( ((room_pack *)node_ptr->value)->room_name, clientbuffer) == 0) {
                        printf("ROOM EXISTS\n");

                        // is the user the owner of room?
                        room = node_ptr->value;
                        if (client_fd == room->owner)
                        {
                           // user does own the room
                            petr_header * p = calloc(1, sizeof(petr_header));
                            p->msg_len = 0;
                            p->msg_type = ERMDENIED;
                            wr_msg(client_fd, p, "Access Denied");
                            free(p);
                            user_owner = true;

                        } else {
                            pthread_mutex_lock(&fd_list_lock);
                            int fd_ind = 0;
                            while (fd_ind < 4) {
                                if (room->fd_list[fd_ind] == client_fd) {
                                    room->fd_list[fd_ind] = 0;
                                    break;
                                }
                                fd_ind += 1;
                            }
                            pthread_mutex_unlock(&fd_list_lock);
                        }
                        break;
                    }
                    roomIndex++;
                    node_ptr = node_ptr->next;
                }
                pthread_mutex_unlock(&room_lock);
                
                // Room exist, and user has access
                if(room != NULL && user_owner == false){

                    // free memory for room and fd_list
                    printf("New job Done\n");
                    // send OK message to client
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = 0;
                    wr_msg(client_fd, p, "job done");
                    free(p);
                } else if (user_owner == false) {
                    // room does not exist send error message 
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = ERMNOTFOUND;
                    wr_msg(client_fd, p, "room not found");
                    free(p);
                }
            }

            if (client->msg_type == RMLIST) { //Done 
                printf("USER WANTS A LIST OF ROOMS\n");
                pthread_mutex_lock(&room_lock);
                node_t * node_ptr = room_list.head;
                char * room_list = calloc(200, sizeof(char));
                int num_room = 0;
                
                while (node_ptr != NULL) {
                    room_pack * room = (room_pack *) node_ptr->value;
                    
                    strcat(room_list, room->room_name); 
                    strcat(room_list, ": ");

                    pthread_mutex_lock(&user_lock);
                    node_t * name_ptr = users.head;
                    while (name_ptr != NULL) {
                        user_pack * user = name_ptr->value;
                        if (user->client_fd == room->owner) {
                            strcat(room_list, user->user);
                            printf("%s\n", room_list);
                            break;
                        }
                        name_ptr = name_ptr->next;
                    }
                    pthread_mutex_unlock(&user_lock);

                    printf("New room: %s\n", room->room_name);
                    pthread_mutex_lock(&fd_list_lock);
                    int fd_ind = 0;
                    while (fd_ind < 4) {
                        pthread_mutex_lock(&user_lock);
                        node_t * name_ptr = users.head;
                        while (name_ptr != NULL) {
                            user_pack * user = name_ptr->value;
                            if (user->client_fd == room->fd_list[fd_ind]) {
                                 strcat(room_list, ",");
                                strcat(room_list, user->user);
                                printf("%s\n", room_list);
                                break;
                            }
                            name_ptr = name_ptr->next;
                        }
                        pthread_mutex_unlock(&user_lock);
                        fd_ind++;
                    }
                    pthread_mutex_unlock(&fd_list_lock);

                    strcat(room_list, "\n");
                    num_room += 1;
                    node_ptr = node_ptr->next;
                }

                pthread_mutex_unlock(&room_lock);
                
                strcat(room_list, "\0");

                printf("Room list: %s\n", room_list);
                petr_header * p = calloc(1, sizeof(petr_header));
                if (num_room > 0) {
                    p->msg_len = strlen(room_list)+1; 
                }
                else {
                    p->msg_len = 0;
                }
                p->msg_type = RMLIST;
                wr_msg(client_fd, p, room_list);
                free(room_list);
                free(p);
                
            }

            if (client->msg_type == USRLIST) { //DONE
                printf("USER WANTS A LIST OF USERS\n");

                pthread_mutex_lock(&user_lock);
                node_t * node_ptr = users.head;
                char * user_list = calloc(200, sizeof(char));
                int num_users = 0;
                
                while (node_ptr != NULL) {
                    user_pack * user = (user_pack *) node_ptr->value;
                    if (user->client_fd != client_fd) {
                        strcat(user_list, user->user); //FIGURE OUT DYNAMIC APPENDS 
                        strcat(user_list, "\n");
                        num_users += 1;
                    }
                    node_ptr = node_ptr->next;
                }
                
                strcat(user_list, "\0");

                printf("UserList: %s\n", user_list);
                petr_header * p = calloc(1, sizeof(petr_header));
                if (num_users > 0) {
                    p->msg_len = strlen(user_list)+1; //DYNAMIC LENGTH?
                }
                else {
                    p->msg_len = 0;
                }
                p->msg_type = USRLIST;
                wr_msg(client_fd, p, user_list);
                free(user_list);
                free(p);

                pthread_mutex_unlock(&user_lock);
                
            }

            if (client->msg_type == RMSEND) {
                printf("USER WANTS TO SEND A MESSAGE TO THE ROOM\n");

                char* roomName = strtok(clientbuffer, "\r\n");
                pthread_mutex_lock(&room_lock);
                node_t * node_ptr = room_list.head;
                room_pack* room;
                int roomExist = 0, UserAccess = 0;
                // find the room
                while (node_ptr != NULL) {

                    if (strcmp(((room_pack *)node_ptr->value)->room_name, roomName) == 0) {
                        printf("ROOM EXISTS\n");
                        // is the user in the room?
                        room = node_ptr->value;
                        if (room->owner == client_fd) {
                            printf("USER IN ROOM\n");
                            UserAccess = 1;
                        }
                        else {
                            int fd_ind = 0;
                            while (fd_ind < 4)
                            {
                                if (client_fd == room->fd_list[fd_ind])
                                {
                                    printf("USER IN ROOM\n");
                                    UserAccess = 1;
                                    break;
                                }
                                fd_ind++;
                            }  
                        }
                        roomExist = 1;
                    }
                    node_ptr = node_ptr->next;
                }
                pthread_mutex_unlock(&room_lock);
                if (roomExist == 0){
                    // room does not exist send error message 
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = ERMNOTFOUND;
                    wr_msg(client_fd, p, "room not found");
                    free(p);
                } 
                else if (UserAccess == 1) {
                    // send message to all members (except owner)
                    printf("CREATING MESSAGE\n");

                    // create message
                    char * message = calloc(300, sizeof(char));
                    strcat(message, roomName);
                    strcat(message, "\r\n");
                    
                    //Get username of client
                    node_t * user = users.head;
                    while (user != NULL) {
                        if (client_fd == ((user_pack*) user->value)->client_fd) {
                            strcat(message, ((user_pack*) user->value)->user);
                            strcat(message, "\r\n");
                            break;
                        }
                        user = user->next;
                    }

                    roomName = strtok(NULL, "\n"); 
                    //roomname contains message now the message
                    if (roomName != NULL)
                    {
                        strcat(message, roomName);
                    }
                    
                    strcat(message, "\0");
                    // format: roomname\r\nUsername\r\nMessage

                    printf("message: %s\n", message);
                    printf("Message length: %ld\n", strlen(message));

                    if (client_fd != room->owner) 
                    { //Checks the owner if the owner isn't the sender

                        petr_header * p = calloc(1, sizeof(petr_header));
                        p->msg_len = strlen(message)+2;
                        p->msg_type = RMRECV;
                        wr_msg(room->owner, p, message);
                        free(p);
                    }

                    int fd_ind = 0;
                    while (fd_ind < 4)
                    {
                        if (room->fd_list[fd_ind] != client_fd && room->fd_list[fd_ind] != 0) {
                            petr_header * p = calloc(1, sizeof(petr_header));
                            p->msg_len = strlen(message)+2;
                            p->msg_type = RMRECV;
                            wr_msg(room->fd_list[fd_ind], p, message);
                            free(p);
                        }
                        fd_ind++;
                    }

                    printf("New job Done\n");
                    // send OK message to client
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = OK;
                    wr_msg(client_fd, p, "job done");
                    free(p);
                    free(message);
                } 
                else {
                    // user not in room
                    printf("USER NOT IN ROOM\n");

                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = ERMDENIED;
                    wr_msg(client_fd, p, "Access Denied");
                    free(p);
                }
            }

            if (client->msg_type == USRSEND) { //DONE
                printf("USER WANTS TO SEND A MESSAGE TO ANOTHER USER\n");

                char * message = calloc(500, sizeof(char)); 

                //Get username of client
                node_t * user = users.head;
                while (user != NULL) {
                    if (client_fd == ((user_pack*) user->value)->client_fd) {
                        //char * sender = ((user_pack*) user->value)->user);
                        strcat(message, ((user_pack*) user->value)->user);
                        strcat(message, "\r\n");
                        break;
                    }
                    user = user->next;
                }
                
                bool user_found = false;

                // Extract the first token and assign to reciever
                char * token = strtok(clientbuffer, "\r\n");
                char * reciever = token;

                // Extract the seocnd token and assign to message
                
                if ((token = strtok(NULL, "\r\n")) != NULL) {
                    strcat(message, token);
                    //char * sending = token;
                }
                else {
                    //char * sending = "";
                }

                /*
                printf("TOTAL MALLOC: %ld\n", strlen(sender)+strlen(sending)+3);
                char * message = malloc(strlen(sender)+strlen(sending)+3); 

                strcat(message, sender);
                strcat(message, "\r\n");
                strcat(message, sending);
                strcat(message, "\0");
                printf( "Message: %s\n", message);
                printf("Message length: %ld\n", strlen(message));
                */


                strcat(message, "\0");
                printf( "Message: %s\n", message);
                printf("Message length: %ld\n", strlen(message)+1);

                user = users.head;
                while (user != NULL) { //Finds the reciever fd
                    if (strcmp(reciever, ((user_pack*) user->value)->user) == 0) {
                        user_found = true;
                        petr_header * p = calloc(1, sizeof(petr_header));
                        p->msg_len = strlen(message)+1;
                        p->msg_type = USRRECV;
                        wr_msg(((user_pack*) user->value)->client_fd, p, message);
                        free(p);
                        break;
                    }
                    user = user->next;
                }
                if (user_found) {
                    printf("New job Done\n");
                    // send OK message to client
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = 0;
                    wr_msg(client_fd, p, "job done");
                    free(p);
                }
                else {
                    printf("User not found\n");
                    // send OK message to client
                    petr_header * p = calloc(1, sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = EUSRNOTFOUND;
                    wr_msg(client_fd, p, "user not found");
                    free(p);
                }
                free(message);
            }

            if (client->msg_type == LOGOUT) { //SOMETIMES WORKS
                printf("USER WANTS TO LOGOUT\n");

                //Remove from userlist
                pthread_mutex_lock(&user_lock);
                node_t * node_ptr = users.head;
                int index = 0;
                while (node_ptr != NULL) {
                    user_pack * user = node_ptr->value;
                    if (user->client_fd == client_fd) {
                            removeByIndex(&users, index);
                            break;
                    }
                    index += 1;
                    node_ptr = node_ptr->next;
                }
                pthread_mutex_unlock(&user_lock);

                //Remove from rooms if he isn't owner or Delete owned rooms

                pthread_mutex_lock(&room_lock);
                node_ptr = room_list.head;
                room_pack * room = NULL;
                bool user_owner = false;
                int roomIndex = 0, UserAccess = 0;
                while (node_ptr != NULL) {
                    // is the user the owner of room?
                    room = node_ptr->value;
                    if (client_fd == room->owner)
                    {
                        // user does own the room, loops through fd_list removing people
                        int fd_ind = 0;
                        while (fd_ind < 4)
                        {
                            if (room->fd_list[fd_ind] != 0) {
                                petr_header * p = calloc(1, sizeof(petr_header));
                                printf("SENDING RMCLOSED TO %d\n", room->fd_list[fd_ind]);
                                p->msg_len = 100 * sizeof(char);
                                p->msg_type = RMCLOSED;
                                wr_msg(room->fd_list[fd_ind], p, clientbuffer);
                                free(p);
                            }
                            fd_ind++;
                        }
                        printf("REMOVE %s FROM LIST\n", room->room_name);
                        removeByIndex(&room_list, roomIndex);

                    } else {
                        // Finds user in list
                        int fd_ind = 0;
                        while (fd_ind < 4) {
                            if (room->fd_list[fd_ind] == client_fd) {
                                printf("REMOVE %d from %s\n", client_fd, room->room_name);
                                room->fd_list[fd_ind] = 0;
                                break;
                            }
                            fd_ind += 1;
                        }
                    }
                    
                    roomIndex++;
                    node_ptr = node_ptr->next;
                }
                pthread_mutex_unlock(&room_lock);

                printf("New job Done\n");
                // send OK message to client
                petr_header * p = calloc(1, sizeof(petr_header));
                p->msg_len = 0;
                p->msg_type = 0;
                wr_msg(client_fd, p, "job done");
                free(p);
                close(client_fd);
            }
            
        }
        else {
            pthread_mutex_unlock(&jbuffer_lock);
        }
    }
    return NULL;
}

// Function running the user
// It will wait for its assigned user to send a message
// and add it to the queue for the job_threads 
void *client_thread(void *clientfd_ptr) {
    int client_fd = *(int *)clientfd_ptr;
    free(clientfd_ptr);
    int received_size;
    fd_set read_fds;

    int retval;
    while (1) {
        
        FD_ZERO(&read_fds);
        FD_SET(client_fd, &read_fds);
        retval = select(client_fd + 1, &read_fds, NULL, NULL, NULL);
        if (retval != 1 && !FD_ISSET(client_fd, &read_fds)) {
            printf("Error with select() function\n");
            break;
        }
        
        printf("new message from %d \n", client_fd);

        pthread_mutex_lock(&buffer_lock);

        petr_header * client = calloc(1, sizeof(petr_header));

        rd_msgheader(client_fd, client);

        bzero(buffer, BUFFER_SIZE);
        if (client->msg_len > 0) {
            received_size = read(client_fd, buffer, client->msg_len);
            if (received_size < 0) {
                printf("Receiving failed\n");
                break;
            } else if (received_size == 0) {
                continue;
            }
        }
        
        printf("adding a new job from %d\n", client_fd);

        job_pack* job = calloc(1, sizeof(job_pack));
        job->job_header = calloc(1, sizeof(petr_header));

        job->messageBuffer = calloc(client->msg_len, sizeof(char));   
        if (client->msg_len > 0) { 
            strcpy(job->messageBuffer, buffer);
        }
        else {
            job->messageBuffer = "";
        }

        job->client_fd = client_fd;
        job->job_header->msg_type = client->msg_type;
        job->job_header->msg_len = client->msg_len;
        
        printf("adding the job to list\n");
         // add job to rear of job list
        pthread_mutex_lock(&jbuffer_lock);
        insertRear(&jobsBuffer, (void*) job);

        free(client);
        pthread_mutex_unlock(&jbuffer_lock);
        pthread_mutex_unlock(&buffer_lock);
        
        /*
        printf("New job Done\n");
        // send OK message to client
        petr_header * p = malloc(sizeof(petr_header));
        p->msg_len = 0;
        p->msg_type = 0;
        wr_msg(client_fd, p, "job done"); 
        */
    }
    // Close the socket at the end
    printf("Close current client connection\n");
    close(client_fd);
    return NULL;
}

void run_server(int server_port) {
    listen_fd = server_init(server_port); // Initiate server and start listening on specified port
    int client_fd;
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    pthread_t tid;
    
    // create job threads
    for (size_t i = 0; i < nJobs; i++)
    {
        pthread_create(&tid, NULL, job_thread, NULL);
    }

    while (1) {
        // Wait and Accept the connection from client
        printf("Wait for new client connection\n");
        int *client_fd = malloc(sizeof(int));
        *client_fd = accept(listen_fd, (SA *)&client_addr, (socklen_t*)&client_addr_len);
        pthread_mutex_lock(&buffer_lock);       
        if (*client_fd < 0) {
            printf("server acccept failed\n");
            exit(EXIT_FAILURE);
        } else {
            printf("Client connetion accepted\n");
            
            int client_fd2 = *(int *)client_fd;
            
            petr_header * client = malloc(sizeof(petr_header));

            rd_msgheader(*client_fd, client);
            read(client_fd2, buffer, client->msg_len);
            
            //User logging in
            bool logged_in = true; 
            // is username repeated?
            pthread_mutex_lock(&user_lock);
            node_t * node_ptr = users.head;
            while (node_ptr != NULL) {
                user_pack * user = (user_pack *) node_ptr->value;
                if (strcmp(user->user, buffer) == 0) {
                    petr_header * p = malloc(sizeof(petr_header));
                    p->msg_len = 0;
                    p->msg_type = 0x1A;
                    wr_msg(*client_fd, p, "User repeats");
                    logged_in = false;
                    free(p);
                    close(*client_fd);
                    break;
                }
                node_ptr = node_ptr->next;
            }
            
            if (logged_in) {
                user_pack * user = malloc(sizeof(user_pack));

                user->client_fd = client_fd2;
                
                char * buffer_val = malloc(sizeof(buffer));
                strcpy(buffer_val, buffer);

                user->user = buffer_val;

                insertRear(&users, (void*) user);

                petr_header * p = malloc(sizeof(petr_header));
                p->msg_len = 0;
                p->msg_type = 0;
                wr_msg(*client_fd, p, "Logged in");
                free(p);

                pthread_create(&tid, NULL, client_thread, (void *)client_fd);
            }
            pthread_mutex_unlock(&user_lock);

        }
        pthread_mutex_unlock(&buffer_lock);
    }
    bzero(buffer, BUFFER_SIZE);
    close(listen_fd);
    return;
}

int main(int argc, char *argv[]) {

    // init users list
    users.head = NULL;
    users.length = 0;
    users.comparator = NULL;  

    // init jobsBuffer list
    jobsBuffer.head = NULL;
    jobsBuffer.length = 0;
    jobsBuffer.comparator = NULL;

    //init room_list list
    room_list.head = NULL;
    room_list.length = 0;
    room_list.comparator = NULL;

    // check for -j N and -h optional commands hint on HW2
    int opt;
    unsigned int port = 0;
    int j = 0;
    while ((opt = getopt(argc, argv, ":hj:")) != -1) {
        switch (opt) {
        case 'h':
            printf("./bin/petr_server [-h] [-j N] PORT_NUMBER AUDIT_ FILENAME\n\n-h \t\tDisplay this help menu, and returns EXIT_SUCCESS.\n-j N \t\tNumber of job threads. Default to 2.\nAUDIT_FILENAME \tFile to output Audit Log message to.\nPORT_NUMBER \tPortnumber to listen on.\n");
            return EXIT_SUCCESS;
        case 'j':
            nJobs = atoi(optarg);
            if (nJobs <= 0){
                fprintf(stderr, "ERROR: N cannot be less than one\n");
                return EXIT_FAILURE;
            }
            j = 1;
            break;
        default: /* '?' */
            fprintf(stderr, "ERROR: Unknown Parsing Flag\n");
            return EXIT_SUCCESS;
        }
    }
    // get port and log file name
    char* logstring;
    if (j == 1)
    {
        logstring = malloc(sizeof(argv[4]));
        strcpy(logstring, argv[4]); 
        port = atoi(argv[3]);
    } else {
        logstring = malloc(sizeof(argv[2]));
        strcpy(logstring, argv[2]); 
        port = atoi(argv[1]);
    }    
        
    if (port == 0) {
        fprintf(stderr, "ERROR: Port number for server to listen is not given\n");
        fprintf(stderr, "Server Application Usage: %s <port_number> <log file>\n",
            argv[0]);
        exit(EXIT_FAILURE);
    }
    // get file 
    Mylog = open(logstring, O_WRONLY | O_CREAT, 0644);
    if (Mylog == -1){
        perror("Error opening file."); 
        exit(EXIT_FAILURE);
    }
    // run server
    run_server(port);
	
    if (close(Mylog) < 0){ 
        perror("Error closing file."); 
        exit(EXIT_FAILURE);
    }
	
    return 0;
}
