/*
 * broker.c
 * Intermediario entre clientes (TCP) y gestores de claves (UDP).
 *
 * - Escucha conexiones TCP de clientes.
 * - Redirige peticiones via UDP al gestor de claves correspondiente:
 *     clave 0-4  -> gestor 1
 *     clave 5-10 -> gestor 2
 * - Si el gestor no responde en TIMEOUT_SEG segundos responde "TIMEOUT".
 *
 * Uso:
 *   ./broker <puerto_broker_tcp>
 *            <ip_gestor1> <puerto_gestor1>
 *            <ip_gestor2> <puerto_gestor2>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>

#define TAM_MSG      256
#define TIMEOUT_SEG  5

/* ---- extrae el numero de clave de un comando ---- */
static int extraer_clave(const char *msg) {
    char cmd[16], clave_str[64];
    if (sscanf(msg, "%15s %63s", cmd, clave_str) < 2)
        return -1;
    /* la clave debe ser numerica */
    char *endp;
    long v = strtol(clave_str, &endp, 10);
    if (*endp != '\0' && *endp != ' ' && *endp != '\n')
        return -1;
    return (int)v;
}

/* ---- envia peticion UDP al gestor y espera respuesta con timeout ---- */
static void consultar_gestor(int sock_udp,
                              struct sockaddr_in *gestor,
                              const char *peticion,
                              char *respuesta) {
    struct timeval tv;
    fd_set rfds;

    if (sendto(sock_udp, peticion, strlen(peticion), 0,
               (struct sockaddr *)gestor, sizeof(struct sockaddr)) == -1) {
        perror("Error sendto gestor");
        snprintf(respuesta, TAM_MSG, "ERROR: no se pudo contactar gestor");
        return;
    }

    /* esperar respuesta con timeout */
    FD_ZERO(&rfds);
    FD_SET(sock_udp, &rfds);
    tv.tv_sec  = TIMEOUT_SEG;
    tv.tv_usec = 0;

    int ret = select(sock_udp + 1, &rfds, NULL, NULL, &tv);
    if (ret == -1) {
        perror("Error select");
        snprintf(respuesta, TAM_MSG, "ERROR: select");
    } else if (ret == 0) {
        printf("[Broker] TIMEOUT esperando respuesta del gestor\n");
        snprintf(respuesta, TAM_MSG, "TIMEOUT");
    } else {
        socklen_t tam = sizeof(struct sockaddr);
        struct sockaddr_in origen;
        memset(respuesta, '\0', TAM_MSG);
        if (recvfrom(sock_udp, respuesta, TAM_MSG - 1, 0,
                     (struct sockaddr *)&origen, &tam) == -1) {
            perror("Error recvfrom gestor");
            snprintf(respuesta, TAM_MSG, "ERROR: recvfrom");
        }
    }
}

/* ---- atiende a un cliente TCP ya conectado ---- */
static void atender_cliente(int fd_cliente,
                             struct sockaddr_in *cliente,
                             int sock_udp,
                             struct sockaddr_in *gestor1,
                             struct sockaddr_in *gestor2) {
    char peticion[TAM_MSG], respuesta[TAM_MSG];

    printf("[Broker] Cliente conectado: %s:%d\n",
           inet_ntoa(cliente->sin_addr), ntohs(cliente->sin_port));

    while (1) {
        memset(peticion,  '\0', TAM_MSG);
        memset(respuesta, '\0', TAM_MSG);

        int nb = recv(fd_cliente, peticion, TAM_MSG - 1, 0);
        if (nb <= 0) {
            if (nb == 0)
                printf("[Broker] Cliente %s:%d cerro conexion\n",
                       inet_ntoa(cliente->sin_addr), ntohs(cliente->sin_port));
            else
                perror("Error recv cliente");
            break;
        }

        /* eliminar salto de linea si viene de fgets */
        peticion[strcspn(peticion, "\n")] = '\0';

        /* eliminar espacios iniciales */
        char *p = peticion;
        while (*p == ' ' || *p == '\t') p++;
        if (p != peticion) memmove(peticion, p, strlen(p) + 1);

        printf("[Broker] Peticion de %s:%d -> \"%s\"\n",
               inet_ntoa(cliente->sin_addr), ntohs(cliente->sin_port), peticion);

        /* EXIT -> cierre de la conexion */
        if (strncmp(peticion, "EXIT", 4) == 0) {
            snprintf(respuesta, TAM_MSG, "ADIOS");
            send(fd_cliente, respuesta, strlen(respuesta), 0);
            printf("[Broker] Cliente %s:%d solicito EXIT\n",
                   inet_ntoa(cliente->sin_addr), ntohs(cliente->sin_port));
            break;
        }

        /* determinar a que gestor va la peticion */
        int num_clave = extraer_clave(peticion);
        if (num_clave < 0 || num_clave > 10) {
            snprintf(respuesta, TAM_MSG, "ERROR: clave fuera de rango (0-10)");
        } else {
            struct sockaddr_in *gestor = (num_clave <= 4) ? gestor1 : gestor2;
            int num_gestor             = (num_clave <= 4) ? 1       : 2;
            printf("[Broker] Redirigiendo a gestor %d (clave %d)\n",
                   num_gestor, num_clave);
            consultar_gestor(sock_udp, gestor, peticion, respuesta);
        }

        printf("[Broker] Respuesta al cliente -> \"%s\"\n", respuesta);

        if (send(fd_cliente, respuesta, strlen(respuesta), 0) == -1) {
            perror("Error send cliente");
            break;
        }
    }

    close(fd_cliente);
    printf("[Broker] Conexion con %s:%d cerrada\n",
           inet_ntoa(cliente->sin_addr), ntohs(cliente->sin_port));
}

