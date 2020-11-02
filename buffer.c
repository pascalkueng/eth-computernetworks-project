#include "buffer.h"

/**
 * Get the first buffer node (lowest sequence number).
 *
 * @param   buffer      Pointer to buffer
 *
 * @return  Pointer to first buffer node (NULL if none)
*/
buffer_node_t* buffer_get_first(buffer_t *buffer) {
    return buffer->head;
}

/**
 * Remove the first buffer node (lowest sequence number).
 *
 * @param   buffer      Pointer to buffer
 *
 * @return  0 iff first node removed, else non-zero
*/
int buffer_remove_first(buffer_t *buffer) {
    if (buffer->head == NULL) {
        return 1;
    } else {
        buffer_node_t* to_remove = buffer->head;
        buffer->head = buffer->head->next;
        free(to_remove);
        return 0;
    }
}

/**
 * Inserting a packet in its place by its sequence number.
 *
 * @param   buffer              Pointer to buffer
 * @param   packet              Pointer to packet
 * @param   last_retransmit     Last retransmission time (long)
*/
void buffer_insert(buffer_t *buffer, packet_t *packet, long last_retransmit) {

    // Node to insert
    buffer_node_t* to_insert = xmalloc(sizeof(buffer_node_t));
    to_insert->packet = *packet;
    to_insert->last_retransmit = last_retransmit;

    // When iterating, previous and current
    buffer_node_t* prev = NULL;
    buffer_node_t* current = buffer_get_first(buffer);

    // If the buffer is empty, add as head
    if (current == NULL) {
        buffer->head = to_insert;
        to_insert->next = NULL;
    } else {

        // If the buffer is not empty, go until a packet with a higher sequence
        // number is found
        while (current != NULL) {

            // If found an element whose sequence number is higher
            if (ntohl(current->packet.seqno) > ntohl(packet->seqno)) {

                // If it was the head (there is no previous)
                if (prev == NULL) {
                    buffer->head = to_insert;
                    to_insert->next = current;

                // If it was somewhere in between
                } else {
                    prev->next = to_insert;
                    to_insert->next = current;
                }

                break;

            // If at the tail, then add there, and finish
            } else if (current->next == NULL) {
                current->next = to_insert;
                to_insert->next = NULL;
                break;
            }

            // Move on
            prev = current;
            current = current->next;

        }

    }

}

/**
 * Remove all buffer nodes until (lower-than exclusive <) a certain packet sequence number from the buffer.
 *
 * @param   buffer              Pointer to buffer
 * @param   seqno_until_excl    Packet sequence number until which to remove (lower-than exclusive <)
 *
 * @return  Number of buffer nodes removed
*/
uint32_t buffer_remove(buffer_t *buffer, uint32_t seqno_until_excl) {
    buffer_node_t* first = buffer_get_first(buffer);
    uint32_t num_removed = 0;
    while (first != NULL) {
        if (ntohl(first->packet.seqno) >= seqno_until_excl) {
            break;
        } else {
            buffer_remove_first(buffer);
            first = buffer_get_first(buffer);
            num_removed++;
        }
    }
    return num_removed;
}

/**
 * Print buffer content to standard error output (stderr).
 *
 * @param   buffer      Pointer to buffer
*/
void buffer_print(buffer_t *buffer) {
    buffer_node_t* current = buffer_get_first(buffer);
    int first = 1;
    while (current != NULL) {
        if (first == 0) {
            fprintf(stderr, " -- ");
        } else {
            first = 0;
        }
        fprintf(stderr, "%d (l=%d)" , ntohl(current->packet.seqno), ntohs(current->packet.len));
        current = current->next;
    }
    fprintf(stderr, "\n");
}

/**
 * Retrieve buffer size.
 *
 * @param   buffer      Pointer to buffer
 *
 * @return  Buffer size
*/
uint32_t buffer_size(buffer_t *buffer) {
    uint32_t i = 0;
    buffer_node_t* current = buffer_get_first(buffer);
    while (current != NULL) {
        i++;
        current = current->next;
    }
    return i;
}

/**
 * Completely clear out the entire buffer.
 *
 * @param   buffer      Pointer to buffer
*/
void buffer_clear(buffer_t *buffer) {
    while (buffer_get_first(buffer) != NULL) {
        buffer_remove_first(buffer);
    }
}

/**
 * Check whether the buffer contains a packet with the given sequence number.
 *
 * @param   buffer      Pointer to buffer
 * @param   seqno       Sequence number to check for
 *
 * @return  1 iff the buffer contains the packet, 0 otherwise
*/
int buffer_contains(buffer_t *buffer, uint32_t seqno) {
    int in_there = 0;
    buffer_node_t* current = buffer_get_first(buffer);
    while (current != NULL) {
        if (ntohl(current->packet.seqno) == seqno) {
            in_there = 1;
            break;
        }
        current = current->next;
    }
    return in_there;
}
