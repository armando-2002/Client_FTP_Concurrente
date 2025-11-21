/* TCPftp.c - Cliente FTP con transferencias concurrentes usando fork()
 * Cada proceso hijo abre su propia sesión de control FTP
 * y realiza LOGIN / PASV / RETR o STOR por separado. Esto evita compartir el
 * mismo socket de control entre procesos y respeta el protocolo FTP.
 */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int errno;

int errexit(const char *format, ...);
int connectTCP(const char *host, const char *service);
int passiveTCP(const char *service, int qlen); 

#define LINELEN 512
#define MAX_CONCURRENT 10


pid_t child_pids[MAX_CONCURRENT];
int active_children = 0;
int use_passive_mode = 1;

char host_g[128] = "localhost";
char service_g[16] = "ftp";
char user_g[128] = "";   
char pass_g[128] = "";   
char current_dir_g[256] = ""; 
void sendCmd(int s, char *cmd, char *res) {
    int n;
    n = strlen(cmd);
    cmd[n] = '\r';
    cmd[n+1] = '\n';
    n = write(s, cmd, n+2);
    if (n < 0) {
        perror("write to control socket");
        res[0] = '\0';
        return;
    }
    n = read(s, res, LINELEN);
    if (n < 0) {
        perror("read from control socket");
        res[0] = '\0';
        return;
    }
    res[n] = '\0';
    printf("%s\n", res);
}
int activo_listen(int s_control) {
    int s_listen, p1, p2;
    struct sockaddr_in addr;
    socklen_t len = sizeof(addr);
    char cmd[LINELEN], res[LINELEN+1];

    s_listen = socket(AF_INET, SOCK_STREAM, 0);
    if (s_listen < 0) {
        perror("socket");
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0; 

    if (bind(s_listen, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(s_listen);
        return -1;
    }

    if (listen(s_listen, 1) < 0) {
        perror("listen");
        close(s_listen);
        return -1;
    }

    len = sizeof(addr);
    if (getsockname(s_listen, (struct sockaddr *)&addr, &len) < 0) {
        perror("getsockname");
        close(s_listen);
        return -1;
    }
    

    p1 = ntohs(addr.sin_port) / 256;
    p2 = ntohs(addr.sin_port) % 256;

    /* Enviar PORT */
    snprintf(cmd, sizeof(cmd), "PORT 127,0,0,1,%d,%d", p1, p2);
    sendCmd(s_control, cmd, res);

    if (strncmp(res, "200", 3) != 0) {
        fprintf(stderr, "Error en PORT: %s\n", res);
        close(s_listen);
        return -1;
    }

    return s_listen;
}
int pasivo(int s) {
    int sdata;
    int nport;
    char cmd[LINELEN], res[LINELEN+1], *p;
    char host[64], port[8];
    int h1, h2, h3, h4, p1, p2;

    sprintf(cmd, "PASV");
    sendCmd(s, cmd, res);
    p = strchr(res, '(');
    if (!p) {
        fprintf(stderr, "pasivo: respuesta PASV no contiene '(': %s\n", res);
        return -1;
    }
    if (sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1, &h2, &h3, &h4, &p1, &p2) != 6) {
        fprintf(stderr, "pasivo: no pude parsear la respuesta PASV: %s\n", res);
        return -1;
    }
    snprintf(host, sizeof(host), "%d.%d.%d.%d", h1, h2, h3, h4);
    nport = p1*256 + p2;
    snprintf(port, sizeof(port), "%d", nport);
    sdata = connectTCP(host, port);

    return sdata;
}

int init_control_connection(char *res) {
    int sc;
    char cmd[LINELEN];
    int n;

    sc = connectTCP(host_g, service_g);
    if (sc < 0) return -1;

    n = read(sc, res, LINELEN);
    if (n <= 0) {
        perror("read banner");
        close(sc);
        return -1;
    }
    res[n] = '\0';
    printf("%s\n", res);

    snprintf(cmd, sizeof(cmd), "USER %s", user_g);
    sendCmd(sc, cmd, res);

    snprintf(cmd, sizeof(cmd), "PASS %s", pass_g);
    sendCmd(sc, cmd, res);
    if ((res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0') != 230) {
        fprintf(stderr, "Login en hijo falló: %s\n", res);
        close(sc);
        return -1;
    }

    return sc;
}

void child_download(char *filename) {
    char cmd[LINELEN], res[LINELEN+1], data[LINELEN+1];
    int sdata = -1, slisten = -1, n; 
    FILE *fp;
    int sc;

    printf("[PID %d] Iniciando descarga de: %s\n", getpid(), filename);

    sc = init_control_connection(res);
    if (sc < 0) exit(1);

    if (strlen(current_dir_g) > 0) {
        sprintf(cmd, "CWD %s", current_dir_g);
        sendCmd(sc, cmd, res);
        if (strncmp(res, "250", 3) != 0) {
        fprintf(stderr, "[PID %d] No pude cambiar a directorio: %s\n", getpid(), current_dir_g);
    }
    }

    if (use_passive_mode) {
        sdata = pasivo(sc); 
    } else {
        slisten = activo_listen(sc); 
    }

    if (sdata < 0 && slisten < 0) {
        fprintf(stderr, "Error estableciendo conexión de datos\n");
        exit(1);
    }


    snprintf(cmd, sizeof(cmd), "RETR %s", filename);
    sendCmd(sc, cmd, res);

    if (!use_passive_mode) {
        struct sockaddr_in fsin;
        socklen_t alen = sizeof(fsin);
        sdata = accept(slisten, (struct sockaddr *)&fsin, &alen);
        close(slisten); 
    }

    if (strncmp(res, "550", 3) == 0 || sdata < 0) {
        printf("[PID %d] Error descarga: %s\n", getpid(), res);
        if(sdata >= 0) close(sdata);
        close(sc);
        exit(1);
    }

    fp = fopen(filename, "wb");
    if (fp == NULL) { perror("fopen"); exit(1); }

    printf("[PID %d] Simulando latencia (3s)...\n", getpid());
    sleep(20);

    while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
        fwrite(data, 1, n, fp);
    }

    fclose(fp);
    close(sdata);

    n = read(sc, res, LINELEN);
    if (n > 0) { res[n] = 0; printf("%s\n", res); }

    snprintf(cmd, sizeof(cmd), "QUIT"); sendCmd(sc, cmd, res); close(sc);
    exit(0);
}
void child_upload(char *filename, char *ftp_cmd) {
    char cmd[LINELEN], res[LINELEN+1], data[LINELEN+1];
    int sdata = -1, slisten = -1, n;
    FILE *fp;
    int sc;

    printf("[PID %d] Iniciando subida (%s): %s\n", getpid(), ftp_cmd, filename);
    fp = fopen(filename, "rb");
    if (fp == NULL) { perror("Open local file"); exit(1); }

    sc = init_control_connection(res);
    if (sc < 0) { fclose(fp); exit(1); }

    if (strlen(current_dir_g) > 0) {
        sprintf(cmd, "CWD %s", current_dir_g);
        sendCmd(sc, cmd, res);
        if (strncmp(res, "250", 3) != 0) {
            fprintf(stderr, "[PID %d] ERROR: No pude cambiar a: %s\n", getpid(), current_dir_g);
            fprintf(stderr, "[PID %d] Respuesta: %s\n", getpid(), res);
        }
    }

    if (use_passive_mode) sdata = pasivo(sc);
    else slisten = activo_listen(sc);

    if (sdata < 0 && slisten < 0) { fclose(fp); close(sc); exit(1); }

    if (strcmp(ftp_cmd, "STOU") == 0) {
        snprintf(cmd, sizeof(cmd), "STOU");
    } else {
        
        char *basename = strrchr(filename, '/');
        if (basename) {
            basename++; 
        } else {
            basename = filename; 
        }
        snprintf(cmd, sizeof(cmd), "%s %s", ftp_cmd, basename);
    }
    
    sendCmd(sc, cmd, res);

    if (!use_passive_mode) {
        struct sockaddr_in fsin;
        socklen_t alen = sizeof(fsin);
        sdata = accept(slisten, (struct sockaddr *)&fsin, &alen);
        close(slisten);
    }

    if (res[0] == '4' || res[0] == '5' || sdata < 0) {
        printf("Error subida: %s\n", res);
        fclose(fp); if(sdata>=0) close(sdata); close(sc); exit(1);
    }

    printf("[PID %d] Simulando latencia (2s)...\n", getpid());
    sleep(2);  

    while ((n = fread(data, 1, LINELEN, fp)) > 0) {
        send(sdata, data, n, 0);
    }

    fclose(fp);
    close(sdata);

    read(sc, res, LINELEN); printf("%s\n", res);
    snprintf(cmd, sizeof(cmd), "QUIT"); sendCmd(sc, cmd, res); close(sc);
    exit(0);
}
void add_child(pid_t pid) {
    if (active_children < MAX_CONCURRENT) {
        child_pids[active_children++] = pid;
    }
}

void reap_children() {
    int status;
    pid_t pid;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("[PADRE] Proceso hijo %d terminó\n", pid);

        for (int i = 0; i < active_children; i++) {
            if (child_pids[i] == pid) {
                for (int j = i; j < active_children - 1; j++) {
                    child_pids[j] = child_pids[j+1];
                }
                active_children--;
                break;
            }
        }
    }
}

