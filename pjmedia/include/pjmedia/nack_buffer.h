#ifndef __PJMEDIA_NACK_BUFFER_H__
#define __PJMEDIA_NACK_BUFFER_H__

/**
 * @file nack_buffer.h
 * @brief Ring buffer for packet ids sent in NACK message.
 */

#include <pjmedia/rtcp_fb.h>
#include <pjmedia/types.h>

PJ_BEGIN_DECL

/**
 * Opaque declaration for nack buffer.
 */
typedef struct pjmedia_nack_buffer pjmedia_nack_buffer;

PJ_DECL(pj_status_t)
pjmedia_nack_buffer_create(pj_pool_t *pool,
                          unsigned size,
                          pjmedia_nack_buffer **buffer);

PJ_DECL(pj_status_t)
pjmedia_nack_buffer_reset(pjmedia_nack_buffer *buffer);

PJ_DECL(pj_status_t)
pjmedia_nack_buffer_push(pjmedia_nack_buffer *buffer,
                         pjmedia_rtcp_fb_nack nack);

PJ_DECL(pj_bool_t)
pjmedia_nack_buffer_frame_dequeued(pjmedia_nack_buffer *buffer,
                                   uint16_t sequence_num);

unsigned pjmedia_nack_buffer_len(pjmedia_nack_buffer *buffer);

PJ_END_DECL

#endif /* __PJMEDIA_NACK_BUFFER_H__ */
