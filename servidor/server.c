/* 
 Servidor TCP Echo
 Acepta un cliente a la vez, devuelve cada mensaje con contador.
*/

// Imports
#include <stdio.h>    // input - output: printf, perror, snprintf
#include <stdlib.h>   // exit()
#include <string.h>   // memset, strlen
#include <unistd.h>   // read, write, close (POSIX)
#include <errno.h>    // perror usa esto para mostrar errores
#include <signal.h>   // signal, sig_atomic_t para Ctrl+C

// <winsock2.h>/<sys/socket.h> para trabajar con sockets
#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif

#define PUERTO 49152 // Puerto del sv
#define BUFFER_SIZE 1024
#define MENSAJE_BIENVENIDA "=== Bienvenido al servidor ===\nEscribe algo y te lo devolvere con el numero de mensaje.\n"

#ifdef _WIN32
// Funcion para inicializar Winsock en Windows
// En POSIX no hace nada, los sockets ya estan disponibles
int inicializar_winsock(void) {
    WSADATA wsa_data;
    int resultado = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (resultado != 0) {
        fprintf(stderr, "[SERVIDOR] ERROR: WSAStartup fallo con codigo %d\n", resultado);
        return -1;
    }
    printf("[SERVIDOR] Winsock inicializado correctamente.\n");
    return 0;
}

// Funcion para limpiar Winsock al salir (solo Windows)
void limpiar_winsock(void) {
    WSACleanup();
    printf("[SERVIDOR] Winsock limpiado.\n");
}
#endif

// Bandera para cierre ordenado con Ctrl+C (volatile + sig_atomic_t por seguridad en signals)
static volatile sig_atomic_t servidor_activo = 1;

// Handler de SIGINT: cambia la bandera para que el loop principal cierre limpio
void handler_sigint(int signo) {
    printf("\n[SERVIDOR] Senal SIGINT recibida (Ctrl+C). Cerrando servidor...\n");
    servidor_activo = 0;
}

int main(void) {
    int socket_servidor, socket_cliente;
    socklen_t longitud_direccion;
    struct sockaddr_in direccion_servidor, direccion_cliente;
    char buffer_entrada[BUFFER_SIZE];
    char buffer_salida[BUFFER_SIZE];
    int contador_mensajes = 0;

    // Registrar handler de Ctrl+C
    signal(SIGINT, handler_sigint);
    printf("[SERVIDOR] Manejador de senal Ctrl+C registrado.\n");

#ifdef _WIN32
    // Inicializar Winsock (necesario en Windows)
    if (inicializar_winsock() != 0) {
        exit(EXIT_FAILURE);
    }
#endif

    // Crear socket TCP IPv4
    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        perror("[SERVIDOR] ERROR: No se pudo crear el socket");
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] Socket creado exitosamente (fd=%d).\n", socket_servidor);

    // Permitir reusar puerto inmediatamente (evita "Address already in use")
    int opcion = 1;
    if (setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, (const char *)&opcion, sizeof(opcion)) < 0) {
        perror("[SERVIDOR] ERROR: No se pudo configurar SO_REUSEADDR");
        close(socket_servidor);
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] SO_REUSEADDR activado.\n");

    // Configurar direccion: todas las interfaces, puerto 49152
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    direccion_servidor.sin_family = AF_INET;
    direccion_servidor.sin_addr.s_addr = INADDR_ANY;
    direccion_servidor.sin_port = htons(PUERTO); // host -> network (byte order)

    // Bind: asociar socket a IP:puerto
    if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("[SERVIDOR] ERROR: No se pudo asociar (bind) el socket");
        close(socket_servidor);
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] Socket asociado a INADDR_ANY:%d exitosamente.\n", PUERTO);

    // Listen: modo escucha con cola de 5
    if (listen(socket_servidor, 5) < 0) {
        perror("[SERVIDOR] ERROR: No se pudo poner el socket a escuchar");
        close(socket_servidor);
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] Socket en modo escucha. Cola de backlog: 5 conexiones.\n");
    printf("[SERVIDOR] Esperando conexiones en el puerto %d...\n\n", PUERTO);

    // Loop principal: acepta clientes hasta Ctrl+C
    while (servidor_activo) {

        // Accept: bloquea hasta que un cliente se conecte
        longitud_direccion = sizeof(direccion_cliente);
        socket_cliente = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &longitud_direccion);

        if (socket_cliente < 0) {
            perror("[SERVIDOR] ERROR: No se pudo aceptar la conexion");
            continue;
        }

        char ip_cliente[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &direccion_cliente.sin_addr, ip_cliente, INET_ADDRSTRLEN); // binario -> "192.168.1.50"
    printf("[SERVIDOR] Cliente conectado! IP: %s | Puerto: %d\n",
           ip_cliente, ntohs(direccion_cliente.sin_port)); // ntohs: network -> host (byte order)

        // Enviar bienvenida
        send(socket_cliente, MENSAJE_BIENVENIDA, strlen(MENSAJE_BIENVENIDA), 0);

        // Loop de echo: recibir -> responder -> repetir
        int bytes_recibidos;
        while (1) {
            // RECIBIR
            memset(buffer_entrada, 0, BUFFER_SIZE);
            bytes_recibidos = recv(socket_cliente, buffer_entrada, BUFFER_SIZE - 1, 0);

            // 0 = cliente cerro conexion, <0 = error
            if (bytes_recibidos <= 0) {
                if (bytes_recibidos == 0)
                    printf("[SERVIDOR] El cliente se desconecto.\n");
                else
                    perror("[SERVIDOR] ERROR: Fallo al leer datos del cliente");
                break;
            }

            buffer_entrada[bytes_recibidos] = '\0';
            contador_mensajes++;
            printf("[SERVIDOR] Mensaje #%d recibido: \"%s\"", contador_mensajes, buffer_entrada);

            // RESPONDER
            // Enviar echo con contador
            snprintf(buffer_salida, BUFFER_SIZE, "Echo [%d]: %s", contador_mensajes, buffer_entrada);
            send(socket_cliente, buffer_salida, strlen(buffer_salida), 0);
            printf("[SERVIDOR] Echo enviado: \"%s\"", buffer_salida);
        }

        // Cerrar conexion con este cliente (el socket servidor sigue vivo)
        close(socket_cliente);
        printf("[SERVIDOR] Conexion cerrada. Volviendo a escuchar...\n\n");
    }

    // Cierre ordenado del servidor
    close(socket_servidor);
    printf("[SERVIDOR] Socket del servidor cerrado. Hasta la proxima!\n");

#ifdef _WIN32
    // Limpiar Winsock (solo en Windows)
    limpiar_winsock();
#endif

    return 0;
    // Guia completa de explicaciones: ver SERVER_GUIA.md
}
