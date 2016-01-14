/* 
 *    conn-network test program 2
 *
 *    send a big message between two processes on different computers
 *
 *    usage: conn-network2 [<hostname>]
 *    if no hostname is supplied, will wait for a connection and then transmit a big message
 *    if a hostname is given, will make a connection and then receive
*/

#include "defs.h"
#include "minithread.h"
#include "minisocket.h"
#include "synch.h"

const size_t BUFFER_SIZE = 100000;

// port on which we do the communication
int port=10000;
char* hostname;

char* GetErrorDescription(int errorcode){
    switch(errorcode){
    case SOCKET_NOERROR:
        return "No error reported";
        break;

    case SOCKET_NOMOREPORTS:
        return "There are no more ports available";
        break;

    case SOCKET_PORTINUSE:
        return "The port is already in use by the server";
        break;

    case SOCKET_NOSERVER:
        return "No server is listening";
        break;

    case SOCKET_BUSY:
        return "Some other client already connected to the server";
        break;

    case SOCKET_SENDERROR:
        return "Sender error";
        break;

    case SOCKET_RECEIVEERROR:
        return "Receiver error";
        break;

    default:
        return "Unknown error";
    }
}

int server(int* arg) {
    (void)arg; //unused

    char buffer[BUFFER_SIZE];

    minisocket_error error;
    minisocket_t *socket = minisocket_server_create(port,&error);
    if (socket==NULL){
        printf("ERROR: %s. Exiting. \n",GetErrorDescription(error));
        return -1;
    }

    // Fill in the buffer with numbers from 0 to BUFFER_SIZE-1
    for (int i=0; i<BUFFER_SIZE; i++){
        buffer[i]=(char)(i%256);
    }

    // send the message
    int bytes_sent=0;
    while (bytes_sent!=BUFFER_SIZE){
        int trans_bytes=
                minisocket_send(socket,buffer+bytes_sent,
                                BUFFER_SIZE-bytes_sent, &error);

        printf("Sent %d bytes.\n",trans_bytes);

        if (error!=SOCKET_NOERROR){
            printf("ERROR: %s. Exiting. \n",GetErrorDescription(error));
            /* close the connection */
            minisocket_close(socket);

            return -1;
        }

        bytes_sent+=trans_bytes;
    }

    minisocket_close(socket);
    return 0;
}

int client(int* arg) {
    char buffer[BUFFER_SIZE];
    network_address_t address;

    network_translate_hostname(hostname, address);

    // create a network connection to the local machine
    minisocket_error error;
    minisocket_t *socket = minisocket_client_create(address, port,&error);
    if (socket==NULL){
        printf("ERROR: %s. Exiting. \n",GetErrorDescription(error));
        return -1;
    }

    // receive the message
    int bytes_received=0;
    while (bytes_received!=BUFFER_SIZE){
        int received_bytes;
        if ( (received_bytes=minisocket_receive(socket,buffer,BUFFER_SIZE-bytes_received, &error))==-1){
            printf("ERROR: %s. Exiting. \n",GetErrorDescription(error));
            /* close the connection */
            minisocket_close(socket);
            return -1;
        }
        /* test the information received */
        for (int i=0; i<received_bytes; i++){
            if (buffer[i]!=(char)( (bytes_received+i)%256 )){
                printf("The %d'th byte received is wrong.\n",
                       bytes_received+i);
                /* close the connection */
                minisocket_close(socket);
                return -1;
            }
        }

        bytes_received+=received_bytes;
    }

    printf("All bytes received correctly.\n");

    minisocket_close(socket);
    return 0;
}


int main(int argc, char** argv) {

    if (argc > 1) {
        hostname = argv[1];
        minithread_system_initialize(client, NULL);
    }
    else {
        minithread_system_initialize(server, NULL);
    }
    return -1;
}

