/**
 * =============================================================================
 * SERVIDOR TCP ECHO - Fase 1: Comunicación básica cliente-servidor
 * =============================================================================
 * Práctica: Sistemas Distribuidos y Tiempo Real (SDTR)
 * Descripción: Servidor TCP que acepta UN cliente a la vez y le devuelve
 *              (echo) todo lo que el cliente le envía, además de contar
 *              cuántos mensajes ha recibido desde que arrancó.
 *
 * PARA COMPILAR (en Linux/Mac o WSL):
 *   gcc -o server server.c
 *
 * PARA EJECUTAR:
 *   ./server
 *
 * PARA PROBAR (desde otra terminal o con netcat):
 *   nc localhost 49152
 *
 * NOTA: Este es el "Fase 1" del enunciado. Solo atiende un cliente a la vez,
 *       sin hilos (pthreads), sin broadcast. La Fase 2 será concurrente.
 * =============================================================================
 */

/* =========================================================================
 * 1. INCLUSIÓN DE CABECERAS (HEADERS)
 * =========================================================================
 * Aquí incluimos las bibliotecas necesarias. Cada una tiene un rol específico.
 */

#include <stdio.h>
/*  stdio.h  → Funciones de entrada/salida estándar de C.
 *             Necesitamos printf() para mostrar mensajes en pantalla,
 *             y snprintf() para formatear strings. */

#include <stdlib.h>
/*  stdlib.h  → Funciones estándar de utilidad general.
 *              Usamos exit() para terminar el programa ante errores graves,
 *              y atoi() o similar si lo necesitáramos. */

#include <string.h>
/*  string.h  → Funciones para manipular cadenas de caracteres y buffers.
 *              Necesitamos memset() para limpiar buffers y strlen()
 *              para obtener la longitud de un string. */

#include <unistd.h>
/*  unistd.h  → API POSIX para llamadas al sistema en Unix/Linux.
 *              Necesitamos read(), write(), close() y sleep() (si se usa).
 *              En Windows con MinGW también está disponible. */

#include <errno.h>
/*  errno.h   → Variable global errno que contiene el código del último
 *              error ocurrido en una llamada al sistema. Nos ayuda a
 *              diagnosticar qué falló exactamente. */

#include <signal.h>
/*  signal.h  → Para manejar señales del sistema operativo.
 *              Usamos signal(SIGINT, ...) para capturar Ctrl+C y cerrar
 *              el servidor de forma ordenada (liberando el socket). */

/* --- Headers de sockets (dependen del sistema operativo) --- */
#ifdef _WIN32
    /* Si estamos en Windows, necesitamos winsock2.h en lugar de los
     * headers POSIX de sockets. También hay que linkear con -lws2_32. */
    #include <winsock2.h>
    #include <ws2tcpip.h>
    typedef int socklen_t;
#else
    /* En Linux/Mac/WSL usamos los headers POSIX estándar.
     * sys/socket.h  → Funciones principales de sockets: socket(), bind(),
     *                  listen(), accept(), send(), recv().
     *netinet/in.h  → Estructuras de direcciones de internet: sockaddr_in,
     *                 y constantes como INADDR_ANY, htons().
     *arpa/inet.h   → Funciones de conversión: inet_ntop(), inet_pton(). */
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
#endif


/* =========================================================================
 * 2. DEFINICIONES DE CONSTANTES
 * =========================================================================
 * Usar #define en vez de "números mágicos" hace al código más legible
 * y más fácil de modificar en el futuro.
 */

#define PUERTO 49152
/*  Puerto en el que el servidor escuchará conexiones.
 *  El enunciado pide un puerto "not well known" (rango 49152-65535),
 *  que son puertos dinámicos/privados. Nosotros elegimos 49152 porque
 *  es el primero del rango y queda fácil de recordar.
 *
 *  Un puerto es como el "número de departamento" dentro de una dirección
 *  IP. El OS usa el IP para llegar a la máquina y el puerto para llegar
 *  al proceso específico que está escuchando. */

