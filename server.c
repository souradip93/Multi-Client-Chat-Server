/* LET THE GAME BEGIN */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/sem.h>
#include <sys/stat.h>
#include <time.h>
#include <signal.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

struct sembuf pop = {};
struct sembuf vop = {};

#define P(s) semop(s, &pop, 1)  /* pop is the structure we pass for doing the P(s) operation */
#define V(s) semop(s, &vop, 1)  /* vop is the structure we pass for doing the V(s) operation */

#define ff(i,a,n) for(i=(a);i<=(n);i++)
#define ffi(i,a,n) for(i=(a);i>=(n);i--)

#define NUM_OF_CLIENTS 5
#define MAX_NUMBER_OF_MESSAGES 100
#define PORT "1995"
#define BACKLOG 5
#define MAX_CLIENTS_GROUP 10
#define MAX_GROUPS 5
#define BUFFER_SIZE 1024

struct clientInfo{
    int clientId;
    int clientSocketId;
};
typedef struct clientInfo clientInfo;

struct messageInfo{
    int sourceClientId;
    int destClientId;
    char message[BUFFER_SIZE];
    int type;
};
typedef struct messageInfo messageInfo;

struct groupInfo{
	int clientId[MAX_CLIENTS_GROUP];
	int isAdmin[MAX_CLIENTS_GROUP];
	int isActive[MAX_CLIENTS_GROUP];
	int groupId;
	int type;
};
typedef struct groupInfo groupInfo;

int isParent = 1;
int serverSocket = 0;

int clientArrayShmId = 0;
clientInfo *clientArray = NULL;

int messageArrayShmId = 0;
messageInfo *messageArray = NULL;

int groupArrayShmId = 0;
groupInfo *groupArray = NULL;

int numOfConnectedClientsShmId = 0;
int *numOfConnectedClients = NULL;

int semid_client = 0;
int semid_group = 0;
int semid_message = 0;
int semid_num_clients = 0;

void exitCall(int s){

    shmdt(clientArray);
    shmdt(messageArray);
    shmdt(groupArray);
    shmdt(numOfConnectedClients);

    if(isParent==0){
    	/* Wait for the zombies to die */
    	//while(waitpid(-1, NULL, WNOHANG) > 0);
        exit(0);
    }

    /* Wait for the zombies to die */
    while(waitpid(-1, NULL, WNOHANG) > 0);

    close(serverSocket);

    shmctl(clientArrayShmId, IPC_RMID, 0);
    shmctl(messageArrayShmId, IPC_RMID, 0);
    shmctl(groupArrayShmId, IPC_RMID, 0);
    shmctl(numOfConnectedClientsShmId, IPC_RMID, 0);

    semctl(semid_client, 0, IPC_RMID, 0);
    semctl(semid_message, 0, IPC_RMID, 0);
    semctl(semid_group, 0, IPC_RMID, 0);
    semctl(semid_num_clients, 0, IPC_RMID, 0);

    printf("Exiting Gracefully like a good boy. BYE.....\n");
    exit(0);
}

char * trim(char *buffer){
    /* Remove whitespaces from beginning and end */
    int i = 0;
    int bufferLength = 0;
    char ch = 0;
    int startPointer = 0;

    bufferLength = strlen(buffer);

    /* Note down the first non space character */
    ff(i, 0, bufferLength-1){
        ch = buffer[i];
        if(ch != ' '){
            startPointer = i;
            break;
        }
    }

    /* Kill the trailing whitespaces by putting a null character on the last space character from the end. Difficult life*/
    ffi(i, bufferLength-1, 0){
        ch = buffer[i];
        if(ch != ' '){
            buffer[i+1] = 0;
            break;
        }
    }

    /* Use the noted first non space character to remove initial whitespace characters */

    char *result = calloc(BUFFER_SIZE, sizeof(char));
    strcpy(result, buffer + startPointer);
    //*str = buffer + startPointer;
    return result;
}


int sendMessage(char *message, int destClientSocketId, int sourceClientId){
    /* Create new process for sending the data */
    
    int numOfBytesWritten = -1;
    //int destClientSocketId = findClientSocketId(destClientId);

    char *result = calloc(BUFFER_SIZE, sizeof(char));
    memset(result, 0, sizeof(result));

    if(sourceClientId != -1){
    	sprintf(result, "Client - %d : ", sourceClientId);
		strcat(result, message);
	} else{
		strcpy(result, message);
	}

    numOfBytesWritten = write(destClientSocketId, result, BUFFER_SIZE);

    free(result);
    printf("Finished sending %d\n", destClientSocketId);
    return numOfBytesWritten;
}

void addClient(int clientId, int acceptedSocket){
	
	printf("Adding client to client array\n");
    int i = 0;
    P(semid_client);
    ff(i, 0, NUM_OF_CLIENTS-1){
        if(clientArray[i].clientId == 0){
            clientArray[i].clientId = clientId;
            clientArray[i].clientSocketId = acceptedSocket;
            break;
        }
    }
    V(semid_client);
}

int createGroup(int groupId, int type){
	int i = 0;
	printf("Creating group\n");

    P(semid_group);

    ff(i, 0, MAX_GROUPS-1){
    	if(groupArray[i].groupId == 0){
    		groupArray[i].groupId = groupId;
    		groupArray[i].type = type;
    		break;
    	}
    }

    V(semid_group);
    return i;
}

int addClientToGroup(int groupIndex, int clientId, int adminClientId, int isActive, int addNew){
    int i = 0; int j = 0; int clientExists = 0;
    P(semid_group);

    /* Check if client alreay exists */
    ff(j, 0, MAX_CLIENTS_GROUP-1){
        if(groupArray[groupIndex].clientId[j] == clientId && groupArray[groupIndex].isActive[j] == 1){
            clientExists = 1;
            break;
        }
	}

	/* If addnew is set then add a new client to respective group */
	if(addNew){
		if(!clientExists/* && groupArray[groupIndex].type == 0*/){
			ff(j, 0, MAX_CLIENTS_GROUP-1){
		        if(groupArray[groupIndex].clientId[j] == 0){
		            groupArray[groupIndex].clientId[j] = clientId;
		            groupArray[groupIndex].isActive[j] = isActive;
		            groupArray[groupIndex].isAdmin[j] = 0;
		            if(adminClientId == clientId){
		            	groupArray[groupIndex].isAdmin[j] = 1;
		            }
		            printf("%d\n", groupArray[groupIndex].clientId[j]);
		            break;
		        }
			}
		} else{
			printf("Client already added\n");
		}
	} else {
		if(!clientExists){
			ff(j, 0, MAX_CLIENTS_GROUP-1){
		        if(groupArray[groupIndex].clientId[j] == clientId){
		            groupArray[groupIndex].isActive[j] = isActive;
                    printf("Updated statis of %d to %d\n", clientId, isActive);
		            break;
		        }
			}
		} else{
			printf("Client already added\n");
		}
	}

     V(semid_group);

     return clientExists;
}

