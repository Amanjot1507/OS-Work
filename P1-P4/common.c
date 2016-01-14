/*
 * Implementations for common utilities.
 *
 */
#include <stdlib.h>
#include <stdio.h>
#include "common.h"
#include "minimsg.h"
#include "minisocket.h"
#include "interrupts.h"

 // Add any function implementations here

 /*
 * this function sends the received packet to the corresponding port
 * if port not found, drops the packet i.e. frees it and returns
 */
void network_handler(void *a)
{
  if (!a) {
    return;
  }

  int old_level = set_interrupt_level(DISABLED);	
  network_interrupt_arg_t *arg = (network_interrupt_arg_t *) a;

  if(arg->size < sizeof(mini_header_t))
  {
  	free(a);
  	set_interrupt_level(old_level);
  	return;
  }

  mini_header_t *header = (mini_header_t *) (arg->buffer);

  if(header->protocol == PROTOCOL_MINIDATAGRAM)
  {
  	handle_udp_packet(arg);
  }
  else if(header->protocol == PROTOCOL_MINISTREAM)
  {
  	handle_tcp_packet(arg);
  }

}