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

#define DEFAULT_CODELEN 8
#define DEFAULT_BUFLEN 512
#define DEFAULT_PORT "27015"
#define MAX_LOBBIES 10000
#define MAX_ARGS 20
#define USERNAME_LEN 35

#define SEPARATOR ,

enum CommType {

    /* DATA / INFO: */

    OK = 0,
    GENERAL_SERVICE_MSG = 1,
    CLIENT_CONNECTED_MSG = 2,
    CLIENT_DISCONNECTED_MSG = 3,

    BROAD_MSG = 20,
    PRIV_USER_MSG = 21,

    CALCULATE = 30,

    PING = 40,

    /*Main menu options*/

    CREATE_LOBBY = 50,
    JOIN_LOBBY = 51,
    LOBBY_LIST = 52,

    /* Errors :  */

    DISCON_CUTOFF = 99,

    INCORRECT_ARGS = 100,
    CODE_TOO_LONG = 101,

    CODE_TAKEN = 120,
    SERVER_FULL = 121,
    TOO_FEW_CLIENTS = 122,

    LOBBY_FULL = 150,
    INVALID_CODE = 151,

    LOBBY_CLOSED = 200,
    KICKED = 201,

    OTHER = 255,
};


struct Client {

    SOCKET socket;
    pthread_mutex_t socketOpLock;
    unsigned char username[USERNAME_LEN];

};

struct Lobby {

    unsigned char code[DEFAULT_CODELEN]; //8 letter code
    bool isPublic;

    struct Client* clients;
    unsigned short int numClients;
    unsigned short int maxClients; //if lobby closed this is 0
    pthread_mutex_t lobby_lock;


};

struct Lobby lobbies[MAX_LOBBIES];
int numLobbies = 0;


struct Client Client_init(SOCKET s, unsigned char* username) {
    
    struct Client c;
    c.socket = s;
    pthread_mutex_init(&c.socketOpLock,NULL);
    strcpy_s(c.username,USERNAME_LEN,username);
    
    return c;
}

bool isClient(struct Client* c) { return !(c == NULL || c->socket == INVALID_SOCKET); }

void Client_sendMsg(struct Client* c,enum CommType code, unsigned char* msg) {
    
    unsigned char sendBuf[DEFAULT_BUFLEN];
    int len = 0;

    pthread_mutex_lock(c->socketOpLock);

    if (msg != NULL) {

        len = strnlen(msg, DEFAULT_BUFLEN - 2);
        strcpy_s(sendBuf + 1, DEFAULT_BUFLEN - 1, msg);
        
    }
    
    if (len > 0) len++; //making sure we send terminator NULL

    sendBuf[0] = code;
    int iResult = send(c->socket, sendBuf, len+1, 0);
    if (code > DISCON_CUTOFF) {
        closesocket(c->socket);
        pthread_mutex_unlock(&c->socketOpLock);
        pthread_exit(PTHREAD_CANCELED);
    }

    pthread_mutex_unlock(&c->socketOpLock);

}

struct Lobby Lobby_init(unsigned char * code,bool isPublic,int maxClients) {
    
    struct Lobby lobby = { 0 };
    lobby.clients = calloc(maxClients, sizeof(struct Client));

    for (int i = 0; i < maxClients; i++) {
        lobby.clients[i].socket = INVALID_SOCKET;
    }

    strcpy_s(lobby.code,DEFAULT_CODELEN,code);
    lobby.isPublic = isPublic;
    lobby.maxClients = maxClients;
    lobby.numClients = 0;
    pthread_mutex_init(&lobby.lobby_lock, NULL);
    return lobby;

}

bool Lobby_isClosed(struct Lobby* lobby) { return (lobby->maxClients == 0); }

int Lobby_sendMsg(struct Lobby* l, enum CommType code,unsigned char* msg, struct Client* sender) {

    for (int i = 0; i < l->maxClients; i++) {

        struct Client* c = &l->clients[i];
        if (isClient(c)) {

            if (sender == c)
                continue;


            Client_sendMsg(c, code, msg);

        }

    }

}

void Lobby_close(struct Lobby* l) {

    Lobby_sendMsg(l,LOBBY_CLOSED, "", NULL);
    l->maxClients = 0;
    l->numClients = 0;
    strcpy_s(l->code,DEFAULT_CODELEN,"\0");
    free(l->clients);
    numLobbies--;

}

