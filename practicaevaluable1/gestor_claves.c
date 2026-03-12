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

#define MAX_ENTRADAS 11
#define TAM_CLAVE 32
#define TAM_VALOR 128
#define TAM_MSG 256

typedef struct
{
    int ocupado;
    char clave[TAM_CLAVE];
    char valor[TAM_VALOR];
} EntradaKV;

static EntradaKV tabla[MAX_ENTRADAS];

static int buscar(const char *clave)
{
    int i;
    for (i = 0; i < MAX_ENTRADAS; i++)
    {
        if (tabla[i].ocupado && strcmp(tabla[i].clave, clave) == 0)
            return i;
    }
    return -1;
}

static int hueco_libre(void)
{
    int i;
    for (i = 0; i < MAX_ENTRADAS; i++)
        if (!tabla[i].ocupado)
            return i;
    return -1;
}

static void procesar(const char *peticion, char *respuesta)
{
    char cmd[16], clave[TAM_CLAVE], valor[TAM_VALOR];
    int idx;

    memset(cmd, '\0', sizeof(cmd));
    memset(clave, '\0', sizeof(clave));
    memset(valor, '\0', sizeof(valor));

    if (sscanf(peticion, "GET %31s", clave) == 1 && strncmp(peticion, "GET", 3) == 0)
    {
        idx = buscar(clave);
        if (idx >= 0)
            snprintf(respuesta, TAM_MSG, "%s", tabla[idx].valor);
        else
            snprintf(respuesta, TAM_MSG, "NOEncontrado");

    } else if (sscanf(peticion, "PUT %31s %127s", clave, valor) == 2 && strncmp(peticion, "PUT", 3) == 0)
    {
        idx = buscar(clave);
        if (idx >= 0)
        {
            strncpy(tabla[idx].valor, valor, TAM_VALOR - 1);
            snprintf(respuesta, TAM_MSG, "INSERTADO");
        } else
        {
            idx = hueco_libre();
            if (idx >= 0)
            {
                tabla[idx].ocupado = 1;
                strncpy(tabla[idx].clave, clave, TAM_CLAVE - 1);
                strncpy(tabla[idx].valor, valor, TAM_VALOR - 1);
                snprintf(respuesta, TAM_MSG, "INSERTADO");
            } else
            {
                snprintf(respuesta, TAM_MSG, "ERROR");
            }
        }

    } else if (sscanf(peticion, "DEL %31s", clave) == 1 && strncmp(peticion, "DEL", 3) == 0)
    {
        idx = buscar(clave);
        if (idx >= 0)
        {
            memset(&tabla[idx], 0, sizeof(EntradaKV));
            snprintf(respuesta, TAM_MSG, "ELIMINADO");
        } else
        {
            snprintf(respuesta, TAM_MSG, "NOEncontrado");
        }

    } else
    {
        snprintf(respuesta, TAM_MSG, "ERROR: comando desconocido");
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_in servidor, broker;
    socklen_t tam_broker = sizeof(struct sockaddr);
    char peticion[TAM_MSG], respuesta[TAM_MSG];
    int puerto;

    if (argc < 2)
    {
        fprintf(stderr, "Uso: %s <puerto>\n", argv[0]);
        return -1;
    }
    puerto = atoi(argv[1]);

    memset(tabla, 0, sizeof(tabla));

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == -1)
    {
        perror("Error en socket"); return -1;
    }

    servidor.sin_family = AF_INET;
    servidor.sin_port   = htons(puerto);
    servidor.sin_addr.s_addr = INADDR_ANY;
    memset(&servidor.sin_zero, '\0', 8);

    if (bind(sockfd, (struct sockaddr *)&servidor, sizeof(struct sockaddr)) == -1)
    {
        perror("Error en bind"); return -1;
    }

    printf("[GestorClaves] Escuchando en puerto %d (UDP)\n", puerto);

    while (1)
    {
        memset(peticion,  '\0', TAM_MSG);
        memset(respuesta, '\0', TAM_MSG);

        if (recvfrom(sockfd, peticion, TAM_MSG - 1, 0, (struct sockaddr *)&broker, &tam_broker) == -1)
        {
            perror("Error en recvfrom");
            continue;
        }

        printf("[GestorClaves] Petición de %s:%d -> \"%s\"\n", inet_ntoa(broker.sin_addr), ntohs(broker.sin_port), peticion);

        procesar(peticion, respuesta);

        printf("[GestorClaves] Respuesta -> \"%s\"\n", respuesta);

        if (sendto(sockfd, respuesta, strlen(respuesta), 0, (struct sockaddr *)&broker, sizeof(struct sockaddr)) == -1)
        {
            perror("Error en sendto");
        }
    }

    close(sockfd);
    return 0;
}