int findGroupIndex(int groupId){
	int i = 0; int groupIndex = -1;
	P(semid_group);

	ff(i, 0, MAX_GROUPS-1){
        if(groupArray[i].groupId == groupId){
        	groupIndex = i;
        	break;
        }
	}

	V(semid_group);
	return groupIndex;
}

void addAdminIfNotPresent(int groupIndex, int clientId){
	int i = 0; int j = 0; int adminPresent = 0;
    P(semid_group);

    /* Silently escape if admin is already present */
	ff(j, 0, MAX_CLIENTS_GROUP-1){
        if(groupArray[groupIndex].clientId[j] == clientId && groupArray[groupIndex].isActive[j] == 1){
        	adminPresent = 1;
        	break;
        }
	}
	if(!adminPresent){
		ff(j, 0, MAX_CLIENTS_GROUP-1){
	        if(groupArray[groupIndex].clientId[j] == 0){
	            groupArray[groupIndex].clientId[j] = clientId;
	            groupArray[groupIndex].isAdmin[j] = 1;
	            groupArray[groupIndex].isActive[j] = 1;
	            break;
	        }
    	}
	}


    V(semid_group);
}

int showAvailableGroups(int clientId, int clientSocketId){
	int i = 0; int j = 0; int c = 0; int clientPresent = 0; int print = 1; int temp = 0;
	char result[BUFFER_SIZE]; char dummy[BUFFER_SIZE];
    memset(result, 0, sizeof(result));

    P(semid_group);

    ff(i, 0, MAX_GROUPS-1){
    	c = 0;
    	if(groupArray[i].groupId != 0){
    		clientPresent = 0;
    		ff(j, 0, MAX_CLIENTS_GROUP-1){
    			if(groupArray[i].clientId[j] == clientId && groupArray[i].isActive[j] == 1){
    				clientPresent = 1;
    				temp = 1;
    				break;
				}
    		}
    		if(clientPresent){
    			if(print){
    				sprintf(result, "LIST OF ONLINE GROUPS \n");
    			}
    			print = 0;
    			sprintf(dummy, "Group Id : %d\n------------------\n", groupArray[i].groupId);
    			strcat(result, dummy);
    			//printf("Group Id : %d\n", groupArray[i].groupId);
    			//printf("------------------\n------------------\n");
    			ff(j, 0, MAX_CLIENTS_GROUP-1){
	    			if(groupArray[i].clientId[j] != 0 && groupArray[i].isActive[j] == 1){
	    				c ++;
	    				if(groupArray[i].isAdmin[j])
	    					sprintf(dummy, "Client %d : %d * Admin\n", c, groupArray[i].clientId[j]);
	    				else
	    					sprintf(dummy, "Client %d : %d\n", c, groupArray[i].clientId[j]);
	    				//printf("Client %d : %d <%d %d>\n", c, groupArray[i].clientId[j],i,j);
	    				strcat(result, dummy);
	    				//break;
					}
	    		}
	    		sprintf(dummy, "\n");
	    		strcat(result, dummy);
	    		//printf("\n\n");
    		}
    	}
    }
    V(semid_group);

    if(temp == 0)
        sprintf(result, "No groups available.\n");
    strcat(result, "\n");

    return sendMessage(result, clientSocketId, -1);
}

int showAvailableAllGroups(int clientSocketId){
	int i = 0; int j = 0; int c = 0; int clientPresent = 0; int print = 1; int temp = 0;
	char result[BUFFER_SIZE]; char dummy[BUFFER_SIZE];
    memset(result, 0, sizeof(result));
    memset(dummy, 0, sizeof(dummy));

    P(semid_group);

    ff(i, 0, MAX_GROUPS-1){
    	c = 0;
    	if(groupArray[i].groupId != 0){
    		temp = 1;
			if(print){
				sprintf(result, "LIST OF ONLINE GROUPS \n");
			}
			print = 0;
			sprintf(dummy, "Group Id : %d\n------------------\n", groupArray[i].groupId);
    		strcat(result, dummy);
			ff(j, 0, MAX_CLIENTS_GROUP-1){
    			if(groupArray[i].clientId[j] != 0 && groupArray[i].isActive[j] == 1){
    				c ++;
    				if(groupArray[i].isAdmin[j])
	    				sprintf(dummy, "Client %d : %d * Admin\n", c, groupArray[i].clientId[j]);
	    			else
    					sprintf(dummy, "Client %d : %d\n", c, groupArray[i].clientId[j]);
	    			strcat(result, dummy);
    				//break;
				}
    		}
    		sprintf(dummy, "\n");
	    	strcat(result, dummy);
    	}
    }
    V(semid_group);

    if(temp == 0)
        sprintf(result, "No groups available.\n");
    strcat(result, "\n");

    return sendMessage(result, clientSocketId, -1);
}

int showAvailableClients(int clientSocketId, int clientId){
    int i = 0;
    int temp = 0;

    char result[BUFFER_SIZE]; char dummy[BUFFER_SIZE];
    memset(result, 0, sizeof(result));


    sprintf(result, "LIST OF ONLINE CLIENTS \n");

    P(semid_client);
    ff(i, 0, NUM_OF_CLIENTS-1){
        if(clientArray[i].clientId != 0){
            temp = 1;
            if(clientArray[i].clientId != clientId)
            	sprintf(dummy, "Client : %d\n", clientArray[i].clientId);
            else
            	sprintf(dummy, "Client : %d * That is you\n", clientArray[i].clientId); 
            strcat(result, dummy);
        }
    }
    V(semid_client);
    if(temp == 0)
        sprintf(result, "No clients connected.\n");
    strcat(result, "\n");

    return sendMessage(result, clientSocketId, -1);
}

