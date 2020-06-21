#define BUFLEN 1600
#define MAX_CLIENTS INT_MAX
#define ID_LEN 20

typedef struct {
  char command_type[13];
  char topic[51];
  int sf;
}Command;

typedef struct {
  char topic[50];
  uint8_t data_type;
  char content[1501];
}UDPmessage;

typedef struct {
  char topic[51];
  char data_type[11];
  char content[1501];
  char ip[16];
  int port_number_udp;
}TCPmessage;

typedef struct{
  int sf;
  char client_id[11];
  int client_descriptor;
  bool is_online;
}Client;

typedef struct {
  int descriptor;
  int sf;
}Subscriber;
