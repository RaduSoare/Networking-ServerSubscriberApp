#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <unistd.h>
#include <string.h>
#include <unordered_map>
#include <vector>
#include <bits/stdc++.h>
#include "utils.h"
#include  <limits.h>
using namespace std;

/*
* Prelucreaza mesajul UDP in functie de tipul de date al mesajului
*/
void convert_udp_message(UDPmessage* udp_message, TCPmessage* tcp_message) {
  if(udp_message->data_type == 0) {
    strcpy(tcp_message->data_type, "INT");
    // consider contentul de dupa bitul de semn ca int
    long temp = ntohl(*(uint32_t*)(udp_message->content + 1));
    // verific bitul de semn
    if(udp_message->content[0] == 1) {
      temp *= -1;
    }
    // setez contentul din mesajul tcp
    sprintf(tcp_message->content, "%ld", temp);
    return;
  }
  if(udp_message->data_type == 1) {
    strcpy(tcp_message->data_type, "SHORT_REAL");
    double temp = ntohs(*(uint16_t*)(udp_message->content));
    temp = abs(temp / 100);
    sprintf(tcp_message->content, "%0.2f", temp);
    return;
  }
  if(udp_message->data_type == 2) {
    strcpy(tcp_message->data_type, "FLOAT");
    float temp = ntohl(*(uint32_t*)(udp_message->content + 1));
    temp = temp / pow(10, udp_message->content[5]);
    if(udp_message->content[0] == 1) {
      temp *= -1;
    }
    sprintf(tcp_message->content, "%0.4f", temp);
    return;
  }
  if(udp_message->data_type == 3) {
    strcpy(tcp_message->data_type, "STRING");
    strcpy(tcp_message->content, udp_message->content);
    return;
  }
}

/*
* Updateaza file_descriptorul maxim
*/
void update_fdmax(int& fdmax, fd_set* read_fds) {
    for (int i = fdmax; i > 2; --i) {
        if(FD_ISSET(i, read_fds)) {
            fdmax = i;
            break;
        }
    }
}

/*
* Cauta subscriberul dupa descriptor
*/
int find_subscriber(int client_searched, vector<Subscriber> subscribers) {
  int i = 0;
  for(i = 0; i < subscribers.size(); i++) {
    if(subscribers[i].descriptor == client_searched) {
      return i;
    }
  }
  return -1;
}

/*
* Inchide socketii activi la inchiderea serverului.
*/
void close_all_sockets(fd_set *read_fds, int fdmax) {
    for (int i = 2; i <= fdmax; ++i) {
        if (FD_ISSET(i, read_fds)) {
            close(i);
        }
    }
}

/*
* Cauta clientul dupa descriptor
*/
Client* find_client(int client_searched, vector<Client*> clients) {
  int i = 0;
  for(i = 0; i < clients.size(); i++) {
    if(clients[i]->client_descriptor == client_searched) {
      return clients[i];
    }
  }
  return NULL;
}

/*
* Verifica daca topicul exista, daca nu, atunci il creeaza
* Verifica daca clientul este deja in lista de clienti
* * daca clientul exista si vrea sa schimbe sf, atunci are loc doar modificarea
* * daca clientul exista si vrea sa se aboneze cu acelasi sf, se refuza abonarea
*/
int subscribe_action(int client_descriptor, Command* tcp_command,
  unordered_map<string, vector<Subscriber>>* topics, vector<Client*> clients) {

    // cauta clientul in lista de clienti
    Client* current = find_client(client_descriptor, clients);
    // creeaza o noua intrare in vectorul de subscriberi pt topic
    Subscriber subs;
    subs.sf = tcp_command->sf;
    subs.descriptor = current->client_descriptor;
    // caz in care topicul nu exista
    if(topics->find(tcp_command->topic) == topics->end()) {
      vector<Subscriber> subscribers;
      subscribers.push_back(subs);
      topics->insert({tcp_command->topic, subscribers});
    // caz in care topicul exista deja
    } else {
      // verific daca clientul exista deja
      int temp = find_subscriber(client_descriptor, topics->at(tcp_command->topic));
      // daca clientul exista, dar vrea sa modifice sf-ul
      if(temp != -1) {
        if(topics->at(tcp_command->topic).at(temp).sf != tcp_command->sf) {
          topics->at(tcp_command->topic).at(temp).sf = tcp_command->sf;
          return 1;
        }
        //std::cout << "Clientul este deja abonat la topic" << '\n';
        return -1;
      }
      // daca topicul exista, iar clientul nu e deja abonat
      // adaug clientul la topicul respectiv
      topics->at(tcp_command->topic).push_back(subs);
    }
    return 1;
  }