int isValidClient(int clientId){
	int i = 0; int out = 0;

	P(semid_client);
    ff(i, 0, NUM_OF_CLIENTS-1){
        if(clientArray[i].clientId == clientId){
            out = 1;
            break;
        }
    }
    V(semid_client);

    return out;
}

int isValidGroup(int groupId){
	int i = 0; int out = 0;

	P(semid_group);
    /* Remove clients that are not admin */
    ff(i, 0, MAX_GROUPS-1){
    	if(groupArray[i].groupId == groupId){
			out = 1;
			break;
    	}
    }
    V(semid_group);

    return out;
}

int pollMessageArray(int clientId, int clientSocketId){
    int i = 0; int end = 0; int errorDetector = 1;

	P(semid_message);
    ff(i,0,MAX_NUMBER_OF_MESSAGES-1){
        if(messageArray[i].destClientId == clientId){
            printf("Message detected in message array %d\n", clientId);

            errorDetector = sendMessage(messageArray[i].message, clientSocketId, messageArray[i].sourceClientId);
            messageArray[i].destClientId = 0;
            messageArray[i].sourceClientId = 0;
            //break;
        }
    }
    V(semid_message);

    return errorDetector;
}

void insertMessageTable(char *message, int destClientId, int sourceClientId){
    int i = 0;

    P(semid_message);
    ff(i, 0, MAX_NUMBER_OF_MESSAGES-1){
        if(messageArray[i].sourceClientId == 0){
            messageArray[i].sourceClientId = sourceClientId;
            messageArray[i].destClientId = destClientId;
            memset(messageArray[i].message, 0, sizeof(messageArray[i].message));
            strcpy(messageArray[i].message, message);
            messageArray[i].type = 0;
            break;
        }
    }
    V(semid_message);
}

void sendMessageToGroup(char *message, int destGroupIndex, int sourceClientId){
	int i = 0; int j = 0; int c = 0; int clientPresent = 0; int print = 1;

    P(semid_group);
	/* Send message with destination client ids as that of group members */    
	ff(j, 0, MAX_CLIENTS_GROUP-1){
		if(groupArray[destGroupIndex].clientId[j] != 0 && groupArray[destGroupIndex].isActive[j]==1 && groupArray[destGroupIndex].clientId[j] != sourceClientId){
			insertMessageTable(message, groupArray[destGroupIndex].clientId[j], sourceClientId);
		}
	}

    V(semid_group);
}

void sendRequestToAdmin(int destGroupIndex, int clientId){
    int j = 0;

    /* Add this poor lad to the group */
    P(semid_group);
    /* Send message with destination client ids as that of group members */    
    ff(j, 0, MAX_CLIENTS_GROUP-1){
        if(groupArray[destGroupIndex].isAdmin[j] == 1){
            char message[BUFFER_SIZE];
            sprintf(message, "Client with Id - %d has requested to join group with Id - %d. To approve enter /approve %d %d", clientId, groupArray[destGroupIndex].groupId, groupArray[destGroupIndex].groupId, clientId);
            insertMessageTable(message, groupArray[destGroupIndex].clientId[j], clientId);
            break;
        }
    }

    V(semid_group);
} 

/* Returns 
0 = clientDoesNotExists
1 = clientExists, grouptype is 0 and clientis is active
2 = clientExists, grouptype is 0 and clientis is client sent request of approval to admin
3 = clientExists, grouptype is 1 and clientis isnt active
4 = clientExists, grouptype is 1 and clientis active
5 = clientExists, grouptype is 1 and client sent request of approval to admin
*/
int checkClientInGroup(int groupIndex, int clientId){
	int j = 0; int res = 0;

    P(semid_group);

	ff(j, 0, MAX_CLIENTS_GROUP-1){
		if(groupArray[groupIndex].clientId[j] == clientId){
			/*if(activeStatusRequired && groupArray[groupIndex].isActive[j] == 1){
				res = groupArray[groupIndex].type == 0 ? 1 : 2;
			} else if(!activeStatusRequired){
				res = groupArray[groupIndex].type == 0 ? 1 : 2;
                if(groupArray[groupIndex].type == 1 && groupArray[groupIndex].isActive[j] == 1)
                    res = 3;
			}*/
            if(groupArray[groupIndex].type == 0){
                if(groupArray[groupIndex].isActive[j] == 1)
                    res = 1;
                else if (groupArray[groupIndex].isActive[j] == -1)
                    res = 2;
            }
            else{
                if(groupArray[groupIndex].isActive[j] == 0)
                    res = 3;
                else if(groupArray[groupIndex].isActive[j] == 1)
                    res = 4;
                else
                    res = 5;
            }
            printf("%d %d\n", groupArray[groupIndex].clientId[j], groupArray[groupIndex].isActive[j]);
            //printf("%d %d %d\n", res, groupArray[groupIndex].type, activeStatusRequired);
			break;
		}
	}

    V(semid_group);
    return res;
}

int isAdmin(int groupIndex, int clientId){
    int j = 0; int res = 0;

    P(semid_group);

    ff(j, 0, MAX_CLIENTS_GROUP-1){
        if(groupArray[groupIndex].clientId[j] == clientId && groupArray[groupIndex].isAdmin[j]==1){
            res = 1;
            break;
        }
    }

    V(semid_group);
    return res;
}

void broadcast(char *message, int clientId, int clientSocketId){
	int i = 0;

	P(semid_client);
    ff(i, 0, NUM_OF_CLIENTS-1){
        if(clientArray[i].clientId != 0){
            insertMessageTable(message, clientArray[i].clientId, clientId);
        }
    }
    V(semid_client);
}

int generateRandomInt(){
	srand(time(0));
    return 10000 + (rand() % 90000);
}

void removeFromClientArray(int clientId){
	int i = 0;

	P(semid_client);
    ff(i, 0, NUM_OF_CLIENTS-1){
        if(clientArray[i].clientId == clientId){
            clientArray[i].clientId = 0;
            break;
        }
    }
    V(semid_client);
}

