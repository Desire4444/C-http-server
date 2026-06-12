#include <stdio.h>
#include <stdbool.h>

#include <winsock2.h>
#include <WS2tcpip.h>

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
    
    if (sockfd < 0)
    {
        printf("Socket creation failed. Error code: %d", WSAGetLastError());
        WSACleanup();
        exit(-1);
    }
    
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    getaddrinfo(NULL, "8080", &hints, &res);
    
    int bind_result = bind(sockfd, res->ai_addr, res->ai_addrlen);
    printf("Bind result is: %d\n", bind_result);

    // Listen for incomming connections
    int listen_result = listen(sockfd, 20);
    if (listen_result < 0)
    {
        printf("Listenning error. Error code: %d\n", WSAGetLastError());
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
