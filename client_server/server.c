#include <string.h>
#include <sys/types.h>          
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <fcntl.h>
#include <stdbool.h>
#include <getopt.h>

#include "message.h"
static int cmd_csd, data_csd, cmd_sd, data_sd, rfd, wfd = -1;
static int do_create(struct message *m1, struct message *m2);
static int do_read(struct message *m1, struct message *m2);
static int do_write(struct message *m1, struct message *m2);
static int do_delete(struct message *m1, struct message *m2);
static int bind_socket(char *server_ipaddr, char *server_port);
static int initialize(char *server_ipaddr, char *server_port1, char *server_port2,
   char *sent_log_file, char *recv_log_file);
static int release(void);


int main(int argc, char *argv[]){
  struct message m1, m2; /*incomming and outgoing message*/
  int r;   /* result code*/
  char c;
  char *server_addr = NULL;
  char *port = NULL, *port2;
  char *send_log_file = NULL, *recv_log_file = NULL;
  while ((c = getopt(argc, argv, "s:p:P:S:R:h")) != -1){
    switch(c){
      case 's':
        server_addr = malloc(strlen(optarg));
        strcpy(server_addr, optarg);
        break;
      case 'p':
        if(strlen(optarg) > 5 || !atoi(optarg)){
          fprintf(stderr, "port between 0-65535\n");
          return 0;
        }
        port = malloc(strlen(optarg));
        strcpy(port, optarg);
        break;
      case 'P':
        if(strlen(optarg) > 5 || !atoi(optarg)){
          fprintf(stderr, "port between 0-65535\n");
          return 0;
        }
        port2 = malloc(strlen(optarg));
        strcpy(port2, optarg);
        break;
      case 'S':
        send_log_file = malloc(strlen(optarg));
        strcpy(send_log_file, optarg);
        break;
      case 'R':
        recv_log_file = malloc(strlen(optarg));
        strcpy(recv_log_file, optarg);
        break;
      case 'h':
        fprintf(stderr, "USAGE: %s -s server_addr -p server_port -P server__port2" 
                " -S send_log_file_path -R recv_log_file_path\n", argv[0]);
        return 0; 
    }
  }
  
  if(!server_addr || !port || !send_log_file || !recv_log_file){
    fprintf(stderr, "USAGE: %s -s server_addr -p server_port -P server_port2"
                " -S send_log_file_path -R recv_log_file_path\n", argv[0]);
    return 0; 
  }
  memset(&m1,0, sizeof(m1)); 
  memset(&m2,0, sizeof(m2));              
  initialize(server_addr, port, port2, send_log_file, recv_log_file);
  while(TRUE){
    struct sockaddr client_addr;
    int clen;
    if((cmd_csd = accept(cmd_sd, &client_addr, &clen)) < 0){
      fprintf(stderr, "Un petit problème lors du accept %d\n", errno);
      return -1;
    }
    if((data_csd = accept(data_sd, &client_addr, &clen)) < 0){
      fprintf(stderr, "Un petit problème lors du accept %d\n", errno);
      return -1;
    }
    pid_t pid = fork();
    if(pid == 0){
      close(cmd_sd);
      close(data_sd);
      rfd = -1; wfd = -1;
      fprintf(stderr, "get connection from (%d) (%d)\n", cmd_csd, data_csd);
      while(TRUE){           /*server runs forever*/
        ifri_receive(cmd_csd, &m1); /* block waiting for a message*/
        switch(m1.opcode){
        /*case CREATE: 
          r = do_create(&m1, &m2);
          break;*/
          case READ:
            r = do_read(&m1, &m2);
            break;
          case WRITE:
            r = do_write(&m1, &m2);
            break;
          /*case DELETE:
            r = do_delete(&m1, &m2);
            break;*/
          default:
            r = E_BAD_OPCODE;
        }
        m2.result = r;  /* return result to client */
        if(r > 0)
          m2.count = r;
        ifri_send(cmd_csd, &m2); /* send reply*/
      }
      break;
    }else{
      close(cmd_csd);
      close(data_csd);
    }
  }
  release();
}

static int do_create(struct message *m1, struct message *m2){
  return OK;
}

static int do_read(struct message *m1, struct message *m2){
  int fd, r = OK;
  double bytessent = 0;
  char buffer[16384];
  if((rfd == -1) && (rfd = open(m1->name, O_RDONLY | S_IRUSR | S_IWUSR ))<0){
    fprintf(stderr, "error when opening file %d %d %s\n",fd, errno, m1->name);
    return E_IO;
  }
  if(!m1->send_file_content){
    lseek(rfd, m1->offset, SEEK_SET);
    r = read(rfd, m2->data, m1->count);
    if(r < 0){
      fprintf(stderr, "error when reading file %d %d\n",fd, errno);
      return  E_IO;
    }
  }else{
    while((r =read(rfd, buffer, 16384))> 0){
      write(data_csd, buffer, r);
      bytessent+=r;
    }
  }
  return r;
}

static int do_write(struct message *m1, struct message *m2){
  int r;
  if( (wfd == -1) && (wfd = open(m1->name, O_CREAT | O_WRONLY | S_IRWXU )) < 0){
    fprintf(stderr, "do_write error when opening (%s), %d:%d\n", m1->name, wfd, errno);
    return E_IO;
  }
  lseek(wfd, m1->offset, SEEK_SET);
  r = write(wfd, m1->data, m1->count);
  if(r < 0)
     return  E_IO;
  return r;
}

static int do_delete(struct message *m1, struct message *m2){
  return OK;
}


static int initialize(char *server_ipaddr, char *server_port1, char *server_port2,
   char *sent_log_file, char *recv_log_file){
   int sfd = open(sent_log_file, O_CREAT | O_RDWR);
   if(sfd < 0){
      fprintf(stderr, "Erreur d'ouverture du fichier (%s) (%s)\n"
       , sent_log_file, strerror(errno) );
      return -1;
   }
   
   int rfd = open(recv_log_file, O_CREAT | O_RDWR);
   if(rfd < 0){
      fprintf(stderr, "Erreur d'ouverture du fichier (%s) (%s)\n"
       , recv_log_file, strerror(errno) );
      return -1;
   }
     
   init_params(sfd, rfd);
   cmd_sd = bind_socket(server_ipaddr, server_port1);
   data_sd = bind_socket(server_ipaddr, server_port2);
   fprintf(stderr, "Connexion binded (%d) (%d)\n", cmd_sd, data_sd);   
}

static int release(void){
  unload_bpf_cubic();
  return (close(data_sd) || close(cmd_sd) || close(data_csd) 
             || close(cmd_csd) || close(rfd) || close(wfd));
}

static int bind_socket(char *server_ipaddr, char *server_port){
  struct sockaddr server_addr;
  int salen;
  int sd = socket(AF_INET, SOCK_STREAM, 0);
   if(resolve_address(&server_addr, &salen, server_ipaddr, server_port, AF_INET, 
      SOCK_STREAM, IPPROTO_TCP)!= 0){
      fprintf(stderr, "Erreur de configuration de sockaddr\n");
      return -1;
   }
    if(bind(sd, &server_addr, salen)!=0){
      fprintf(stderr, "Un petit problème lors du bind: %d\n", errno);
      return -1;
    }
    if(listen(sd, 10)!=0){
      fprintf(stderr, "Un petit problème lors du listen %d\n", errno);
      return -1;
    }
    return sd;
}