#define BUFFER_SIZE 1024
/*  Tamaño del buffer (buffer = zona de memoria temporal) donde guardamos
 *  los datos que recibimos del cliente o que vamos a enviar.
 *  1024 bytes = 1 KB es un tamaño razonable para un echo simple. */

#define MENSAJE_BIENVENIDA "=== Bienvenido al servidor echo (Fase 1) ===\nEscribe algo y te lo devolveré con el número de mensaje.\n"
/*  Mensaje que le enviamos al cliente apenas se conecta.
 *  Esto le indica al usuario que la conexión fue exitosa. */


/* =========================================================================
 * 3. VARIABLE GLOBAL PARA MANEJO SEGURO DE CIERRE
 * =========================================================================
 * volatile: le dice al compilador que esta variable puede cambiar en
 * cualquier momento (por ejemplo, por una señal del SO), así que no
 * optimice su lectura.
 * sig_atomic_t: garantiza que leer/escribir esta variable sea atómico
 * (no se interrumpe a mitad), lo cual es seguro dentro de handlers de señales.
 */
static volatile sig_atomic_t servidor_activo = 1;

/**
 * handler_sigint - Función que se ejecuta cuando el usuario presiona Ctrl+C.
 *
 * Ctrl+C envía la señal SIGINT al proceso. Si no la manejamos, el SO
 * termina el programa de golpe y no liberamos el socket (puede causar
 * problemas al re-ejecutar el servidor, por ejemplo "Address already in use").
 *
 * Con esta función, simplemente cambiamos la bandera para que el loop
 * principal se entere y cierre todo limpiamente.
 */
void handler_sigint(int signo) {
    printf("\n[SERVIDOR] Señal SIGINT recibida (Ctrl+C). Cerrando servidor...\n");
    servidor_activo = 0;
}


/* =========================================================================
 * 4. FUNCIÓN PRINCIPAL (main)
 * =========================================================================
 * Aquí es donde toda la magia sucede. Seguimos el flujo típico de un
 * servidor TCP:
 *
 *   socket() → bind() → listen() → accept() → read()/write() → close()
 *
 * Cada paso se explica detalladamente en su sección.
 */
