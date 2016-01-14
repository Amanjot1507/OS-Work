/*
 *	Implementation of minisockets.
 */

#include "minisocket.h"
#include "alarm.h"
#include <stdio.h>
#include "interrupts.h"

struct minisocket
{
  char socket_type;
  m_status socket_state;
  int local_port;
  network_address_t local_addr;
  int remote_port;
  network_address_t remote_addr;  
  queue_t *data;
  semaphore_t *data_ready;
  unsigned int seq_number;
  unsigned int ack_number;
  int ack_flag;
  semaphore_t *wait_for_ack;
  semaphore_t *send_receive_mutex;
};

minisocket_t *ports[N_PORTS];
static int n_client_ports;
//static int fragment_length = MAX_NETWORK_PKT_SIZE - sizeof(mini_header_reliable_t);
static void send_control_message(int, unsigned int, network_address_t, unsigned int,
				 unsigned int, unsigned int, minisocket_error *);
static void minisocket_free (minisocket_t *);
static network_address_t local_host;
static semaphore_t *ports_mutex;

void minisocket_initialize()
{
  for (int i = 0; i < N_PORTS; i++) {
    ports[i] = NULL;
  }
  ports_mutex = semaphore_create();
  semaphore_initialize(ports_mutex, 1);
  
  n_client_ports = MIN_CLIENT_PORT;
  network_get_my_address(local_host);
}

static mini_header_reliable_t *create_control_header(int msg_type, unsigned int dest_port,
						    network_address_t dest_addr, unsigned int src_port,
						     unsigned int seq, unsigned int ack)
{
  mini_header_reliable_t *header = (mini_header_reliable_t *) malloc(sizeof(mini_header_reliable_t));
  if (!header) {
    return NULL;
  }
    
  header->protocol = PROTOCOL_MINISTREAM + '0';

  network_address_t local_addr;
  network_address_copy(local_host, local_addr);
  //network_get_my_address(local_addr);

  pack_address(header->source_address, local_addr);
  pack_unsigned_short(header->source_port, src_port);

  header->message_type = msg_type + '0';
  pack_address(header->destination_address, dest_addr);
  pack_unsigned_short(header->destination_port, dest_port);
  pack_unsigned_int(header->seq_number, seq); 
  pack_unsigned_int(header->ack_number, ack); 
  return header;
}

static void send_control_message(int msg_type, unsigned int dest_port,
				 network_address_t dest_addr, unsigned int src_port,
				 unsigned int seq, unsigned int ack, minisocket_error *error)
{
  mini_header_reliable_t *header = create_control_header(msg_type, dest_port,
							 dest_addr, src_port,
							 seq, ack);
  if (!header) {
    *error = SOCKET_OUTOFMEMORY;
    return;
  }

  if (network_send_pkt(dest_addr, sizeof(mini_header_reliable_t), (char *) header, 0, NULL) == -1)
  {
    *error = SOCKET_SENDERROR;
    return;
  }
}

