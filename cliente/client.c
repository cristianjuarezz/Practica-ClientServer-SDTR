/*
 Cliente TCP Echo
 Se conecta al servidor, envia mensajes y recibe respuestas con contador.
*/

// Imports
#include <stdio.h>    // input - output: printf, perror, fgets, stdin
#include <stdlib.h>   // exit()
#include <string.h>   // memset, strlen, strcspn
#include <unistd.h>   // close (POSIX)
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

#define PUERTO 49152        // Puerto del servidor al que nos conectamos
#define BUFFER_SIZE 1024    // Tamano del buffer para enviar/recibir datos
#define DIRECCION_SERVIDOR "127.0.0.1"  // IP del servidor (localhost)

// Bandera para cierre ordenado con Ctrl+C (volatile + sig_atomic_t por seguridad en signals)
static volatile sig_atomic_t cliente_activo = 1;

// Handler de SIGINT: cambia la bandera para que el loop principal cierre limpio
void handler_sigint(int signo) {
    (void)signo; // Evitar warning de parametro no usado (requerido por la API de signals)
    printf("\n[CLIENTE] Senal SIGINT recibida (Ctrl+C). Cerrando conexion...\n");
    cliente_activo = 0;
}

#ifdef _WIN32
// Funcion para inicializar Winsock en Windows
// En POSIX no hace nada, los sockets ya estan disponibles
int inicializar_winsock(void) {
    WSADATA wsa_data;
    int resultado = WSAStartup(MAKEWORD(2, 2), &wsa_data);
    if (resultado != 0) {
        fprintf(stderr, "[CLIENTE] ERROR: WSAStartup fallo con codigo %d\n", resultado);
        return -1;
    }
    printf("[CLIENTE] Winsock inicializado correctamente.\n");
    return 0;
}

// Funcion para limpiar Winsock al salir (solo Windows)
void limpiar_winsock(void) {
    WSACleanup();
    printf("[CLIENTE] Winsock limpiado.\n");
}
#endif

