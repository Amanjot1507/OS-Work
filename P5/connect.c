#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ifaddrs.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>
#include "global.h"
#include <ctype.h>

/* Information about files and sockets is kept here.  An outgoing connection is one
 * requested by the local user using the 'C' command.  An incoming connection is
 * one created by a remote user.  We don't try to re-establish incoming connections
 * after they break.
 */
struct file_info {
	struct file_info *next;						// linked list management
	char *uid;									// unique connection id
	int fd;										// file descriptor
	enum file_info_type {
		FI_FREE,							// unused entry
		FI_FILE,							// file descriptor, probably stdin
		FI_SERVER,							// server socket
		FI_INCOMING,						// incoming connection
		FI_OUTGOING,						// outgoing connection
	} type;										// type of file
	enum { FI_UNKNOWN, FI_KNOWN } status;		// is the peer address known?
	struct sockaddr_in addr;					// address of peer if known
	void (*handler)(struct file_info *, int events);		// event handler`
	int events;									// POLLIN, POLLOUT
	char *input_buffer;							// stuff received
	int amount_received;						// size of input buffer
	char *output_buffer;						// stuff to send
	int amount_to_send;							// size of output buffer
	union {
		struct fi_outgoing {
			enum { FI_CONNECTING, FI_CONNECTED } status;
			double connect_time;			// time of last attempt to connect
		} fi_outgoing;
	} u;
};
struct file_info *file_info;
int nfiles;
char *uid_gen = (char *) 1;
int gossip_gen = 0;
int *dist = NULL, *prev = NULL, *graph = NULL;
struct sockaddr_in my_addr;
void send_gossip();

void report_error(int result, char *function, int errsv)
{
	if(result == -1)
		fprintf(stderr, "%s() failed. Error %d\n", function, errsv);

	return;
}


/* Add file info about 'fd'.
 */
static struct file_info *file_info_add(enum file_info_type type, int fd,
						void (*handler)(struct file_info *, int events), int events){
	struct file_info *fi = calloc(1, sizeof(*fi));
	fi->uid = uid_gen++;
	fi->type = type;
	fi->fd = fd;
	fi->handler = handler;
	fi->events = events;
	fi->next = file_info;
	file_info = fi;
	nfiles++;
	return fi;
}

/* Destroy a file_info structure.
 */
static void file_info_delete(struct file_info *fi){
	struct file_info **pfi, *fi2;

	for (pfi = &file_info; (fi2 = *pfi) != 0; pfi = &fi2->next) {
		if (fi2 == fi) {
			*pfi = fi->next;
			break;
		}
	}
	free(fi->input_buffer);
	free(fi->output_buffer);
	free(fi);
}

/* Put the given message in the output buffer.
 */
void file_info_send(struct file_info *fi, char *buf, int size){
	fi->output_buffer = realloc(fi->output_buffer, fi->amount_to_send + size);
	memcpy(&fi->output_buffer[fi->amount_to_send], buf, size);
	fi->amount_to_send += size;
}

/* Same as file_info_send, but it's done on sockaddr_in instead of file_info
 */
struct file_info* sockaddr_to_file(struct sockaddr_in dst) {
    struct file_info* fi;
    for (fi = file_info; fi != 0; fi = fi->next) {
        if (addr_cmp(dst, fi->addr) == 0) {
            return fi;
        }
    }
    printf("sockaddr not connected to host");
    return NULL;
}

/* Broadcast to all connections except fi.
 */
void file_broadcast(char *buf, int size, struct file_info *fi){
	printf("In send broadcast\n");
	struct file_info *fi2;
	for (fi2 = file_info; fi2 != 0; fi2 = fi2->next) {
	  char * fi2_addr = addr_to_string(fi2->addr);
		//printf("Checking %s type = %d outgoing status = %d\n", fi2_addr, fi2->type, fi2->u.fi_outgoing.status);
		if (fi2->type == FI_FREE || fi2 == fi) {
			//printf("C1\n");
			continue;
		}
		if (fi2->type == FI_OUTGOING && fi2->u.fi_outgoing.status == FI_CONNECTING) {
			//printf("C2\n");
			continue;
		}
		if (fi2->type == FI_OUTGOING || fi2->type == FI_INCOMING) {
			//printf("Sending broadcast\n");
			file_info_send(fi2, buf, size);
		}
	}
	//printf("C3\n");
}