/*
* Se realizeaza dezabonarea doar daca topicul exista si clientul e abonat
* -3 -> cod eroare pentru topic inexistent
* -2 -> cod eroare client neabonat la topic
*/
int unsubscribe_action(int client_descriptor, char topic[],
  unordered_map<string, vector<Subscriber>>* topics) {
    // caz topic inexistent
    if(topics->find(topic) == topics->end()) {
      // std::cout << "Topicul nu exista" << '\n';
      return -3;
      // se sterge subscriberul din vectorul de abonati al topicului
    } else {
      vector<Subscriber> temp = topics->at(topic);
      int i = 0;
      for(i = 0; i < temp.size(); i++) {
        if(temp[i].descriptor == client_descriptor) {
          topics->at(topic).erase(topics->at(topic).begin() + i);
          break;
        }
      }
      if(i == temp.size()) {
        // std::cout << "Clientul nu este abonat la topic" << '\n';
        return -2;
      }
    }
    return 1;
  }
/*
* Se parcurge lista de topicuri
* Daca un abonat este online i se trimite mesajul imediat
* Daca un abonat este offline si are sf = 1, se retine mesajul intr-un vector
*/
void send_udp_message(TCPmessage* tcp_message,
  unordered_map<string, vector<Subscriber>>* topics,
  unordered_map<int, vector<TCPmessage>>* stored_msj,
  vector<Client*> clients) {

  // se creeaza topicul daca nu exista
  if(topics->find(tcp_message->topic) == topics->end()) {
    vector<Subscriber> subscribers;
    topics->insert({tcp_message->topic, subscribers});
  } else {
    vector<Subscriber> subscribers = topics->at(tcp_message->topic);
    for(int i = 0; i < subscribers.size(); i++) {
      Client* current = find_client(subscribers[i].descriptor, clients);
      // se trimite mesajul fiecarui abonat online la topic
      if(current->is_online == true) {
        int bytes_send = send(current->client_descriptor, (char*)tcp_message,
        sizeof(TCPmessage), 0);
        if(bytes_send < 0) {
           std::cout << "Cannot send udp message" << '\n';
          continue;
        }
      }
      // daca un client e ofline si are sf=1 retin mesajul
      else if(current->is_online == false && subscribers[i].sf == 1){
            if(stored_msj->find(current->client_descriptor) == stored_msj->end()) {
              vector<TCPmessage> messages;
              messages.push_back(*tcp_message);
              stored_msj->insert({current->client_descriptor, messages});
            } else {
              stored_msj->at(current->client_descriptor).push_back(*tcp_message);
            }

      }
    }
  }
}

/*
* Daca un client vrea sa se conecteze cu un nume deja existent, iar
* clientul cu acel nume este offline , se updateaza descriptorul
* altfel daca este online, nu se accepta conexiunea
*/
int add_new_client(vector<Client*>* clients, int fd, char id[]) {
  if(clients->size() > 0) {
    for(int i = 0; i < clients->size(); i++) {
        // daca un client cu acelasi id exista deja
        if(clients->at(i) != NULL && strcmp(clients->at(i)->client_id, id) == 0) {
          // daca clientul deja adaugat este offline
          if(clients->at(i)->is_online == false) {
            clients->at(i)->client_descriptor = fd;
            clients->at(i)->is_online = true;
          // daca clientul existent este online
          } else {
            //std::cout << "ID already in use" << '\n';
            return -1;
          }
        return 1;
      }
    }

  }
  // daca clientul nu exista, se creeaza unul nou
  Client* new_client = new Client;
  new_client->client_descriptor = fd;
  strcpy(new_client->client_id, id);
  new_client->is_online = true;
  new_client->sf = -1;
  clients->push_back(new_client);
  return 1;
}
  /*
  * Se parcurge lista cu mesaje de trimis dupa conectare si se trimit mesajele
  */
  void send_stored_msj(unordered_map<int, vector<TCPmessage>>* stored_msg, int fd) {
    for(auto x : *stored_msg) {
        if(x.first == fd) {
          vector<TCPmessage> msg = stored_msg->at(fd);
          for(int i = 0; i < msg.size(); i++) {
            int bytes_send = send(fd, (char*)&msg[i], sizeof(TCPmessage), 0);
            if(bytes_send < 0) {
              std::cout << "Cannot send udp message" << '\n';
              continue;
            }
          }
          stored_msg->at(x.first).clear();
        }

      }
  }


