#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <malloc.h>
#include <pthread.h>

#include <sys/select.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>

void stopInt(int sig){
    signal(sig, SIG_IGN);
}

// Define Max Client Num
#define MAX_CLIENT 10
// One byte for Null Termination
#define BUF_SIZE 1024+1

pthread_mutex_t mutex;

//////////////////// KVS STRUCT & KVS FUNC ////////////////////
unsigned int kvs_cnt = 0;
struct kvs{
    char* key;
    char* value;
};
struct kvs KVS[10];

void KVSinit(){

    int i;
    for(i = 0; i < 10; i++){
        memset(&KVS[i], 0, sizeof(struct kvs));
    }
}

void KVSfin(){

    int i;
    for(i = 0; i < kvs_cnt; i++){
        free(KVS[i].key);
        free(KVS[i].value);
    }
}

int KVSfind(char* key){

    int i;
    for(i = 0; i < kvs_cnt; i++){
        if(strncmp(KVS[i].key, key, strlen(key))==0){
            return i;
        }
    }
    return -1;
}

void KVSset(char *key, char *value){

    int idx = KVSfind(key);
    if(idx != -1){
        free(KVS[idx].value);
        KVS[idx].value = value;
    }
    else{
        KVS[kvs_cnt].key = key;
        KVS[kvs_cnt].value = value;
        kvs_cnt++;
    }
}

char* KVSget(char* key){

    int idx = KVSfind(key);
    if(idx != -1){
        char* tmp = KVS[idx].value;
        return tmp;
    }
    else{
        return NULL;
    }
}
///////////////////////////////////////////////////////////////



//////////////////// RESP Parsing FUNC ////////////////////
char* parseResp(char* buf){

    // Extract NUM to num
    // Ex: $(((NUM)))\r\nStrings\r\n
    unsigned int num = atoi(buf+1);

    // Mov buf to Strings
    // Ex: $NUM\r\nStrings\r\n => Strings\r\n
    while(*buf != '\r') buf++;
    buf += 2;

    // Extract Strings to extr
    // Ex: (((Strings)))\r\n
    char* extr = (char*)malloc(num+1);  // For Null Termination
    memset(extr, 0, num+1);             // For Null Termination
    strncpy(extr, buf, num);

    // return Strings' Pointer
    return extr;
}
///////////////////////////////////////////////////////////



//////////////////// Client Interaction FUNC ////////////////////
void* cliInteract(void* clientSocket){

    int soc = *(int*)clientSocket;
    printf("%d Socket connected\n", soc);

    int res;
    char buf[BUF_SIZE];
    char* key;
    char* value;
    char* tmp;

    for(;;){

        res = recv(soc, buf, BUF_SIZE, 0);
        if(res <= 0){
            perror("***recv API error***\n");
            close(soc);
            exit(1);
        }
        buf[res] = '\0';

        // Process SET Command
        if(strncmp(buf, "*3\r\n$3\r\nSET\r\n", 13)==0){

            // Mov Buf to First String
            tmp = buf+13;

            key = parseResp(tmp);
            
            // Mov Buf to Second String
            tmp++;
            while(*tmp != '$') tmp++;
            
            value = parseResp(tmp);

            pthread_mutex_lock(&mutex);
            KVSset(key, value);
            pthread_mutex_unlock(&mutex);
            
            strncpy(buf, "+OK\r\n\0", 6);
        }
        // Process GET Command
        else if(strncmp(buf, "*2\r\n$3\r\nGET\r\n", 13)==0){

            key = parseResp(buf+13);

            pthread_mutex_lock(&mutex);
            value = KVSget(key);
            pthread_mutex_unlock(&mutex);

            if(value != NULL){
                sprintf(buf, "$%ld\r\n%s\r\n\0", strlen(value), value);
            }
            else{
                strncpy(buf, "$-1\r\n\0", 5);
            }
        }
        // Process EXIT Command
        else if(strncmp(buf, "*1\r\n$4\r\nQUIT\r\n", 14)==0){

            res = send(soc, "+OK\r\n", 5, 0);
            if(res <= 0){
                perror("***send API error***\n");
                close(soc);
                exit(1);
            }
            break;
        }
        // Process Error
        else{
            strncpy(buf, "-ERR\r\n\0", 7);
        }

        res = send(soc, buf, strlen(buf), 0);
        if(res <= 0){
            perror("***send API error***\n");
            close(soc);
            exit(1);
        }

    }

    close(soc);
    printf("%d Socket disconnected\n", soc);

}
/////////////////////////////////////////////////////////////////



//////////////////// Process Socket FUNC ////////////////////
int main(int argc, char *argv[]){

    KVSinit();

    int serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(serverSocket < 0){
        perror("***socket API error ***\n");
        return -1;
    }

    struct sockaddr_in sockStruct;
    memset(&sockStruct, 0, sizeof(struct sockaddr_in));

    sockStruct.sin_family = AF_INET;
    sockStruct.sin_port = htons(atoi(argv[1]));
    sockStruct.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(serverSocket, (struct sockaddr *)&sockStruct, sizeof(sockStruct)) < 0){
        perror("***bind API error***\n");
        close(serverSocket);
        return -1;
    }

    if(listen(serverSocket, MAX_CLIENT) < 0){
        perror("***listen API error***\n");
        close(serverSocket);
        return -1;
    }

    int threadHandle;
    
    int res;
    fd_set reads, temps;
    struct timeval timeout;

    FD_ZERO(&reads);
    FD_SET(serverSocket, &reads);

    signal(SIGINT, stopInt);
    printf("Ctrl + C for Stop Program\n");

    pthread_mutex_init(&mutex, NULL);
    for(;;){

        temps = reads;
        timeout.tv_sec = 0;
        timeout.tv_usec = 500;

        res = select(serverSocket+1, &temps, 0, 0, &timeout);
        if(res < 0){

            // ctrl + c
            if(errno == EINTR){
                break;
            }
            // Error
            else{
                perror("***select API error***\n");
                close(serverSocket);
                return -1;
            }
        }
        // timeout
        else if(res == 0){
            continue;
        }
        else{
            if(FD_ISSET(serverSocket, &temps)){

                int clientSocket = accept(serverSocket, NULL, NULL);
                if(clientSocket < 0){
                    perror("***accept API error***\n");
                    close(serverSocket);
                    return -1;
                }

                pthread_t thread;
                if(pthread_create(&thread, NULL, cliInteract, (void*)&clientSocket) < 0){
                    perror("***pthread_create API error***\n");
                    close(serverSocket);
                    return -1;
                }

                if(pthread_detach(thread) != 0){
                    perror("***pthread_detach API error***\n");
                    close(serverSocket);
                    return -1;
                }
            }
        }

    }
    pthread_mutex_destroy(&mutex);
    
    close(serverSocket);
    KVSfin();

    return 0;

}
/////////////////////////////////////////////////////////////
