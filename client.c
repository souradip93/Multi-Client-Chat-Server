#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <libgen.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/sem.h>
#include <time.h>
#include <signal.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/sendfile.h>

#define BUFFER_SIZE 1024

int clientSocket = 0;
int isParent = 0;

int sendFile(int socket_fd, char *path){
    int numOfBytesWritten = 0;
    int count = 0;
    FILE *file;
    char message[BUFFER_SIZE];
    int ret = 0;
    int c = 0;

    memset(message, 0, sizeof(message));

    file = fopen(path, "r");


    while ((c = fgetc(file)) != EOF)
    {
        message[count++] = (char) c;
        if(count == BUFFER_SIZE){
            numOfBytesWritten = write(socket_fd, message, BUFFER_SIZE);
            if(numOfBytesWritten <= 0){
                ret = -1;
                break;
            }
            else{
                ret = numOfBytesWritten;
            }
            count = 0;
            memset(message, 0, sizeof(message));
        }
    }

    fclose(file);
    
    if(count != 0){
        //printf("Message - %s\n", message);
        numOfBytesWritten = write(socket_fd, message, count);
        if(numOfBytesWritten <= 0)
            ret = -1;
        else
            ret = numOfBytesWritten;
    }
    
    return ret;
}

int writeall(int socket_fd, char *buffer){
    int end = 0;
    int numOfBytesWritten = 0;

    numOfBytesWritten = write(socket_fd, buffer, BUFFER_SIZE);

    return numOfBytesWritten;
}

void exitCall(int s)
{
    if(isParent==0){
        /* Wait for the zombies to die */
        while(waitpid(-1, NULL, WNOHANG) > 0);
        close(clientSocket);
        exit(0);
    }

    char buffer[BUFFER_SIZE];
    strcpy(buffer, "/quit\n");
    writeall(clientSocket, buffer);

    while(waitpid(-1, NULL, WNOHANG) > 0);
    close(clientSocket);
    printf("Exiting Gracefully like a good boy. BYE.....\n");
    exit(0);
}

int receiveFile(char *message, int clientSocket){
    char *saveptr1 = NULL;
    char buffer[BUFFER_SIZE];
    int ret = 0;
    char path[256];
    char output[BUFFER_SIZE];
    off_t *offset = NULL;
    int size = 0;
    struct stat sb;
    int destClientId;
    char *type = NULL;
    char *buffer2 = NULL;
    int n = 0;
    int currentRead = 0;
    int remaining = 0;
    int readBytes = 0;

    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, message);
    type = strtok_r(buffer, " ", &saveptr1);
    /* Moving pointer to the corect location */
    buffer2 = buffer + strlen(type) + 1;

    n = sscanf(buffer2, "%d %d %s", &destClientId, &size, path);
    if(n<3){
        //printf("ERROR\n");
    } else if(strncmp(type, "/sendfile", strlen("/sendfile")) == 0){
        char new_path[256];
        memset(new_path, 0 , sizeof(new_path));
        sprintf(new_path, "client_repo/%d_%s", destClientId, path);

        remaining = size - currentRead;
        FILE *f = fopen(new_path, "w");
        fclose(f);

        f = fopen(new_path, "a+");
        fflush(stdin);
        while(remaining > 0){
            
            memset(buffer, 0, sizeof(buffer));
            //printf("NEW START\n");

            readBytes = (remaining < BUFFER_SIZE)? remaining : BUFFER_SIZE;
            n = read(clientSocket, buffer, readBytes);
            fprintf(f, "%s", buffer);

            currentRead += readBytes;
            memset(buffer, 0, sizeof(buffer));

            remaining = size - currentRead;
            
        }
        fclose(f);

        printf("File received successfully to %s\n", new_path);
        

        ret = 1;
        //fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);
        memset(message, 0, sizeof(message));
    }
    return ret;
}

