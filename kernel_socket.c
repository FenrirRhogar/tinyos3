#include "kernel_socket.h"
#include "tinyos.h"
#include "kernel_pipe.h"
#include "kernel_dev.h"
#include "kernel_streams.h"
#include "kernel_cc.h"
#include "kernel_sched.h"
#include "stddef.h" 
#include "stdio.h"


static file_ops socket_file_ops = {
	.Open = NULL,
	.Read = socket_read,
	.Write = socket_write,
	.Close = socket_close
};

int socket_read()
{

}

int socket_write()
{

}

int socket_close()
{

}

Fid_t sys_Socket(port_t port)
{
	if (port < 0 || port > MAX_FILEID-1){
		return NOFILE; // Not legal port
	}

	Fid_t fid[1];
	FCB *fcb[1];
	if (FCB_reserve(1, fid, &fcb) == 0)	// Reserve a FCB for the socket
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

	// Install the socket to the PORT_MAP[]
	//PORT_MAP[scb->port] = fcb->streamobj;

	// Mark the socket as SOCKET_LISTENER
	scb->type = SOCKET_LISTENER;

	// Initialize the listener_socket fields of the union
	scb->socket_union.listener_s = (listener_socket*)xmalloc(sizeof(listener_socket));
	rlnode_init(&(scb->socket_union.listener_s->queue), NULL);

	return 0;
}


Fid_t sys_Accept(Fid_t lsock)
{
	return NOFILE;
}


int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{
	return -1;
}


int sys_ShutDown(Fid_t sock, shutdown_mode how)
{
	return -1;
}

