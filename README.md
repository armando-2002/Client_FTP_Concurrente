# Cliente FTP Concurrente (TCPftp)

Este proyecto es un **Cliente FTP** diseñado en C que soporta **transferencias concurrentes**. A diferencia de un cliente FTP básico, este programa utiliza procesos hijos (`fork`) para manejar subidas y bajadas de archivos en segundo plano, permitiendo que el usuario siga enviando comandos sin esperar a que termine la transferencia actual.

##  Características Principales
* **Concurrencia:** Puedes iniciar múltiples descargas (`cget`) o subidas (`cput`) simultáneamente.
* **Modos:** Soporta modo Activo (PORT) y Pasivo (PASV).
* **Navegación:** Comandos estándar para moverse por los directorios del servidor (`cd`, `pwd`, `ls`).
* **Gestión:** Crear carpetas, borrar archivos y renombrar.

---

##  Compilación

El proyecto incluye un `Makefile` para facilitar la compilación. Se deben asegurar de tener los archivos auxiliares (`connectTCP.c`, `errexit.c`, etc.) en la misma carpeta.

1.  Abre una terminal en la carpeta del proyecto.
2.  Ejecuta el comando:
    ```bash
    make
    ```
3.  Esto generará el ejecutable llamado: `TCPftp`

> **Nota:** Para limpiar los archivos generados y recompilar desde cero, usa `make clean`.

---

## 🚀Ejecución

Para iniciar el cliente, usa la siguiente sintaxis en la terminal,en donde el numero de puerto es opcional:

```bash
./TCPftp <HOST> [PUERTO]

