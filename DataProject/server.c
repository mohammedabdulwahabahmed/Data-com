#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <time.h>

#define DATA_SIZE 255
#define MESSAGE_SIZE 512
#define MAX_CLIENTS 10
#define MAXUSER 10

struct User {
    socklen_t clilen;
    struct sockaddr_in cli_addr;
    int newsockfd;
    char username[16];
    bool is_active;
};

struct User users[MAXUSER];
int user_anz = 0;
struct sockaddr_in serv_addr;

void error(const char *msg) {
    perror(msg);
    exit(1);
}

void create_log_directory() {
    struct stat st = {0};
    if (stat("logs", &st) == -1) {
        mkdir("logs", 0700);
    }
}

void write_log(const char *username, const char *message) {
    char filename[100], log_entry[DATA_SIZE + 100];
    time_t t = time(NULL);
    struct tm *tm = localtime(&t);
    sprintf(filename, "logs/%d-%02d-%02d.log", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    strftime(log_entry, sizeof(log_entry), "%Y-%m-%d %H:%M:%S", tm);
    sprintf(log_entry + strlen(log_entry), " [%s]: %s\n", username, message);
    FILE *file = fopen(filename, "a");
    if (file) {
        fprintf(file, "%s", log_entry);
        fclose(file);
    }
}

void remove_user(int user_index) {
    if (user_index < user_anz - 1) {
        memmove(&users[user_index], &users[user_index + 1], (user_anz - user_index - 1) * sizeof(struct User));
    }
    user_anz--;
}

void *client_socket_reader(void *usernr) {
    int mynr = *(int *)usernr;
    char buffer[DATA_SIZE], message_to_send[MESSAGE_SIZE], splitter[DATA_SIZE]; // Declare splitter here
    int n;

    while (users[mynr].is_active) {
        bzero(buffer, DATA_SIZE);
        n = read(users[mynr].newsockfd, buffer, DATA_SIZE - 1);
        if (n <= 0) {
            printf("Client %s disconnected\n", users[mynr].username);
            users[mynr].is_active = false;
            remove_user(mynr);
            continue;
        }
   // Introduce a 6% chance of corruption
        if (rand() % 100 < 6) {
            strncpy(buffer, "CORRUPTED MESSAGE", DATA_SIZE - 1);
            printf("!!!!!!!!Corrupted a message from %s\n", users[mynr].username);
            write_log(users[mynr].username, "!!!!!!!SENT A CORRUPTED MESSAGE!!!!!!!");
        } else {
            write_log(users[mynr].username, buffer);
        }

        sscanf(buffer, "%s", splitter);

        // Handle logout
        if (strncmp(buffer, "logout", 6) == 0) {
            printf("Client %s logged out\n", users[mynr].username);
            write(users[mynr].newsockfd, "You are disconnected\n", 22);
            users[mynr].is_active = false;
            remove_user(mynr);
            break; // End the while loop and exit the thread
        }

        // Log the received message
        write_log(users[mynr].username, buffer);

        // Handle list command
        if (strncmp(buffer, "list", 4) == 0) {
            char userList[MESSAGE_SIZE] = "Connected users:\n";
            for (int i = 0; i < user_anz; i++) {
                strcat(userList, users[i].username);
                strcat(userList, "\n");
            }
            write(users[mynr].newsockfd, userList, strlen(userList));
        }else if (strcmp(splitter, "help") == 0) {
            char helpMessage[] = "----Help--------\nlist\nhelp\nlogout\n----------------\n";
            write(users[mynr].newsockfd, helpMessage, strlen(helpMessage));
        // Handle private messages
        }else if (buffer[0] == '@') {
            // Extract the recipient's username
            char recipient[16];
            sscanf(buffer, "@%15s", recipient);
            char *message_start = strchr(buffer, ' ');
            if (message_start) {
                message_start++;
                snprintf(message_to_send, MESSAGE_SIZE, "[Private] %s: %s", users[mynr].username, message_start);
                bool sent = false;
                for (int i = 0; i < user_anz; i++) {
                    if (strcmp(users[i].username, recipient) == 0 && users[i].is_active) {
                        write(users[i].newsockfd, message_to_send, strlen(message_to_send));
                        sent = true;
                        break;
                    }
                }
                if (!sent) {
                    char errorMsg[] = "Error: Recipient not found.\n";
                    write(users[mynr].newsockfd, errorMsg, strlen(errorMsg));
                }
            }
        }
        // Broadcast messages
        else {
            snprintf(message_to_send, MESSAGE_SIZE, "%s: %s", users[mynr].username, buffer);
            for (int i = 0; i < user_anz; i++) {
                if (i != mynr && users[i].is_active) {
                    write(users[i].newsockfd, message_to_send, strlen(message_to_send));
                }
            }
        }
    }

    close(users[mynr].newsockfd);
    free(usernr);
    return NULL;
}

int main(int argc, char *argv[]) {
    srand(time(NULL)); // Seed the random number generator

    int sockfd, portno;
    socklen_t clilen;
    pthread_t pt;

    if (argc < 2) {
        fprintf(stderr, "ERROR, no port provided\n");
        exit(1);
    }

    create_log_directory();

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");

    bzero((char *)&serv_addr, sizeof(serv_addr));
    portno = atoi(argv[1]);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) 
        error("ERROR on binding");

    listen(sockfd, MAX_CLIENTS);
    clilen = sizeof(struct sockaddr_in);

    printf("Server started on port %d\n", portno);

    while (1) {
        struct User newUser;
        newUser.newsockfd = accept(sockfd, (struct sockaddr *)&newUser.cli_addr, &clilen);
        if (newUser.newsockfd < 0) 
            error("ERROR on accept");

        bzero(newUser.username, sizeof(newUser.username));
        int read_size = read(newUser.newsockfd, newUser.username, sizeof(newUser.username) - 1);
        if (read_size < 0) {
            close(newUser.newsockfd);
            continue;
        }

        newUser.clilen = clilen;
        newUser.is_active = true;

        printf("Client %s created\n", newUser.username);

        users[user_anz] = newUser;
        int *usernr = malloc(sizeof(int));
        *usernr = user_anz;
        if (pthread_create(&pt, NULL, client_socket_reader, usernr) != 0) {
            printf("Error creating thread for user %s\n", newUser.username);
            close(newUser.newsockfd);
        } else {
            user_anz++;
        }
    }

    close(sockfd);
    return 0;
}