void removeFromMessageArray(int clientId){
	int i = 0;

	P(semid_message);
    ff(i, 0, MAX_NUMBER_OF_MESSAGES-1){
        if(messageArray[i].destClientId == clientId){
            messageArray[i].sourceClientId = 0;
            messageArray[i].destClientId = 0;
            memset(messageArray[i].message, 0, sizeof(messageArray[i].message));
            messageArray[i].type = 0;
            break;
        }
    }
    V(semid_message);
}

void removeFromGroupArray(int clientId, int clientSocketId){
	int i = 0; int j = 0; int deleteGroup = 0;

    P(semid_group);

    /* Remove clients that are not admin */
    ff(i, 0, MAX_GROUPS-1){
    	if(groupArray[i].groupId != 0){
			ff(j, 0, MAX_CLIENTS_GROUP-1){
				//printf("<%d><%d><%d>\n", groupArray[i].clientId[j] , clientId, groupArray[i].isAdmin[j]);
    			if(groupArray[i].clientId[j] == clientId && groupArray[i].isAdmin[j] == 0){
    				groupArray[i].clientId[j] = 0;
    				groupArray[i].isActive[j] = 0;
    				break;
				}
    		}
    	}
    }

    /* Remove groups in which client is admin */
    ff(i, 0, MAX_GROUPS-1){
    	if(groupArray[i].groupId != 0){
    		deleteGroup = 0;
			ff(j, 0, MAX_CLIENTS_GROUP-1){
    			if(groupArray[i].clientId[j] == clientId && groupArray[i].isAdmin[j] == 1){
    				deleteGroup = 1;
    				//printf("DELETING GROUP\n");
    				break;
				}
    		}

    		if(deleteGroup){
    			ff(j, 0, MAX_CLIENTS_GROUP-1){
					groupArray[i].clientId[j] = 0;
					groupArray[i].isAdmin[j] = 0;
					groupArray[i].isActive[j] = 0;
	    		}
	    		groupArray[i].groupId = 0;
    		}
    		//printf("|%d %d|\n", groupArray[i].clientId[0], groupArray[i].clientId[1]);
    	}
    }

    //printf("<%d %d><%d %d>\n", groupArray[0].clientId[0], groupArray[0].isActive[0], groupArray[0].clientId[1], groupArray[0].isActive[1]);
    V(semid_group);
}

void writeFile(int acceptedSocket, int sourceClientId, int size, char path[256]){
    char message[BUFFER_SIZE];
    int remaining = 0;
    int currentRead = 0;
    int readBytes = 0;
    int n = 0;

    char new_path[256];
    memset(new_path, 0, sizeof(new_path));
    sprintf(new_path, "server_repo/%d_%s", sourceClientId, path);

    FILE *file = fopen(new_path, "w");
    fclose(file);

    file = fopen(new_path, "a+");
    fflush(stdin);
    remaining = size - currentRead;
    while(remaining > 0){
        memset(message, 0, sizeof(message));
        //printf("NEW START\n");

        readBytes = (remaining < BUFFER_SIZE)? remaining : BUFFER_SIZE;
        n = read(acceptedSocket, message, readBytes);

        currentRead += readBytes;
        fprintf(file, "%s", message);
        memset(message, 0, sizeof(message));

        remaining = size - currentRead;
        printf("%d %d\n", size, remaining);
    }
    fclose(file);
}

void sendFile(int acceptedSocket, int sourceClientId, int destClientId, int size, char path[256]){

    char message[BUFFER_SIZE];
    int remaining = 0;
    int currentRead = 0;
    int readBytes = 0;
    int n = 0;
    int count = 0;
    int numOfBytesWritten = 0;
    char c = 0;

    char new_path[256];
    memset(new_path, 0, sizeof(new_path));
    sprintf(new_path, "server_repo/%d_%s", sourceClientId, path);

    printf("%s\n", new_path);
    struct stat sb;
    if (stat(new_path, &sb) == -1) {
        perror("stat");
    }

    memset(message, 0, sizeof(message));
    sprintf(message, "/sendfile %d %d %d_%s", destClientId, (int)sb.st_size, sourceClientId, path);
    insertMessageTable(message, destClientId, -1);

    
    //printf("%d %d\n", size, sb.st_size);

    FILE *file = fopen(new_path, "r");
    memset(message, 0, sizeof(message));
    while ((c = fgetc(file)) != EOF)
    {
        message[count++] = (char) c;
        if(count == BUFFER_SIZE){
            //printf("%s", message);
            insertMessageTable(message, destClientId, -1);
            memset(message, 0, sizeof(message));
            count = 0;
        }
    }

    fclose(file);

    if(count != 0){
        insertMessageTable(message, destClientId, -1);
        //printf("%s\n", message);
    }
    memset(message, 0, sizeof(message));
}

void sendFileToGroup(int acceptedSocket, int clientId, int destGroupId, int size, char path[256]){
    int groupIndex = findGroupIndex(destGroupId);
    int i = 0;
    P(semid_group);

    /* Remove clients that are not admin */
    ff(i, 0, MAX_CLIENTS_GROUP-1){
        if(groupArray[groupIndex].isActive[i] == 1 && groupArray[groupIndex].clientId[i] != clientId){
            sendFile(acceptedSocket, clientId, groupArray[groupIndex].clientId[i], size, path);

        }
    }

    V(semid_group);
}

void quitCall(int clientId, int acceptedSocket){
	char *message = calloc(BUFFER_SIZE, sizeof(char));
	memset(message, 0, sizeof(message));
	sprintf(message, "Offline.\n");
	broadcast(message, clientId, acceptedSocket);
	free(message);

	removeFromClientArray(clientId);
	removeFromMessageArray(clientId);
	removeFromGroupArray(clientId, acceptedSocket);

	//printf("|%d %d|\n", groupArray[0].clientId[0], groupArray[0].clientId[1]);
	printf("EXITING\n");
}

