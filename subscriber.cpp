#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <iostream>
#include "utils.h"
using namespace std;

/*
* Parsare comanda de subscribe/unsusbcribe de la input
*/
Command* parse_client_command(char buffer[]) {
  Command* command = new Command;
  char* token = new char;
  token = strtok(buffer, " ");
  if(strcmp(token, "subscribe") == 0) {

    strcpy(command->command_type, token);
    token = strtok(NULL, " ");
    strcpy(command->topic, token);
    token = strtok(NULL, " ");
    if(token == NULL) {
      return NULL;
    }
    command->sf = atoi(token);
  } else if(strcmp(token, "unsubscribe") == 0) {

    strcpy(command->command_type, token);
    token = strtok(NULL, " ");
    strncpy(command->topic, token, strlen(token) - 1);
    // cod cunoscut care sa spuna ca in comanda de unsubscribe campul
    // nu e relevant
    command->sf = -1;

  } else {
    // alta comanda in afara de subscribe/unsubscribe
    return NULL;
  }
  // sf invalid
  if(command->sf != -1 && command->sf != 0 && command->sf != 1) {
     std::cout << "Invalid input" << '\n';
    return NULL;
  }
  return command;
}

int main(int argc, char const *argv[]) {


  if(argc < 3) {
     std::cout << "Invalid command" << '\n';
    return -1;
  }
  int neagleFlag = 1;
  char buffer[BUFLEN];
  char client_id[ID_LEN];
  char ip[20];
  int port_number;
  struct sockaddr_in subs_addr;

  fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

  // retin datele din comanda
  strcpy(client_id, argv[1]);
  if(strlen(client_id) > 10) {
     std::cout << "Invalid client id" << '\n';
    return -1;
  }
  strcpy(ip, argv[2]);
  port_number = atoi(argv[3]);

  // creez socket
  int sub_socket = socket(PF_INET, SOCK_STREAM, 0);
  if(sub_socket < 0) {
     std::cout << "Socket error" << '\n';
    exit(EXIT_FAILURE);
  }
  subs_addr.sin_family = AF_INET;
  subs_addr.sin_port = htons(atoi(argv[3]));
  inet_aton(argv[2], &subs_addr.sin_addr);

  // cerere de conectare la server
  int check = connect(sub_socket, (struct sockaddr*)&subs_addr, sizeof(subs_addr));
  if(check < 0 ){
     std::cout << "Connect error" << '\n';
    exit(EXIT_FAILURE);
  }
  // trimitere id user catre server
  check = send(sub_socket, argv[1], strlen(argv[1]) + 1, 0);
  if(check < 0 ){
     std::cout << "ID send error" << '\n';
    exit(EXIT_FAILURE);
  }

  // primire validare id de la server
  memset(buffer, 0, BUFLEN);
  int id_validation = recv(sub_socket, buffer, sizeof(int), 0);
  if(id_validation < 0) {
     std::cout << "Error id validation" << '\n';
    exit(EXIT_FAILURE);
  }
  // caz in care serverul nu valideaza id-ul
  if(atoi(buffer) < 0) {
    // std::cout << "ID already in use" << '\n';
    return -1;
  }

  // dezactivare algoritm Nagle
  setsockopt(sub_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&neagleFlag, sizeof(int));

  FD_ZERO(&read_fds);
  FD_ZERO(&tmp_fds);
  FD_SET(sub_socket, &read_fds);
  fdmax = sub_socket;
  FD_SET(0, &read_fds);

  while(1) {
    tmp_fds = read_fds;
    int ret = select(sub_socket + 1, &tmp_fds, NULL, NULL, NULL);
    if(ret < 0) {
      std::cout << "Select error" << '\n';
      exit(EXIT_FAILURE);
    }
    // tratare caz cand primesc input de la stdin
    if(FD_ISSET(0, &tmp_fds)) {

      memset(buffer, 0, BUFLEN);
			fgets(buffer, BUFLEN - 1, stdin);
      if (strncmp(buffer, "exit", 4) == 0) {
        // std::cout << "Client closed" << '\n';
				break;
			}

      // tratare caz comanda diferita de exit
      Command* command = parse_client_command(buffer);
      if(command != NULL) {
        // trimitere comanda
        int bytes_send = send(sub_socket, (char*)command, sizeof(Command), 0);
        if(bytes_send < 0) {
           std::cout << "Send error" << '\n';
          exit(EXIT_FAILURE);
        }

        // validare daca s-a efectuat cu succes comanda primita
        memset(buffer, 0, BUFLEN);
        int command_validation = recv(sub_socket, buffer, sizeof(int),0);
        if(command_validation < 0) {
           std::cout << "Eroare validare comanda" << '\n';
          exit(EXIT_FAILURE);
        }
        if(atoi(buffer) == -1) {
          // std::cout << "Client is already subscribed." << '\n';
          continue;
        } else if(atoi(buffer) == -2) {
          //std::cout << "Client is not subscribed." << '\n';
          continue;
        } else if(atoi(buffer) == -3) {
          // std::cout << "Topic does not exist." << '\n';
          continue;
        }

        // afisare comanda
        if(strcmp(command->command_type, "subscribe") == 0 && atoi(buffer) > 0) {
          std::cout << "subscribed " << command->topic << "." << '\n';
        } else if(strcmp(command->command_type, "unsubscribe") == 0 && atoi(buffer) > 0){
          std::cout << "unsubscribed " << command->topic << "." << '\n';
        }
      } else {
        // std::cout << "Invalid command" << '\n';
        continue;
      }

    // caz primire mesaj de la server / client UDP
    } else if (FD_ISSET(sub_socket, &tmp_fds)){
      memset(buffer, 0, BUFLEN);
      int bytesreceived = recv(sub_socket, buffer, sizeof(TCPmessage), 0);
      if(bytesreceived < 0) {
         std::cout << "UDP message received error" << '\n';
        exit(EXIT_FAILURE);
      }
      if(bytesreceived == 0) {
        // std::cout << "Conexiunea cu serverul a fost inchisa" << '\n';
        break;
      }
      // afisare mesaj primit de la server
      TCPmessage* msg_from_server = (TCPmessage*)buffer;
      std::cout << msg_from_server->ip  << ":";
      std::cout << msg_from_server->port_number_udp  << " - ";
      std::cout << msg_from_server->topic  << " - ";
      std::cout << msg_from_server->data_type  << " - ";
      std::cout << msg_from_server->content  << endl;

    }
  }
  close(sub_socket);
  return 0;
}
