# Guia Completa: Servidor TCP Echo - Fase 1

**Practica:** Sistemas Distribuidos y Tiempo Real (SDTR)
**Servidor TCP** que acepta UN cliente a la vez y le devuelve (echo) todo lo que el cliente le envia, ademas de contar cuantos mensajes ha recibido desde que arranco.

> Este es el "Fase 1" del enunciado. Solo atiende un cliente a la vez, sin hilos (pthreads), sin broadcast. La Fase 2 sera concurrente.

---

## Compilacion y Ejecucion

```bash
# Compilar (Linux/Mac/WSL):
gcc -o server server.c

# Ejecutar:
./server

# Probar (desde otra terminal):
nc localhost 49152
```

---

## Inclusion de Cabeceras (Headers)

| Header | Uso |
|---|---|
| `stdio.h` | Funciones de entrada/salida estandar: `printf()`, `snprintf()`. |
| `stdlib.h` | Funciones de utilidad general: `exit()`, `atoi()`. |
| `string.h` | Manipulacion de cadenas y buffers: `memset()`, `strlen()`. |
| `unistd.h` | API POSIX: `read()`, `write()`, `close()`. En Windows (MinGW) tambien disponible. |
| `errno.h` | Variable global `errno` con el codigo del ultimo error en una llamada al sistema. |
| `signal.h` | Manejo de senales del SO: `signal(SIGINT, ...)`. |

### Headers de Sockets (dependen del SO)

**Windows** (`#ifdef _WIN32`):
- `winsock2.h` / `ws2tcpip.h` — API de sockets Winsock. Se linkea con `-lws2_32`.
- Se define `typedef int socklen_t` por compatibilidad.

**Linux/Mac/WSL** (else):
- `sys/socket.h` — Funciones principales: `socket()`, `bind()`, `listen()`, `accept()`, `send()`, `recv()`.
- `netinet/in.h` — Estructuras de direcciones: `sockaddr_in`, `INADDR_ANY`, `htons()`.
- `arpa/inet.h` — Conversion de direcciones: `inet_ntop()`, `inet_pton()`.

---

## Constantes

- **`PUERTO 49152`** — Puerto en el rango "not well known" (49152-65535), puertos dinamicos/privados. Un puerto es como el "numero de departamento" dentro de una direccion IP.
- **`BUFFER_SIZE 1024`** — 1 KB de memoria temporal para datos de entrada/salida.
- **`MENSAJE_BIENVENIDA`** — Mensaje enviado al cliente al conectarse.

---

## Variable Global: `servidor_activo`

```c
static volatile sig_atomic_t servidor_activo = 1;
```

- `volatile` — Le dice al compilador que puede cambiar en cualquier momento (por una senal del SO), asi que no optimice su lectura.
- `sig_atomic_t` — Garantiza que leer/escribir sea atomico (no se interrumpe a mitad), seguro dentro de handlers de senales.

---

## Funcion `handler_sigint`

Se ejecuta cuando el usuario presiona Ctrl+C (senal SIGINT). Sin ella, el SO termina el programa de golpe sin liberar el socket, causando "Address already in use" al re-ejecutar. Simplemente cambia la bandera `servidor_activo = 0` para que el loop principal cierre todo limpiamente.

---

## Flujo de la Funcion `main()`

El flujo tipico de un servidor TCP:

```
socket() -> bind() -> listen() -> accept() -> read()/write() -> close()
```

### Paso 1: Senales
`signal(SIGINT, handler_sigint)` — Registra el handler para Ctrl+C.

### Paso 2: Crear el Socket

```c
socket(AF_INET, SOCK_STREAM, 0);
```

| Parametro | Valor | Significado |
|---|---|---|
| `domain` | `AF_INET` | Familia de direcciones IPv4. |
| `type` | `SOCK_STREAM` | Socket TCP (orientado a conexion, confiable, con orden garantizado). Como una llamada telefonica. `SOCK_DGRAM` seria UDP (carta sin confirmacion). |
| `protocol` | `0` | Protocolo automatico. El OS elige TCP para el tipo y familia dados. |

**Retorna:** file descriptor (entero >= 0) o -1 si falla.

> Un "file descriptor" es un numero entero que el OS usa para identificar cada recurso abierto. En Unix todo se maneja como archivos.

### Paso 3: SO_REUSEADDR

```c
setsockopt(socket_servidor, SOL_SOCKET, SO_REUSEADDR, &opcion, sizeof(opcion));
```

| Parametro | Significado |
|---|---|
| `socket_servidor` | El socket a configurar. |
| `SOL_SOCKET` | Nivel de opciones: se aplica al socket mismo. |
| `SO_REUSEADDR` | Opcion a activar. Permite reusar el puerto inmediatamente. |
| `&opcion` | Puntero al valor (1 = activar). |
| `sizeof(opcion)` | Tamano del valor. |

**Por que:** Sin esto, si matas el servidor con Ctrl+C y lo re-ejecutas rapido, el OS puede decir "Address already in use" porque el puerto esta en estado TIME_WAIT.

### Paso 4: Configurar Direccion (`sockaddr_in`)

