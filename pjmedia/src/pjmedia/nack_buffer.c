#include <pjmedia/nack_buffer.h>
#include <pjmedia/rtcp_fb.h>
#include <pjmedia/types.h>
#include <pj/pool.h>
#include <pj/errno.h>
#include <pj/assert.h>
#include <pj/log.h>

#define THIS_FILE   "nack_buffer.c"

struct pjmedia_nack_buffer {
    unsigned head;
    unsigned tail;
    unsigned count;
    unsigned size;
    pjmedia_rtcp_fb_nack *packets;
};

static pj_bool_t blp_contains(pjmedia_rtcp_fb_nack *packet, pj_uint16_t sequence_num) {
    PJ_ASSERT_RETURN(sequence_num != packet->pid, PJ_FALSE);

    pj_uint16_t diff;
    if (sequence_num < packet->pid) {
        diff = 0xFFFF - packet->pid + sequence_num; 
    } else {
        diff = sequence_num - packet->pid;
    }

    if (diff <= 16 && (packet->blp & (1 << (diff - 1)))) {
        return PJ_TRUE;
    } else {
        return PJ_FALSE;
    }    
}

PJ_DEF(pj_status_t)
pjmedia_nack_buffer_create(pj_pool_t *pool,
                           unsigned size,
                           pjmedia_nack_buffer **buffer) {

    pjmedia_nack_buffer *nack_buffer = PJ_POOL_ZALLOC_T(pool, pjmedia_nack_buffer);
    PJ_ASSERT_RETURN(nack_buffer != NULL, PJ_ENOMEM);

    nack_buffer->packets = pj_pool_alloc(pool, size * sizeof(pjmedia_rtcp_fb_nack));
    PJ_ASSERT_RETURN(nack_buffer->packets != NULL, PJ_ENOMEM);

    nack_buffer->size = size;
    pjmedia_nack_buffer_reset(nack_buffer);

    *buffer = nack_buffer;
    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t)
pjmedia_nack_buffer_reset(pjmedia_nack_buffer *buffer) {
    PJ_ASSERT_RETURN(buffer, PJ_EINVAL);

    PJ_LOG(3, (THIS_FILE, "Nack buffer restarted."));
    buffer->count = 0;
    buffer->head = 0;
    buffer->tail = 0;

    return PJ_SUCCESS;
}

PJ_DEF(pj_status_t)
pjmedia_nack_buffer_push(pjmedia_nack_buffer *buffer,
                         pjmedia_rtcp_fb_nack nack) {

    PJ_ASSERT_RETURN(buffer, PJ_EINVAL);
    
    if (buffer->count < buffer->size) {
        buffer->count++;
    } else {
        PJ_LOG(3, (THIS_FILE, "Buffer is full, overwriting oldest packet."));
        buffer->tail = (buffer->tail + 1) % buffer->size;
    }

    buffer->packets[buffer->head] = nack;
    buffer->head = (buffer->head + 1) % buffer->size;

    return PJ_SUCCESS;
}

// Function to check if a packet with the given sequence number was NACKed and remove older packets.
PJ_DECL(pj_bool_t)
pjmedia_nack_buffer_frame_dequeued(pjmedia_nack_buffer *buffer,
                                   pj_uint16_t sequence_num) {

    if (buffer->count == 0) {
        return PJ_FALSE; 
    }

    static pj_uint16_t half_uint16 = 0xFFFF / 2;
    
    int lower = 0;
    int upper = buffer->count - 1;
    int index = -1;

    // Perform a binary search for the packet with the closest PID <= the sequence number
    while (lower <= upper) {
        int middle = (lower + upper) / 2;
        pj_uint16_t pid = buffer->packets[(buffer->tail + middle) % buffer->size].pid;  

        if (pid <= sequence_num) {
            if (sequence_num - pid < half_uint16) {
                index = middle;
                lower = middle + 1;
            } else {
                upper = middle - 1;
            }
        } else {
            if (pid - sequence_num < half_uint16) {
                upper = middle - 1; 
            } else {
                index = middle;
                lower = middle + 1; 
            }
        }
    }

    if (index == -1) {
        return PJ_FALSE;
    }

    pjmedia_rtcp_fb_nack *packet = &buffer->packets[(buffer->tail + index) % buffer->size];

    // Remove all older packets
    buffer->tail = (buffer->tail + index + 1) % buffer->size;
    buffer->count -= index + 1;

    if (packet->pid == sequence_num || blp_contains(packet, sequence_num)) {
        return PJ_TRUE;
    } else {
        return PJ_FALSE;
    }
}

unsigned pjmedia_nack_buffer_len(pjmedia_nack_buffer *buffer) {
    return buffer->count;
}