int main(void) {

    /* Variables para los file descriptors (descriptores de archivo).
     * En Unix, todo se maneja como archivos: sockets, archivos reales, etc.
     * Un "file descriptor" es simplemente un número entero que el OS usa
     * para identificar cada recurso abierto. */
    int socket_servidor;    /* Socket del servidor (donde escuchamos) */
    int socket_cliente;     /* Socket de la conexión aceptada con un cliente */
    socklen_t longitud_direccion;  /* Tamaño de la estructura sockaddr_in */

    /* Estructuras de dirección del servidor y del cliente.
     * sockaddr_in es la estructura específica para direcciones IPv4.
     * Contiene: familia de direcciones (AF_INET), puerto, y dirección IP. */
    struct sockaddr_in direccion_servidor;
    struct sockaddr_in direccion_cliente;

    /* Buffers para enviar y recibir datos.
     * buffer_entrada: donde guardamos lo que el cliente nos envía.
     * buffer_salida: donde preparamos lo que le vamos a devolver. */
    char buffer_entrada[BUFFER_SIZE];
    char buffer_salida[BUFFER_SIZE];

    /* Contador de mensajes: el enunciado pide que el servidor devuelva
     * al cliente el número de mensaje desde que inició. Empezamos en 1
     * para que el primer mensaje sea "Mensaje #1". */
    int contador_mensajes = 0;


    /* =====================================================================
     * PASO 1: MANEJO DE SEÑALES
     * =====================================================================
     * Registramos una función handler para SIGINT (Ctrl+C).
     * Así podemos cerrar el servidor de forma ordenada.
     */
    signal(SIGINT, handler_sigint);
    printf("[SERVIDOR] Manejador de señal Ctrl+C registrado.\n");


    /* =====================================================================
     * PASO 2: CREAR EL SOCKET
     * =====================================================================
     * socket() crea un endpoint de comunicación (el "tubo" por donde
     * hablará el servidor con los clientes).
     *
     * Parámetros:
     *   AF_INET     → Familia de direcciones IPv4. (También existe AF_INET6
     *                  para IPv6, pero en esta práctica usamos IPv4).
     *   SOCK_STREAM → Tipo de socket TCP (orientado a conexión, confiable,
     *                  con orden garantizado). Es como hacer una llamada
     *                  telefónica: primero marcás, se conecta, y hablás.
     *                  SOCK_DGRAM sería UDP (como mandar una carta sin
     *                  confirmación de llegada).
     *   0           → Protocolo automático. El OS elige el protocolo
     *                  adecuado para el tipo y familia elegidos (TCP).
     *
     * Retorna: un file descriptor (entero ≥ 0) si tuvo éxito, o -1 si falló.
     */
    socket_servidor = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_servidor < 0) {
        perror("[SERVIDOR] ERROR: No se pudo crear el socket");
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] Socket creado exitosamente (fd=%d).\n", socket_servidor);


    /* =====================================================================
     * PASO 3: CONFIGURAR SO_REUSEADDR
     * =====================================================================
     * setsockopt() permite modificar opciones del socket.
     *
     * SO_REUSEADDR le dice al OS: "si este puerto estaba en uso reciente
     * (por ejemplo, porque el servidor anterior cerró hace poco), permitime
     * reusarlo sin tener que esperar".
     *
     * SIN esto, si matás el servidor con Ctrl+C y lo volvés a ejecutar
     * rápido, el OS puede decir "ERROR: Address already in use" porque
     * el puerto está en estado TIME_WAIT (espera por si quedaban paquetes
     * volando en la red). Con SO_REUSEADDR, se reutiliza inmediatamente.
     *
     * Parámetros:
     *   socket_servidor       → El socket a configurar
     *   SOL_SOCKET            → Nivel de opciones: se aplica al socket mismo
     *   SO_REUSEADDR          → La opción que queremos activar
     *   &opcion               → Puntero al valor (1 = activar, 0 = desactivar)
     *   sizeof(opcion)        → Tamaño del valor
     */
    int opcion = 1;
    if (setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opcion, sizeof(opcion)) < 0) {
        perror("[SERVIDOR] ERROR: No se pudo configurar SO_REUSEADDR");
        close(socket_servidor);
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] SO_REUSEADDR activado (podemos reusar el puerto si es necesario).\n");


    /* =====================================================================
     * PASO 4: CONFIGURAR LA DIRECCIÓN DEL SERVIDOR (struct sockaddr_in)
     * =====================================================================
     * Antes de poder usar bind(), necesitamos decirle al socket DÓNDE
     * (IP y puerto) debe escuchar. Para eso llenamos la estructura
     * sockaddr_in con los datos de dirección.
     */
    memset(&direccion_servidor, 0, sizeof(direccion_servidor));
    /* memset: llena toda la estructura con ceros. Es buena práctica porque
     * así nos aseguramos de que no haya basura en los campos que no usamos. */

    direccion_servidor.sin_family = AF_INET;
    /* Familia de direcciones: IPv4 (debe coincidir con lo que pasamos a socket()). */

    direccion_servidor.sin_addr.s_addr = INADDR_ANY;
    /* INADDR_ANY significa: "escucha en TODAS las interfaces de red de
     * esta máquina". Si la máquina tiene IP 192.168.1.100 Y 127.0.0.1,
     * el servidor aceptará conexiones por cualquiera de las dos.
     * Si pusiéramos inet_addr("127.0.0.1"), solo aceptaría conexiones
     * locales (localhost). INADDR_ANY es más flexible y es lo estándar. */

    direccion_servidor.sin_port = htons(PUERTO);
    /* htons = "Host TO Network Short".
     * Convierte el número de puerto del formato de la máquina (host)
     * al formato de la red (network). Las máquinas pueden usar little-endian
     * o big-endian, pero la red siempre usa big-endian.
     * Si no hiciéramos esta conversión, el puerto podría interpretarse
     * mal y el servidor escucharía en el puerto equivocado. */


    /* =====================================================================
     * PASO 5: BIND - ASOCIAR EL SOCKET A UNA DIRECCIÓN Y PUERTO
     * =====================================================================
     * bind() "amarra" el socket a una dirección IP y un puerto específico.
     * Después de esta llamada, el OS sabe que cuando llegue un paquete
     * destinado a la IP:PUERTO de este servidor, debe entregarlo a nuestro
     * socket.
     *
     * Sin bind(), el socket existe pero no tiene dirección asignada, como
     * un teléfono celular sin número.
     */
    if (bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor)) < 0) {
        perror("[SERVIDOR] ERROR: No se pudo asociar (bind) el socket a la dirección");
        close(socket_servidor);
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] Socket asociado a INADDR_ANY:%d exitosamente.\n", PUERTO);


    /* =====================================================================
     * PASO 6: LISTEN - PONER EL SOCKET EN MODO ESCUCHA
     * =====================================================================
     * listen() transforma el socket activo en un "socket pasivo": ya no
     * va a initiate conexiones, sino que ESPERARÁ a que clientes se
     * conecten.
     *
     * Parámetros:
     *   socket_servidor → El socket que pondremos a escuchar
     *   5               → La "cola de backlog": cuántas conexiones pendientes
     *                      puede tener el SO antes de rechazar nuevas.
     *                      En Linux el máximo real suele ser 128, pero 5
     *                      es suficiente para una práctica. Si llegan más
     *                      de 5 clientes simultáneamente, los que queden
     *                      fuera esperarán o recibirán error.
     */
    if (listen(socket_servidor, 5) < 0) {
        perror("[SERVIDOR] ERROR: No se pudo poner el socket a escuchar");
        close(socket_servidor);
        exit(EXIT_FAILURE);
    }
    printf("[SERVIDOR] Socket en modo escucha. Cola de backlog: 5 conexiones.\n");
    printf("[SERVIDOR] Esperando conexiones en el puerto %d...\n\n", PUERTO);


    /* =====================================================================
     * BUCLE PRINCIPAL DEL SERVIDOR
     * =====================================================================
     * El servidor entra en un loop infinito donde:
     *   1. Acepta UNA conexión de un cliente (bloqueante)
     *   2. Le envía un mensaje de bienvenida
     *   3. Recibe y devuelve (echo) cada mensaje del cliente
     *   4. Cuando el cliente se desconecta, vuelve a esperar
     *
     * Este loop se repite hasta que recibamos Ctrl+C (SIGINT).
     */
    while (servidor_activo) {

        /* =================================================================
         * PASO 7: ACCEPT - ACEPTAR UNA CONEXIÓN ENTRANTE
         * =================================================================
         * accept() es BLOQUEANTE: el programa se detiene aquí y no
         * avanza hasta que un cliente intente conectarse. Es como cuando
         * estás esperando que suene el teléfono — no podés hacer otra
         * cosa hasta que suene.
         *
         * Cuando un cliente se conecta, accept() crea UN NUEVO socket
         * dedicado exclusivamente a esa conexión. El socket original
         * (socket_servidor) sigue escuchando para otras conexiones
         * futuras. Es como el mozo del restaurante: el restaurante
         * (socket_servidor) sigue abierto, pero ahora hay un mozo
         * (socket_cliente) atendiendo a ESTE cliente en particular.
         *
         * Retorna: el file descriptor del nuevo socket para comunicarse
         *          con el cliente, o -1 si hubo error.
         *
         * NOTA: En la Fase 1 solo atendemos UN cliente a la vez, así que
         *       mientras estemos dentro de este inner loop (echo), el
         *       servidor NO acepta nuevas conexiones. La Fase 2 con
         *       pthreads resolverá esto.
         */
        longitud_direccion = sizeof(direccion_cliente);
        socket_cliente = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &longitud_direccion);

        if (socket_cliente < 0) {
            /* Si accept() falla, no cerramos todo el servidor. Solo
             * mostramos el error y volvemos a intentar. */
            perror("[SERVIDOR] ERROR: No se pudo aceptar la conexión");
            continue;  /* Vuelve al inicio del while para reintentar */
        }

        /* inet_ntop: "Network TO Presentation". Convierte la dirección IP
         * del cliente de formato binario a formato legible por humanos
         * (ej: "192.168.1.50"). */
        char ip_cliente[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &direccion_cliente.sin_addr, ip_cliente, INET_ADDRSTRLEN);

        printf("[SERVIDOR] ¡Cliente conectado! IP: %s | Puerto: %d\n",
               ip_cliente, ntohs(direccion_cliente.sin_port));
        /* ntohs: "Network TO Host Short". Lo contrario de htons().
         * Convierte el puerto de formato de red a formato de la máquina
         * para que podamos mostrarlo correctamente. */


        /* =============================================================
         * PASO 8: ENVIAR MENSAJE DE BIENVENIDA
         * =============================================================
         * send() envía datos a través del socket conectado.
         *
         * Parámetros:
         *   socket_cliente          → El socket dedicado a este cliente
         *   MENSAJE_BIENVENIDA      → El dato a enviar (un string)
         *   strlen(MENSAJE_BIENVENIDA) → Cuántos bytes enviar
         *   0                       → Flags (0 = comportamiento normal)
         *
         * send() retorna el número de bytes enviados, o -1 si falló.
         * En una práctica como esta, podemos asumir que el envío funciona.
         */
        send(socket_cliente, MENSAJE_BIENVENIDA, strlen(MENSAJE_BIENVENIDA), 0);


        /* =============================================================
         * LOOP DE ECHO - RECIBIR Y DEVOLVER MENSAJES
         * =============================================================
         * Este es el corazón de la Fase 1: el patrón echo.
         *
         * El servidor queda en un loop leyendo lo que el cliente envía
         * y devolviéndolo de vuelta, como un eco. Además, agrega el
         * número de mensaje como pide el enunciado.
         */
        int bytes_recibidos;

        while (1) {

            /* ---------------------------------------------------------
             * PASO 9: READ - RECIBIR DATOS DEL CLIENTE
             * ---------------------------------------------------------
             * read() lee datos desde el socket y los guarda en buffer_entrada.
             * También es BLOQUEANTE: si el cliente no envió nada, el
             * servidor se queda dormido aquí esperando.
             *
             * Parámetros:
             *   socket_cliente     → De dónde leer
             *   buffer_entrada     → Dónde guardar lo leído
             *   BUFFER_SIZE - 1    → Cuántos bytes como máximo leer
             *                        (reservamos 1 byte para el '\0')
             *
             * Retorna:
             *   > 0 → cantidad de bytes leídos (el cliente envió datos)
             *     0 → el cliente cerró la conexión (envió FIN/TCP FIN)
             *   < 0 → ocurrió un error de lectura
             */
            memset(buffer_entrada, 0, BUFFER_SIZE);
            bytes_recibidos = read(socket_cliente, buffer_entrada, BUFFER_SIZE - 1);

            if (bytes_recibidos <= 0) {
                /* Si bytes_recibidos es 0, el cliente cerró la conexión
                 * de forma ordenada (por ejemplo, cerró su terminal).
                 * Si es negativo, hubo un error de red.
                 * En ambos casos, salimos del loop de echo y cerramos
                 * el socket de este cliente. */
                if (bytes_recibidos == 0) {
                    printf("[SERVIDOR] El cliente se desconectó.\n");
                } else {
                    perror("[SERVIDOR] ERROR: Fallo al leer datos del cliente");
                }
                break;  /* Sale del while(1) y cierra la conexión */
            }

            /* Aseguramos que el buffer sea un string válido (terminado en '\0').
             * bytes_recibidos nos dice cuántos bytes llegó, así que ponemos
             * el '\0' justo después del último byte. */
            buffer_entrada[bytes_recibidos] = '\0';

            /* Incrementamos el contador y mostramos en la consola del
             * servidor qué fue lo que recibimos. */
            contador_mensajes++;
            printf("[SERVIDOR] Mensaje #%d recibido: \"%s\"", contador_mensajes, buffer_entrada);
            /* Nota: no ponemos \n al final de printf porque el cliente
             * probablemente ya manda un \n al final de su mensaje. */


            /* ---------------------------------------------------------
             * PASO 10: SEND - ENVIAR EL ECHO CON CONTADOR AL CLIENTE
             * ---------------------------------------------------------
             * Preparamos la respuesta con snprintf() y se la devolvemos.
             * snprintf() es como printf(), pero en vez de imprimir en
             * pantalla, escribe el resultado en un buffer (string).
             * El segundo parámetro es el tamaño máximo del buffer para
             * evitar overflow de buffer (un bug de seguridad grave).
             *
             * La respuesta tiene formato: "Echo [#N]: texto_original"
             * así el cliente ve que el servidor procesó su mensaje.
             */
            snprintf(buffer_salida, BUFFER_SIZE, "Echo [%d]: %s", contador_mensajes, buffer_entrada);

            send(socket_cliente, buffer_salida, strlen(buffer_salida), 0);

            printf("[SERVIDOR] Echo enviado: \"%s\"", buffer_salida);

        }  /* Fin del loop de echo */

        /* =================================================================
         * PASO 11: CERRAR LA CONEXIÓN CON EL CLIENTE
         * =================================================================
         * Cuando el cliente se desconecta, cerramos su socket dedicado.
         * Esto libera los recursos del OS asociados a esta conexión.
         *
         * IMPORTANTE: NO cerramos socket_servidor, porque el servidor
         * sigue vivo y debe seguir escuchando nuevas conexiones.
         *
         * close() en Windows se llama closesocket(), pero para mantener
         * compatibilidad usamos close() que funciona en la mayoría de
         * compiladores C en Windows con POSIX (como MinGW/WSL).
         */
        close(socket_cliente);
        printf("[SERVIDOR] Conexión con el cliente cerrada. Volviendo a escuchar...\n\n");

    }  /* Fin del bucle principal while(servidor_activo) */


    /* =====================================================================
     * PASO 12: CIERRE ORDENADO DEL SERVIDOR
     * =====================================================================
     * Cuando el usuario presiona Ctrl+C, llegamos hasta acá.
     * Cerramos el socket del servidor para liberar el puerto y los
     * recursos del sistema operativo.
     */
    close(socket_servidor);
    printf("[SERVIDOR] Socket del servidor cerrado. ¡Hasta la próxima!\n");

    return 0;
    /* return 0 → indica al SO que el programa terminó correctamente.
     * Un valor distinto de 0 (como return 1) indicaría error. */
}

