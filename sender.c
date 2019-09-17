/*
  sender.c
  UDP Sender Program
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
#include <signal.h>
#include <math.h>

// packet types
#define INIT '0'
#define DATA '1'
#define ACK '2'

// constant values
#define DATASIZE 1024
#define FILENAMESIZE 56
#define BUFSIZE 1200
#define MAXTRIES 3
#define RTT 500000

// function definitions
void error (char *e);
void mult(char buffer[BUFSIZE], char *type, char filename[FILENAMESIZE], long *filesize, int *mode, long *seq_num, char data[DATASIZE]);
void demult(char buffer[BUFSIZE], char *type, char filename[FILENAMESIZE], long *filesize, int *mode, long *seq_num, char data[DATASIZE]);
void stop_and_wait();
void gobackn(long N);

// global variables
int port;
char *hostname;;
int mode;
char *filename;
long filesize;
char * filebuffer;
char buffer[BUFSIZE];
char recv_buffer[BUFSIZE];
int phase = 0;
long random_packet;

int mode_exist = 0;
int port_exist = 0;
int hostname_exist = 0;
int filename_exist = 0;
int test_exist = 0;
int test_case = 0;

int main(int argc, char** argv){

  FILE * file;
  size_t res;
  int i;

  // parse command line input
  for (i=0; i<argc; i++){ // mode
    if(strcmp(argv[i],"-m")==0){
      mode = atoi(argv[i+1]);
      mode_exist = 1;
    }
    else if(strcmp(argv[i],"-p")==0){ // port
      port = atoi(argv[i+1]);
      port_exist = 1;
    }
    else if(strcmp(argv[i],"-h")==0){ // hostname
      hostname = argv[i+1];
      hostname_exist = 1;
    }
    else if(strcmp(argv[i],"-f")==0){ // filename
      filename = argv[i+1];
      filename_exist = 1;
    }
    else if(strcmp(argv[i],"-t")==0){ // test case
      test_exist = 1;
      test_case = atoi(argv[i+1]);
    }
  }

  if(!mode_exist || !port_exist || !filename_exist ||!hostname_exist){
    printf("\tUsage:\n\
          [-m mode] [-p port] [-f filename] [-h hostname] <-t test>\n\
          [required] <optional>\n");
    exit(1);
  }

  // open file and get the size of the file
  file = fopen(filename,"rb");
  if(file == NULL)
    error("Cannot open file!");
  fseek (file , 0 , SEEK_END);
  filesize = ftell (file);
  rewind (file);

  // store the file content in a buffer
  filebuffer = (char*) malloc (sizeof(char)*filesize);
  if(filebuffer == NULL)
    error("Cannot create file buffer!");

  if(fread(filebuffer,1,filesize,file) != filesize)
    error("Cannot read file");

  fclose(file);

  // begin transmission
  if (mode == 1) // stop and wait
    stop_and_wait();
  else if(mode > 1) // go-back-n with windows size N=mode
    gobackn(mode);

  return 0;
}

void stop_and_wait(){
  int sock;
  struct sockaddr_in receiver_address;
  struct hostent *receiver;
  long seq_num;
  char type;
  char data[DATASIZE];
  char FNAME[FILENAMESIZE];
  int tries;
  int go;
  int to_status;
  struct timeval timeout;
  int microsecond = RTT;
  int sent = 0;
  int sent_data;
  socklen_t len;
  fd_set fdset;
  long total_packets;

  // create socket
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    error("Cannot open socket!");

  // get receiver info
  receiver = gethostbyname(hostname);
  if (receiver == NULL)
    error("Receiver cannot be found!");

  // build receiver address
  memset(&receiver_address, 0, sizeof(receiver_address));
  receiver_address.sin_family = AF_INET;
  memcpy(&receiver_address.sin_addr, receiver->h_addr, sizeof(receiver_address.sin_addr));
  receiver_address.sin_port = htons(port);

  // create INIT packet
  type = INIT;
  bzero(FNAME,sizeof(FNAME));
  memcpy(FNAME,&(*filename),strlen(filename));
  seq_num = 0;
  bzero(data,DATASIZE);
  mult(buffer,&type,FNAME,&filesize,&mode,&seq_num,data);

  // calculate total number of data packets
  total_packets = (long)ceil((double)filesize/(double)DATASIZE);

  len = sizeof(receiver_address);

  // set timeout
  FD_ZERO (&fdset);
  FD_SET  (sock, &fdset);
  timeout.tv_sec = 0;
  timeout.tv_usec = RTT;
  tries = 0;
  go = 0;

  // send INIT packet up to MAXTRIES times until an ACK is received
  while(tries < MAXTRIES){
    tries++;
    sent_data = sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, len);
    if( sent_data < 0)
      error("Cannot send package!");
    printf("-> INIT\n");
    if(test_case == 1 && phase == 0){ // test case 1
      to_status = 0;
      phase = 1;
    }
    else
      to_status = select(sock+1,&fdset,NULL,NULL,&timeout);
    if(to_status < 0) // error
      error("Select error");
    else if(to_status == 0){ // timeout
      printf("TIMEOUT-%d FOR INIT\n",tries);
      timeout.tv_usec = (tries+1) * RTT; // try again with higher timeout
    }else{ // success
      go = 1;
      break;
    }
  }

  // terminate connection if there is no progress after MAXTRIES tries
  if(!go)
    error("Sender time out...\n");

  // receive packet from receiver
  if(recvfrom(sock, recv_buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, &len) < 0)
    error("No response!\n");

  // get the packet content
  demult(recv_buffer,&type,FNAME,&filesize,&mode,&seq_num,data);

  // sender only accepts packets of type ACK
  if(type != ACK)
    error("Unknown response!\n");

  printf("<- ACK INIT\n");

  phase = 0;
  srand(time(NULL));
  random_packet = rand()%total_packets+1;

  // send the file
  while(sent < filesize){
    // divide the file into chunks
    type = DATA;
    seq_num++;
    bzero(data,DATASIZE);
    memcpy(data,filebuffer+sent,DATASIZE);

    // create the DATA packet
    mult(buffer,&type,FNAME,&filesize,&mode,&seq_num,data);

    //set timeout
    timeout.tv_sec = 0;
    timeout.tv_usec = RTT;
    tries = 0;
    go = 0;

    // send DATA packet up to MAXTRIES times until an ACK is received
    while(tries < MAXTRIES){
      FD_ZERO (&fdset);
      FD_SET  (sock, &fdset);
      tries++;
      if(test_case == 2 && phase == 0 && seq_num == random_packet){
        phase = 1;
        receiver_address.sin_port = htons(port-1);
        sent_data = sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, len);
        if( sent_data < 0)
          error("Cannot send package!");
        printf("-> PACKET %ld\n",seq_num);
      }
      else if(test_case == 3 && seq_num == random_packet){
        receiver_address.sin_port = htons(port-1);
        sent_data = sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, len);
        if( sent_data < 0)
          error("Cannot send package!");
        printf("-> PACKET %ld\n",seq_num);
      }else{
        receiver_address.sin_port = htons(port);
        sent_data = sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, len);
        if( sent_data < 0)
          error("Cannot send package!");
        printf("-> PACKET %ld\n",seq_num);
      }
      to_status = select(sock+1,&fdset,NULL,NULL,&timeout);
      if(to_status < 0) // error
        error("Select error");
      else if(to_status == 0){ // timeout
        printf("TIMEOUT-%d FOR PACKET %ld\n",tries,seq_num);
        timeout.tv_usec = (tries+1) * RTT; // try again with higher timeout
      }else{ // success
        go = 1;
        break;
      }
    }

    if(go==1){ // if we receive a packet
      sent = sent + sizeof(data); // increment data chunk pointer
      if(recvfrom(sock, recv_buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, &len) < 0)
        error("No response!");

      // get packet contents
      demult(recv_buffer,&type,FNAME,&filesize,&mode,&seq_num,data);

      if(type != ACK)
        error("Unknown response!");

      printf("<- ACK %ld\n",seq_num);
    }
    else // terminate connection if there is no progress after MAXTRIES tries
      error("Sender timeout...\n");
  }
  close(sock);
  printf("Transmission complete\n");
}

void gobackn(long N){
  int sock;
  struct sockaddr_in receiver_address;
  struct hostent *receiver;
  long req_num;
  long seq_num;
  long base = 1;
  long max = N;
  char type;
  char data[DATASIZE];
  char FNAME[FILENAMESIZE];
  int tries;
  int sent = 0;
  long total_packets;
  socklen_t len;
  struct timeval timeout;
  fd_set fdset;
  int to_status;
  int go;
  int sent_data;

  // create socket
  sock = socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
    error("Cannot open socket!");

  // get receiver info
  receiver = gethostbyname(hostname);
  if (receiver == NULL)
    error("Receiver cannot be found!");

  // build receiver address
  memset(&receiver_address, 0, sizeof(receiver_address));
  receiver_address.sin_family = AF_INET;
  memcpy(&receiver_address.sin_addr, receiver->h_addr, sizeof(receiver_address.sin_addr));
  receiver_address.sin_port = htons(port);

  // create INIT packet
  type = INIT;
  memcpy(FNAME,&(*filename),strlen(filename));
  seq_num = 1;
  bzero(data,DATASIZE);
  mult(buffer,&type,FNAME,&filesize,&mode,&seq_num,data);

  // calculate total number of data packets
  total_packets = (long)ceil((double)filesize/(double)DATASIZE);

  len = sizeof(receiver_address);

  // set timeout
  FD_ZERO (&fdset);
  FD_SET  (sock, &fdset);
  timeout.tv_sec = 0;
  timeout.tv_usec = RTT;
  tries = 0;
  go = 0;

  // send INIT packet up to MAXTRIES times until an ACK is received
  while(tries < MAXTRIES){
    tries++;
    sent_data = sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, len);
    if( sent_data < 0)
      error("Cannot send package!");
    printf("-> INIT\n");
    to_status = select(sock+1,&fdset,NULL,NULL,&timeout);
    if(to_status < 0) // error
      error("Select error");
    else if(to_status == 0){ // timeout
      printf("TIMEOUT-%d FOR INIT\n",tries);
      timeout.tv_usec = (tries+1) * RTT; // try again with higher timeout
    }else{ // success
      go = 1;
      break;
    }
  }

  // terminate connection if there is no progress after MAXTRIES tries
  if(!go)
    error("Sender time out...\n");

  // receive packet
  if(recvfrom(sock, recv_buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, &len) < 0)
    error("No response!");

  // get packet contents
  demult(recv_buffer,&type,FNAME,&filesize,&mode,&req_num,data);

  if(type != ACK || req_num != 1)
    error("Unknown response!");

  printf("<- ACK INIT\n");

  // set timeout
  FD_ZERO (&fdset);
  FD_SET  (sock, &fdset);
  timeout.tv_sec = 0;
  timeout.tv_usec = RTT;
  tries = 0;

  while(1){ // main loop
    // every window is sent at most MAXTRIES times
    if(tries >= MAXTRIES)
      error("Connection timeout!");

    // send the window
    max = base+N-1;
    while(seq_num <= total_packets && seq_num <= max){
      memcpy(data,filebuffer+(seq_num-1)*DATASIZE,DATASIZE);
      type = DATA;
      mult(buffer,&type,FNAME,&filesize,&mode,&seq_num,data);

      if(sendto(sock, buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, len) < 0)
          error("Cannot send package!");

      printf("-> PACKET %ld\n",seq_num);
      seq_num++;
    }

    to_status = select(sock+1,&fdset,NULL,NULL,&timeout);

    if(to_status < 0) // error
      error("Select error");
    else if(to_status == 0){ // timeout
      tries++;
      timeout.tv_usec = (tries+1) * RTT; // try again with higher timeout
      seq_num = base; // send the window again from scratch
      printf("TIMEOUT-%d\n", tries);
    }else{ // we receive a packet

      if(recvfrom(sock, recv_buffer, BUFSIZE, 0,(struct sockaddr *) &receiver_address, &len) < 0)
        error("No response!");

      // get packet content
      demult(recv_buffer,&type,FNAME,&filesize,&mode,&req_num,data);
      printf("<- REQUEST %ld\n",req_num);

      // all packets delivered so terminate the connection
      if(req_num == total_packets + 1){
        printf("Transmission complete\n");
        break;
      }

      // slide the window
      if(req_num > base)
        base = req_num;

      // reset tries and timeout value
      tries = 0;
      timeout.tv_usec = RTT;
    }

  }
  close(sock);
}

void mult(char buffer[BUFSIZE], char *type, char filename[FILENAMESIZE], long *filesize, int *mode, long *seq_num, char data[DATASIZE]){
  // writes the packet fields into the packet and does conversions if necessary
  long fls = htonl(*filesize);
  int mod = htons(*mode);
  long sn = htonl(*seq_num);
  int i=0;
  memcpy(buffer,type,sizeof(char));
  i = i+sizeof(char);
  memcpy(buffer+i,filename,FILENAMESIZE);
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
  memcpy(filename,buffer+i,FILENAMESIZE);
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

void error (char *e){
  // print error message and die
  printf("%s\n",e);
  exit(1);
}
