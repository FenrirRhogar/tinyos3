#include "kernel_socket.h"
#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_sched.h"
#include "stddef.h" 
#include "stdio.h"

int socket_read(void* socket_cb, char* buf, unsigned int size);
int socket_write(void* socket_cb, const char* buf, unsigned int size);
int socket_close(void* socket_cb);

static file_ops socket_file_ops = {
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};

int socket_read(void* socket_cb, char* buf, unsigned int size)
{
	// Check if SCB is valid
	if (socket_cb == NULL) {
		return -1; // Invalid SCB
	}

	// Cast SCB to the appropriate type
	SCB* scb = (SCB*)socket_cb;

	// Check if buf is valid
	if (buf == NULL) {
		return -1; // Invalid buffer
	}

	// Check if the socket is of type SOCKET_PEER
	if(scb->type!=SOCKET_PEER){
		return -1;
	}

	// Check if size is valid
	if (size == 0) {
		return -1; // Invalid size
	}

	return pipe_read(scb->socket_union.peer_s->read_pipe, buf, size); // Read from the pipe
}


int socket_write(void* socket_cb, const char* buf, unsigned int size)
{
	// Check if SCB is valid
	if (socket_cb == NULL) {
		return -1; // Invalid SCB
	}

	// Cast SCB to the appropriate type
	SCB* scb = (SCB*)socket_cb;

	// Check if buf is valid
	if (buf == NULL) {
		return -1; // Invalid buffer
	}

	// Check if the socket is of type SOCKET_PEER
	if(scb->type!=SOCKET_PEER){
		return -1;
	}

	// Check if size is valid
	if (size == 0) {
		return -1; // Invalid size
	}

	return pipe_write(scb->socket_union.peer_s->write_pipe, buf, size); // Write to the pipe
}

int socket_close(void* socket_cb)
{
	// Check if SCB is valid
	if (socket_cb == NULL) {
		return -1; // Invalid SCB
	}

	// Cast SCB to the appropriate type
	SCB* scb = (SCB*)socket_cb;

	// Clear the stream object
	scb->fcb->streamobj = NULL;

	// Deallocating resources based on the socket type
	switch(scb->type){
        case SOCKET_LISTENER:
            PORT_MAP[scb->port]=NULL;
            kernel_broadcast(&scb->socket_union.listener_s->req_available);
            if(scb->refcount == 0){
                free(scb);
            }
            break;
		case SOCKET_UNBOUND:
            if(scb->refcount==0){
                free(scb);
            }
            break;
        case SOCKET_PEER:
            pipe_reader_close(scb->socket_union.peer_s->read_pipe);
            pipe_writer_close(scb->socket_union.peer_s->write_pipe);
            if(scb->socket_union.peer_s->peer){
                scb->socket_union.peer_s->peer->socket_union.peer_s->peer = NULL;
            }
            if(scb->refcount==0){
                free(scb);
            }
            break;
        default:
            break;
    }

	return 0;
}

Fid_t sys_Socket(port_t port)
{
	if (port < 0 || port > MAX_PORT){
		return NOFILE; // Not legal port
	}

	Fid_t fid[1];
	FCB *fcb[1];
	if (FCB_reserve(1, fid, fcb) == 0)	// Reserve a FCB for the socket
	{
		return NOFILE;
	}
	
	SCB *scb = (SCB *)xmalloc(sizeof(SCB));	// Allocate memory for the socket control block
	scb->refcount = 1;	// Set the reference count to 1
	scb->fcb = fcb[0];	// Set the file control block
	scb->type = SOCKET_UNBOUND;	// Set the socket type to unbound
	scb->port = port;	// Set the port number
	scb->socket_union.unbound_s = (unbound_socket*)xmalloc(sizeof(unbound_socket));	// Allocate memory for the unbound socket
	rlnode_init(&(scb->socket_union.unbound_s->unbound_socket), NULL);	// Initialize the queue
	fcb[0]->streamobj = scb;	// Set the stream object
	fcb[0]->streamfunc = &socket_file_ops;	// Set the stream function

	return fid[0];
}

int sys_Listen(Fid_t sock)
{
	if (sock < 0 || sock > MAX_FILEID-1){
		return -1; // Not legal socket
	}

	FCB *fcb = get_fcb(sock); // Get the FCB from the socket

	// Necessary checks
	if (fcb == NULL) {
		return -1; // Invalid fcb
	}
	SCB* scb = fcb->streamobj;
	if (scb == NULL) {
		return -1; // Invalid socket
	}
	if (scb->port == NOPORT) {
		return -1; // Socket is not bound to a port
	}
	
	if (scb->type != SOCKET_UNBOUND) {
		return -1; // Socket is not unbound
	}

	if(PORT_MAP[scb->port] != NULL && PORT_MAP[scb->port]->type == SOCKET_LISTENER){
		return -1; // Socket has already been initialized
	}

	// Install the socket to the PORT_MAP[]
	PORT_MAP[scb->port] = scb;

	// Mark the socket as SOCKET_LISTENER
	scb->type = SOCKET_LISTENER;

	// Initialize the listener_socket fields of the union
	scb->socket_union.listener_s = (listener_socket*)xmalloc(sizeof(listener_socket));
	rlnode_init(&(scb->socket_union.listener_s->queue), NULL);
	scb->socket_union.listener_s->req_available=COND_INIT;
	return 0;
}

