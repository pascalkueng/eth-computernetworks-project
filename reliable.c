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
#include "buffer.h"


struct reliable_state {
    rel_t *next;			/* Linked list for traversing all connections */
    rel_t **prev;

    conn_t *c;			/* This is the connection object */

    int MAXWND;
    int TIMEOUT;

    // sender
    int SNDUNA;
    int SNDNXT;
    int SNDWND;
    buffer_t* send_buffer;
    int SNDEOF;

    // receiver
    int RCVNXT;
    buffer_t* rec_buffer;
    int RCVEOF;
};
rel_t *rel_list;

/* Creates a new reliable protocol session, returns NULL on failure.
* ss is always NULL */
rel_t *
rel_create (conn_t *c, const struct sockaddr_storage *ss,
const struct config_common *cc)
{
    rel_t *r;

    r = xmalloc (sizeof (*r));
    memset (r, 0, sizeof (*r));

    if (!c) {
        c = conn_create (r, ss);
        if (!c) {
            free (r);
            return NULL;
        }
    }

    r->c = c;
    r->next = rel_list;
    r->prev = &rel_list;
    if (rel_list)
    rel_list->prev = &r->next;
    rel_list = r;

    r->MAXWND = cc->window+1;
    r->TIMEOUT = cc->timeout;

    // sender
    r->SNDUNA = 0;
    r->SNDNXT = 1;
    r->SNDWND = 1;
    r->send_buffer = xmalloc(sizeof(buffer_t));
    r->send_buffer->head = NULL;
    r->SNDEOF = 0;

    // receiver
    r-> RCVNXT = 1;
    r->rec_buffer = xmalloc(sizeof(buffer_t));
    r->rec_buffer->head = NULL;
    r->RCVEOF = 0;

    return r;
}

void
rel_destroy (rel_t *r)
{
    if (r->next) {
        r->next->prev = r->prev;
    }
    *r->prev = r->next;
    conn_destroy (r->c);
    buffer_clear(r->send_buffer);
    free(r->send_buffer);
    buffer_clear(r->rec_buffer);
    free(r->rec_buffer);
    free(r);
}

// n is the expected length of pkt
void
rel_recvpkt (rel_t *r, packet_t *pkt, size_t n)
{
    // Receive packets, store them in receive buffer, send acks and release data to application layer

    uint16_t length = (uint16_t) ntohs(pkt->len);
    uint32_t ackno = (uint32_t) ntohl(pkt->ackno);
    uint32_t seqno = (uint32_t) ntohl(pkt->seqno);
    uint16_t pkt_cksum = pkt->cksum;

    if(n != length){ // wrong length
        return;
    }
    pkt->cksum = 0;
    if(pkt_cksum != cksum((void*) pkt, n)){ // wrong checksum
        return;
    }
    if(n==8){ // packet is ACK
        if(ackno>r->SNDUNA){
            r->SNDUNA = ackno;
            buffer_remove(r->send_buffer, ackno);
        }
        r->SNDWND = r->SNDNXT - r->SNDUNA;

        if(r->SNDEOF == 1 && r->SNDWND == 0){ // client waiting for fin ack and no packets in transit
            r->SNDEOF = 2; // client is finished
        }
        if((r->RCVEOF == 1) && (r->SNDEOF == 2) && (r->SNDWND) == 0){ // if server & client finished and no packets in transit
                rel_destroy(r);
            }
        else if(r->SNDWND < r->MAXWND){
            rel_read(r);
        }
        return;
    }

    else { // data packet
        if(r->RCVEOF == 1){
            if(r->SNDEOF == 2){
                rel_destroy(r);
            }
            return;
        }
        if(seqno < r->RCVNXT){ // packet already received, send ack but don't buffer
            // send back cumulative ack
            struct ack_packet *ackpkt;
            ackpkt = malloc(sizeof(struct ack_packet));
            ackpkt->len = htons(8);
            ackpkt->ackno = htonl(r->RCVNXT);
            ackpkt->cksum = 0;
            ackpkt->cksum = cksum(ackpkt, 8);
            conn_sendpkt(r->c, (packet_t *) ackpkt, 8);
            return;
        }
        
        if(seqno < r->RCVNXT + r->MAXWND){ // drop frame if sequence number too large
            if(buffer_contains(r->rec_buffer, seqno)==0){ // not already in buffer
                struct timeval now;
                gettimeofday(&now, NULL);
                buffer_insert(r->rec_buffer, pkt, (now.tv_sec * 1000 + now.tv_sec / 1000));
            }

            if (seqno == r->RCVNXT){
                buffer_node_t *current = buffer_get_first(r->rec_buffer);
                r->RCVNXT = ntohl(current->packet.seqno) + 1;
                while (current->next != NULL) {
                    if(!buffer_contains(r->rec_buffer, ntohl(current->packet.seqno)+1)){
                        break;
                    }
                    r->RCVNXT++;
                    current = current->next;
                }
                rel_output(r);
            }
            // send back cumulative ack
            struct ack_packet *ackpkt;
            ackpkt = malloc(sizeof(struct ack_packet));
            ackpkt->len = htons(8);
            ackpkt->ackno = htonl(r->RCVNXT);
            ackpkt->cksum = 0;
            ackpkt->cksum = cksum(ackpkt, 8);
            conn_sendpkt(r->c, (packet_t *) ackpkt, 8);
        }
    }
}

