//
// Created by Gonzalo_gm on 5/3/26.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define TAM_MSG 256

int main(int argc, char *argv[])
{
    if (argc < 3)
    {
        fprintf(stderr, "Uso: %s <ip_broker> <puerto_broker>\n", argv[0]);
        return -1;
    }


    char *ip_broker    = argv[1];
    int   puerto_broker = atoi(argv[2]);

    int fdSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (fdSocket == -1)
    {
        perror("Error en socket");
        return -1;
    }

    struct sockaddr_in servidor;
    servidor.sin_family = AF_INET;
    servidor.sin_port   = htons(puerto_broker);
    if (inet_aton(ip_broker, &servidor.sin_addr) == 0)
    {
        fprintf(stderr, "Dirección IP inválida: %s\n", ip_broker);
        return -1;
    }
    memset(&servidor.sin_zero, '\0', 8);

    if (connect(fdSocket, (struct sockaddr *)&servidor, sizeof(struct sockaddr)) == -1)
    {
        perror("Error al conectar");
        return -1;
    }

    printf("[Cliente] Conectado al broker %s:%d\n", ip_broker, puerto_broker);
    printf("[Cliente] Comandos: GET <n> | PUT <n> <val> | DEL <n> | EXIT\n\n");

    char mensaje[TAM_MSG], respuesta[TAM_MSG];

    while (1)
    {
        printf("> ");
        fflush(stdout);

        memset(mensaje,   '\0', TAM_MSG);
        memset(respuesta, '\0', TAM_MSG);

        if (fgets(mensaje, TAM_MSG, stdin) == NULL)
            break;

        mensaje[strcspn(mensaje, "\n")] = '\0';

        if (strlen(mensaje) == 0)
            continue;

        if (send(fdSocket, mensaje, strlen(mensaje), 0) == -1)
        {
            perror("Error en send");
            break;
        }

        int nb = recv(fdSocket, respuesta, TAM_MSG - 1, 0);
        if (nb <= 0)
        {
            if (nb == 0)
                printf("[Cliente] El broker ha cerrado la conexión\n");
            else
                perror("Error en recv cliente");
            break;
        }

        printf("[Cliente] Respuesta: %s\n", respuesta);

        if (strncmp(mensaje, "EXIT", 4) == 0)
            break;
    }

    close(fdSocket);
    printf("[Cliente] Conexión cerrada\n");
    return 0;
}