Fid_t sys_Accept(Fid_t lsock)
{
	if (lsock < 0 || lsock > MAX_FILEID-1){
		return NOFILE; // Not legal socket
	}

	FCB *fcb = get_fcb(lsock); // Get the FCB from the listening socket

	// Necessary checks
	if (fcb == NULL) {
		return NOFILE; // Invalid fcb
	}
	SCB* scb = fcb->streamobj;
	if (scb == NULL) {
		return NOFILE; // Invalid socket
	}
	if (scb->port == NOPORT) {
		return NOFILE; // Socket is not bound to a port
	}
	if (scb->type != SOCKET_LISTENER) {
		return NOFILE; // Socket is not a listener socket
	}

	scb->refcount++; // Increase the reference count

	// Wait for a connection request while we do not have any requests && the port is still valid
	while(is_rlist_empty(&scb->socket_union.listener_s->queue) && PORT_MAP[scb->port]!=NULL){ 
        kernel_wait(&scb->socket_union.listener_s->req_available, SCHED_USER); 
    }

	// Check if the port is still valid (might have been closed while we were waiting)
	if (PORT_MAP[scb->port] == NULL) {
		scb->refcount--; // Decrease the reference count
		return NOFILE; // Port is no longer valid
	}

	if(scb==NULL || scb->type!=SOCKET_LISTENER || scb!=PORT_MAP[scb->port]){
        scb->refcount--;  
        return NOFILE;
    }

	// Get the first connection request from the queue
	rlnode* connection_request = rlist_pop_front(&(scb->socket_union.listener_s->queue));

	if (connection_request == NULL) {
		return NOFILE; // No connection request
	}
	
	connection_request->connection_request->admitted = 1; // Mark the connection request as admitted

	// Construct peer
	Fid_t peer = sys_Socket(scb->port); // Create a new socket
	if (peer == NOFILE) {
		return NOFILE; // Could not create socket
	}

	// Get the client SCB from the connection request
	SCB* client = connection_request->connection_request->peer;
	if (client == NULL)
	{
		return NOFILE; // Invalid client
	}
	client->type = SOCKET_PEER; // Mark the client as SOCKET_PEER

	// Get the server SCB from the listening socket
	SCB* server = get_fcb(peer)->streamobj;
	if (server == NULL) {
		return NOFILE; // Invalid server
	}
	server->type = SOCKET_PEER; // Mark the server as SOCKET_PEER

	// Initialize the peer_socket fields of the union
	PIPE_CB* writer_pipe = (PIPE_CB*)xmalloc(sizeof(PIPE_CB));
	PIPE_CB* reader_pipe = (PIPE_CB*)xmalloc(sizeof(PIPE_CB));

	if(writer_pipe == NULL || reader_pipe == NULL){
		return NOFILE; // Could not allocate memory for the pipes
	}

	// Initialization of the pipes
	writer_pipe->has_space = COND_INIT;
	writer_pipe->has_data = COND_INIT;
	writer_pipe->w_position = 0;
	writer_pipe->r_position = 0;
	writer_pipe->current_size = 0;

	reader_pipe->has_space = COND_INIT;
	reader_pipe->has_data = COND_INIT;
	reader_pipe->w_position = 0;
	reader_pipe->r_position = 0;
	reader_pipe->current_size = 0;

	writer_pipe->reader = server->fcb;
	writer_pipe->writer = client->fcb;
	reader_pipe->reader = client->fcb;
	reader_pipe->writer = server->fcb;

	// Initialization of the server and client peer_socket fields
	server->socket_union.peer_s->write_pipe = writer_pipe;
    server->socket_union.peer_s->read_pipe = reader_pipe;
    server->socket_union.peer_s->peer = client;

    client->socket_union.peer_s->read_pipe = writer_pipe;
    client->socket_union.peer_s->write_pipe = reader_pipe;
    client->socket_union.peer_s->peer = server;

	// Signal the Connect side
	kernel_signal(&connection_request->connection_request->connected_cv);

	// Decrease refcount
	scb->refcount--;

	return peer;
}



int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
    SCB* scb_peer = get_scb(sock);

    if(scb_peer == NULL  scb_peer->type != SOCKET_UNBOUND  port<= 0  port>= MAX_PORT+1){
        return -1;
    }

    SCB* scb_server = PORT_MAP[port];

    if(scb_server == NULL  scb_server->type != SOCKET_LISTENER){
        return -1;
    }

    CR* cr = (CR*)xmalloc(sizeof(CR));
    cr->admitted =0;
    cr->peer =scb_peer;
    cr-> connected_cv = COND_INIT;
    rlnode_init(&cr->queue_node, cr);
    rlist_push_back(&scb_server->listener_s.queue, &cr->queue_node);
    kernel_signal(&scb_server->listener_s.req_available);

    scb_peer->refcount++;

    if(timeout > 0){
        kernel_timedwait(&cr->connected_cv, SCHED_IO, timeout);
    }
    else{
        kernel_wait(&cr->connected_cv, SCHED_IO)
    }

    decref_socket(scb_peer);

    int returnValue;

    if(cr->admitted == 0){
        returnValue = -1;
    }
    else{
        returnValue = 0;
    }

    rlist_remove(&cr->queue_node);
    free(cr);


    return returnValue;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
}

