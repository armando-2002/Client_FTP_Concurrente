/* TCPftp.c - main, sendCmd, pasivo */

#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern int  errno;

int  errexit(const char *format, ...);
int  connectTCP(const char *host, const char *service);
int  passiveTCP(const char *service, int qlen);

#define  LINELEN    128

/* Envia cmds FTP al servidor, recibe respuestas y las despliega */
void sendCmd(int s, char *cmd, char *res) {
  int n;

  n = strlen(cmd);
  cmd[n] = '\r';		/* formatear cmd FTP: \r\n al final */
  cmd[n+1] = '\n';
  n = write(s, cmd, n+2);	/* envia cmd por canal de control */
  n = read (s, res, LINELEN);	/* lee respuesta del svr */
  res[n] = '\0';		/* despliega respuesta */
  printf ("%s\n", res);
}

/* envia cmd PASV; recibe IP,pto del SVR; se conecta al SVR y retorna sock conectado */
int pasivo (int s){
  int sdata;			/* socket para conexion de datos */
  int nport;			/* puerto (en numeros) en SVR */
  char cmd[128], res[128], *p;  /* comando y respuesta FTP */
  char host[64], port[8];	/* host y port del SVR (como strings) */
  int h1,h2,h3,h4,p1,p2;	/* octetos de IP y puerto del SVR */

  sprintf (cmd, "PASV");
  sendCmd(s, cmd, res);
  p = strchr(res, '(');
  sscanf(p+1, "%d,%d,%d,%d,%d,%d", &h1,&h2,&h3,&h4,&p1,&p2);
  snprintf(host, 64, "%d.%d.%d.%d", h1,h2,h3,h4);
  nport = p1*256 + p2;
  snprintf(port, 8, "%d", nport);
  sdata = connectTCP(host, port);

  return sdata;
}

void ayuda () {
  printf ("Cliente FTP. Comandos disponibles:\n \
    help		- despliega este texto\n \
    dir		- lista el directorio actual del servidor\n \
    get <archivo>	- copia el archivo desde el servidor al cliente\n \
    put <file>		- copia el archivo desde el cliente al servidor\n \
    pput <file>	- copia el archivo desde el cliente al servidor, con PORT\n \
    cd <dir>		- cambia al directorio dir en el servidor\n \
    quit		- finaliza la sesion FTP\n\n");
}

void salir (char *msg) {
  printf ("%s\n", msg);
  exit (1);
}

int main(int argc, char *argv[]) {
  char  *host = "localhost";  /* host to use if none supplied  */
  char  *service = "ftp";     /* default service name    */
  char  cmd[128], res[128];   /* resfer for cmds and replys from svr */
  char  data[LINELEN+1];      /* resfer to receive dirs (LIST) and send/recv files */
  char  hdata[64], pdata[8], user[32], *pass, prompt[64], *ucmd, *arg;
  int   s, s1=0, sdata, n;           /* socket descriptors, read count*/
  FILE  *fp;
  struct  sockaddr_in addrSvr;
  unsigned int alen;

  switch (argc) {
  case 1:
    host = "localhost";
    break;
  case 3:
    service = argv[2];
    /* FALL THROUGH */
  case 2:
    host = argv[1];
    break;
  default:
    fprintf(stderr, "Uso: TCPftp [host [port]]\n");
    exit(1);
  }

  s = connectTCP(host, service);

  n = read (s, res, LINELEN);		/* lee msg del SVR, despues de conexion */
  res[n] = '\0';
  printf ("%s\n", res);

  while (1) {
    printf ("Please enter your username: ");
    scanf ("%s", user);
    sprintf (cmd, "USER %s", user); 
    sendCmd(s, cmd, res);
  
    //scanf ("%s", user);
    pass = getpass("Enter your password: ");
    sprintf (cmd, "PASS %s", pass); 
    sendCmd(s, cmd, res);
    if ((res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0') == 230) break;
  }

  fgets (prompt, sizeof(prompt), stdin);
  ayuda();

  while (1) {
    printf ("ftp> ");
    if (fgets(prompt, sizeof(prompt), stdin) != NULL) {
      prompt[strcspn(prompt, "\n")] = 0;

      ucmd = strtok (prompt, " ");
  
      if (strcmp(ucmd, "dir") == 0) {
        sdata = pasivo(s);
        sprintf (cmd, "LIST"); 
        sendCmd(s, cmd, res);
        while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
          fwrite(data, 1, n, stdout);
        }
        close(sdata);
        n = read (s, res, LINELEN);
        res[n] = '\0';
        printf ("%s\n", res);

      } else if (strcmp(ucmd, "get") == 0) {
  	arg = strtok (NULL, " ");
        sdata = pasivo(s);
        sprintf (cmd, "RETR %s", arg); 
        sendCmd(s, cmd, res);
        if ((res[0]-'0')*100 + (res[1]-'0')*10 + (res[2]-'0') > 500) continue;
        fp = fopen(arg, "wb");
        while ((n = recv(sdata, data, LINELEN, 0)) > 0) {
          fwrite(data, 1, n, fp);
        }
        fclose(fp);
        close(sdata);
        n = read (s, res, LINELEN);
        res[n] = '\0';
        printf ("%s\n", res);
  
      } else if (strcmp(ucmd, "put") == 0) {
  	arg = strtok (NULL, " ");
        fp = fopen(arg, "r");
	if (fp == NULL) {perror ("Open local file"); continue;}
        sdata = pasivo(s);
        sprintf (cmd, "STOR %s", arg);
        sendCmd(s, cmd, res);

        while ((n = fread (data, 1, 64, fp)) > 0) {
          send (sdata, data, n, 0);
        }
        fclose(fp);
        close(sdata);
        n = read (s, res, LINELEN);
        res[n] = '\0';
        printf ("%s\n", res);

      } else if (strcmp(ucmd, "pput") == 0) {
  	arg = strtok (NULL, " ");
        fp = fopen(arg, "r");
	if (fp == NULL) {perror ("Open local file"); continue;}
       
	char *ip;
	if (s1==0) {
	   s1 = passiveTCP ("1030", 5);
	   char lname[64];
	   gethostname(lname, 64);
	   struct hostent *hent = gethostbyname (lname);
           ip = inet_ntoa(*((struct in_addr*) hent->h_addr_list[0]));
	   for(int i = 0; i <= strlen(ip); i++) {
             if(ip[i] == '.') {
               ip[i] = ',';
             }
           }
	}
        sprintf (cmd, "PORT %s,%s", ip,"4,6");
        sendCmd(s, cmd, res);
        
        sprintf (cmd, "STOR %s", arg); 
        sendCmd(s, cmd, res);
        sdata = accept(s1, (struct sockaddr *)&addrSvr, &alen);
        fp = fopen(arg, "r");
	if (fp == NULL) {perror ("fopen"); exit (1);}
        while ((n = fread (data, 1, LINELEN, fp)) > 0) {
          send (sdata, data, n, 0);
        }
        fclose(fp);
        close(sdata);
        n = read (s, res, LINELEN);
        res[n] = '\0';
        printf ("%s\n", res);

      } else if (strcmp(ucmd, "cd") == 0) {
  	arg = strtok (NULL, " ");
        sprintf (cmd, "CWD %s", arg); 
        sendCmd(s, cmd, res);

      } else if (strcmp(ucmd, "quit") == 0) {
        sprintf (cmd, "QUIT"); 
        sendCmd(s, cmd, res);
	exit (0);

      } else if (strcmp(ucmd, "help") == 0) {
	ayuda();

      } else {
        printf("%s: comando no implementado.\n", ucmd);
      }
    }
  }
}