minisocket_t* minisocket_server_create(int port, minisocket_error *error)
{
  if (port < MIN_SERVER_PORT || port > MAX_SERVER_PORT) {
    *error = SOCKET_INVALIDPARAMS;
    return NULL;
  }
  if (ports[port]) {
    *error = SOCKET_PORTINUSE;
    return NULL;
  }

  minisocket_t *new_socket =  (minisocket_t *) malloc(sizeof(minisocket_t));
  if (!new_socket) {
    *error = SOCKET_OUTOFMEMORY;
    return NULL;
  }
  // Initialize new socket
  new_socket->socket_state = INITIAL;
  semaphore_P(ports_mutex);
  ports[port] = new_socket;
  semaphore_V(ports_mutex);
  new_socket->socket_type = 's';
  new_socket->local_port = port;
  network_address_copy(local_host, new_socket->local_addr);

  new_socket->data = queue_new();
  new_socket->data_ready = semaphore_create();
  new_socket->ack_flag = 0;
  new_socket->wait_for_ack = semaphore_create();
  new_socket->send_receive_mutex = semaphore_create();
  semaphore_initialize(new_socket->data_ready, 0);
  semaphore_initialize(new_socket->wait_for_ack, 0);
  semaphore_initialize(new_socket->send_receive_mutex, 1);

  interrupt_level_t old_level;
  while (1) {
    new_socket->seq_number = 0;
    new_socket->ack_number = 0;
    new_socket->remote_port = -1;
    new_socket->remote_addr[0] = 0;
    new_socket->remote_addr[1] = 0;
    new_socket->socket_state = WAITING_SYN;
    
    // receiving SYN
    while (1) {
      semaphore_P(new_socket->data_ready);
      network_interrupt_arg_t *arg = NULL;
      queue_dequeue(new_socket->data, (void **) &arg);
      mini_header_reliable_t *header = (mini_header_reliable_t *) arg->buffer;

      if (header->message_type -'0'== MSG_SYN) {
	unpack_address(header->source_address, new_socket->remote_addr);
	new_socket->remote_port = unpack_unsigned_short(header->source_port);
	new_socket->socket_state = WAITING_ACK;
	new_socket->seq_number = 0;
	new_socket->ack_number = 1;
	break;
      }
      else {
	free(arg);
      }
    }
    minisocket_error s_error;
    int wait_val = 100;
    while (wait_val <= 12800) {
      send_control_message(MSG_SYNACK, new_socket->remote_port, new_socket->remote_addr, new_socket->local_port, 0 , 1, &s_error);
      new_socket->seq_number = 1;
      new_socket->ack_number = 1;
      if (s_error == SOCKET_OUTOFMEMORY) {
	minisocket_free(new_socket);
	semaphore_P(ports_mutex);
	ports[new_socket->local_port] = NULL;
	semaphore_V(ports_mutex);
	*error = s_error;
	return NULL;
      }
      alarm_id a = register_alarm(wait_val, (alarm_handler_t) semaphore_V, new_socket->data_ready);
      semaphore_P(new_socket->data_ready);
      old_level = set_interrupt_level(DISABLED);
      if (queue_length(new_socket->data)) {
	deregister_alarm(a);
      }
      set_interrupt_level(old_level);
      if (!queue_length(new_socket->data)) {
	wait_val *= 2;
	continue;
      }
      network_interrupt_arg_t *arg = NULL;
      queue_dequeue(new_socket->data, (void **) &arg); 
      mini_header_reliable_t *header = (mini_header_reliable_t *) arg->buffer;
      network_address_t saddr;
      unpack_address(header->source_address, saddr);
      int sport = unpack_unsigned_short(header->source_port);
      if (header->message_type - '0' == MSG_SYN) {

	if (new_socket->remote_port == sport && network_compare_network_addresses(new_socket->remote_addr, saddr)) {
	  continue;
	}
	send_control_message(MSG_FIN, sport, saddr, new_socket->local_port, 0, 0, &s_error);
      }
      if (header->message_type - '0' == MSG_ACK) {

      	if (new_socket->remote_port == sport && network_compare_network_addresses(new_socket->remote_addr, saddr)) {
	  
	  network_interrupt_arg_t *packet = NULL;
	  while (queue_dequeue(new_socket->data, (void **)&packet) != -1) {
	    free(packet);
	  }
	  semaphore_initialize(new_socket->data_ready, 0);
	  new_socket->socket_state = OPEN;
	  new_socket->seq_number = 1;
	  new_socket->ack_number = 2;
	  return new_socket;
	}
      }
      free (arg);
    }
  }
  return NULL;
}