void Lobby_join(unsigned char* code,struct Client* cin) {
    
    struct Client c = *cin;

    struct Lobby* l = NULL;
    for (int i = 0; i < MAX_LOBBIES; i++) {
        if (! Lobby_isClosed(&lobbies[i]) && strcmp(lobbies[i].code, code) == 0) {
            l = &lobbies[i];
            pthread_mutex_lock(&l->lobby_lock);
            break;
        }
    }

    if (l == NULL) {
        Client_sendMsg(&c, INVALID_CODE, NULL);
        return;
    }


    if (l->numClients >= l->maxClients) {

        Client_sendMsg(&c, LOBBY_FULL, NULL);
        return;

    }

    int clientID = -1;

    for (int i = 0; i < l->maxClients; i++) {
        
        if (!isClient(&l->clients[i])) {

            l->numClients++;

            clientID = i;
            l->clients[i] = c;

            pthread_mutex_unlock(&l->lobby_lock);

            break;

        }

    }

    Client_sendMsg(&c, OK, NULL);

    //printf("%s joined lobby %s\n", c.username, l->code);

    Lobby_sendMsg(l, CLIENT_CONNECTED_MSG, c.username, &c);

    char inputBuf[DEFAULT_BUFLEN];

    while (1) {

        int iRecv = recv(c.socket, inputBuf, DEFAULT_BUFLEN, NULL);
        if (iRecv > 0) {

            unsigned char* msg = inputBuf + 1;

            switch (inputBuf[0]){

            case BROAD_MSG: {

                char outputMsg[DEFAULT_BUFLEN - 1];

                unsigned short len = strlen(c.username);
                strcpy_s(outputMsg, (DEFAULT_BUFLEN - 2), c.username);
                strcpy_s(outputMsg + len, (DEFAULT_BUFLEN - 2) - USERNAME_LEN, ",");
                strcpy_s(outputMsg + len + 1, (DEFAULT_BUFLEN - 2) - USERNAME_LEN, msg);

                Lobby_sendMsg(l, BROAD_MSG, outputMsg, &l->clients[clientID]);

            } break;

            case CLIENT_DISCONNECTED_MSG: {
                Lobby_sendMsg(l, CLIENT_DISCONNECTED_MSG, c.username, &l->clients[clientID]);

                closesocket(c.socket);
                lobbies->numClients--;
                lobbies->clients[clientID].socket = INVALID_SOCKET;

                return;
            
            } break;

            case CALCULATE :{

                double resval = -300;

                int msglen = strnlen(msg, DEFAULT_BUFLEN);

                char* arg1 = msg;
                char op = NULL;
                char* arg2 = NULL;

                for (int i = 0; i < msglen; i++) {
                    if (msg[i] == '+' || msg[i] == '-' || msg[i] == '*' || msg[i] == '/') {
                        
                        op = msg[i];
                        msg[i] = '\0';
                        double arg1val = atof(arg1);
                        msg[i] = op;
                        arg2 = msg + 1;

                        double arg2val = atof(arg2);

                        switch (op) {
                        case '+':
                            resval = arg1val + arg2val;
                        break;
                        case '-':
                            resval = arg1val - arg2val;
                        break;
                        case '/':
                            resval = arg1val / arg2val;
                        break;
                        case '*':
                            resval = arg1val * arg2val;
                        break;
                        }

                        break;
                    }
                }
                

                unsigned char outputMsg[DEFAULT_BUFLEN - 1] = "";

                
                unsigned char res[30] = "";
                sprintf_s(res, 30, "%f", resval);

                unsigned short usernlen = strlen(c.username);


                strncat_s(outputMsg, DEFAULT_BUFLEN-1, c.username, DEFAULT_BUFLEN-1);
                strncat_s(outputMsg, DEFAULT_BUFLEN - 1, ", result of", DEFAULT_BUFLEN - 1);
                strncat_s(outputMsg, DEFAULT_BUFLEN - 1, msg, DEFAULT_BUFLEN - 1);
                strncat_s(outputMsg, DEFAULT_BUFLEN - 1, ": ", DEFAULT_BUFLEN - 1);
                strncat_s(outputMsg, DEFAULT_BUFLEN - 1, res, DEFAULT_BUFLEN - 1);


                Lobby_sendMsg(l, BROAD_MSG, outputMsg, NULL);

            }break;

            default: {

            }
            }

        } else if (iRecv <= 0) {

            printf("recv failed with error: %d on client %d,%s\n",WSAGetLastError(),(int)c.socket,c.username);

            Lobby_sendMsg(l, CLIENT_DISCONNECTED_MSG, c.username, &l->clients[clientID]);

            closesocket(c.socket);
            lobbies->numClients--;
            lobbies->clients[clientID].socket = INVALID_SOCKET;

            return;

        }

    }
}

void Lobby_run(struct Lobby* l) {
    
    printf("lobby %s is now running\n", l->code);

}

int createLobby(unsigned char code[8], bool isPublic, unsigned short maxClients, enum CommType* err) {
    
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;

    if (maxClients < 2) return TOO_FEW_CLIENTS;
    if (numLobbies >= MAX_LOBBIES) return SERVER_FULL;
    
    pthread_mutex_lock(&mutex);

    int availableSlot = -1;
    for (int i = 0; i < numLobbies;  i++) {
        
        if (Lobby_isClosed(&lobbies[i]) && availableSlot == -1)
            availableSlot = i;

        else if (strcmp(lobbies[i].code, code) == 0) {
            
            pthread_mutex_unlock(&mutex);
            *err = CODE_TAKEN;
            return -1;

        }
        
    }

    if (availableSlot != -1) 
        lobbies[availableSlot] = Lobby_init(code, isPublic, maxClients);
    
    else {
        struct Lobby l = Lobby_init(code, isPublic, maxClients);
        availableSlot = numLobbies;
        lobbies[numLobbies++] = l;
    }
    pthread_mutex_unlock(&mutex);
    
    *err = OK;
    return availableSlot;
}

