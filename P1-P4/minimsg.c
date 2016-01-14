/*
 *  Implementation of minimsgs and miniports.
 */
#include "minimsg.h"
#include "interrupts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#define N_TRUE 1
#define N_FALSE 0

/*
  * The miniport structure. Contains a field p_type which is 'u' for unbound ports and
  * and 'b' for bound ports. p_number holds the port number. Contains a union of bounded
  * and unbounded port types. Unbounded ports have a data queue and a semaphore to indicate
  * the availability of datagrams. Bounded ports have a remote network address and a remote
  * port number to which the data is being sent
  */
struct miniport
{
  char p_type;
  int p_number;
  union 
  {
    struct
    {
      queue_t *data;
      semaphore_t *data_ready;
    } unbound_t;
    struct
    {
      network_address_t remote_addr;
      int remote_unbound_port;
    } bound_t;
  };
};
 
//The number of bound ports. Wraps around to 0 when the count reaches MAX_PORTS
int nPorts;		
//An integer array that holds N_TRUE if the bounded port number is not in use and N_FALSE
//otherwise
int bound_ports_free[MAX_PORTS];
//The array of unbound ports. Used by the network handler in minithreads library to access
//a particular port and signal the threads waiting on that port.
miniport_t *unbound_ports[MAX_PORTS];
//Mutex variable for modifying the shared datastructures - nPorts, bound_ports_free
//Since it is allowed to disable interrupts for creating unbound ports, we don't use 
//a semaphore for the same
semaphore_t *mutex;

void
minimsg_initialize()
{
  //Initialize nPorts to 0 and the global mutex to 1, bound_ports_free to N_TRUE and unbound_ports to NULL
  nPorts = 0;
  mutex = semaphore_create();
  semaphore_initialize(mutex, 1);
  for(int i=0; i<MAX_PORTS; i++)
    {
      bound_ports_free[i] = N_TRUE;
      unbound_ports[i] = NULL;
    }
}

queue_t* minimsg_get_data_queue(int arg)
{
  return unbound_ports[arg]->unbound_t.data;
}

semaphore_t* minimsg_get_semaphore(int arg)
{
  return unbound_ports[arg]->unbound_t.data_ready;
}

miniport_t*
miniport_create_unbound(int port_number)
{
  // Unbound port number should be in the range 0-32767
  if(port_number > MAX_UNBOUND_PORT || port_number < MIN_UNBOUND_PORT)
  {
    return NULL;
  }
  
  interrupt_level_t old_level = set_interrupt_level(DISABLED);
  
  //If the port has already been created return a reference to the already created port
  if (unbound_ports[port_number] != NULL) {
    return unbound_ports[port_number];
  }
  
  //Allocate a new port and set the corresponding reference in the unbound_ports array
  miniport_t *newport = (miniport_t *) malloc(sizeof(miniport_t));
  if (!newport) {
    set_interrupt_level(old_level);
    return NULL;
  }
  unbound_ports[port_number] = newport;
  set_interrupt_level(old_level);
    
  // Set the type to unbounded, initialize the data queue, and the waiting semaphore to 0
  newport->p_type = 'u';
  newport->p_number = port_number;
  newport->unbound_t.data = queue_new();
  newport->unbound_t.data_ready = semaphore_create();
  semaphore_initialize(newport->unbound_t.data_ready, 0);
  
  return newport;
}

miniport_t*
miniport_create_bound(network_address_t addr, int remote_unbound_port_number)
{
  //Network address should not be null and remote_unbound_port_number should be in the range 0-32767
  if(!addr || remote_unbound_port_number < MIN_UNBOUND_PORT || remote_unbound_port_number > MAX_UNBOUND_PORT)
  {
    return NULL;
  }
  
  miniport_t *newport = (miniport_t *) malloc(sizeof(miniport_t));

  if (!newport) {
    return NULL;
  }
  
  newport->p_type = 'b'; 
  
  //Set initial port number value to -1 to check whether a free port was found or not
  newport->p_number = -1;

  //P the mutex semaphore to perform operations on the bound port queue
  semaphore_P(mutex);

  // If we have not reached the end of the port space, set ports_free[nPorts] to 0, assign
  // new port number and increment nPorts
  if (nPorts < MAX_PORTS - 1) {
    bound_ports_free[nPorts] = N_FALSE;
    newport->p_number = nPorts + MIN_BOUND_PORT;
    nPorts++;
  }
  // We have reached the end of the port space. Search for the first free port number
  else {
    for (int i = 0; i < MAX_PORTS; i++) {
      // If port found, set ports_free[i] to 0 and assign the port
      if (bound_ports_free[i] == N_TRUE) {
        bound_ports_free[i] = N_FALSE;
        newport->p_number = i + MIN_BOUND_PORT;
      }
    }
  }

  //V the mutex
  semaphore_V(mutex);

  // Check whether a free port number was found or not
  if (newport->p_number == -1) {
    return NULL;
  }

  network_address_copy(addr, newport->bound_t.remote_addr);	
  newport->bound_t.remote_unbound_port = remote_unbound_port_number;
  
  return newport;
}

