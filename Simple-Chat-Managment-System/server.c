#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"
#define MAX_LOBBIES 10000
#define MAX_ARGS 20

#define SEPARATOR ,

struct handleConn_data {
    int lock;
    pthread_t tid;
    SOCKET s;
};

struct Lobby {

    char code[8]; //8 letter code
    bool isPublic;

    SOCKET* clients;
    unsigned short int numClients;
    unsigned short int maxClients;

};
#define lobby_init(code,isPublic,maxClients) (struct Lobby){code,isPublic,calloc(maxClients, sizeof(struct Lobby)),0,maxClients}

struct Lobby lobbies[MAX_LOBBIES];
int numLobbies = 0;

enum createLobby_ERRS OK,TOO_FEW_CLIENTS,SERVER_FULL,CODE_TAKEN;

int createLobby(char code[8], bool isPrivate, unsigned short maxClients) {
    
    if (maxClients < 2) return TOO_FEW_CLIENTS;
    if (numLobbies >= MAX_LOBBIES) return SERVER_FULL;

    for (int i = 0; i < numLobbies;  i++) {
        if (strcmp(lobbies[i].code, code)) {
            return CODE_TAKEN;
        }
    }

    //note: change this to a system witch actually initializes a lobby in the first available slot
    lobbies[numLobbies++] = lobby_init(code,isPrivate, maxClients);
    
    return OK;

}
void close_lobby() {

}

void* handleConn(void * datain) {

    //get data from struct
    struct handleConn_data * data = ((struct handleConn_data*)datain);
    SOCKET ClientSocket = data->s;
    pthread_t tid = data->tid;

    //unlock main thread
    data->lock = 0;
    
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;

    enum Request {
        CREATE = 0,
        JOIN = 1,
        INFO = 2
    };

    // Receive until the peer shuts down the connection

    iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
    if (iResult >= 1) {
            
        enum Request reqType = recvbuf[0]; //first byte is reqType

        char* args[MAX_ARGS];
        int numArgs = 0;

        if (iResult > 1) { 
            
            //if some arguments are given, then get them into a string array for args

            char* next_token = NULL;
            char* token = strtok_s(recvbuf + 1, ",", &next_token);
            
            while (token != NULL) {
                
                args[numArgs++] = token;
                token = strotk_s(NULL, ",", &next_token);

            }

        }

        switch (reqType) {

        case CREATE: { //ARGS : ReqCode,isPrivate,maxClients

            if (numArgs == 4) {

                char* reqCode = args[0];
                bool isPrivate = strcmp(args[1],"private") ? true : false ;
                int maxClients = atoi(args[2]);

                enum createLobby_ERR err = createLobby(reqCode, isPrivate, maxClients);
                
                if (err != OK) {
                    /*TODO: display error*/
                    closesocket(ClientSocket);
                    return;
                }


            } else { /*Incorrect Args Error*/
                
                closesocket(ClientSocket);
                return;

            }
        }break;

        case JOIN: {



        }break;

        case INFO: {

            for (int i = 0; i < numLobbies; i++) {
                if (lobbies[i].isPublic != true) continue; //skip private lobbies


                char output[64];

                /*
                    Standard output format :
                    [SID],[Connected Clients],[Max Clients]
                    ... more ...
                    [Last entry]
                    (close socket)

                    Standard error format:
                    ![ERR]
                    (close socket)
                */

                snprintf(output,64,"%s,%hu,%hu",lobbies[i].code,lobbies[i].numClients,lobbies[i].maxClients);
                    
                int iSendResult = send(ClientSocket, output, 64, 0);
                if (iSendResult == SOCKET_ERROR) {
                    printf("send failed: %d\n", WSAGetLastError());
                    closesocket(ClientSocket);
                    return;
                }

            }

        }break;

        }

        free(args);
        closesocket(ClientSocket);
    }
    else if (iResult == 0)
        printf("Connection closing...\n");
    else {
        printf("recv failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }



    // shutdown the connection since we're done
    iResult = shutdown(ClientSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    // cleanup
    closesocket(ClientSocket);

}

int __cdecl main(void)
{
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo* result = NULL;
    struct addrinfo hints;

    int iSendResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for the server to listen for client connections.
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket
    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    while(1) {

        iResult = listen(ListenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            printf("listen failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        // Accept a client socket
        ClientSocket = accept(ListenSocket, NULL, NULL); //NOTE: BLOCKING BIT
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            WSACleanup();
            return 1;
        }

        printf("new Client accepted\n");
        struct handleConn_data thread_data;
        thread_data.s = ClientSocket;
        thread_data.lock = 1;

        pthread_create(&(thread_data.tid), NULL, handleConn, (void*)&thread_data);
        while (thread_data.lock) continue; //wait until data is read

        ClientSocket = INVALID_SOCKET;

    }

    // No longer need server socket
    closesocket(ListenSocket);

    return 0;
}