int main(int argc, char** argv) {

  char buffer[BUFLEN];
  int tcp_socket, udp_socket, port_number, new_tcp_socket;
  struct sockaddr_in tcp_addr, udp_addr, client_addr;
  int bytesreceived;
  int neagleFlag = 1;
  char id[20];
  int id_validation;

  unordered_map<int, vector<TCPmessage>> stored_msj;
  unordered_map<string, vector<Subscriber>> topics;
  vector<Client*> clients;

  socklen_t tcp_socket_len = sizeof(struct sockaddr_in);
  socklen_t udp_socket_len = sizeof(struct sockaddr_in);
  socklen_t client_len;

  fd_set read_fds;	// multimea de citire folosita in select()
	fd_set tmp_fds;		// multime folosita temporar
	int fdmax;			// valoare maxima fd din multimea read_fds

  // retin portul pe care se face conexiunea
  port_number = atoi(argv[1]);
  if(port_number <= 1024) {
    std::cout << "Busy port" << '\n';
    return -1;
  }

  // creez socket TCP
  tcp_socket = socket(PF_INET, SOCK_STREAM, 0);
  if(tcp_socket < 0) {
    std::cout << "Could not create socket TCP" << '\n';
    return -1;
  }

  memset((char *) &tcp_addr, 0, sizeof(tcp_addr));
  tcp_addr.sin_family = AF_INET;
  tcp_addr.sin_port = htons(port_number);
  tcp_addr.sin_addr.s_addr = INADDR_ANY;
  int tcp_bind = bind(tcp_socket, (struct sockaddr*)&tcp_addr, sizeof(struct sockaddr));
  if(tcp_bind < 0) {
    std::cout << "Error bind TCP" << '\n';
    return -1;
  }
  // pasivizare socket TCP
  int tcp_listener = listen(tcp_socket, MAX_CLIENTS);
  if(tcp_listener < 0) {
    std::cout << "Error listen" << '\n';
    return -1;
  }

  // creez socket UDP
  udp_socket = socket(PF_INET, SOCK_DGRAM, 0);
  if(udp_socket < 0) {
    std::cout << "Could not create socket UDP" << '\n';
    return -1;
  }
  udp_addr.sin_family = AF_INET;
  udp_addr.sin_port = htons(port_number);
  udp_addr.sin_addr.s_addr = INADDR_ANY;
  int udp_bind = bind(udp_socket, (struct sockaddr*)&udp_addr, udp_socket_len);
  if(udp_bind < 0) {
    std::cout << "Error bind UDP" << '\n';
    return -1;
  }

  FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);
  FD_SET(tcp_socket, &read_fds);
  FD_SET(udp_socket, &read_fds);
  FD_SET(0, &read_fds);
  fdmax = udp_socket;
  while(1) {
    memset(buffer, 0, BUFLEN);
    tmp_fds = read_fds;
    int check = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
    if(check < 0) {
      std::cout << "Error select" << '\n';
      exit(EXIT_FAILURE);
    }
    for(int i = 0; i <= fdmax; i++) {
      if(FD_ISSET(i, &tmp_fds)) {
        // verificare daca s-a scris in STDIN
        if(i == 0) {
          fgets(buffer, BUFLEN, stdin);
          if(strcmp(buffer, "exit\n") == 0) {
            // std::cout << "Server closed" << '\n';
            close_all_sockets(&read_fds, fdmax);
            return 0;
          } else {
            //std::cout << "Invalid command" << '\n';
          }
          // verificare daca s-a primit un mesaj UDP
        } else if(i == udp_socket) {
          UDPmessage* udp_message = new UDPmessage;
          TCPmessage* tcp_message = new TCPmessage;
          // citire mesaj UDP
          int bytesread = recvfrom(udp_socket, buffer, BUFLEN, 0,
            (struct sockaddr*)&udp_addr, &udp_socket_len);
          if(bytesread < 0) {
              std::cout << "UDP message is empty" << '\n';
              exit(EXIT_FAILURE);
          }

          udp_message = (UDPmessage*)buffer;
          strcpy(tcp_message->ip, inet_ntoa(udp_addr.sin_addr));
          tcp_message->port_number_udp = ntohs(udp_addr.sin_port);
          strncpy(tcp_message->topic, udp_message->topic, 50);
          tcp_message->topic[50] = '\0';
          // parsare mesaj UDP
          convert_udp_message(udp_message, tcp_message);
          // trimitere mesaje catre abonatii TCP
          send_udp_message(tcp_message, &topics, &stored_msj, clients);
        // verificare daca s-a primit cere de conexiune noua
        } else if(i == tcp_socket) {

            client_len = sizeof(tcp_addr);
            // accepta cererea venita pe socketul pasiv
            new_tcp_socket = accept(tcp_socket, (struct sockaddr*)&client_addr,
              &client_len);
            if(new_tcp_socket < 0) {
              std::cout << "New TCP socket error." << '\n';
              exit(EXIT_FAILURE);
            }

            // dezactivare algoritm Nagle
            setsockopt(new_tcp_socket, IPPROTO_TCP, TCP_NODELAY, (char *)&neagleFlag, sizeof(int));

            // se adauga noul socket la multimea descriptorilor
            FD_SET(new_tcp_socket, &read_fds);

            // se actualizeaza cel mai mare descriptor
            if(new_tcp_socket > fdmax) {
              fdmax = new_tcp_socket;
            }

            // se primeste ID-ul clientului conectat
            bytesreceived = recv(new_tcp_socket, buffer, 11, 0);
            if(bytesreceived < 0) {
              std::cout << "ID receiving failed" << '\n';
              exit(EXIT_FAILURE);
            }

            strcpy(id, buffer);
            // se incearca adaugarea noului client
            id_validation = add_new_client(&clients, new_tcp_socket, buffer);
            memset(buffer, 0, BUFLEN);
            sprintf(buffer, "%d", id_validation);
            // se trimite confirmare pentru validitatea id-ului
            int send_id_validation = send(new_tcp_socket, buffer, sizeof(int), 0);
            if(send_id_validation < 0) {
              std::cout << "Error id validation" << '\n';
              continue;
            }
            // daca s-a realizat conectarea se afiseaza mesajul de confirmare
            if(id_validation > 0) {
              std::cout << "New client " << id << " connected from "<<
              inet_ntoa(client_addr.sin_addr) << ":" << ntohs(client_addr.sin_port)
              << "." << '\n';
            }
            // trimitere mesaje stocate pentru clientii cu sf=1
            send_stored_msj(&stored_msj, new_tcp_socket);



        } else {
          // comanda de la client tcp
          memset(buffer, 0, BUFLEN);
					bytesreceived = recv(i, buffer, sizeof(buffer), 0);
          // verificare deconectare client
          if(!bytesreceived) {
            // caut clientul dupa id
            Client* current = find_client(i,clients);
            if(current != NULL) {
              std::cout << "Client " << current->client_id << " disconected." << '\n';
            }
            // caz in care clientul nu a fost adugat in lista de clienti
            if(current == NULL) {
              FD_CLR(i, &read_fds);
              update_fdmax(fdmax, &read_fds);
              close(i);
              continue;
            }
            // clientul este marcat ca offline dupa deconectare
            current->is_online = false;

            // eliminare descriptor dupa deconectare
            FD_CLR(i, &read_fds);
            update_fdmax(fdmax, &read_fds);
            close(i);
          } else {
            // s-a primit o comanda valida de subscribe/unsubscribe
            Command* tcp_command = (Command*)buffer;
            int check = 0;
            if(strcmp(tcp_command->command_type, "subscribe") == 0) {
              check = subscribe_action(i, tcp_command, &topics, clients);

            } else if(strcmp(tcp_command->command_type, "unsubscribe") == 0){
              check = unsubscribe_action(i, tcp_command->topic, &topics);
            }
            // validare finalizare comanda
            memset(buffer, 0, BUFLEN);
            sprintf(buffer, "%d", check);
            int command_validation = send(i, buffer, sizeof(int), 0);
            if(command_validation < 0) {
              // std::cout << "Eroare validare comanda" << '\n';
            }
          }
        }
      }
    }
  }
  close_all_sockets(&read_fds, fdmax);
  return 0;
}