/*
 * Send the gossip to connected peers. 
 */
void send_gossip()
{
	int size = 256;
	char *payload = (char *) malloc(size);
	char *g;
	int index = 0;
	struct file_info *fi;

	for(fi=file_info;fi!=NULL;fi=fi->next)
	{
	  char *addr = addr_to_string(fi->addr);
		printf("Checking %s type = %d outgoing status = %d\n", addr, fi->type,fi->u.fi_outgoing.status);
		if(fi->type == FI_INCOMING || (fi->type == FI_OUTGOING && fi->status == FI_KNOWN && fi->u.fi_outgoing.status == FI_CONNECTED))
		{
		  printf("Address extracted - %s\n", addr);
			if(strlen(addr) + index > size)
			{
				size *= 2;
				payload = realloc(payload, size);
			}
			if (index == 0) {
				strcpy(payload, ";");
				sprintf(payload, "%s%s", payload, addr);		
			}
			else {
				sprintf(payload, "%s;%s", payload, addr);
			}
			printf("Gossip Payload = %s\n", payload);
			index += strlen(addr) + 1;
		}
		free(addr);
	}
	
	payload[index++] = '\n';
	payload[index++] = '\0';
	char *my_addr_str = (char *) addr_to_string(my_addr);
	gossip_gen++;
	g = (char *) malloc(sizeof(char)*(strlen(payload) + strlen(my_addr_str) + 4 + 4));
	printf ("payload length = %d, my_addr length = %d\n", strlen(payload), strlen(my_addr_str));
	memset(g, 0, strlen(payload) + strlen(my_addr_str) + 8);
	sprintf(g, "G%s/%d/%s", my_addr_str, gossip_gen, payload);
	printf("Gossip created - %s\n", g);
	printf("DONE\n");
	file_broadcast(g, strlen(g), NULL);
	free(my_addr_str);
}

void updateGraph()
{
  if(dist) {
    free(dist);
    dist = NULL;
  }
  if(prev) {
    free(prev);
    prev = NULL;
  }
  if(graph) {
    free(graph);
    graph = NULL;
  }
int r= 0, s=0;
  dist = (int *) malloc(nl_nsites(nl) * sizeof(int));
  prev = (int *) malloc(nl_nsites(nl) * sizeof(int));
  graph = (int *) malloc(sizeof(int) * (nl_nsites(nl)) * (nl_nsites(nl)));
	for(r=0;r<nl_nsites(nl);r++)
    	{
    		dist[r] = INFINITY;
    		prev[r] = UNDEFINED;
    	for(s=0;s<nl_nsites(nl);s++)
    	{
    		graph[INDEX(r, s, nl_nsites(nl))] = 0;
    	}
    }


	struct gossip *g = gossip;

	while(g != NULL)
	{
		int len = strlen(gossip_latest(g));
		char *lat = (char *) malloc(sizeof(char) * (len+1));
		strcpy(lat, gossip_latest(g));

		char *addr = lat;

		char *ctr = index(addr, '/');		
		*ctr++ = 0;

		char *payload = index(ctr, '/');		
		*payload++ = 0;

		char *token = strtok(payload, ";");	

		while (token) 
		{	
			//printf("Address = %s\n", addr);
			//printf("Token = %s\n", token);	
			set_dist(nl, graph, nl_nsites(nl), addr, token, 1);
			token = strtok(NULL, ";");
		}
		g = gossip_next(g);
		free(lat);
	}
	char *my_addr_str = addr_to_string(my_addr);
	int my_index = nl_index(nl, my_addr_str);
	// update connections of immediate neighbours
	struct file_info *f = file_info;
	while (f) {
		char *addr = addr_to_string(f->addr);

		if(strcmp(addr,my_addr_str ) != 0 && (f->type == FI_INCOMING || (f->type == FI_OUTGOING && f->status == FI_KNOWN && f->u.fi_outgoing.status == FI_CONNECTED)))
		{
			set_dist(nl, graph, nl_nsites(nl), addr, my_addr_str, 1);
		}

		f = f->next;
		free(addr);
	}
	free(my_addr_str);
	// call graph on updated graph
	dijkstra(graph, nl_nsites(nl), my_index, dist, prev);
	
	printf("PRINTING GRAPH\n");
    for(r=0;r<nl_nsites(nl);r++)
    {
    	for(s=0;s<nl_nsites(nl);s++)
    	{
    		printf("%d ", graph[INDEX(r, s, nl_nsites(nl))]);
    	}

    	printf("\n");
    }

	printf("\nPRINTING DISTANCE\n");
	for(r=0;r<nl_nsites(nl);r++)
	{
	  printf("Distance to Site [%d] %s = %d\n", r, nl_name(nl,r), dist[r]);
	}

	printf("\nPRINTING PREV\n");
	for(r=0;r<nl_nsites(nl);r++)
	{
	  printf("Previous to Site [%d] %s = %d\n", r, nl_name(nl,r), prev[r]);
	}
}