/* ========== main ========== */
int main(int argc, char *argv[]) {
    if (argc < 6) {
        fprintf(stderr,
            "Uso: %s <puerto_tcp> <ip_g1> <puerto_g1> <ip_g2> <puerto_g2>\n",
            argv[0]);
        return -1;
    }

    int puerto_tcp  = atoi(argv[1]);
    char *ip_g1     = argv[2];
    int  puerto_g1  = atoi(argv[3]);
    char *ip_g2     = argv[4];
    int  puerto_g2  = atoi(argv[5]);

    /* ---- socket TCP para clientes ---- */
    int sock_tcp = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_tcp == -1) { perror("Error socket TCP"); return -1; }

    /* reutilizar puerto */
    int opt = 1;
    setsockopt(sock_tcp, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in broker_addr;
    broker_addr.sin_family = AF_INET;
    broker_addr.sin_port   = htons(puerto_tcp);
    broker_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    memset(&broker_addr.sin_zero, '\0', 8);

    if (bind(sock_tcp, (struct sockaddr *)&broker_addr,
             sizeof(struct sockaddr)) == -1) {
        perror("Error bind TCP"); return -1;
    }

    if (listen(sock_tcp, 10) == -1) {
        perror("Error listen"); return -1;
    }

    /* ---- socket UDP para gestores ---- */
    int sock_udp = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_udp == -1) { perror("Error socket UDP"); return -1; }

    /* direcciones de los gestores */
    struct sockaddr_in gestor1, gestor2;

    gestor1.sin_family = AF_INET;
    gestor1.sin_port   = htons(puerto_g1);
    if (inet_aton(ip_g1, &gestor1.sin_addr) == 0) {
        fprintf(stderr, "IP gestor1 invalida: %s\n", ip_g1); return -1;
    }
    memset(&gestor1.sin_zero, '\0', 8);

    gestor2.sin_family = AF_INET;
    gestor2.sin_port   = htons(puerto_g2);
    if (inet_aton(ip_g2, &gestor2.sin_addr) == 0) {
        fprintf(stderr, "IP gestor2 invalida: %s\n", ip_g2); return -1;
    }
    memset(&gestor2.sin_zero, '\0', 8);

    printf("[Broker] TCP escuchando en puerto %d\n", puerto_tcp);
    printf("[Broker] Gestor1 -> %s:%d | Gestor2 -> %s:%d\n",
           ip_g1, puerto_g1, ip_g2, puerto_g2);

    /* ---- bucle principal: aceptar clientes secuencialmente ---- */
    while (1) {
        struct sockaddr_in cliente;
        socklen_t tam = sizeof(struct sockaddr);

        printf("[Broker] Esperando conexiones TCP...\n");
        int fd_cli = accept(sock_tcp, (struct sockaddr *)&cliente, &tam);
        if (fd_cli == -1) {
            perror("Error accept"); continue;
        }

        atender_cliente(fd_cli, &cliente, sock_udp, &gestor1, &gestor2);
    }

    close(sock_tcp);
    close(sock_udp);
    return 0;
}