int sendFileCommand(char message[BUFFER_SIZE], int clientSocket){
    char *saveptr1 = NULL;
    char buffer[BUFFER_SIZE];
    int ret = 0;
    char path[256];
    char output[BUFFER_SIZE];
    off_t *offset = NULL;
    int size = 0;
    struct stat sb;
    int destClientId;
    char *type = NULL;
    char *buffer2 = NULL;
    int n = 0;

    memset(buffer, 0, sizeof(buffer));
    strcpy(buffer, message);

    type = strtok_r(buffer, " ", &saveptr1);
    /* Moving pointer to the corect location */
    buffer2 = buffer + strlen(type) + 1;
    n = sscanf(buffer2, "%d %s", &destClientId, path);
    if(n < 2){
        // /printf("ERROR\n");
    } else if(strncmp(type, "/sendfile", strlen("/sendfile")) == 0){
        ret = 1;
        
        if (stat(path, &sb) == -1) {
            perror("stat");
        }
        else {
            size = sb.st_size;

            memset(output, 0, sizeof(output));
            char *fname = basename(path);
            sprintf(output, "/sendfile %d %d %s", destClientId, size, fname);
            writeall(clientSocket, output);

            ret = sendFile(clientSocket, path);
        }
    }
    return ret;
}

int main(){

    char *lineptr = NULL;
    size_t n = 0;
    char buffer[BUFFER_SIZE];
    char temp[BUFFER_SIZE];

    struct addrinfo *hints = NULL;
    struct addrinfo *results = NULL;
    struct addrinfo *iterator = NULL;
    int errorDetector;

    hints = malloc(sizeof(struct addrinfo));
    hints->ai_family = AF_INET;
    hints->ai_socktype = SOCK_STREAM;
    hints->ai_flags = AI_PASSIVE;

    /* Should return the valid addresses in results data structure */
    errorDetector = getaddrinfo(NULL, "1995", hints, &results);
    /* Free the hints ds. I dont know why i like dynamic memory so much */
    free(hints);

    if(errorDetector != 0){
        perror("Getting address info : ");
        exit(0);
    }

    /* Iterating results of getaddrinfo to get a valid socket address */
    for(iterator = results; iterator!=NULL; iterator = results->ai_next){
        clientSocket = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);

        if(clientSocket == -1){
            perror("Creating socket : ");
            continue;
        }

        errorDetector = connect(clientSocket, iterator->ai_addr, iterator->ai_addrlen);
        if(errorDetector == -1){
            perror("Connecting : ");
            close(clientSocket);
            continue;
        }

        /* Everything is successful. I won. Ohh its just the initial part :( */
        break;  
    }

    /* If there are no good adresses available. I hope and pray this does not occur ever. */
    if(iterator == NULL){
        perror("No available address : ");
        exit(0);
    }

    /* Yes, I dont need you anymore. Bye */
    freeaddrinfo(results);

    signal(SIGINT, exitCall);
    signal(SIGQUIT, exitCall);
    signal(SIGTSTP, exitCall);

    //int flags = fcntl(clientSocket, F_GETFL, 0);
    //fcntl(clientSocket, F_SETFL, flags | O_NONBLOCK);

    isParent = 1;

    int pid = fork();
    memset(buffer, 0, sizeof(buffer));
    if(pid == 0){

        /* P+rocess to handle client requests*/

        isParent = 0;

        while(1){
            //printf("Enter your query : ");
            char buffer[BUFFER_SIZE];
            errorDetector = getline(&lineptr, &n, stdin);
            if(errorDetector == -1){
                free(lineptr);
                lineptr = NULL;
                break;
            }
            //printf("Input <%s>\n", lineptr);
            memset(buffer, 0, sizeof(buffer));
            strcpy(buffer, lineptr);
            free(lineptr);
            lineptr = NULL;

            if(buffer[0] == 10 && buffer[0] == 0){
                buffer[0] = 0;
                continue;
            }

            int ret = sendFileCommand(buffer, clientSocket);
            if(ret >= 1){
                memset(buffer, 0, sizeof(buffer));
                continue;
            }     

            errorDetector = writeall(clientSocket, buffer);
            if(errorDetector == -1 || errorDetector == 0){
                printf("Connection closed.\n");
                close(clientSocket);
                break;
            }
        }

        exitCall(0);
        
    }

    while(1){

        memset(temp, 0, BUFFER_SIZE);
        errorDetector = read(clientSocket, temp, BUFFER_SIZE);
        if(errorDetector <= 0){
            break;
        } else{
            if(strlen(temp)==0)
                continue;
            int ret = receiveFile(temp, clientSocket);
            //printf("Output of receive File %d\n", ret);
            if(ret >= 1){
                memset(temp, 0, sizeof(temp));
                //printf("NEXT\n");
                continue;
            }

            printf("%s\n", temp);
            memset(temp, 0, sizeof(temp));

            continue;
        }
    }
    exitCall(0);

    return 1;

}