void send_handler(struct file_info *fi, char *msg)
{
  	printf ("SEND HANDLER CALLED\n");
  	if(isalpha(*msg))
  	{
  		printf("Incorrect send command\n");
  		return;
  	}	

	int len = strlen(msg);
	char *ctr = index(msg, '/');
	*ctr++ = 0;

	char *payload = rindex(ctr, '/');
	*payload++ = 0;
	char *my_addr_str = addr_to_string(my_addr);
	if(strcmp(msg, my_addr_str) == 0)
	{
		printf("%s\n", payload);
		free(my_addr_str);
		return;
	}
	else
	{
		int ttl = atoi(ctr);

		if(ttl == 0)
		{
			//free(msg);
			free(my_addr_str);
			return;
		}
		
		int dest_index = nl_index(nl, msg);
		if (dest_index == -1) {
			//free(msg);
			free(my_addr_str);
			return;
		}
		int my_index =  nl_index(nl, my_addr_str);
		
		while (prev[dest_index] != my_index) {
			dest_index = prev[dest_index];
			//Node disconnected 
			if(dest_index == -1)
			{
				free(my_addr_str);
				return;
			}
		}
		// send to the dest_index
		char *dest_addr = nl_name(nl, dest_index);
		ttl--;
		char *str = (char *) malloc(len);
		sprintf(str, "S%s/%d/%s\n",msg, ttl, payload);
		
		struct file_info *f = file_info;
		
		while(f)
		{
		  char *f_addr_str = addr_to_string(f->addr);
		  if(strcmp(dest_addr, f_addr_str) == 0 &&
		     (f->type == FI_INCOMING || (f->type == FI_OUTGOING && f->status == FI_KNOWN
						 && f->u.fi_outgoing.status == FI_CONNECTED)))
		    break;
		    
		  
		  f = f->next;
		  //free(f_addr_str);
		  //free(myaddr);
		}
		
		if (f) {
		  printf("%s\n", addr_to_string(f->addr));
		  file_info_send(f, str, strlen(str));
		}
		else
		  printf("f not found\n");
	}
	free(my_addr_str);

}

/* This is a timer handler to reconnect to a peer after a period of time elapsed.
 */
static void timer_reconnect(void *arg){
	void try_connect(struct file_info *fi);

	struct file_info *fi;
	for (fi = file_info; fi != 0; fi = fi->next) {
		if (fi->type != FI_FREE && fi->uid == arg) {
			printf("reconnecting\n");
			try_connect(fi);
			return;
		}
	}
	printf("reconnect: entry not found\n");
}

/* The user typed in a C<addr>:<port> command to connect to a peer.
 */
static void connect_command(struct file_info *fi, char *addr_port){
	void try_connect(struct file_info *fi);

	if (fi->type != FI_FILE) {
		fprintf(stderr, "unexpected connect message\n");
		return;
	}

	char *p = index(addr_port, ':');
	if (p == 0) {
		fprintf(stderr, "do_connect: format is C<addr>:<port>\n");
		return;
	}
	*p++ = 0;

	struct sockaddr_in addr;
	if (addr_get(&addr, addr_port, atoi(p)) < 0) {
		return;
	}
	*--p = ':';

	fi = file_info_add(FI_OUTGOING, -1, 0, 0);
	fi->u.fi_outgoing.status = FI_CONNECTING;

	/* Even though we're connecting, you're allowed to connect using any address to
	 * a peer (including a localhost address).  So we have to wait for the Hello
	 * message until we're certain who it is on the other side.  Until then, we keep
	 * the address we know for debugging purposes.
	 */
	fi->addr = addr;

	try_connect(fi);
}

