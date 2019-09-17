/*
  receiver.c
  UDP Receiver Program
  Berkcan Gurel
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <math.h>

// packet types
#define INIT '0'
#define DATA '1'
#define ACK '2'

// constant values
#define DATASIZE 1024
#define BUFSIZE 2048
#define FILENAMESIZE 56
#define RECVTIMEOUT 15

// function definitions
void error (char *e);
void mult(char buffer[BUFSIZE], char *type, char filename[FILENAMESIZE], long *filesize, int *mode, long *seq_num, char data[DATASIZE]);
void demult(char buffer[BUFSIZE], char *type, char filename[FILENAMESIZE], long *filesize, int *mode, long *seq_num, char data[DATASIZE]);
void merge(const int num_pack, const char* filename);

const char *prefix = "packet";

int main(int argc, char **argv) {
  int sock;
  int port;
  int optval = 1;
  char *hostname;
  struct in_addr **addr_list;
  struct sockaddr_in sender_address;
  struct sockaddr_in receiver_address;
  struct hostent *sender;
  long seq_num;
  char type;
  char filename[FILENAMESIZE];
  char packetname[FILENAMESIZE];
  long filesize;
  int mode;
  int sender_mode;
  char data[DATASIZE];
  char buffer[BUFSIZE];
  char recv_buffer[BUFSIZE];
  socklen_t len;
  int received = 0;
  FILE *file;
  int recv_seq_num;
  long total_packets;
  long req_num = 1;
  struct timeval timeout;
  fd_set fdset;
  int to_status;
  int mode_exist = 0;
  int port_exist = 0;
  int hostname_exist = 0;
  int test_exist = 0;
  int test_case = 0;
  int phase = 0;
  int i;

  // parse command line input
  for (i=0; i<argc; i++){
    if(strcmp(argv[i],"-m")==0){
      mode = atoi(argv[i+1]);
      mode_exist = 1;
    }
    else if(strcmp(argv[i],"-p")==0){
      port = atoi(argv[i+1]);
      port_exist = 1;
    }
    else if(strcmp(argv[i],"-h")==0){
      hostname = argv[i+1];
      hostname_exist = 1;
    }
    else if(strcmp(argv[i],"-t")==0){
      test_exist = 1;
      test_case = atoi(argv[i+1]);
    }
  }

  if(!mode_exist || !port_exist){
    printf("\tUsage:\n\
          [-m mode] [-p port] <-h hostname> <-t test>\n\
          [required] <optional>\n");
    exit(1);
  }

  // create socket
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    error("Cannot open socket!");

  setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

  // get receiver info
  if(hostname_exist){
    sender = gethostbyname(hostname);
    if (sender == NULL)
      error("Sender cannot be found!");
  }

  // build receiver address
  memset(&receiver_address, 0, sizeof(receiver_address));
  receiver_address.sin_family = AF_INET;
  receiver_address.sin_addr.s_addr = INADDR_ANY;
  receiver_address.sin_port = htons(port);

  // bind to a port
  if (bind(sock, (struct sockaddr *) &receiver_address, sizeof(receiver_address)) < 0)
    error("ERROR on binding");

  // set timeout
  FD_ZERO (&fdset);
  FD_SET  (sock, &fdset);
  timeout.tv_sec = 2*RECVTIMEOUT;
  timeout.tv_usec = 0;

  to_status = select(sock+1,&fdset,NULL,NULL,&timeout);
  if(to_status < 0) // error
    error("Select error");
  else if(to_status == 0){ // receiver was idle
    error("Receiver time out...");
  }

  len = sizeof(sender_address);

  // receive packet
  if(recvfrom(sock, recv_buffer, BUFSIZE, 0,(struct sockaddr *) &sender_address, &len) < 0)
    error("No init\n");

  // get packet contents
  demult(recv_buffer,&type,filename,&filesize,&sender_mode,&seq_num,data);

  // check for mode mismatch
  if(mode != sender_mode)
    error("Incompatible modes!");

  // check if the sender is the designated host given from the command line
  if(hostname_exist){
    sender = gethostbyaddr((const char *)&sender_address.sin_addr.s_addr, sizeof(sender_address.sin_addr.s_addr), AF_INET);
    if(strcmp(sender->h_name,hostname) != 0)
      error("Unexpected sender");
  }

  // check packet type
  if (type != INIT)
    error("No INIT message!");

  // calculate total number of data packets
  total_packets = (long)ceil((double)filesize/(double)DATASIZE);

  printf("<- INIT\n");

  // create and send ACK message for INIT
  type = ACK;
  if(mode == 1)
    mult(buffer,&type,filename,&filesize,&sender_mode,&seq_num,data);
  else
    mult(buffer,&type,filename,&filesize,&sender_mode,&req_num,data);
  if(sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &sender_address, len) < 0)
    error("Cannot send package!");

  printf("-> ACK INIT\n");

  // main loop
  if (mode == 1){ // stop and wait
    recv_seq_num = 1;
    while(received < filesize){ // when there is still packets to receive
      to_status = select(sock+1,&fdset,NULL,NULL,&timeout);
      if(to_status < 0) // error
        error("Select error");
      else if(to_status == 0){ // channel was idle for too long
        error("Receiver time out...");
      }

      // receive packet
      bzero(data,DATASIZE);
      int x = recvfrom(sock, recv_buffer, BUFSIZE, 0,(struct sockaddr *) &sender_address, &len);
      if( x < 0)
        error("Cannot receive packet");

      // get packet contents
      demult(recv_buffer,&type,filename,&filesize,&sender_mode,&seq_num,data);

      // if the received packet is the expected packet
      if(seq_num == recv_seq_num){
        recv_seq_num++;
        received = received + sizeof(data);
        printf("<- PACKET %ld\n",seq_num);

        // write packet to disk
        char tmp[DATASIZE+1];
        memcpy(tmp,data,DATASIZE);
        tmp[DATASIZE] = '\0';
        strcpy(packetname,prefix);
        sprintf(packetname+strlen(prefix),"%ld",seq_num);
        file = fopen(packetname,"wb");
        fwrite(tmp,1,sizeof(tmp),file);
        fclose(file);

        // create and send ACK for the received DATA packet
        type = ACK;
        mult(buffer,&type,filename,&filesize,&sender_mode,&seq_num,data);
        if(sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &sender_address, len) < 0)
          error("Cannot send package!");
        printf("-> ACK %ld\n",seq_num);
      }
    }
    printf("Transmission complete\n");
    close(sock);

    // combine data packets to re-create the original file
    pid_t pid = getpid();
    bzero(packetname,FILENAMESIZE);
    strcpy(packetname,filename);
    sprintf(packetname+strlen(filename),"%d",pid);
    merge(seq_num,packetname);
  }
  else if(mode > 1){ // go-back-n with windows size N=mode
    while(req_num <= total_packets){ // when there is still packets to receive
      FD_ZERO (&fdset);
      FD_SET  (sock, &fdset);
      to_status = select(sock+1,&fdset,NULL,NULL,&timeout);
      if(to_status < 0) // error
        error("Select error");
      else if(to_status == 0){ // channel was idle for too long
        error("Receiver time out...");
      }

      // receive packet
      bzero(data,DATASIZE);
      int x = recvfrom(sock, recv_buffer, BUFSIZE, 0,(struct sockaddr *) &sender_address, &len);
      if( x < 0)
        error("Cannot receive packet");

      // get packet content
      demult(recv_buffer,&type,filename,&filesize,&sender_mode,&seq_num,data);

      if(seq_num == req_num){ // expected packet
        req_num++;
        received = received + sizeof(data);
        printf("<- PACKET %ld\n",seq_num);

        // write packet to disk
        char tmp[DATASIZE+1];
        memcpy(tmp,data,DATASIZE);
        tmp[DATASIZE] = '\0';
        strcpy(packetname,prefix);
        sprintf(packetname+strlen(prefix),"%ld",seq_num);
        file = fopen(packetname,"wb");
        fwrite(tmp,1,strlen(tmp),file);
        fclose(file);
      }
      // send ACK for the unreceived packet with smallest seq num
      if(!(test_case == 4 && req_num != total_packets + 1 && req_num % 2 == 1)){ // test case 4
        type = ACK;
        mult(buffer,&type,filename,&filesize,&sender_mode,&req_num,data);
        if(sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &sender_address, len) < 0)
          error("Cannot send package!");
        printf("-> REQUEST %ld\n",req_num);
      }
    }
    printf("Transmission complete\n");
    close(sock);

    // combine data packets to re-create the original file
    pid_t pid = getpid();
    bzero(packetname,FILENAMESIZE);
    strcpy(packetname,filename);
    sprintf(packetname+strlen(filename),"%d",pid);
    merge(seq_num,packetname);
  }
}

void mult(char buffer[BUFSIZE], char *type, char filename[FILENAMESIZE], long *filesize, int *mode, long *seq_num, char data[DATASIZE]){
  // writes the packet fields into the packet and does conversions if necessary
  long fls = htonl(*filesize);
  int mod = htons(*mode);
  long sn = htonl(*seq_num);
  int i=0;
  memcpy(buffer,type,sizeof(char));
  i = i+sizeof(char);
  memcpy(buffer+i,&(*filename),FILENAMESIZE);
  i = i+FILENAMESIZE;
  memcpy(buffer+i,&fls,sizeof(long));
  i = i+sizeof(long);
  memcpy(buffer+i,&mod,sizeof(int));
  i = i+sizeof(int);
  memcpy(buffer+i,&sn,sizeof(long));
  i = i+sizeof(long);
  memcpy(buffer+i,data,DATASIZE);

}

void demult(char buffer[BUFSIZE], char *type, char filename[FILENAMESIZE], long *filesize, int *mode, long *seq_num, char data[DATASIZE]){
  // reads the packet fields from the packet and does conversions if necessary
  int i=0;
  memcpy(type,buffer,sizeof(char));
  i = i+sizeof(char);
  memcpy(&(*filename),buffer+i,FILENAMESIZE);
  i = i+FILENAMESIZE;
  memcpy(filesize,buffer+i,sizeof(long));
  i = i+sizeof(long);
  memcpy(mode,buffer+i,sizeof(int));
  i = i+sizeof(int);
  memcpy(seq_num,buffer+i,sizeof(long));
  i = i+sizeof(long);
  memcpy(data,buffer+i,DATASIZE);
  *filesize = ntohl(*filesize);
  *mode = ntohs(*mode);
  *seq_num = ntohl(*seq_num);

}

void merge(const int num_pack, const char* filename){
  // combine small packets into a large file
  char buffer[DATASIZE];
  char packetname[FILENAMESIZE];
  int i = 1;
  long size;
  FILE* fr;
  FILE* fw = fopen(filename, "wb");
  if(fw == NULL)
    error("Cannot open file");

  while(i<=num_pack){

    bzero(packetname,FILENAMESIZE);
    strcpy(packetname,prefix);
    sprintf(packetname+strlen(prefix),"%d",i);

    fr = fopen(packetname, "rb");
    if(fr == NULL)
      error("Cannot open file");

    char curr_char;
    while ((curr_char = getc(fr)) != EOF)
      fwrite(&curr_char,1,sizeof(curr_char),fw);

    fclose(fr);
    remove(packetname);
    i++;
  }
  fclose(fw);
}

void error (char *e){
  // print error message and die
  printf("%s\n",e);
  exit(1);
}