void
miniport_destroy(miniport_t* miniport)
{
  assert(miniport);
  
  // If unbounded, then free the data queue and the data_ready semaphore
  if (miniport->p_type == 'u') {
    //Empty the data queue of the miniport before destroying the queue.
    interrupt_level_t old_level = set_interrupt_level(DISABLED);
    unbound_ports[miniport->p_number] = NULL;
    set_interrupt_level(old_level);

    void **item = NULL;
    while(queue_dequeue(miniport->unbound_t.data, item) != -1);

    int result = queue_free(miniport->unbound_t.data);
    assert(result == 0);
    semaphore_destroy(miniport->unbound_t.data_ready);
    
  }
  // If bounded, then free the remote_addr pointer
  else {
    //free(miniport->bound_t.remote_addr);
    semaphore_P(mutex);
    bound_ports_free[miniport->p_number - MIN_BOUND_PORT] = N_TRUE;
    semaphore_V(mutex);
  }
  // Free miniport
  free(miniport);
}

/*
 *
 * HEADER FORMAT - 
 *
 * -----------------------------------------------------------------------------------
 *| PROTOCOL | SOURCE ADDRESS | SOURCE PORT | DESTINATION ADDRESS | DESTINATION PORT  |
 * -----------------------------------------------------------------------------------
 * 0         1                9             11                    19                 21
 *
 */

int
minimsg_send(miniport_t* local_unbound_port, miniport_t* local_bound_port, minimsg_t* msg, int len)
{
  if(!local_unbound_port || !local_bound_port || !msg)
  {
    return 0;
  }
    
  if (len < 0 || len > MINIMSG_MAX_MSG_SIZE) 
    return -1;
  
  mini_header_t *header = (mini_header_t *) malloc(sizeof(mini_header_t));

  // Set the Protocol at appropriate position
  header->protocol = PROTOCOL_MINIDATAGRAM + '0';

  //Get the local source address
  network_address_t local_addr = {0, 0};
  network_get_my_address(local_addr);
  
  //Pack the local address' into the appropriate position
  pack_address(header->source_address, local_addr);

  //Pack the local unbound port's address that will be used by the receiver for sending back data
  pack_unsigned_short(header->source_port, (unsigned short) local_unbound_port->p_number);
  
  //Pack the remote address into the appropriate position
  pack_address(header->destination_address, local_bound_port->bound_t.remote_addr);
  
  //Pack the remote unbound port number into the appropriate position
  pack_unsigned_short(header->destination_port, (unsigned short) local_bound_port->bound_t.remote_unbound_port);

  //Send the datagram over the network
  int result = network_send_pkt(local_bound_port->bound_t.remote_addr, sizeof(mini_header_t), (char *) header, len, (char *) msg);

  if (result == -1) { 
    return result;
  }

  return (result - sizeof(mini_header_t));
}

int
minimsg_receive(miniport_t* local_unbound_port, miniport_t** new_local_bound_port, minimsg_t* msg, int *len)
{
  if(!local_unbound_port || !msg || !len)
  {
    return 0;
  }

  //Decrement the data_ready semaphore. Wait on it if there are no datagrams ready
  semaphore_P(local_unbound_port->unbound_t.data_ready);

  //Get the first argument from the data queue
  network_interrupt_arg_t *arg = NULL;

  queue_dequeue(local_unbound_port->unbound_t.data, (void **) &arg);  
  assert(arg);
  
  //Get the message and message length from the argument
  mini_header_t *header = (mini_header_t *) arg->buffer;
  char *message = arg->buffer + sizeof(mini_header_t);
  int message_length = arg->size - sizeof(mini_header_t);
  assert(message);
    
  //If the message length is 0 - no message, return
  if (message_length == 0) {
    msg = NULL;
    len = 0;
    free(arg);
    return 0;
  }

  //Get source address and port number
  network_address_t source_address;
  unpack_address(header->source_address, source_address);
  int source_port_number = unpack_unsigned_short(header->source_port);

  //Create a new local bound port for replying back to the sender
  *new_local_bound_port = miniport_create_bound(source_address, source_port_number);
  
  //Pass the message back to the msg parameter passed in the function.
  //NOTE - A security leak could be caused if just do - msg = message[i + sizeof(header)]
  //		 The application could go back to the header and retrieve data from/modify it causing
  //		 unexpected behavior
  for(int i=0; i < message_length && i < (*len); i++) {
    msg[i] = message[i];
  } 
  
  //Write the length of the message to the len parameter of the function
  *len = message_length;
  //Free the data packet before returning to the caller
  free(arg);

  //Return the size of the payload - header
  return message_length;
}

void handle_udp_packet(network_interrupt_arg_t *arg)
{
  if(arg->size <= sizeof(mini_header_t) || arg->size > MAX_NETWORK_PKT_SIZE)
  {
    free(arg);
    return;
  }

  mini_header_t *header = (mini_header_t *) arg->buffer;
  int port = unpack_unsigned_short(header->destination_port);
  
  if (unbound_ports[port] == NULL) {
    free(arg);
    return;
  }

  queue_append(minimsg_get_data_queue(port), arg);
  semaphore_V(minimsg_get_semaphore(port));
  return;
}