minisocket_t* minisocket_client_create(const network_address_t addr, int port, minisocket_error *error)
{

  if (!addr || port < MIN_SERVER_PORT || port > MAX_SERVER_PORT) {
    *error = SOCKET_INVALIDPARAMS;
    return NULL;
  }

  int port_val = -1;
  // CHECK what n_client_ports does
  if (!ports[n_client_ports]) {
    port_val = n_client_ports;
  }
  else {
    for (int i = MIN_CLIENT_PORT; i <= MAX_CLIENT_PORT; i++) {
      if (!ports[i]) {
	port_val = i;
	break;
      }
    }
  }
  n_client_ports = port_val + 1;
  if (n_client_ports > MAX_CLIENT_PORT) {
    n_client_ports = 0;
  }
    
  if (port_val == -1) {
    *error = SOCKET_NOMOREPORTS;
    return NULL;
  }

  minisocket_t *new_socket =  (minisocket_t *) malloc(sizeof(minisocket_t));
  if (!new_socket) {
    *error = SOCKET_OUTOFMEMORY;
    return NULL;
  }
  new_socket->socket_state = INITIAL;
  semaphore_P(ports_mutex);
  ports[port_val] = new_socket;
  semaphore_V(ports_mutex);
  new_socket->socket_type = 'c';

  network_address_copy(local_host, new_socket->local_addr);
  //network_get_my_address(new_socket->local_addr);

  new_socket->local_port = port_val;
  network_address_copy(addr, new_socket->remote_addr);
  new_socket->remote_port = port;
  new_socket->data = queue_new();
  new_socket->data_ready = semaphore_create();
  new_socket->wait_for_ack = semaphore_create();
  new_socket->send_receive_mutex = semaphore_create();
  semaphore_initialize(new_socket->data_ready, 0);
  semaphore_initialize(new_socket->wait_for_ack, 0);
  semaphore_initialize(new_socket->send_receive_mutex, 1);
  new_socket->socket_state = WAITING_SYNACK;
  
  minisocket_error s_error;
  int wait_val = 100;
  interrupt_level_t old_level;
  while (wait_val <= 12800) {

    send_control_message(MSG_SYN, new_socket->remote_port, new_socket->remote_addr, new_socket->local_port, 0, 0, &s_error);
    new_socket->seq_number = 1;
    new_socket->ack_number = 0;   
    if (s_error == SOCKET_OUTOFMEMORY) {
      minisocket_free(new_socket);
      semaphore_P(ports_mutex);
      ports[new_socket->local_port] = NULL;
      semaphore_V(ports_mutex);
      *error = s_error;
      return NULL;
    }
    
    alarm_id a = register_alarm(wait_val, (alarm_handler_t) semaphore_V, new_socket->data_ready);
    semaphore_P(new_socket->data_ready);
    old_level = set_interrupt_level(DISABLED);
    if (queue_length(new_socket->data)) {
      deregister_alarm(a);
    }
    set_interrupt_level(old_level);
    if (!queue_length(new_socket->data)) {
      wait_val *= 2;
      continue;
    }
    network_interrupt_arg_t *arg = NULL;
    queue_dequeue(new_socket->data, (void **) &arg);
    mini_header_reliable_t *header = (mini_header_reliable_t *) arg->buffer;
    network_address_t saddr;
    unpack_address(header->source_address, saddr);
    int sport = unpack_unsigned_short(header->source_port);
    if (sport == new_socket->remote_port && network_compare_network_addresses(saddr, new_socket->remote_addr)) {
      if (header->message_type - '0' == MSG_SYNACK) {
	new_socket->seq_number = 1;
	new_socket->ack_number = 1;
	send_control_message(MSG_ACK, new_socket->remote_port, new_socket->remote_addr, new_socket->local_port, 1, 1, &s_error);
	if (s_error == SOCKET_OUTOFMEMORY) {
	  minisocket_free(new_socket);
	  semaphore_P(ports_mutex);
	  ports[new_socket->local_port] = NULL;
	  semaphore_V(ports_mutex);
	  *error = s_error;
	  return NULL;
	}
	network_interrupt_arg_t *packet = NULL;
	while (queue_dequeue(new_socket->data, (void **)&packet) != -1) {
	  free(packet);
	}
	semaphore_initialize(new_socket->data_ready, 0);
	new_socket->socket_state = OPEN;
	new_socket->seq_number = 2;
	new_socket->ack_number = 1;
	return new_socket;
      }
      if (header->message_type -'0' == MSG_FIN) {
	minisocket_free(new_socket);
	return NULL;
      }
    }
    free(arg);
  }
  minisocket_free(new_socket);
  return NULL;
}