/*
 * =============================================================================
 * RESUMEN DEL FLUJO (para repasar):
 * =============================================================================
 *
 *   1. socket()      → Creamos el "tubo" de comunicación
 *   2. setsockopt()  → Activamos SO_REUSEADDR para reusar el puerto
 *   3. bind()        → Le decimos al OS en qué IP:PUERTO escuchar
 *   4. listen()      → Ponemos el socket en modo "esperando clientes"
 *   5. accept()      → Esperamos a que un cliente se conecte (bloqueante)
 *   6. send()        → Le enviamos el mensaje de bienvenida
 *   7. read()        → Leemos lo que el cliente envía (bloqueante)
 *   8. send()        → Le devolvemos el echo con contador
 *   9. (repetir 7-8 hasta que el cliente se desconecte)
 *  10. close()       → Cerramos la conexión con el cliente
 *  11. (volver al paso 5 para aceptar otro cliente)
 *  12. close()       → Cerramos el servidor (cuando llega Ctrl+C)
 *
 * =============================================================================
 * COMANDOS PARA COMPILAR Y PROBAR:
 * =============================================================================
 *
 *   Terminal 1 (servidor):
 *     gcc -o server server.c
 *     ./server
 *
 *   Terminal 2 (cliente):
 *     nc localhost 49152
 *     Hola, esto es una prueba    ← escribís esto
 *     Echo [1]: Hola, esto es una prueba    ← esto te responde el server
 *
 * =============================================================================
 */
