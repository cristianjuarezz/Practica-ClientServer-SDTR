# Sistemas Distribuidos - Cliente/Servidor

Práctica de comunicación cliente/servidor en C utilizando sockets TCP bajo estándares POSIX.

## Estructura

```
Practica-ClientServer-SDTR/
├── servidor/
│   └── server.c
├── cliente/
│   └── client.c
├── cliente-servidor.md    # Enunciado de la práctica
└── README.md
```

## Fases

1. **Fase 1** - Comunicación básica echo (cliente-servidor)
2. **Fase 2** - Sistema de mensajería broadcast con pthreads
3. **Fase 3** - Análisis de red con Wireshark

## Compilación

### Linux / macOS / WSL

```bash
# Servidor
gcc servidor/server.c -o server -lpthread

# Cliente
gcc cliente/client.c -o client
```

### Windows (MinGW)

```bash
# Servidor (requiere -lws2_32 para Winsock)
gcc -o server.exe servidor/server.c -lws2_32

# Cliente
gcc -o client.exe cliente/client.c -lws2_32
```

## Ejecución

**Terminal 1 — Servidor:**

```bash
# Linux/macOS/WSL
./server

# Windows
.\server.exe
```

**Terminal 2 — Cliente:**

```bash
# Linux/macOS/WSL
./client

# Windows
.\client.exe
```

Escribí mensajes y presioná Enter. Escribí `salir` o Ctrl+D (Linux) / Ctrl+Z (Windows) para desconectarte.

## Herramientas instaladas (Windows)

| Herramienta | Paquete | Versión | Instalar |
|---|---|---|---|
| gcc, make, gdb, g++ | WinLibs (POSIX, UCRT) | 16.1.0 | `winget install BrechtSanders.WinLibs.POSIX.UCRT` |
| ncat (netcat) | Nmap | 7.80 | `winget install Insecure.Nmap` |

## Requisitos

- GCC
- Linux, macOS, WSL o Windows (con MinGW)
- Puerto superior a 1024
- Windows: Winsock (`-lws2_32`) y cast en `setsockopt` (ya aplicado en el código)
