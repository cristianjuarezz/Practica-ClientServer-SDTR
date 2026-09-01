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

```bash
# Servidor
gcc servidor/server.c -o server -lpthread

# Cliente
gcc cliente/client.c -o client
```

## Requisitos

- GCC
- Linux (servidor), Linux o Windows (cliente)
- Puerto superior a 1024