int main(){
	struct addrinfo *hints = NULL;
    struct addrinfo *results = NULL;
    struct addrinfo *iterator = NULL;
    int errorDetector = 0;
    int acceptedSocket = 0;
    int firstCall = 0;
    struct sockaddr_storage *client_addr = NULL;
    socklen_t addrsize;
    int pid = 0;
    int i = 0;
    int j = 0;
    int clientId = 0;
    struct timespec ts1, ts2, serverStartTime;
    double cal_time_taken_global;
    double cal_time_taken_local;


    /* SEMAPHORE MAGIC STARTS */

    semid_message = semget(IPC_PRIVATE, 1, 0777|IPC_CREAT);
    semid_client = semget(IPC_PRIVATE, 1, 0777|IPC_CREAT);
    semid_group = semget(IPC_PRIVATE, 1, 0777|IPC_CREAT);
    semid_num_clients = semget(IPC_PRIVATE, 1, 0777|IPC_CREAT);

    semctl(semid_client, 0, SETVAL, 1);
    semctl(semid_message, 0, SETVAL, 1);
    semctl(semid_group, 0, SETVAL, 1);
    semctl(semid_num_clients, 0, SETVAL, 1);

    pop.sem_num = vop.sem_num = 0;
    pop.sem_flg = vop.sem_flg = 0;
    pop.sem_op = -1 ; vop.sem_op = 1 ;

    /* SEMAPHORE MAGIC ENDS */

    hints = malloc(sizeof(struct addrinfo));
    hints->ai_family = AF_INET;
    hints->ai_socktype = SOCK_STREAM;
    hints->ai_flags = AI_PASSIVE;

    /* Should return the valid addresses in results data structure */
    errorDetector = getaddrinfo(NULL, PORT, hints, &results);
    /* Free the hints ds. I dont know why i like dynamic memory so much */
    free(hints);

    if(errorDetector != 0){
        perror("Getting address info : ");
        exit(0);
    }

    /* Iterating results of getaddrinfo to get a valid socket address */
    for(iterator = results; iterator!=NULL; iterator = results->ai_next){
        serverSocket = socket(iterator->ai_family, iterator->ai_socktype, iterator->ai_protocol);

        if(serverSocket == -1){
            perror("Creating socket : ");
            continue;
        }

        /* This removes the address binding issue forever :) */
        int key = 1;
        setsockopt(serverSocket, SOL_SOCKET, SO_REUSEADDR, &key, sizeof(int));

        errorDetector = bind(serverSocket, iterator->ai_addr, iterator->ai_addrlen);
        if(errorDetector == -1){
            perror("Binding : ");
            close(serverSocket);
            continue;
        }

        /* Everything is successful. I won. Ohh its just the initial part :( */
        break;
    }

    /* If there are no good adresses available. I hope and pray this does not occur ever. */
    if(iterator == NULL){
        perror("No available address.\n");
        exit(0);
    }

    /* Yes, I dont need you anymore. Bye */
    freeaddrinfo(results);

    /* Listen */
    errorDetector = listen(serverSocket, BACKLOG);
    if(errorDetector == -1){
        perror("Listening : ");
        exit(0);
    }

    firstCall = 1;
    isParent = 1;

    signal(SIGINT, exitCall);
    signal(SIGQUIT, exitCall);
    signal(SIGTSTP, exitCall);

    addrsize = sizeof(client_addr);

    clientArrayShmId = shmget(IPC_PRIVATE, NUM_OF_CLIENTS*sizeof(clientInfo), 0777|IPC_CREAT);
    clientArray = (clientInfo *) shmat(clientArrayShmId, 0, 0);

    messageArrayShmId = shmget(IPC_PRIVATE, MAX_NUMBER_OF_MESSAGES*sizeof(messageInfo), 0777|IPC_CREAT);
    messageArray = (messageInfo *) shmat(messageArrayShmId, 0, 0);

    groupArrayShmId = shmget(IPC_PRIVATE, MAX_GROUPS*sizeof(groupInfo), 0777|IPC_CREAT);
    groupArray = (groupInfo *) shmat(groupArrayShmId, 0, 0);

    numOfConnectedClientsShmId = shmget(IPC_PRIVATE, sizeof(int), 0777|IPC_CREAT);
    numOfConnectedClients = (int *) shmat(numOfConnectedClientsShmId, 0, 0);

    ff(i,0, NUM_OF_CLIENTS-1){
        clientArray[i].clientId = 0;
        clientArray[i].clientSocketId = 0;
    }

    ff(i,0, MAX_NUMBER_OF_MESSAGES-1){
        messageArray[i].sourceClientId = 0;
        messageArray[i].destClientId = 0;
        memset(messageArray[i].message, 0, BUFFER_SIZE);
        messageArray[i].type = 0;
    }

    ff(i,0, MAX_GROUPS-1){
	    ff(j,0, MAX_CLIENTS_GROUP-1){
	        groupArray[i].clientId[j] = 0;
	        groupArray[i].isAdmin[j] = 0;
            groupArray[i].isActive[j] = 0;
	    }
	    groupArray[i].groupId = 0;
	    groupArray[i].type = 0;
	}

	*numOfConnectedClients = 0;

    shmdt(clientArray);
    shmdt(messageArray);
    shmdt(groupArray);
    shmdt(numOfConnectedClients);

    while(1){

        acceptedSocket = accept(serverSocket, (struct sockaddr*)client_addr, &addrsize);
        if(acceptedSocket == -1){
            perror("Accept : ");
            continue;
        }
        int flags = fcntl(acceptedSocket, F_GETFL, 0);
        fcntl(acceptedSocket, F_SETFL, flags | O_NONBLOCK);
        /* Giving birth on every accepted connection */
        printf("ACCEPTED CONNECTION\n");
        
        clientId = generateRandomInt();

        pid = fork();
        if(pid==0){

            /* Child process */
            isParent = 0;
			close(serverSocket);


            //char *buffer = NULL;
            char buffer[BUFFER_SIZE];
            char *buffer2 = NULL;
            char *newbuffer = NULL;
            //char *temp = NULL;
            char temp[BUFFER_SIZE];
            int end = 0;
            char *saveptr1 = NULL;
            int errorDetector1 = 0;
            char *type = NULL;
            char message[BUFFER_SIZE];
            char welcomeMessage[BUFFER_SIZE];
            //int errorDetector1 = 0;

			/* Attach the shared memories */
            clientArray = (clientInfo *) shmat(clientArrayShmId, 0, 0);
            messageArray = (messageInfo *) shmat(messageArrayShmId, 0, 0);
            groupArray = (groupInfo *) shmat(groupArrayShmId, 0, 0);
            numOfConnectedClients = (int *) shmat(numOfConnectedClientsShmId, 0, 0);

            P(semid_num_clients);
            *numOfConnectedClients  = *numOfConnectedClients + 1;
            V(semid_num_clients);

            /* Greet the client gracefully */
            if(*numOfConnectedClients > NUM_OF_CLIENTS){
            	sprintf(welcomeMessage, "Connection Limit Exceeded !!\n");
            	sendMessage(welcomeMessage, acceptedSocket, -1);
            	close(acceptedSocket);
            	*numOfConnectedClients  = *numOfConnectedClients - 1;
            	exitCall(0);
            } else{
            	sprintf(welcomeMessage, "WELCOME ( Client Id : %d ) \n", clientId);
            	sendMessage(welcomeMessage, acceptedSocket, -1);
            }

            /* Add the client to the client array */
            addClient(clientId, acceptedSocket);

            //buffer = calloc(256, sizeof(char));
            //temp = calloc(256, sizeof(char));
            memset(buffer, 0, sizeof(buffer));

            while(1){

                memset(temp, 0, sizeof(temp));
                errorDetector = read(acceptedSocket, temp, sizeof(temp));

                if(errorDetector == -1){

                	/* No one loves me. No one sends message */
                    if(errno == EAGAIN || errno == EWOULDBLOCK){
                    	/* Let me stalk message array */
						errorDetector1 = pollMessageArray(clientId, acceptedSocket);
						/* Dont tolerate any errors */
						if(errorDetector1 <= 0){  
							printf("Error in Sending\n");
							quitCall(clientId, acceptedSocket);

							P(semid_num_clients);
                            *numOfConnectedClients = *numOfConnectedClients - 1;
                            V(semid_num_clients);
							break;
						}
                    }
                }
                /* Dont tolerate any errors */
                else if(errorDetector == 0){
                    printf("Error in receiving\n");
                    quitCall(clientId, acceptedSocket);
                    P(semid_num_clients);
                    *numOfConnectedClients = *numOfConnectedClients - 1;
                    V(semid_num_clients);
                    break;
                }
                /* Business starts from here */
                else{
                	if(temp[errorDetector-1] == 0){
                        //temp[errorDetector-1] = 0;
                        if(errorDetector-2>=0){
                        	//temp[errorDetector-2] = 0;
                        }
                        else
                        	buffer[strlen(buffer)-1] = 0;

                        /* The above 4 lines may need to be changed based on the client */
 
                        strcat(buffer,temp);

                        if(*buffer == 0)
                            continue;

                        newbuffer = trim(buffer);
                        printf("TYPE - %s\n", newbuffer);
                        type = strtok_r(newbuffer, " ", &saveptr1);
                        
                        /* Activegroups shows the groups in whihc the client is a part */
                        if(strncmp(type, "/activegroups", strlen("/activegroups")) == 0){

                        	errorDetector1 = showAvailableGroups(clientId, acceptedSocket);
                            if(errorDetector1 <= 0){
                            	printf("ERROR IN SENDING\n");
                            	quitCall(clientId, acceptedSocket);

                            	P(semid_num_clients);
                            	*numOfConnectedClients = *numOfConnectedClients - 1;
                            	V(semid_num_clients);

                            	free(newbuffer);
                            	break;
                            }

                        } else if(strncmp(type, "/activeallgroups", strlen("/activeallgroups")) == 0){

                        	errorDetector1 = showAvailableAllGroups(acceptedSocket);
                            if(errorDetector1 <= 0){
                            	printf("ERROR IN SENDING\n");
                            	quitCall(clientId, acceptedSocket);

                            	P(semid_num_clients);
                            	*numOfConnectedClients = *numOfConnectedClients - 1;
                            	V(semid_num_clients);

                            	free(newbuffer);
                            	break;
                            }

                        } else if(strncmp(type, "/active", strlen("/active")) == 0){
                            errorDetector1 = showAvailableClients(acceptedSocket, clientId);
                            if(errorDetector1 <= 0){
                            	printf("ERROR IN SENDING\n");
                            	quitCall(clientId, acceptedSocket);

                            	P(semid_num_clients);
                            	*numOfConnectedClients = *numOfConnectedClients - 1;
                            	V(semid_num_clients);

                            	free(newbuffer);
                            	break;
                            }

                        } else if(strncmp(type, "/sendgroup", strlen("/sendgroup")) == 0){
                        	buffer2 = newbuffer + strlen(type) + 1;
                            int destGroupId; //char message[256];
                            //strcpy(output, message);

                            int n = sscanf(buffer2, "%d %[^\n]s", &destGroupId, message);
                            int groupIndex = findGroupIndex(destGroupId);

                            if(n < 2){
                            	strcpy(message, "Server : Wrong/Invalid input\n");
                            	insertMessageTable(message, clientId, -1);
                        	}else if(groupIndex == -1){
                            	strcpy(message, "Server : Invalid group id\n");
                            	insertMessageTable(message, clientId, -1);
                            } else if(strlen(message) == 0){
                            	strcpy(message, "Server : Empty input\n");
                            	insertMessageTable(message, clientId, -1);
                            } else{
                            	int clientExists = checkClientInGroup(groupIndex, clientId);
	                            if(clientExists == 4 || clientExists == 1)
									sendMessageToGroup(message, groupIndex, clientId);
								else{
									sprintf(message, "Server : Client does not belong to group %d\n", destGroupId);
                            		insertMessageTable(message, clientId, -1);
								}
							}
                            
                        } else if(strncmp(type, "/sendfile", strlen("/sendfile")) == 0){
                            buffer2 = newbuffer + strlen(type) + 1;
                            int destClientId; //char message[256];]
                            char path[256];
                            int size = 0;
                            int currentRead = 0;
                            int remaining = 0;
                            int readBytes = 0;
                            memset(message, 0, sizeof(message));

                            int n = sscanf(buffer2, "%d %d %s", &destClientId, &size, path);

                            if(n < 3){
                                strcpy(message, "Server : Wrong/Invalid input\n");
                                insertMessageTable(message, clientId, -1);
                            } else{

                                int validClient = isValidClient(destClientId);
                                int validGroup = isValidGroup(destClientId);

                                /* Block the socket */
                                fcntl(acceptedSocket, F_SETFL, flags & ~O_NONBLOCK);

                                if(validClient){
                                    /* Cliend id is valid */
                                    writeFile(acceptedSocket, clientId, size, path);
                                    /* Unblock the socket */
                                    fcntl(acceptedSocket, F_SETFL, flags | O_NONBLOCK);

                                    sendFile(acceptedSocket, clientId, destClientId, size, path);
                                } else if(validGroup){
                                    /* Group id is valid */
                                    int groupIndex = findGroupIndex(destClientId);
                                    int clientExists = checkClientInGroup(groupIndex, clientId);
                                    if(clientExists == 1 || clientExists == 4){

                                        writeFile(acceptedSocket, clientId, size, path);
                                        fcntl(acceptedSocket, F_SETFL, flags | O_NONBLOCK);


                                        sendFileToGroup(acceptedSocket, clientId, destClientId, size, path);
                                    }
                                    else{
                                        memset(message, 0, sizeof(message));
                                        strcpy(message, "Server : Client doesnt belong to this group\n");
                                        insertMessageTable(message, clientId, -1);
                                    }
                                } else{
                                    memset(message, 0, sizeof(message));
                                    strcpy(message, "Server : Wrong/Invalid input\n");
                                    insertMessageTable(message, clientId, -1);
                                }
                                printf("UNBLOCK\n");
                                fcntl(acceptedSocket, F_SETFL, flags | O_NONBLOCK);
                            }

                        } else if(strncmp(type, "/send", strlen("/send")) == 0){
                            /* Moving pointer to the corect location */
                            buffer2 = newbuffer + strlen(type) + 1;
                        	int destClientId; //char message[256];
                            int n = sscanf(buffer2, "%d %[^\n]s", &destClientId, message);

                            if(n < 2){
                            	strcpy(message, "Server : Wrong/Invalid input\n");
                            	insertMessageTable(message, clientId, -1);
                        	}else if(!isValidClient(destClientId)){
                            	strcpy(message, "Server : Invalid client id\n");
                            	insertMessageTable(message, clientId, -1);
                            } else if(strlen(message) == 0){
                            	strcpy(message, "Server : Empty input\n");
                            	insertMessageTable(message, clientId, -1);
                            } else{
                            	insertMessageTable(message, destClientId, clientId);
                        	}

                        } else if(strncmp(type, "/broadcast", strlen("/broadcast")) == 0){
                            buffer2 = newbuffer + strlen(type) + 1;

                            int n = sscanf(buffer2, " %[^\n]s", message);
                            //printf("<<<%s>>>\n", message);
                            if(n < 1){
                            	strcpy(message, "Server : Wrong/Invalid input\n");
                            	insertMessageTable(message, clientId, -1);
                        	} else if(strlen(message) == 0){
                            	strcpy(message, "Server : Empty input\n");
                            	insertMessageTable(message, clientId, -1);
                            } else {
                            	broadcast(message, clientId, acceptedSocket);
                        	}

                        } else if(strncmp(type, "/makegroupreq", strlen("/makegroupreq")) == 0){

        					int groupId = generateRandomInt();
        					int clientExists = 0;
        					char *group_client = NULL;
                        	char *str2 = NULL;
                        	int c = 0;
                        	int n = 0;
                        	int group_client_int = 0;
                        	int len = 0;
        					int groupIndex = createGroup(groupId, 1);

        					addAdminIfNotPresent(groupIndex, clientId);
        					sprintf(message, "Please join our group with group id %d\n", groupId);

                        	for (str2 = NULL; ; str2 = NULL) {
						        group_client = strtok_r(str2, " ", &saveptr1);
						        /* Why to bother when newbuffer doesnt exist? */
						        if(group_client == NULL)
						            break;

						        /* Why to bother when \0 is alone?? */
						        if(*group_client == 0)
						            continue;

						        n = sscanf(group_client, "%d %n", &group_client_int, &len);
						        if((n && len == strlen(group_client)) == 0){
					                //sprintf(message, "Server : Invalid client id %s\n", group_client);
                            		//insertMessageTable(message, clientId, -1);
					                continue;
					            }


						        clientExists = isValidClient(group_client_int);
						        if(!clientExists){
						        	//sprintf(message, "Server : Invalid client id %s\n", group_client);
                            		//insertMessageTable(message, clientId, -1);
                            		continue;
						        }

						        if(group_client_int == clientId)
						        	continue;

						        clientExists = addClientToGroup(groupIndex, group_client_int, clientId, 0, 1);

						        if(!clientExists)
						        	c++;

						        insertMessageTable(message, group_client_int, clientId);
						        if(c == MAX_CLIENTS_GROUP-1){
						        	printf("Number of clients : %d\n", c);
						        	break;
						        }
						    }

						    sprintf(message, "Server : Succesfully created group with Id %d\n", groupId);
                            insertMessageTable(message, clientId, -1);

						    //free(message);

                        } else if(strncmp(type, "/makegroup", strlen("/makegroup")) == 0){

        					int groupId = generateRandomInt();
        					int groupIndex = 0;
                        	char *str2 = NULL;
                        	char *group_client = NULL;
                        	int clientExists = 0;
                        	int c = 0;
                        	int n = 0;
                        	int group_client_int = 0;
                        	int len = 0;

                        	groupIndex = createGroup(groupId, 0);

                        	if(groupIndex == -1){
                            	strcpy(message, "Server : Invalid group id\n");
                            	insertMessageTable(message, clientId, -1);
                            	continue;
                            }

                            for (str2 = NULL; ; str2 = NULL) {
						        group_client = strtok_r(str2, " ", &saveptr1);
						        /* Why to bother when newbuffer doesnt exist? */
						        if(group_client == NULL)
						            break;

						        /* Why to bother when \0 is alone?? */
						        if(*group_client == 0)
						            continue;

						        n = sscanf(group_client, "%d %n", &group_client_int, &len);
						        if((n && len == strlen(group_client)) == 0){
					                //sprintf(message, "Server : Invalid client id %s\n", group_client);
                            		//insertMessageTable(message, clientId, -1);
					                continue;
					            }

						        if(!isValidClient(group_client_int)){
						        	//sprintf(message, "Server : Invalid client id %s\n", group_client);
                            		//insertMessageTable(message, clientId, -1);
                            		continue;
						        }

						        /* Admin needs to be the showstopper. Dont add him now.*/
						        if(group_client_int == clientId)
						        	continue;

						        clientExists = addClientToGroup(groupIndex, group_client_int, clientId, 1, 1);

						        if(!clientExists)
						        	c++;

						        if(c == MAX_CLIENTS_GROUP-1){
						        	printf("Number of clients : %d\n", c);
						        	break;
						        }
						    }
						    addAdminIfNotPresent(groupIndex, clientId);

						    sprintf(message, "Server : Succesfully created group with Id %d\n", groupId);
                            insertMessageTable(message, clientId, -1);

                            sprintf(message, "Server : You are added to group with Id %d\n", groupId);
                            sendMessageToGroup(message, groupIndex, clientId);

                            //free(message);

                        } else if(strncmp(type, "/joingroup", strlen("/joingroup")) == 0){

                        	buffer2 = newbuffer + strlen(type) + 1;
                        	//char message[256]; 
                        	int destGroupId = 0;
                        	int clientExists = 0;
                            int groupIndex = 0;

                        	int n = sscanf(buffer2, "%d", &destGroupId);

                        	if(n < 1){
                            	strcpy(message, "Server : Wrong/Invalid input\n");
                            	insertMessageTable(message, clientId, -1);
                        	} else {

                                if(!isValidGroup(destGroupId)){
                                	strcpy(message, "Server : Invalid group id\n");
                                	insertMessageTable(message, clientId, -1);
                                } else{
                                    groupIndex = findGroupIndex(destGroupId);
                                	clientExists = checkClientInGroup(groupIndex, clientId);

                                    printf("Client Exists %d\n", clientExists);

    	                        	if(clientExists == 3){
    	                        		addClientToGroup(groupIndex, clientId, -1, 1, 0);
    	                        		sprintf(message, "Server : Succesfully added to group with Id %d\n", destGroupId);
                                		insertMessageTable(message, clientId, -1);
    	                        	} else if(clientExists == 1 || clientExists == 4){
    	                        		sprintf(message, "Server : Client is already added to group with Id %d\n", destGroupId);
                                		insertMessageTable(message, clientId, -1);
    	                        	}
    	                        	else if(clientExists == 2 || clientExists == 5){
                                        sprintf(message, "Server : Request pending with Admin.\n");
                                        insertMessageTable(message, clientId, -1);
    	                        	} else{
                                        addClientToGroup(groupIndex, clientId, -1, -1, 1);
                                        sendRequestToAdmin(groupIndex, clientId);
                                        sprintf(message, "Server : Request sent to Admin. Now wait till eternity.\n");
                                        insertMessageTable(message, clientId, -1);
                                    }
                                }
                            }
                            //free(message);
                        	
                        } else if(strncmp(type, "/approve", strlen("/approve")) == 0){
                            buffer2 = newbuffer + strlen(type) + 1;
                            int destGroupId = 0;
                            int destClientId = 0;
                            int groupIndex = 0;
                            int clientExists = 0;

                            int n = sscanf(buffer2, "%d %d", &destGroupId, &destClientId);
                            if(n < 1){
                                strcpy(message, "Server : Wrong/Invalid input\n");
                                insertMessageTable(message, clientId, -1);
                            } else {

                                if(!isValidClient(destClientId)){
                                    strcpy(message, "Server : Invalid client Id\n");
                                    insertMessageTable(message, clientId, -1);
                                } else if(!isValidGroup(destGroupId)){
                                    strcpy(message, "Server : Invalid group Id\n");
                                    insertMessageTable(message, clientId, -1);
                                } else {
                                    groupIndex = findGroupIndex(destGroupId);
                                    if(!isAdmin(groupIndex, clientId)){
                                        strcpy(message, "Server : You are not an admin stupid.\n");
                                        insertMessageTable(message, clientId, -1);
                                    } else{
                                        clientExists = checkClientInGroup(groupIndex, destClientId);
                                        printf("Client Exists %d\n", clientExists);
                                        if(clientExists == 3 || clientExists == 0){
                                            sprintf(message, "Server : Client didnt send any request.\n");
                                            insertMessageTable(message, clientId, -1);
                                        } else if(clientExists == 1 || clientExists == 4){
                                            sprintf(message, "Server : Client is already added to group with Id %d\n", destGroupId);
                                            insertMessageTable(message, clientId, -1);
                                        }
                                        else if(clientExists == 2 || clientExists == 5){
                                            addClientToGroup(groupIndex, destClientId, -1, 1, 0);
                                            sprintf(message, "Server : Client added to group.\n");
                                            insertMessageTable(message, clientId, -1);
                                        }
                                    }
                                }
                            }

                        } else if(strncmp(type, "/quit", strlen("/quit")) == 0){
                        	//insertMessageTable(type, clientId, clientId);
                        	quitCall(clientId, acceptedSocket);

                        	P(semid_num_clients);
                        	*numOfConnectedClients = *numOfConnectedClients - 1;
                        	V(semid_num_clients);
                        	break;

                        } else{
                        	char *message = calloc(BUFFER_SIZE, sizeof(char));
                        	memset(message, 0, sizeof(message));
        					sprintf(message, "Server : Invalid command.\n");
        					insertMessageTable(message, clientId, -1);
        					free(message);
                        }

                        free(newbuffer);
                        memset(buffer, 0, sizeof(buffer));
                        continue;
                    }
                    strcat(buffer,temp);
                    
                }
            }

            //free(buffer);
            //free(temp);
            close(acceptedSocket);

            exitCall(0);
		}

        close(acceptedSocket);
    }
    exitCall(0);
    return 0;
}	