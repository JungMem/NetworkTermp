#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>
#include <arpa/inet.h>

// One byte for Null Termination
#define BUF_SIZE 1024+1

unsigned int countSp(const char *str, unsigned int *ptr) {

    // var For check ptr's idx
    unsigned int countNum = 0;
    // var For check sp's place
    unsigned int countPtr = 0;

    while (*str != '\0') {

        if (*str == ' ') {

            if(countNum < 2) ptr[countNum] = countPtr;   
            countNum++;
        }
        countPtr++;
        str++;
    }
    return countNum;
}




//////////////////// Transform ASCII TO RESP2 or RESP2 TO ASCII FUNC ////////////////////

// return -1: Error
// return 0: EXIT
// return 1: GET || SET
int transAscii(char* buf, char* comp){

    unsigned int ptr[3];

    // Process Set Command
    if(strncmp(buf, "set", 3)==0){

        if(countSp(buf, ptr) != 2){
            printf("(error) ERR wrong number of arguments for 'set' command\n");
            return -1;
        }

        // Ex: set(ptr[0])key(ptr[1])value(ptr[2])
        ptr[2] = strlen(buf);

        // Slice Strings
        // Ex: set(\0)key(\0)value\0
        int i;
        for(i = 0; i < 2; i++) buf[ptr[i]] = '\0';

        strncpy(comp, "*3\r\n$3\r\nSET\r\n\0", 14);
        sprintf(comp+strlen(comp), "$%d\r\n%s\r\n\0", ptr[1]-ptr[0]-1, buf+ptr[0]+1);
        sprintf(comp+strlen(comp), "$%d\r\n%s\r\n\0", ptr[2]-ptr[1]-1, buf+ptr[1]+1);

        return 1;

    }
    // Process Get Command
    if(strncmp(buf, "get", 3)==0){

        if(countSp(buf, ptr) != 1){
            printf("(error) ERR wrong number of arguments for 'get' command\n");
            return -1;
        }

        // Ex: get(ptr[0])key(ptr[1])
        ptr[1] = strlen(buf);

        // Slice Strings
        // Ex: get(\0)key\0
        buf[ptr[0]] = '\0';

        strncpy(comp, "*2\r\n$3\r\nGET\r\n\0", 14);
        sprintf(comp+strlen(comp), "$%d\r\n%s\r\n\0", ptr[1]-ptr[0]-1, buf+ptr[0]+1);

        return 1;

    }
    // Process EXIT Command
    else if(strncmp(buf, "EXIT\0", sizeof(5))==0){

        strncpy(comp, "*1\r\n$4\r\nQUIT\r\n\0", 15);
        return 0;
    }
    // Process Error Exception
    else{
        printf("(error) ERR unknown command '%s'\n", buf);
        return -1;
    }

}

// return -1: ERROR
// return 0: Simple Strings
// return 1: Bulk Strings
int parseResp(char* buf){

    // Parse Bulk Strings
    if(*buf == '$'){

        if(strncmp(buf, "$-1", 3) == 0){
            printf("(nil)\n");
            return 1;
        }
        else{

            // Ex: $4\r\ntest\r\n => test\r\n
            while(*buf != '\r') buf++;
            buf += 2;

            // backup To print out
            char* backup = buf;

            // Ex: test\r\n => \r\n
            while(*buf != '\r') buf++;
            
            // Ex: $4\r\ntest\r\n => $4\r\ntest\0\n
            *buf = '\0';

            printf("\"%s\"\n", backup);

            return 1;
        }

    }
    // Parse Simple Strings
    else if(strncmp(buf, "+OK\r\n", 5)==0){
        printf("OK\n");
        return 0;
    }
    // Process Error Exeception
    else{
        printf("***Unintelligible Response from Server***\n");
        return -1;
    }

}
/////////////////////////////////////////////////////////////////////////////////////////



//////////////////// Process Socket and Server Interaction FUNC ////////////////////
int main(int argc, char* argv[]){

    // Process Socket
    int clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(clientSocket < 0){
        perror("***socket API error***\n");
        return -1;
    }

    struct sockaddr_in sockStruct;
    memset(&sockStruct, 0, sizeof(struct sockaddr_in));

    sockStruct.sin_family = AF_INET;
    sockStruct.sin_port = htons(atoi(argv[2]));

    if(inet_pton(AF_INET, argv[1], &sockStruct.sin_addr.s_addr) != 1){
        perror("***inet_pton API error***\n");
        close(clientSocket);
        return -1;
    }

    if(connect(clientSocket, (struct sockaddr *)&sockStruct, sizeof(sockStruct)) < 0){
        perror("***connect API error***\n");
        close(clientSocket);
        return -1;
    }

    // Server Interaction
    for(;;){

        int res = 0;

        // flag For transAscii
        int flag1 = 0;
        // flag For parseResp
        int flag2 = 0;
        
        char buf[BUF_SIZE];
        char comp[BUF_SIZE];

        fgets(buf, BUF_SIZE, stdin);
        buf[strcspn(buf, "\n")] = '\0';

        flag1 = transAscii(buf, comp);
        if(flag1 == -1) continue;

        res = send(clientSocket, comp, strlen(comp), 0);
        if(res <= 0){
            perror("***send API error***\n");
            close(clientSocket);
            return -1;
        }

        res = recv(clientSocket, buf, BUF_SIZE, 0);
        if(res <= 0){
            perror("***recv API error***\n");
            close(clientSocket);
            return -1;
        }
        buf[res] = '\0';

        flag2 = parseResp(buf);
        // ERROR or EXIT
        if(flag2 == -1 || (flag1 == 0 && flag2 == 0)){
            break;
        }

    }

    close(clientSocket);
    return 0;

}
////////////////////////////////////////////////////////////////////////////////////
