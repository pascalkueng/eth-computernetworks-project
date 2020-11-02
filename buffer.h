#ifndef BUFFER_H
#define BUFFER_H

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <netinet/in.h>

#include "rlib.h"

/*
 * A buffer is a priority queue of buffer nodes.
 * It is ordered by the packet sequence number (seqno).
 *
 * Each buffer node has three properties: (a) a full copy of the packet (incl. its sequence number),
 * (b) the last time it was transmitted, and (c) the next packet in the list (NULL if none).
 *
 * The content of the buffer (its nodes) are allocated on the heap, including the full packet copies.
 * After serving its purpose, its content must be freed explicitly (via buffer_clear(buffer)) for proper clean-up.
 * Free-ing merely the buffer pointer DOES NOT suffice (but it should be done of course after clearing the buffer
 * content).
*/

typedef struct buffer_node {
    packet_t packet;
    long last_retransmit;
    struct buffer_node* next;
} buffer_node_t;

typedef struct buffer {
    buffer_node_t* head;
} buffer_t;

/**
 * Get the first buffer node (lowest sequence number).
 *
 * @param   buffer      Pointer to buffer
 *
 * @return  Pointer to first buffer node (NULL if none)
*/
buffer_node_t* buffer_get_first(buffer_t *buffer);

/**
 * Remove the first buffer node (lowest sequence number).
 *
 * @param   buffer      Pointer to buffer
 *
 * @return  0 iff first node removed, else non-zero
*/
int buffer_remove_first(buffer_t *buffer);

/**
 * Inserting a packet in its place by its sequence number.
 * The packet itself is completely copied onto the heap.
 *
 * @param   buffer              Pointer to buffer
 * @param   packet              Pointer to packet
 * @param   last_retransmit     Last retransmission time (long)
*/
void buffer_insert(buffer_t *buffer, packet_t *packet, long last_retransmit);

/**
 * Remove all buffer nodes until (lower-than exclusive <) a certain packet sequence number from the buffer.
 *
 * @param   buffer              Pointer to buffer
 * @param   seqno_until_excl    Packet sequence number until which to remove (lower-than exclusive <)
 *
 * @return  Number of buffer nodes removed
*/
uint32_t buffer_remove(buffer_t *buffer, uint32_t seqno_until_excl);

/**
 * Print buffer content to standard error output (stderr).
 *
 * @param   buffer      Pointer to buffer
*/
void buffer_print(buffer_t *buffer);

/**
 * Retrieve buffer size.
 *
 * @param   buffer      Pointer to buffer
 *
 * @return  Buffer size
*/
uint32_t buffer_size(buffer_t *buffer);

/**
 * Completely clear out the entire buffer.
 *
 * @param   buffer      Pointer to buffer
*/
void buffer_clear(buffer_t *buffer);

/**
 * Check whether the buffer contains a packet with the given sequence number.
 *
 * @param   buffer      Pointer to buffer
 * @param   seqno       Sequence number to check for
 *
 * @return  1 iff the buffer contains the packet, 0 otherwise
*/
int buffer_contains(buffer_t *buffer, uint32_t seqno);

#endif /* BUFFER_H */