/* We uniquely identify a socket connection by the lower of the local TCP/IP address
 * and the remote one.  Because two connections cannot share TCP/IP addresses, this
 * should work.
 */
static void get_id(int skt, struct sockaddr_in *addr){
	struct sockaddr_in this, that;
	socklen_t len;

	len = sizeof(that);
	if (getsockname(skt, (struct sockaddr *) &that, &len) < 0) {
		perror("getsockname");
		exit(1);
	}
	len = sizeof(that);
	if (getpeername(skt, (struct sockaddr *) &that, &len) < 0) {;
		perror("getpeername");
		exit(1);
	}
	*addr = addr_cmp(this, that) < 0 ? this : that;
}

/* Received a H(ello) message of the form H<addr>:<port>.  This is the first message
 * that's sent over a TCP connection (in both directions) so the peers can identify
 * one another.  The port is the server port of the endpoint rather than the
 * connection's port.
 *
 * Sometimes accidentally peers try to connect to one another at the same time.
 * This code kills one of the connections.
 */
void hello_received(struct file_info *fi, char *addr_port){
	char *p = index(addr_port, ':');
	if (p == 0) {
		fprintf(stderr, "do_hello: format is H<addr>:<port>\n");
		return;
	}
	*p++ = 0;

	struct sockaddr_in addr;
	if (addr_get(&addr, addr_port, atoi(p)) < 0) {
		return;
	}
	*--p = ':';

	printf("Got hello from %s:%d on socket %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), fi->fd);

	/* If a connection breaks and is re-established, a duplicate hello message is likely
	 * to arrive, but we can ignore it as we already know the peer.
	 */
	if (fi->status == FI_KNOWN) {
		fprintf(stderr, "Duplicate hello (ignoring)\n");
		if (addr_cmp(addr, fi->addr) != 0) {
			fprintf(stderr, "do_hello: address has changed???\n");
		}
		return;
	}

	/* It is possible to set up a connection to self.  We deal with it by ignoring the
	 * Hello message but keeping the connection established.
	 */
	if (addr_cmp(addr, my_addr) == 0) {
		fprintf(stderr, "Got hello from self??? (ignoring)\n");
		return;
	}

	/* Search the connections to see if there is already a connection to this peer.
	 */
	struct file_info *fi2;
	for (fi2 = file_info; fi2 != 0; fi2 = fi2->next) {
		if (fi2->type == FI_FREE || fi2->status != FI_KNOWN) {
			continue;
		}
		if (addr_cmp(fi2->addr, addr) != 0) {
			continue;
		}

		/* Found another connection.  We're going to keep just one.  First see if
		 * this is actually an existing connection.  It may have broken.
		 */
		if (fi2->fd == -1) {
			printf("Found a defunct connection---dropping that one\n");
			if (fi2->type == FI_OUTGOING) {
				fi->type = FI_OUTGOING;
				fi->u.fi_outgoing = fi2->u.fi_outgoing;
			}
			fi2->type = FI_FREE;
			return;
		}

		/* Of the two, we keep the "lowest one".  We identify a connection by the lowest
		 * endpoint address.
		 */
		struct sockaddr_in mine, other;
		get_id(fi->fd, &mine);
		get_id(fi2->fd, &other);
		if (addr_cmp(mine, other) < 0) {
			/* We keep mine.
			 */
			printf("duplicate connection -- keep mine\n");
			if (fi2->type == FI_OUTGOING) {
				fi->type = FI_OUTGOING;
				fi->u.fi_outgoing = fi2->u.fi_outgoing;
			}
			close(fi2->fd);
			fi2->type = FI_FREE;
		}
		else {
			printf("duplicate connection -- keep other\n");

			/* The other one wins.
			 */
			if (fi->type == FI_OUTGOING) {
				fi2->type = FI_OUTGOING;
				fi2->u.fi_outgoing = fi->u.fi_outgoing;
			}
			close(fi->fd);
			fi->type = FI_FREE;
			return;
		}
	}

	/* No other connection.  Keep this one.
	 */
	printf("New Connection\n");
	fi->addr = addr;
	fi->status = FI_KNOWN;
	if(!nl)
	{
		nl = (struct node_list *) nl_create();
		char *my_addr_str = addr_to_string(my_addr);
		nl_add(nl, my_addr_str);
		free(my_addr_str);
	}
	char *addr_str = addr_to_string(addr);
	nl_add(nl, addr_str);
        free(addr_str);
	updateGraph();
	send_gossip();
}

/* A line of input (a command) is received.  Look at the first character to determine
 * the type and take it from there.
 */
static void handle_line(struct file_info *fi, char *line){
  switch (line[0]) {
  case 0:
    break;
  case 'C':
    connect_command(fi, &line[1]);
    break;
  case 'G':
    gossip_received(fi, &line[1]);
    break;
  case 'H':
    hello_received(fi, &line[1]);
    break;
  case 'E':
  case 'e':
    exit(0);
    break;
  case 'S':
    send_handler(fi, &line[1]);
    break;
  default:
    fprintf(stderr, "unknown command: '%s'\n", line);
  }
}

/* Input is available on the given connection.  Add it to the input buffer and
 * see if there are any complete lines in it.
 */
static void add_input(struct file_info *fi){
	fi->input_buffer = realloc(fi->input_buffer, fi->amount_received + 100);
	int n = read(fi->fd, &fi->input_buffer[fi->amount_received], 100);
	if (n < 0) {
		perror("read");
		exit(1);
	}
	if (n > 0) {
		fi->amount_received += n;
		for (;;) {
			char *p = index(fi->input_buffer, '\n');
			if (p == 0) {
				break;
			}
			*p++ = 0;
			handle_line(fi, fi->input_buffer);
			int n = fi->amount_received - (p - fi->input_buffer);
			memmove(fi->input_buffer, p, n);
			fi->amount_received = n;
			if (fi->fd == 0) {
				printf("> ");
				fflush(stdout);
			}
		}
	}
}

/* Activity on a socket: input, output, or error...
 */
static void message_handler(struct file_info *fi, int events){
  	printf("Message handler called\n");
	char buf[512];

	if (events & (POLLERR | POLLHUP)) {
		double time;
		int error;
		socklen_t len = sizeof(error);
		if (getsockopt(fi->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
			perror("getsockopt");
		}
		switch (error) {
		case 0:
			printf("Lost connection on socket %d\n", fi->fd);
			time = timer_now() + 1;
			break;
		default:
			printf("Error '%s' on socket %d\n", strerror(error), fi->fd);
			time = timer_now() + 5;
		}

		close(fi->fd);

		/* Start a timer to reconnect.
		 */
		if (fi->type == FI_OUTGOING) {
			timer_start(time, timer_reconnect, fi->uid);
			fi->fd = -1;
			fi->u.fi_outgoing.status = FI_CONNECTING;
		}
		else {
			fi->type = FI_FREE;
		}
		char *my_addr_str = addr_to_string(my_addr);
		char *fi_addr_str = addr_to_string(fi->addr);
		  
		set_dist(nl, graph, nl_nsites(nl), fi_addr_str, my_addr_str, 0);
		updateGraph();
		send_gossip();
		free (my_addr_str);
		free (fi_addr_str);

		return;
	}
	if (events & POLLOUT) {
		int n = send(fi->fd, fi->output_buffer, fi->amount_to_send, 0);
		if (n < 0) {
			perror("send");
		}
		if (n > 0) {
			if (n == fi->amount_to_send) {
				fi->amount_to_send = 0;
			}
			else {
				memmove(&fi->output_buffer[n], fi->output_buffer, fi->amount_to_send - n);
				fi->amount_to_send -= n;
			}
		}
	}
	if (events & POLLIN) {
		add_input(fi);
	}
	if (events & ~(POLLIN|POLLOUT|POLLERR|POLLHUP)) {
		printf("message_handler: unknown events\n");
		fi->events = 0;
	}
}

/* Send a H(ello) message to the peer to identify ourselves.
 */
static void send_hello(struct file_info *fi){
	char buffer[64];
	sprintf(buffer, "H%s:%d\n", inet_ntoa(my_addr.sin_addr), ntohs(my_addr.sin_port));
	file_info_send(fi, buffer, strlen(buffer));
}

/* This function is invoked in response to a non-blocking socket connect() call once
 * an outgoing connection is established or fails.
 */
static void connect_handler(struct file_info *fi, int events){
	char buf[512];

	if (events & (POLLERR | POLLHUP)) {
		double time;
		int error;
		socklen_t len = sizeof(error);
		if (getsockopt(fi->fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
			perror("getsockopt");
		}
		switch (error) {
		case 0:
			printf("No connection on socket %d\n", fi->fd);
			time = timer_now() + 3;
			break;
		case ETIMEDOUT:
			printf("Timeout on socket %d\n", fi->fd);
			time = fi->u.fi_outgoing.connect_time + 5;
			break;
		default:
			printf("Error '%s' on socket %d\n", strerror(error), fi->fd);
			time = timer_now() + 5;
		}

		/* Start a timer to reconnect.
		 */
		timer_start(time, timer_reconnect, fi->uid);

		close(fi->fd);
		fi->fd = -1;
		fi->u.fi_outgoing.status = FI_CONNECTING;

		return;
	}
	if (events & POLLOUT) {
		printf("Socket %d connected\n", fi->fd);
		fi->handler = message_handler;
		fi->events = POLLIN;
		fi->u.fi_outgoing.status = FI_CONNECTED;

		send_hello(fi);
		gossip_to_peer(fi);
	}
	if (events & ~(POLLOUT|POLLERR|POLLHUP)) {
		printf("message_handler: unknown events %x\n", events);
		fi->events = 0;
	}
}

/* Invoke a non-blocking connect() to try and establish a connection.
 */
void try_connect(struct file_info *fi){
	int skt = socket(AF_INET, SOCK_STREAM, 0);
	if (skt < 0) {
		perror("try_connect: socket");
		return;
	}

    /* Make the socket, skt, non-blocking.
     */
    // YOUR CODE HERE
    int res = fcntl(skt, F_SETFL, O_NONBLOCK);
    report_error(res, "fcntl", errno);

	if (connect(skt, (struct sockaddr *) &fi->addr, sizeof(fi->addr)) < 0) {
		printf("Connecting to %s:%d on socket %d\n", inet_ntoa(fi->addr.sin_addr), ntohs(fi->addr.sin_port), skt);
		if (errno != EINPROGRESS) {
			perror("try_connect: connect");
			close(skt);
			return;
		}

		fi->fd = skt;
		fi->events = POLLOUT;
		fi->handler = connect_handler;
		fi->u.fi_outgoing.connect_time = timer_now();
	}
	else {
		printf("Connected to %s:%d on socket %d\n", inet_ntoa(fi->addr.sin_addr), ntohs(fi->addr.sin_port), skt);
		fi->fd = skt;
		fi->events = POLLIN;
		fi->handler = message_handler;
		fi->u.fi_outgoing.connect_time = timer_now();
		fi->u.fi_outgoing.status = FI_CONNECTED;
	}
}

/* Standard input is available.
 */
static void stdin_handler(struct file_info *fi, int events){
	if (events & POLLIN) {
		add_input(fi);
	}
	if (events & ~POLLIN) {
		fprintf(stderr, "input_handler: unknown events %x\n", events);
	}
}

/* Activity on the server socket.  Typically an incoming connection.  Accept the
 * connection and create a new file_info descriptor for it.
 */
static void server_handler(struct file_info *fi, int events){
	if (events & POLLIN) {
		struct sockaddr_in addr;
		socklen_t len = sizeof(addr);
		int skt = accept(fi->fd, (struct sockaddr *) &addr, &len);
		if (skt < 0) {
			perror("accept");
		}

		/* Make the socket non-blocking.
		 */
        // YOUR CODE HERE
        int res = fcntl(skt, F_SETFL, O_NONBLOCK);
    	report_error(res, "fcntl", errno);

		/* Keep track of the socket.
		 */
		struct file_info *fi = file_info_add(FI_INCOMING, skt, message_handler, POLLIN);

		/* We don't know yet who's on the other side exactly until we get the H(ello)
		 * message.  But for debugging it's convenient to keep the peer's socket address.
		 */
		fi->addr = addr;

		printf("New connection from %s:%d on socket %d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), skt);

		send_hello(fi);
		gossip_to_peer(fi);
	}
	if (events & ~POLLIN) {
		fprintf(stderr, "server_handler: unknown events %x\n", events);
	}
}



/* Usage: a.out [port].  If no port is specified, a default port is used.
 */
int main(int argc, char *argv[]){
	int bind_port = argc > 1 ? atoi(argv[1]) : 0;
	int i;
    
	/* Read from standard input.
	 */
	struct file_info *input = file_info_add(FI_FILE, 0, stdin_handler, POLLIN);

	/* Create a TCP socket.
	 */
	int skt;
    // YOUR CODE HERE
    skt = socket(AF_INET, SOCK_STREAM, 0);
    report_error(skt, "socket", errno);

	/* Make the socket non-blocking.
	 */
    // YOUR CODE HERE
    int res = fcntl(skt, F_SETFL, O_NONBLOCK);	
    report_error(res, "fcntl", errno);

    int optval = 1;
    res = setsockopt(skt, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    report_error(res, "setsocketopt", errno);

	/* Initialize addr in as far as possible.
	 */
    // YOUR CODE HERE
    struct sockaddr_in sock_addr;
    sock_addr.sin_family = AF_INET;
    sock_addr.sin_port = bind_port;
        
	/* Bind the socket.
	 */
    // YOUR CODE HERE
    res = bind(skt, (struct sockaddr *) &sock_addr, sizeof(struct sockaddr));
    report_error(res, "bind", errno);

	/* Keep track of the socket.
	 */
	file_info_add(FI_SERVER, skt, server_handler, POLLIN);

	/* Get my address.
	 */
	struct sockaddr_in addr;
	socklen_t len = sizeof(addr);
	if (getsockname(skt, (struct sockaddr *) &addr, &len) < 0) {
		perror("getsocknane");
	}

	/* Get my IP addresses.
	 */
	struct ifaddrs *addr_list, *ifa;
	if (getifaddrs(&addr_list) < 0) {
		perror("getifaddrs");
		return 1;
	}
	for (ifa = addr_list; ifa != 0; ifa = ifa->ifa_next) {
		if (ifa->ifa_addr != 0 && ifa->ifa_addr->sa_family == AF_INET &&
										!(ifa->ifa_flags & IFF_LOOPBACK)) {
			struct sockaddr_in *si = (struct sockaddr_in *) ifa->ifa_addr;
			printf("%s: %s:%d\n", ifa->ifa_name, inet_ntoa(si->sin_addr), ntohs(addr.sin_port));
			my_addr = *si;
			my_addr.sin_port = addr.sin_port;
		}
	}
	freeifaddrs(addr_list);

	/* Pretend standard input is a peer...
	 */
	input->addr = my_addr;
	input->status = FI_KNOWN;

	/* Listen on the socket.
	 */
	if (listen(skt, 5) < 0) {
		perror("listen");
		return 1;
	}
	
	// printf("server socket = %d\n", skt);
	printf("> "); fflush(stdout);
	for (;;) {
		/* Handle expired timers first.
		 */
		int timeout = timer_check();

		/* Prepare poll.
		 */
		struct pollfd *fds = calloc(nfiles, sizeof(*fds));
		struct file_info *fi, **fi_index = calloc(nfiles, sizeof(*fi_index));
		int i;
		for (i = 0, fi = file_info; fi != 0; fi = fi->next) {
			if (fi->type != FI_FREE && fi->fd >= 0) {
				fds[i].fd = fi->fd;
				fds[i].events = fi->events;
				if (fi->amount_to_send > 0) {
					fds[i].events |= POLLOUT;
				}
				fi_index[i++] = fi;
			}
		}

		int n = i;			// n may be less than nfiles
		if (poll(fds, n, timeout) < 0) {
			perror("poll");
			return 1;
		}

		/* See if there's activity on any of the files/sockets.
		 */
		for (i = 0; i < n; i++) {
			if (fds[i].revents != 0 && fi_index[i]->type != FI_FREE) {
				(*fi_index[i]->handler)(fi_index[i], fds[i].revents);
			}
		}
		free(fds);
		free(fi_index);
	}

	return 0;
}
