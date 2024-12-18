#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <sys/stat.h>
#include <time.h>

#define DATA_SIZE 255

struct thread_data {
    int sockfd;
    char username[DATA_SIZE];
};

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

    // Create a filename based on the current date
    sprintf(filename, "logs/%d-%02d-%02d.log", tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

    // Format the log entry with timestamp, username, and message
    strftime(log_entry, sizeof(log_entry), "%Y-%m-%d %H:%M:%S", tm);
    sprintf(log_entry + strlen(log_entry), " [%s]: %s\n", username, message);

    // Write the log entry to the file
    FILE *file = fopen(filename, "a");
    if (file) {
        fprintf(file, "%s", log_entry);
        fclose(file);
    }
}

unsigned long crc32(const unsigned char *data, unsigned int len) {
    unsigned long crc = 0;
    for (unsigned int i = 0; i < len; ++i) {
        crc += data[i];
    }
    return crc % 0xFFFFFFFF;
}

void check_2d_parity(const unsigned char *data, int width, int height) {
    int parity = 0;
    for (int i = 0; i < width * height; ++i) {
        for (int bit = 0; bit < 8; ++bit) {
            if (data[i] & (1 << bit)) parity++;
        }
    }
    printf("Parity check result: %s\n", (parity % 2 == 0) ? "Even" : "Odd");
}

void *socket_reader(void *thread_arg) {
    struct thread_data *my_data = (struct thread_data *) thread_arg;
    int sockfd = my_data->sockfd;
    char buffer[DATA_SIZE];
    int n;

    while (1) {
        bzero(buffer, sizeof(buffer));
        n = read(sockfd, buffer, sizeof(buffer) - 1);
        if (n < 0) {
            perror("ERROR reading from socket");
            exit(1);
        }
        printf("%s\n", buffer);
        write_log(my_data->username, buffer);  // Log the received message
    }

    free(thread_arg);  // Free the allocated memory
    return NULL;
}

int main(int argc, char *argv[]) {
    int sockfd, portno, n;
    struct sockaddr_in serv_addr;
    struct hostent *server;
    char buffer[DATA_SIZE];
    pthread_t pt;

    if (argc < 4) {
       fprintf(stderr,"usage %s hostname port username\n", argv[0]);
       exit(0);
    }

    portno = atoi(argv[2]);
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    server = gethostbyname(argv[1]);
    if (server == NULL) {
        fprintf(stderr,"ERROR, no such host\n");
        exit(0);
    }

    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    bcopy((char *)server->h_addr, (char *)&serv_addr.sin_addr.s_addr, server->h_length);
    serv_addr.sin_port = htons(portno);

    if (connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR connecting");
        exit(1);
    }

    strncpy(buffer, argv[3], DATA_SIZE - 1);
    n = write(sockfd, buffer, strlen(buffer));
    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
    }

    create_log_directory();
    struct thread_data *t_data = malloc(sizeof(struct thread_data));
    if (t_data == NULL) {
        perror("ERROR allocating memory");
        exit(1);
    }
    t_data->sockfd = sockfd;
    strncpy(t_data->username, argv[3], DATA_SIZE - 1);
    pthread_create(&pt, NULL, socket_reader, (void *)t_data);

    while(1) {
        bzero(buffer, DATA_SIZE);
        fgets(buffer, DATA_SIZE - 1, stdin);
        buffer[strcspn(buffer, "\n")] = 0;

        if (buffer[0] == '@') {
            unsigned long crc = crc32((unsigned char *)buffer, strlen(buffer));
            printf("Sending CRC: %lu\n", crc);
            check_2d_parity((unsigned char *)buffer, strlen(buffer), 1);
        }

        if (strcmp(buffer, "/list") == 0) {
            strcpy(buffer, "Server list");
        } else if (strcmp(buffer, "/logout") == 0) {
            strcpy(buffer, "Server logout");
            write(sockfd, buffer, strlen(buffer));
            break;
        }

        n = write(sockfd, buffer, strlen(buffer));
        if (n < 0) {
            perror("ERROR writing to socket");
            exit(1);
        }
    }

    close(sockfd);
    return 0;
}
