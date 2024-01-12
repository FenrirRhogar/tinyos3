#ifndef __KERNEL_SOCKET_H__
#define __KERNEL_SOCKET_H__

#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_pipe.h"

typedef struct  socket_control_block SCB;
typedef struct listener_socket listener_socket;
typedef struct unbound_socket unbound_socket;
typedef struct peer_socket peer_socket;
typedef struct connection_request connection_request;

SCB* PORT_MAP[MAX_PORT]; // Map of port numbers to SCBs

/**
 * @brief Enumeration representing different types of sockets.
 * 
 * This enumeration defines three types of sockets:
 * - LISTENER_SOCKET: A socket that listens for incoming connections.
 * - UNBOUND_SOCKET: A socket that is not bound to any specific address or port.
 * - PEER_SOCKET: A socket that is connected to a specific peer.
 */
typedef enum {
    SOCKET_LISTENER,    /**< A socket that listens for incoming connections. */
    SOCKET_UNBOUND,     /**< A socket that is not bound to any specific address or port. */
    SOCKET_PEER         /**< A socket that is connected to a specific peer.  */
} socket_type;


typedef struct listener_socket {
    rlnode queue;
    CondVar req_available;
} listener_socket;

typedef struct unbound_socket { 
    rlnode unbound_socket;
} unbound_socket;

typedef struct peer_socket {
    SCB* peer;
    PIPE_CB* write_pipe;
    PIPE_CB* read_pipe;
} peer_socket;

/**
 * @brief Structure representing a socket control block.
 * 
 * The socket control block (SCB) is used to manage the state and properties of a socket.
 * It contains information such as the reference count, file control block (FCB), socket type,
 * port number, and a union of different socket types (listener, unbound, peer).
 */
typedef struct socket_control_block {
    uint refcount;              /**< Reference count of the socket. */
    FCB* fcb;                   /**< File control block associated with the socket. */
    socket_type type;           /**< Type of the socket. */
    port_t port;                /**< Port number associated with the socket. */
    union {
        listener_socket* listener_s;  /**< Listener socket type. */
        unbound_socket* unbound_s;    /**< Unbound socket type. */
        peer_socket* peer_s;          /**< Peer socket type. */
    } socket_union;
} SCB;

/**
 * @brief Structure representing a connection request.
 * 
 * This structure is used to represent a connection request in the system.
 * It contains information about whether the request has been admitted,
 * the peer SCB (Socket Control Block), the connected condition variable,
 * and the queue node for maintaining the request in a queue.
 */
typedef struct connection_request {
    int admitted;           /**< Flag indicating if the request has been admitted */
    SCB* peer;              /**< Pointer to the peer Socket Control Block */
    CondVar connected_cv;   /**< Condition variable for signaling connection status */
    rlnode queue_node;      /**< Queue node for maintaining the request in a queue */
} connection_request;

#endif // __KERNEL_SOCKET_H__