void
rel_read (rel_t *s)
{
    // Read input from console into buffer and send packet

    while(s->SNDWND < s->MAXWND){
        // construct packet
        packet_t *pkt;
        pkt = malloc(sizeof(packet_t));
        int nrBytes = conn_input(s->c, pkt->data, 500);
        if(nrBytes == 0){
            free(pkt);
            return;
        }
        if(nrBytes == -1){
            nrBytes = 0;
            s->SNDEOF = 1;
        }
        pkt->len = htons(nrBytes + 12);
        pkt->seqno = htonl(s->SNDNXT);
        pkt->ackno = htonl(1);
        pkt->cksum = 0;
        pkt->cksum = cksum(pkt, nrBytes+12);

        // buffer and send packet
        struct timeval now;
        gettimeofday(&now, NULL);
        buffer_insert(s->send_buffer, pkt, (now.tv_sec * 1000 + now.tv_sec / 1000));
        conn_sendpkt(s->c, pkt, nrBytes+12);
        s->SNDNXT++;
        s->SNDWND = s->SNDNXT - s->SNDUNA;
        free(pkt);
    }
}

void
rel_output (rel_t *r)
{
    // Output receive buffer to application layer

    buffer_node_t *current = buffer_get_first(r->rec_buffer);
    while (current != NULL) {
        if (ntohl(current->packet.seqno) < r->RCVNXT) {
            if(ntohs(current->packet.len)==12){
                conn_output(r->c, NULL, 0);
                r->RCVEOF = 1;
                return;
            }
            conn_output(r->c, current->packet.data, ntohs(current->packet.len)-12);
            buffer_remove_first(r->rec_buffer);
        }  
        current = current->next;
    }
}

void
rel_timer ()
{
    // Go over all reliable senders, and have them send out
    // all packets whose timer has expired

    rel_t *current = rel_list;
    while (current != NULL) {
        buffer_node_t *currentpkt = buffer_get_first(rel_list->send_buffer);
        while(currentpkt != NULL){
            struct timeval now;
            gettimeofday(&now, NULL);
            long now_ms = now.tv_sec * 1000 + now.tv_sec / 1000;
            long timediff = now_ms - currentpkt->last_retransmit;
            if(timediff >= rel_list->TIMEOUT){
                conn_sendpkt(rel_list->c, &currentpkt->packet, ntohs(currentpkt->packet.len));
                currentpkt->last_retransmit = now_ms;
            }
            currentpkt = currentpkt->next;
        }
        current = rel_list->next;
    }
}