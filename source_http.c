#include <stdio.h>
#include <stdbool.h>
#include <winsock2.h>
#include <stdlib.h>
#include <Windows.h>

#pragma comment(lib, "ws2_32.lib") // used for mingw cokmpilers not in vscode enviroment

int main(){

    // 1.>>>Initialization of winsock
    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0){
        printf("Winsock initialization failed. Error Code: %d\n", WSAGetLastError());
        return 1;
    }

    // 2.>>>Creating a socket now!
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);

    struct sockaddr addr;
    int bind_result = bind(sockfd, &addr, sizeof(struct sockaddr));
    // memset();
    
    if (sockfd < 0)
    {
        printf("Socket creation failed. Error code: %d", WSAGetLastError());
        WSACleanup();
        exit(-1);
    }

    for(int i = 0;i < 6; i++){
        // a request to recieve must be recieved here!
        printf("Witing for response.......\n");
        Sleep(1000);
    }
    
    // 3.>>> Cleanup 
    closesocket(sockfd);
    WSACleanup();

    return 0;
}