struct handleConn_data {
    int lock;
    pthread_t tid;
    SOCKET s;
};
void* handleConn(void * datain) {

    //get data from struct
    struct handleConn_data * data = ((struct handleConn_data*)datain);
    SOCKET ClientSocket = data->s;
    pthread_t tid = data->tid;

    //unlock main thread
    data->lock = 0;
    
    unsigned char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;
    int iResult;


    // Receive until the peer shuts down the connection

    iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
    if (iResult >= 1) {
            
        enum Request reqType = recvbuf[0]; //first byte is reqType

        unsigned char* args[MAX_ARGS];
        int numArgs = 0;

        if (iResult > 1) { 
            
            //if some arguments are given, then get them into a string array for args

            unsigned char* next_token = NULL;
            unsigned char* token = strtok_s(recvbuf + 1, ",", &next_token);
            
            while (token != NULL) {
                
                args[numArgs++] = token;
                token = strtok_s(NULL, ",", &next_token);

            }

        }

        switch (reqType) {

        case CREATE_LOBBY: { //ARGS : ReqCode,isPublic,maxClients

            if (numArgs == 3) {

                unsigned char* reqCode = args[0];
                if (strnlen(reqCode,DEFAULT_BUFLEN+1) >= DEFAULT_CODELEN) {
                    
                    unsigned char out = CODE_TOO_LONG;
                    send(ClientSocket,&out, 1, NULL);
                    closesocket(ClientSocket);
                    pthread_exit(PTHREAD_CANCELED);
                }

                bool isPublic = strcmp(args[1], "public") == 0;
                int maxClients = atoi(args[2]);

                enum CommType err; 
                int lobbyIndex = createLobby(reqCode, isPublic, maxClients,&err);
                
                send(ClientSocket, &err, 1, NULL);
                
                if (lobbyIndex != -1)
                    Lobby_run(&lobbies[lobbyIndex]);
                
            }


        }break;

        case JOIN_LOBBY: {
            
            if (numArgs == 2) {
            
                struct Client c = Client_init(ClientSocket, args[1]);
                Lobby_join(args[0],&c);
  
            }
            
            pthread_exit(PTHREAD_CANCELED);

        }break;

        case LOBBY_LIST: {

            for (int i = 0; i < numLobbies; i++) {
                if (lobbies[i].isPublic != true) continue; //skip private lobbies


                unsigned char output[DEFAULT_BUFLEN];

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

                snprintf(output+1,DEFAULT_BUFLEN," %s \t\t %hu / %hu",lobbies[i].code,lobbies[i].numClients,lobbies[i].maxClients);
                output[0] = OK;
                int iSendResult = send(ClientSocket, output, DEFAULT_BUFLEN, 0);
                if (iSendResult == SOCKET_ERROR) {
                    printf("send failed: %d\n", WSAGetLastError());
                    closesocket(ClientSocket);
                    pthread_exit(PTHREAD_CANCELED);
                    return;
                }

            }

        }break;

        }

    }
    else if (iResult == 0)
        printf("Connection closing...\n");
    else {
        printf("recv failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        pthread_exit(PTHREAD_CANCELED);
        return 1;
    }



    // shutdown the connection since we're done
    
    iResult = shutdown(ClientSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        pthread_exit(PTHREAD_CANCELED);
        return 1;
    }

    // cleanup
    closesocket(ClientSocket);
    pthread_exit(PTHREAD_CANCELED);

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
        return 1;
    }

    // Create a SOCKET for the server to listen for client connections.
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        return 1;
    }

    // Setup the TCP listening socket
    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        return 1;
    }

    freeaddrinfo(result);

    while(1) {

        iResult = listen(ListenSocket, SOMAXCONN);
        if (iResult == SOCKET_ERROR) {
            printf("listen failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            return 1;
        }

        // Accept a client socket
        ClientSocket = accept(ListenSocket, NULL, NULL); //NOTE: BLOCKING BIT
        if (ClientSocket == INVALID_SOCKET) {
            printf("accept failed with error: %d\n", WSAGetLastError());
            closesocket(ListenSocket);
            return 1;
        }

        printf("new Client accepted\n");
        struct handleConn_data thread_data;
        thread_data.s = ClientSocket;
        thread_data.lock = 1;

        pthread_create(&(thread_data.tid), NULL, handleConn, (void*)&thread_data);
        while (thread_data.lock) continue; //wait until data is read

    }

    // No longer need server socket
    closesocket(ListenSocket);

    return 0;
}