int minisocket_send(minisocket_t *socket, const char *msg, int len, minisocket_error *error)
{
  if (socket->socket_state == CLOSED || socket->socket_state == CLOSING) {
    *error=SOCKET_SENDERROR;
    return 0;
  }
  if (!socket || socket->socket_state != OPEN || !msg || len == 0)
  {
    *error = SOCKET_INVALIDPARAMS;
    return -1;
  }
  semaphore_P(socket->send_receive_mutex);
  int fragment_length = MAX_NETWORK_PKT_SIZE - sizeof(mini_header_reliable_t);
  int transfer_length = len > fragment_length ? fragment_length : len;
  //int start_byte = 0;
  int sent_byte = 0;

  do {
    int wait = 100;
    mini_header_reliable_t *header = create_control_header(MSG_ACK, socket->remote_port,
							   socket->remote_addr, socket->local_port,
							   socket->seq_number, socket->ack_number);
    
    transfer_length = len - sent_byte > fragment_length ? fragment_length : len - sent_byte;
    while (wait <= 12800) {
      socket->ack_flag = 0;
      int res = network_send_pkt(socket->remote_addr, sizeof(mini_header_reliable_t),
				 (char *)header, transfer_length, msg + sent_byte);
      socket->seq_number += transfer_length;
      if (res == -1) {
        *error = SOCKET_SENDERROR;
	semaphore_V(socket->send_receive_mutex);
	return (sent_byte == 0) ? -1 : sent_byte; 
      }
      alarm_id a = register_alarm(wait, (alarm_handler_t) semaphore_V, socket->data_ready);
      semaphore_P(socket->wait_for_ack);
      if (socket->socket_state == CLOSED || socket->socket_state == CLOSING) {
	*error=SOCKET_SENDERROR;
	return 0;
      }
      interrupt_level_t old_level = set_interrupt_level(DISABLED);
      // Function was woken up by the firing of the alarm
      if (socket->ack_flag == 0) {
        wait *= 2;
	socket->seq_number -= transfer_length;
	semaphore_V(socket->send_receive_mutex);
        set_interrupt_level(old_level);
        continue;
      }
      // Function was woken up by the network handler
      else if (socket->ack_flag == 1) {
	// ACK has been received
        deregister_alarm(a);
	sent_byte += transfer_length;
	semaphore_V(socket->send_receive_mutex);
        set_interrupt_level(old_level);
        break;
      }
    }
    if (wait > 12800) {
      *error = SOCKET_SENDERROR;
      semaphore_V(socket->send_receive_mutex);
      return (sent_byte == 0) ? -1 : sent_byte; 
    }
  } while (sent_byte != len);
  semaphore_V(socket->send_receive_mutex);
  return len;
}

int minisocket_receive(minisocket_t *socket, char *msg, int max_len, minisocket_error *error)
{  
  if (socket->socket_state == CLOSED || socket->socket_state == CLOSING) {
    *error=SOCKET_RECEIVEERROR;
    return 0;
  }
  if (socket->socket_state != OPEN || !msg) {
    *error = SOCKET_INVALIDPARAMS;
    return -1;
  } 

  if(max_len == 0)
  {
    *error = SOCKET_NOERROR;
    return 0;
  }
  semaphore_P(socket->send_receive_mutex);
  semaphore_P(socket->data_ready);
  if (socket->socket_state == CLOSED || socket->socket_state == CLOSING) {
    *error=SOCKET_RECEIVEERROR;
    return 0;
  }

  network_interrupt_arg_t *arg;
  queue_dequeue(socket->data, (void **) &arg);
  int msg_len = arg->size - sizeof(mini_header_reliable_t);
  assert(msg_len > 0);

  int copy_len = ( msg_len > max_len) ? max_len : msg_len;

  for(int i=0;i<copy_len;i++)
  {
    msg[i] = arg->buffer[sizeof(mini_header_reliable_t) + i];
  }

  //If the packet contained more data than the buffer could take, then enqueue the packet with the remaining message again
  if(copy_len > max_len)
  {
    mini_header_reliable_t *h = (mini_header_reliable_t *) malloc(sizeof(mini_header_reliable_t));

    if(!h)
    {
      *error = SOCKET_OUTOFMEMORY;
      semaphore_V(socket->send_receive_mutex);
      return -1;
    }

    
    mini_header_reliable_t *header = (mini_header_reliable_t *) arg->buffer;
    //Update the sequence number in the header
    unsigned int seq_no = unpack_unsigned_int(header->seq_number);
    pack_unsigned_int(header->seq_number, seq_no + max_len);
    //Copy the remaining message to the start of the message space in the buffer
    memcpy(arg->buffer + sizeof(mini_header_reliable_t), arg->buffer + sizeof(mini_header_reliable_t) + max_len, msg_len - max_len);
    //Set the packet size to its correct length
    arg->size = msg_len + sizeof(mini_header_reliable_t);
    //Prepend the packet in the data queue
    queue_prepend(socket->data, &arg);
  }
  semaphore_V(socket->send_receive_mutex);
  return copy_len;
}

static void minisocket_free (minisocket_t * socket) {
  network_interrupt_arg_t *packet = NULL;
  while (queue_dequeue(socket->data, (void **)&packet) != -1) {
    free(packet);
  }
  queue_free(socket->data);
  socket->data = NULL;
  semaphore_destroy(socket->data_ready);
  semaphore_destroy(socket->wait_for_ack);
  semaphore_destroy(socket->send_receive_mutex);
  semaphore_P(ports_mutex);
  ports[socket->local_port] = NULL;
  semaphore_V(ports_mutex);
  free(socket);
}
  