void wait_all_children() {
    printf("[PADRE] Esperando a %d procesos activos...\n", active_children);
    while (active_children > 0) {
        int status;
        pid_t pid = wait(&status);
        if (pid > 0) {
            printf("[PADRE] Proceso hijo %d terminó\n", pid);
            active_children--;
        }
    }
    printf("[PADRE] Todas las transferencias completadas\n");
}

void ayuda() {
    printf("\n============ CLIENTE FTP CONCURRENTE ============\n\n");
    printf("TRANSFERENCIAS:\n");
    printf("  get/cget <arch>   - Descarga (RETR)\n");
    printf("  put/cput <arch>   - Sube (STOR)\n");
    printf("  appe <arch>       - Sube añadiendo (APPE)\n");
    printf("  stou <arch>       - Sube con nombre único (STOU)\n");
    printf("  mget <a1> <a2>    - Múltiples descargas\n");
    printf("  mput <a1> <a2>    - Múltiples subidas\n\n");
    
    printf("NAVEGACIÓN:\n");
    printf("  dir [ruta]        - Listar con detalles (LIST)\n");
    printf("  nlst [ruta]       - Listar solo nombres (NLST)\n");
    printf("  cd <dir>          - Cambiar directorio (CWD)\n");
    printf("  cdup / ..         - Subir nivel (CDUP)\n");
    printf("  pwd               - Directorio actual (PWD)\n\n");
    
    printf("GESTIÓN ARCHIVOS:\n");
    printf("  mkdir <dir>       - Crear directorio (MKD)\n");
    printf("  rmdir <dir>       - Eliminar directorio (RMD)\n");
    printf("  delete <arch>     - Eliminar archivo (DELE)\n");
    printf("  rename <v> <n>    - Renombrar (RNFR/RNTO)\n\n");
    
    printf("CONFIGURACIÓN:\n");
    printf("  passive           - Modo pasivo (PASV)\n");
    printf("  active            - Modo activo (PORT)\n");
    printf("  type <A|I>        - ASCII o Binario (TYPE)\n");
    printf("  mode <S|B|C>      - Stream/Block/Compress (MODE)\n");
    printf("  stru <F|R|P>      - File/Record/Page (STRU)\n\n");
    
    printf("INFORMACIÓN:\n");
    printf("  syst              - Sistema del servidor (SYST)\n");
    printf("  stat [arch]       - Estado (STAT)\n");
    printf("  noop              - Keep-alive (NOOP)\n\n");
    
    printf("AVANZADO:\n");
    printf("  rest <offset>     - Reiniciar desde punto (REST)\n");
    printf("  allo <bytes>      - Reservar espacio (ALLO)\n");
    printf("  site <params>     - Comandos del servidor (SITE)\n");
    printf("  acct <cuenta>     - Cuenta (ACCT)\n");
    printf("  rein              - Reiniciar sesión (REIN)\n\n");
    
    printf("CONTROL:\n");
    printf("  wait              - Esperar transferencias\n");
    printf("  status            - Ver procesos activos\n");
    printf("  help              - Esta ayuda\n");
    printf("  quit              - Salir (QUIT)\n");
    printf("=================================================\n\n");
}