```c
memset(&direccion_servidor, 0, sizeof(direccion_servidor));
direccion_servidor.sin_family = AF_INET;          // IPv4
direccion_servidor.sin_addr.s_addr = INADDR_ANY;  // Todas las interfaces
direccion_servidor.sin_port = htons(PUERTO);       // Puerto en formato red
```

- `INADDR_ANY` — Escucha en TODAS las interfaces de red. Mas flexible que `inet_addr("127.0.0.1")`.
- `htons()` — "Host TO Network Short". Convierte el puerto de formato de maquina (host) al formato de la red (big-endian). Sin esto, el puerto se interpretaria mal.

### Paso 5: Bind

```c
bind(socket_servidor, (struct sockaddr *)&direccion_servidor, sizeof(direccion_servidor));
```

Amarra el socket a una direccion IP y puerto. Sin bind(), el socket existe pero no tiene direccion asignada, como un telefono sin numero.

### Paso 6: Listen

```c
listen(socket_servidor, 5);
```

Transforma el socket activo en pasivo (espera clientes, no inicia conexiones).

| Parametro | Significado |
|---|---|
| `socket_servidor` | El socket a poner a escuchar. |
| `5` | Cola de backlog: maximo de conexiones pendientes antes de rechazar. En Linux el real suele ser 128, pero 5 es suficiente para una practica. |

### Paso 7: Accept (BLOQUEANTE)

```c
socket_cliente = accept(socket_servidor, (struct sockaddr *)&direccion_cliente, &longitud_direccion);
```

El programa se detiene hasta que un cliente intente conectarse. Crea un **nuevo socket dedicado** exclusivamente a esa conexion. El socket original (`socket_servidor`) sigue escuchando para conexiones futuras.

> En la Fase 1 solo atendemos UN cliente a la vez: mientras estamos en el echo, el servidor NO acepta nuevas conexiones.

- `inet_ntop()` — "Network TO Presentation". Convierte IP de binario a legible ("192.168.1.50").
- `ntons()` — "Network TO Host Short". Convierte el puerto de formato red a formato de maquina.

### Paso 8: Enviar Bienvenida

```c
send(socket_cliente, MENSAJE_BIENVENIDA, strlen(MENSAJE_BIENVENIDA), 0);
```

| Parametro | Significado |
|---|---|
| `socket_cliente` | Socket dedicado a este cliente. |
| `MENSAJE_BIENVENIDA` | Dato a enviar. |
| `strlen(...)` | Cantidad de bytes a enviar. |
| `0` | Flags (0 = comportamiento normal). |

### Paso 9: Read (BLOQUEANTE)

```c
bytes_recibidos = read(socket_cliente, buffer_entrada, BUFFER_SIZE - 1);
```

| Parametro | Significado |
|---|---|
| `socket_cliente` | De donde leer. |
| `buffer_entrada` | Donde guardar lo leido. |
| `BUFFER_SIZE - 1` | Maximo de bytes a leer (reservamos 1 byte para `\0`). |

**Retorno:**
- `> 0` — Bytes leidos (el cliente envio datos).
- `= 0` — El cliente cerro la conexion (envio TCP FIN).
- `< 0` — Error de lectura.

### Paso 10: Echo con Contador

```c
snprintf(buffer_salida, BUFFER_SIZE, "Echo [%d]: %s", contador_mensajes, buffer_entrada);
send(socket_cliente, buffer_salida, strlen(buffer_salida), 0);
```

`snprintf()` es como `printf()` pero escribe en un buffer. El segundo parametro es el tamano maximo para evitar buffer overflow.

### Paso 11: Cerrar Conexion con Cliente

```c
close(socket_cliente);
```

Libera los recursos del OS para esa conexion. **NO** se cierra `socket_servidor` porque el servidor sigue vivo.

> En Windows se llama `closesocket()`, pero `close()` funciona en MinGW/WSL.

### Paso 12: Cierre Ordenado del Servidor

```c
close(socket_servidor);
```

Cuando llega Ctrl+C, el loop principal termina y cerramos el socket del servidor, liberando el puerto.

---

## Resumen del Flujo

```
 1. socket()       -> Creamos el "tubo" de comunicacion
 2. setsockopt()   -> Activamos SO_REUSEADDR para reusar el puerto
 3. bind()         -> Le decimos al OS en que IP:PUERTO escuchar
 4. listen()       -> Ponemos el socket en modo "esperando clientes"
 5. accept()       -> Esperamos a que un cliente se conecte (bloqueante)
 6. send()         -> Le enviamos el mensaje de bienvenida
 7. read()         -> Leemos lo que el cliente envia (bloqueante)
 8. send()         -> Le devolvemos el echo con contador
 9. (repetir 7-8 hasta que el cliente se desconecte)
10. close()        -> Cerramos la conexion con el cliente
11. (volver al paso 5 para aceptar otro cliente)
12. close()        -> Cerramos el servidor (cuando llega Ctrl+C)
```

---

## Comandos para Probar

**Terminal 1 (servidor):**
```bash
gcc -o server server.c
./server
```

**Terminal 2 (cliente):**
```
nc localhost 49152
Hola, esto es una prueba
Echo [1]: Hola, esto es una prueba
```