int main(void) {
    int socket_cliente;
    struct sockaddr_in direccion_servidor;
    char buffer_entrada[BUFFER_SIZE];   // Buffer para datos recibidos del servidor
    char buffer_salida[BUFFER_SIZE];    // Buffer para datos enviados al servidor

    // Registrar handler de Ctrl+C
    signal(SIGINT, handler_sigint);
    printf("[CLIENTE] Manejador de senal Ctrl+C registrado.\n");

#ifdef _WIN32
    // Inicializar Winsock (necesario en Windows)
    if (inicializar_winsock() != 0) {
        exit(EXIT_FAILURE);
    }
#endif

    // Crear socket TCP IPv4
    // AF_INET = IPv4, SOCK_STREAM = TCP, 0 = protocolo por defecto
    socket_cliente = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_cliente < 0) {
        perror("[CLIENTE] ERROR: No se pudo crear el socket");
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }
    printf("[CLIENTE] Socket creado exitosamente (fd=%d).\n", socket_cliente);

    // Configurar direccion del servidor
    // Nos vamos a conectar a 127.0.0.1:49152 (localhost)
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    direccion_servidor.sin_family = AF_INET;             // IPv4
    direccion_servidor.sin_port = htons(PUERTO);         // Puerto (host -> network byte order)

    // Convertir IP de string a binario
    // inet_pton: "127.0.0.1" -> formato binario para la struct
    if (inet_pton(AF_INET, DIRECCION_SERVIDOR, &direccion_servidor.sin_addr) <= 0) {
        perror("[CLIENTE] ERROR: Direccion invalida o no soportada");
        close(socket_cliente);
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }
    printf("[CLIENTE] Direccion del servidor configurada: %s:%d\n", DIRECCION_SERVIDOR, PUERTO);

    // Connect: establecer conexion con el servidor
    // Si el servidor no esta corriendo, fallara con "Connection refused"
    printf("[CLIENTE] Conectando al servidor...\n");
    if (connect(socket_cliente, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("[CLIENTE] ERROR: No se pudo conectar al servidor");
        close(socket_cliente);
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }
    printf("[CLIENTE] Conexion establecida con el servidor %s:%d\n\n", DIRECCION_SERVIDOR, PUERTO);

    // Recibir mensaje de bienvenida del servidor
    // El servidor lo envia inmediatamente despues de aceptar la conexion
    memset(buffer_entrada, 0, BUFFER_SIZE);
    int bytes_recibidos = recv(socket_cliente, buffer_entrada, BUFFER_SIZE - 1, 0);
    if (bytes_recibidos > 0) {
        buffer_entrada[bytes_recibidos] = '\0';
        printf("[CLIENTE] --- Mensaje del servidor ---\n");
        printf("%s", buffer_entrada);
        printf("[CLIENTE] ------------------------------\n\n");
    } else if (bytes_recibidos == 0) {
        printf("[CLIENTE] El servidor cerro la conexion inmediatamente.\n");
        close(socket_cliente);
#ifdef _WIN32
        limpiar_winsock();
#endif
        return 0;
    } else {
        perror("[CLIENTE] ERROR: No se pudo recibir el mensaje de bienvenida");
        close(socket_cliente);
#ifdef _WIN32
        limpiar_winsock();
#endif
        exit(EXIT_FAILURE);
    }

    // Loop principal: leer input del usuario, enviar al servidor, recibir respuesta
    printf("[CLIENTE] Escribe mensajes para enviar al servidor.\n");
    printf("[CLIENTE] Escribe 'salir' o presiona Ctrl+D (Linux) / Ctrl+Z (Windows) para terminar.\n\n");

    while (cliente_activo) {
        // Mostrar prompt
        printf("> ");
        fflush(stdout);  // Forzar que el prompt se muestre antes de esperar input

        // Leer linea del usuario desde stdin
        // fgets lee hasta BUFFER_SIZE-1 o hasta salto de linea
        if (fgets(buffer_salida, BUFFER_SIZE, stdin) == NULL) {
            // EOF: el usuario presiono Ctrl+D (Linux) o Ctrl+Z (Windows)
            // o fin de archivo de algun otro origen
            printf("\n[CLIENTE] Fin de entrada (EOF). Cerrando conexion...\n");
            break;
        }

        // Eliminar el salto de linea (\n) que deja fgets al final
        // strcspn encuentra la posicion del primer '\n', lo reemplazamos por '\0'
        buffer_salida[strcspn(buffer_salida, "\n")] = '\0';

        // Verificar si el usuario quiere salir
        if (strcmp(buffer_salida, "salir") == 0) {
            printf("[CLIENTE] Comando 'salir' detectado. Cerrando conexion...\n");
            break;
        }

        // No enviar lineas vacias (solo Enter sin texto)
        if (strlen(buffer_salida) == 0) {
            continue;
        }

        // Enviar mensaje al servidor
        // send() retorna el numero de bytes enviados, o -1 si hay error
        int bytes_enviados = send(socket_cliente, buffer_salida, strlen(buffer_salida), 0);
        if (bytes_enviados < 0) {
            perror("[CLIENTE] ERROR: No se pudo enviar el mensaje");
            break;
        }
        printf("[CLIENTE] Enviado (%d bytes): \"%s\"\n", bytes_enviados, buffer_salida);

        // Recibir respuesta del servidor (echo con contador)
        // recv() bloquea hasta que haya datos disponibles
        memset(buffer_entrada, 0, BUFFER_SIZE);
        bytes_recibidos = recv(socket_cliente, buffer_entrada, BUFFER_SIZE - 1, 0);

        if (bytes_recibidos > 0) {
            buffer_entrada[bytes_recibidos] = '\0';
            printf("[CLIENTE] Respuesta: \"%s\"\n", buffer_entrada);
        } else if (bytes_recibidos == 0) {
            // El servidor cerro la conexion
            printf("[CLIENTE] El servidor cerro la conexion.\n");
            break;
        } else {
            perror("[CLIENTE] ERROR: Fallo al recibir respuesta del servidor");
            break;
        }
    }

    // Cierre ordenado: cerrar socket y limpiar recursos
    printf("[CLIENTE] Cerrando conexion con el servidor...\n");
    close(socket_cliente);
    printf("[CLIENTE] Socket cerrado.\n");

#ifdef _WIN32
    // Limpiar Winsock (solo en Windows)
    limpiar_winsock();
#endif

    printf("[CLIENTE] Cliente terminado. Hasta la proxima!\n");
    return 0;
}
