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
## ⚙️ Configuración Crítica del Servidor vsftpd

La configuración del servidor vsftpd fue clave para el éxito de las pruebas de concurrencia y las operaciones de escritura. Sin estos ajustes específicos, las transferencias fallarían con errores 550 (Permission Denied) o problemas de conexión en modo pasivo.

### 🔑 Parámetros Esenciales Implementados

**Habilitación de usuarios locales y escritura:**
Se configuró el acceso para usuarios del sistema mediante `local_enable=YES` y se habilitaron los permisos de escritura global con `write_enable=YES`. La máscara de permisos se estableció en `local_umask=022`, lo que permite crear archivos con permisos `rw-r--r--` (644) y directorios con `rwxr-xr-x` (755).

**Solución al problema de chroot:**
Un punto crítico que resolvió múltiples problemas de acceso fue la directiva `allow_writeable_chroot=YES`. Esta configuración es absolutamente necesaria cuando los usuarios están "enjaulados" (chrooted) en su directorio personal. Sin esta directiva, vsftpd rechaza por seguridad cualquier intento de escritura en directorios con permisos de escritura dentro del chroot, generando errores 550.

**Configuración del modo pasivo:**
Para facilitar las conexiones de datos a través del firewall local y evitar conflictos de puertos, se configuró explícitamente un rango de puertos estrecho mediante `pasv_min_port=40000` y `pasv_max_port=40100`. Esto permite que el administrador del sistema abra solo este rango específico en el firewall, mejorando la seguridad. La directiva `pasv_address=127.0.0.1` asegura que el servidor anuncie la dirección correcta en las respuestas PASV.

### ✅ Beneficios de esta Configuración

- ✅ **Escritura funcional**: Los usuarios pueden subir, renombrar y eliminar archivos sin errores 550
- ✅ **Seguridad mediante chroot**: Los usuarios permanecen confinados en su directorio sin comprometer la funcionalidad
- ✅ **Modo pasivo predecible**: El rango de puertos estrecho facilita la configuración de firewall y NAT
- ✅ **Compatibilidad con concurrencia**: Múltiples conexiones simultáneas funcionan sin conflictos de puerto
- ✅ **Trazabilidad**: Los logs detallados (`xferlog_enable=YES`) permiten auditar todas las transferencias

### ⚠️ Consideraciones Importantes

**Estrategia "una sesión por transferencia":**
Como conclusión del diseño implementado, la estrategia de abrir una sesión FTP completa (LOGIN + PASV/PORT + transferencia + QUIT) por cada proceso hijo demostró ser efectiva y robusta. Aunque genera overhead adicional por las múltiples autenticaciones, garantiza que:
- Cada transferencia sea completamente independiente
- No haya condiciones de carrera en el socket de control
- Los fallos se aíslen sin afectar otras operaciones

**Gestión de procesos zombie:**
Esta arquitectura requiere una gestión cuidadosa de la limpieza de procesos hijos para evitar la acumulación de procesos "zombie". El código implementa `reap_children()` con llamadas no bloqueantes a `waitpid(-1, &status, WNOHANG)` en cada iteración del loop principal, asegurando que los procesos terminados sean recolectados inmediatamente.

---

## 📦 Requisitos Previos

### Software Necesario
```bash
# Compilador GCC
sudo apt-get install gcc make

# Servidor FTP (para pruebas locales)
sudo apt-get install vsftpd

## 🚀Ejecución

Para iniciar el cliente, usa la siguiente sintaxis en la terminal,en donde el numero de puerto es opcional:

```bash
./TCPftp <HOST> [PUERTO]