int main(int argc, char *argv[]) {
    char *host = "localhost";
    char *service = "ftp";
    char cmd[LINELEN], res[LINELEN+1];
    char user[32], *pass, prompt[256], *ucmd, *arg;
    int s, n;
    pid_t pid; 

    switch (argc) {
    case 1: host = "localhost"; break;
    case 3: service = argv[2]; 
    case 2: host = argv[1]; break;
    default: fprintf(stderr, "Uso: TCPftp [host [port]]\n"); exit(1);
    }

    strncpy(host_g, host, sizeof(host_g)-1);
    strncpy(service_g, service, sizeof(service_g)-1);

    s = connectTCP(host, service);
    n = read(s, res, LINELEN);
    res[n] = '\0';
    printf("%s\n", res);
    while (1) {
        printf("Please enter your username: ");
        scanf("%31s", user);
        snprintf(cmd, sizeof(cmd), "USER %s", user);
        sendCmd(s, cmd, res);
        
        pass = getpass("Enter your password: ");
        snprintf(cmd, sizeof(cmd), "PASS %s", pass);
        sendCmd(s, cmd, res);
        if ((res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0') == 230) {
            strncpy(user_g, user, sizeof(user_g)-1);
            strncpy(pass_g, pass, sizeof(pass_g)-1);
            break;
        }
        printf("Login falló.\n");
    }

    fgets(prompt, sizeof(prompt), stdin);
    ayuda();

    while (1) {
        reap_children(); 
        
        printf("ftp (%s)> ", use_passive_mode ? "PASV" : "PORT");
        
        if (fgets(prompt, sizeof(prompt), stdin) == NULL) break;
        prompt[strcspn(prompt, "\n")] = 0;
        ucmd = strtok(prompt, " ");
        if (!ucmd) continue;
        if (strcmp(ucmd, "cget") == 0 || strcmp(ucmd, "get") == 0) {
            arg = strtok(NULL, " ");
            if (arg) {
                pid = fork(); 
        if (pid == 0) { close(s); child_download(arg); }
        else if (pid > 0) add_child(pid);
            } else printf("Uso: cget <archivo>\n");

} else if (strcmp(ucmd, "cput") == 0 || strcmp(ucmd, "put") == 0) {
    arg = strtok(NULL, " ");
    if (arg) {
        pid = fork();
        if (pid == 0) { close(s); child_upload(arg, "STOR"); }  
        else if (pid > 0) add_child(pid);
    } else printf("Uso: cput <archivo>\n");


        } else if (strcmp(ucmd, "appe") == 0) {
            arg = strtok(NULL, " ");
            if (arg) {
               pid = fork();  
        if (pid == 0) { close(s); child_upload(arg, "APPE"); }
        else if (pid > 0) add_child(pid);
            } else printf("Uso: appe <archivo>\n");

       
        } else if (strcmp(ucmd, "stou") == 0) {
            arg = strtok(NULL, " ");
            if (arg) {
                pid = fork();  
        if (pid == 0) { close(s); child_upload(arg, "STOU"); } 
        else if (pid > 0) add_child(pid);
            } else printf("Uso: stou <archivo_local>\n");

       
        } else if (strcmp(ucmd, "mget") == 0) {
            while ((arg = strtok(NULL, " "))) {
                pid = fork();  
        if (pid == 0) { close(s); child_download(arg); }
        else if (pid > 0) add_child(pid);
            }


        } else if (strcmp(ucmd, "mput") == 0) {
    while ((arg = strtok(NULL, " "))) {
        pid = fork();
        if (pid == 0) { close(s); child_upload(arg, "STOR"); }  
        else if (pid > 0) add_child(pid);
    }

        } else if (strcmp(ucmd, "cd") == 0) { 
            arg = strtok(NULL, " ");
            if (arg) {
                snprintf(cmd, sizeof(cmd), "CWD %s", arg); sendCmd(s, cmd, res);
                if (strncmp(res, "250", 3) == 0) {
                    snprintf(cmd, sizeof(cmd), "PWD"); sendCmd(s, cmd, res);
                    char *q1 = strchr(res, '"');
                    if (q1) { char *q2 = strchr(q1+1, '"'); if(q2) { *q2=0; strcpy(current_dir_g, q1+1); } }
                }
            }

   
        } else if (strcmp(ucmd, "cdup") == 0 || strcmp(ucmd, "..") == 0) {
            snprintf(cmd, sizeof(cmd), "CDUP"); sendCmd(s, cmd, res);
            if (strncmp(res, "250", 3) == 0) {
                snprintf(cmd, sizeof(cmd), "PWD"); sendCmd(s, cmd, res);
                char *q1 = strchr(res, '"');
                if (q1) { char *q2 = strchr(q1+1, '"'); if(q2) { *q2=0; strcpy(current_dir_g, q1+1); } }
            }


        } else if (strcmp(ucmd, "pwd") == 0) { 
            snprintf(cmd, sizeof(cmd), "PWD"); sendCmd(s, cmd, res);

        } else if (strcmp(ucmd, "dir") == 0 || strcmp(ucmd, "nlst") == 0) {
            int sdata = -1, slisten = -1;
            char data[LINELEN];
            char *target = strtok(NULL, " ");
            
            if (use_passive_mode) sdata = pasivo(s);
            else slisten = activo_listen(s);

            if (strcmp(ucmd, "dir")==0) snprintf(cmd, sizeof(cmd), "LIST %s", target ? target : "");
            else snprintf(cmd, sizeof(cmd), "NLST %s", target ? target : "");
            
            sendCmd(s, cmd, res);

            if (!use_passive_mode) {
                struct sockaddr_in fsin; socklen_t alen = sizeof(fsin);
                sdata = accept(slisten, (struct sockaddr *)&fsin, &alen);
                close(slisten);
            }
            
            if (sdata >= 0) {
                while ((n = read(sdata, data, LINELEN)) > 0) fwrite(data, 1, n, stdout);
                close(sdata);
                read(s, res, LINELEN); printf("%s\n", res);
            } else {
                printf("Error al listar directorio (Fallo conexion de datos)\n");
            }
        } else if (strcmp(ucmd, "mkdir") == 0) { 
            arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "MKD %s", arg); sendCmd(s, cmd, res); }

        } else if (strcmp(ucmd, "rmdir") == 0) { 
            arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "RMD %s", arg); sendCmd(s, cmd, res); }

        /* ORIGINAL: DELETE */
        } else if (strcmp(ucmd, "delete") == 0) { 
            arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "DELE %s", arg); sendCmd(s, cmd, res); }

        /* NUEVO: RENAME */
        } else if (strcmp(ucmd, "rename") == 0) { 
            char *old = strtok(NULL, " "); char *new = strtok(NULL, " ");
            if(old && new) {
                snprintf(cmd, sizeof(cmd), "RNFR %s", old); sendCmd(s, cmd, res);
                if(strncmp(res, "350", 3)==0) { snprintf(cmd, sizeof(cmd), "RNTO %s", new); sendCmd(s, cmd, res); }
            } else printf("Uso: rename <viejo> <nuevo>\n");
        
        } else if (strcmp(ucmd, "passive") == 0) { 
            use_passive_mode = 1; printf("Modo cambiado a PASV (Pasivo)\n");

        } else if (strcmp(ucmd, "active") == 0) { /* Cambiar a PORT */
            use_passive_mode = 0; printf("Modo cambiado a PORT (Activo)\n");

        } else if (strcmp(ucmd, "syst") == 0) { snprintf(cmd, sizeof(cmd), "SYST"); sendCmd(s, cmd, res);
        } else if (strcmp(ucmd, "noop") == 0) { snprintf(cmd, sizeof(cmd), "NOOP"); sendCmd(s, cmd, res);
        } else if (strcmp(ucmd, "type") == 0) { arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "TYPE %s", arg); sendCmd(s, cmd, res); }
        } else if (strcmp(ucmd, "mode") == 0) { arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "MODE %s", arg); sendCmd(s, cmd, res); }
        } else if (strcmp(ucmd, "stru") == 0) { arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "STRU %s", arg); sendCmd(s, cmd, res); }
        } else if (strcmp(ucmd, "site") == 0) { arg = strtok(NULL, ""); if(arg) { snprintf(cmd, sizeof(cmd), "SITE %s", arg); sendCmd(s, cmd, res); }
        } else if (strcmp(ucmd, "allo") == 0) { arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "ALLO %s", arg); sendCmd(s, cmd, res); }
        } else if (strcmp(ucmd, "acct") == 0) { arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "ACCT %s", arg); sendCmd(s, cmd, res); }
        } else if (strcmp(ucmd, "stat") == 0) { arg = strtok(NULL, " "); snprintf(cmd, sizeof(cmd), "STAT %s", arg?arg:""); sendCmd(s, cmd, res);
        } else if (strcmp(ucmd, "rest") == 0) { arg = strtok(NULL, " "); if(arg) { snprintf(cmd, sizeof(cmd), "REST %s", arg); sendCmd(s, cmd, res); }
        } else if (strcmp(ucmd, "rein") == 0) { 
            snprintf(cmd, sizeof(cmd), "REIN"); sendCmd(s, cmd, res);
            user_g[0]=0; pass_g[0]=0; current_dir_g[0]=0; 
        } else if (strcmp(ucmd, "wait") == 0) {
            wait_all_children();

        } else if (strcmp(ucmd, "status") == 0) {
            printf("Procesos activos: %d\n", active_children);
            for(int i=0; i<active_children; i++) printf(" - PID %d\n", child_pids[i]);

        } else if (strcmp(ucmd, "quit") == 0 || strcmp(ucmd, "abor") == 0) {
            wait_all_children();
            snprintf(cmd, sizeof(cmd), "QUIT"); sendCmd(s, cmd, res);
            close(s); exit(0);

        } else if (strcmp(ucmd, "help") == 0) {
            ayuda();
        
        } else {
            printf("Comando desconocido. Escriba 'help'.\n");
        }
    }
}