void minisocket_close(minisocket_t *socket)
{
  if (socket) {
    if (socket->socket_state == CLOSING) {
      socket->socket_state = WAITING_SYN;
      return;
    }
    if (socket->socket_state == CLOSED) {
      minisocket_free(socket);
      return;
    }

    minisocket_error s_error;
    int wait_val = 100;
    while (wait_val <= 12800) {
      send_control_message(MSG_FIN, socket->remote_port, socket->remote_addr,
			   socket->local_port, socket->seq_number, socket->ack_number, &s_error);
      socket->seq_number += 1;
      if (s_error == SOCKET_OUTOFMEMORY) {
	return;
      }
      
      alarm_id a = register_alarm(wait_val, (alarm_handler_t) semaphore_V, socket->wait_for_ack);
      semaphore_P(socket->wait_for_ack);
      interrupt_level_t old_level = set_interrupt_level(DISABLED);
      if (socket->ack_flag == 0) {
	wait_val *= 2;
	socket->seq_number--;
	set_interrupt_level(old_level);
	continue;
      }
      else if (socket->ack_flag == 1) {
	deregister_alarm(a);
	minisocket_free(socket);
	set_interrupt_level(old_level);
	return;
      }
    }
  }
  semaphore_V(socket->send_receive_mutex);
}
    
void minisocket_handle_tcp_packet(network_interrupt_arg_t *arg)
{
  mini_header_reliable_t *header = (mini_header_reliable_t *) (arg->buffer);
  int port = unpack_unsigned_short(header->destination_port);
  if (port < MIN_SERVER_PORT || port > MAX_CLIENT_PORT || !ports[port]) {
    free(arg);
    return;
  }
  if (ports[port]->socket_state == INITIAL || ports[port]->socket_state == CLOSED) {
    free(arg);
    return;
  }

  if (ports[port]->socket_state != OPEN) {
    // handled in handshake
    queue_append(ports[port]->data, arg);
    semaphore_V(ports[port]->data_ready);
    return;
  }

  minisocket_error s_error;
  network_address_t saddr;
  unpack_address(header->source_address, saddr);
  int sport = unpack_unsigned_short(header->source_port);
  if (!network_compare_network_addresses(ports[port]->remote_addr, saddr) || ports[port]->remote_port != sport) {
    if(header->message_type -'0' == MSG_SYN)
    {
      send_control_message(MSG_FIN, sport, saddr, port, 0, 0, &s_error);
    }

    free(arg);
    return;    
  }

  //If the message is of type SYNACK and from the same client
  if (header->message_type - '0' == MSG_SYNACK) {
    send_control_message(MSG_ACK, sport, saddr, port, 0, 0, &s_error);
    free(arg);
    return;
  }

  //If the message is of type FIN
  if (header->message_type - '0' == MSG_FIN) {
    ports[port]->ack_number += 1;
    send_control_message(MSG_ACK, sport, saddr, port, ports[port]->seq_number, ports[port]->ack_number, &s_error);
    ports[port]->socket_state = CLOSING;
    int count = semaphore_get_count(ports[port]->data_ready);
    while (count < 0) {
      semaphore_V(ports[port]->data_ready);
      count++;
    }
    register_alarm(15000, (alarm_handler_t) minisocket_close, ports[port]); 
    //minisocket_free(ports[port]);
    free(arg);
    return;
  }

  //If the message is of type ACK from the same client
  if (header->message_type - '0' == MSG_ACK)
  {
    unsigned int ack_no = unpack_unsigned_int(header->ack_number);
    int packet_size = arg->size - sizeof(mini_header_reliable_t);
    minisocket_error s_error;
    //If it's a correct acknowledgement of the sent data, enqueue it and V the wait for ack semaphore
    if (ack_no == ports[port]->seq_number/* + MAX_NETWORK_PKT_SIZE - sizeof(mini_header_reliable_t) + 1*/) {
      if (packet_size != 0) {
	queue_append(ports[port]->data, arg);
	semaphore_V(ports[port]->data_ready);
        ports[port]->ack_number += packet_size;
        send_control_message(MSG_ACK, sport, saddr, port, ports[port]->seq_number, ports[port]->ack_number, &s_error);
      }
      if (ports[port]->ack_flag == 0) {
        ports[port]->ack_flag = 1;
        semaphore_V(ports[port]->wait_for_ack);
      }
      return;
    }
    else {
      free(arg);
      return;
    }
  }
}
