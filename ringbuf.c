
#include "ringbuf.h"

#include <string.h>

#define ringbuffer_shift_ptr(p, s, e, w) ((p) + (w) <= (e) ? (p) + (w) : (s) + ((w) - ((e)-(p))))
#define safe_sub(a, b) ((a) >= (b) ? ((a) - (b)) : 0)

typedef enum {
  RINGBUF_STATE_1 = 0      //  #   RA   [WP] WA  [RP]  RA   #
, RINGBUF_STATE_2          //  #   WA   [RP] RA  [WP]  WA   #
, RINGBUF_STATE_3          //  #   WA   [WP,RP]        WA   # | WRITTEN == 0
, RINGBUF_STATE_4          //  #   RA   [WP,RP]        RA   # | WRITTEN != 0
, RINGBUF_STATE_INVALID    // SHOULD NOT HAPPEN
} ringbuffer_state_t;

static ringbuffer_state_t ringbuffer_get_state(ringbuffer_t *rb) {
    uint8_t *rp = rb->rp;
    uint8_t *wp = rb->wp;
    uint8_t *bs = rb->bs;
    uint8_t *be = rb->be;
    size_t  written = rb->written;
    ringbuffer_state_t state = RINGBUF_STATE_INVALID;
    if( wp < rp ) {
        state = RINGBUF_STATE_1;
    } else if( wp > rp ) {
        state = RINGBUF_STATE_2;
    } else if( wp == rp && !written ) {
        state = RINGBUF_STATE_3;
    } else if( wp == rp && written ) {
        state = RINGBUF_STATE_4;
    }
    return state;
}

void ringbuffer_reset(ringbuffer_t *rb) {
    rb->bs = &rb->data[0];
    rb->be = &rb->data[rb->data_size];
    rb->rp = rb->bs;
    rb->wp = rb->bs;
    rb->written = 0;
}

ringbuffer_t* ringbuffer_alloc(size_t data_size, uint8_t *data) {
    if( data_size < sizeof(ringbuffer_t) ) {
        return (ringbuffer_t*)0;
    } else {
        ringbuffer_t *tmp = (ringbuffer_t*)data;
        tmp->data_size = data_size - sizeof(ringbuffer_t) + 1;
        ringbuffer_reset(tmp);
        return tmp;
    }
    return (ringbuffer_t*)0;
}

size_t ringbuffer_write_avail(ringbuffer_t *rb) {
    uint8_t *rp = rb->rp;
    uint8_t *wp = rb->wp;
    uint8_t *bs = rb->bs;
    uint8_t *be = rb->be;
    size_t  avail = 0;

    switch( ringbuffer_get_state(rb) ) {
        case RINGBUF_STATE_1:
            avail = (size_t)(rp - wp);
            break;
        case RINGBUF_STATE_2:
            avail = ((size_t)(be - wp) + (size_t)(rp - bs));
            break;
        case RINGBUF_STATE_3:
            avail = ((size_t)(be - wp) + (size_t)(wp - bs));
            break;
        case RINGBUF_STATE_4:
            avail = 0;
            break;
    }

    return avail;
}

size_t ringbuffer_read_avail(ringbuffer_t *rb) {
    uint8_t *rp = rb->rp;
    uint8_t *wp = rb->wp;
    uint8_t *bs = rb->bs;
    uint8_t *be = rb->be;
    size_t  avail = 0;

    switch( ringbuffer_get_state(rb) ) {
        case RINGBUF_STATE_1:
            avail = ((size_t)(be - rp) + (size_t)(wp - bs));
            break;
        case RINGBUF_STATE_2:
            avail = ((size_t)(wp - rp));
            break;
        case RINGBUF_STATE_3:
            avail = 0;
            break;
        case RINGBUF_STATE_4:
            avail = ((size_t)(be - rp) + (size_t)(wp - bs));
            break;
    }

    return avail;
}

size_t ringbuffer_write(ringbuffer_t *rb, const void *src, size_t size) {
    uint8_t *rp = rb->rp;
    uint8_t *wp = rb->wp;
    uint8_t *bs = rb->bs;
    uint8_t *be = rb->be;
    size_t towrite = size;
    size_t avail = ringbuffer_write_avail(rb);
    towrite = towrite < avail ? towrite : avail;
    if( !avail || !towrite ) return 0;
    switch( ringbuffer_get_state(rb) ) {
        case RINGBUF_STATE_1:
            memcpy(wp, src, towrite);
            break;
        case RINGBUF_STATE_2:
        case RINGBUF_STATE_3: {
            size_t rest = (size_t)(be - wp);
            size_t w1 = towrite <= rest ? towrite : rest;
            size_t w2 = safe_sub(towrite, w1);
            size_t rest2 = (size_t)(rp - bs);
            size_t w3 = w2 <= rest2 ? w2 : rest2;
            if( w1 ) memcpy(wp, src, w1);
            if( w3 ) memcpy(bs, src + w1, w3);
            }
            break;
        default:
            towrite = 0;
            break;
    }
    rb->wp = ringbuffer_shift_ptr(wp, bs, be, towrite);
    rb->written += towrite;
    return towrite;
}

size_t ringbuffer_read(ringbuffer_t *rb, void *dst, size_t size) {
    uint8_t *rp = rb->rp;
    uint8_t *wp = rb->wp;
    uint8_t *bs = rb->bs;
    uint8_t *be = rb->be;
    size_t toread = size;
    size_t avail = ringbuffer_read_avail(rb);
    toread = toread < avail ? toread : avail;
    if( !avail || !toread ) return 0;
    switch( ringbuffer_get_state(rb) ) {
        case RINGBUF_STATE_1:
        case RINGBUF_STATE_4: {
            size_t rest = (size_t)(be - rp);
            size_t r1 = toread <= rest ? toread : rest;
            size_t r2 = safe_sub(toread, r1);
            size_t rest2 = (size_t)(wp - bs);
            size_t r3 = r2 <= rest2 ? r2 : rest2;
            if( r1 ) memcpy(dst, rp, r1);
            if( r3 ) memcpy(dst + r1, bs, r3);
        }
        break;
        case RINGBUF_STATE_2:
            memcpy(dst, rp, toread);
            break;
        default:
            toread = 0;
            break;
    }
/*    printf("DEBUG %08X\n", rb->rp); */
    rb->rp = ringbuffer_shift_ptr(rp, bs, be, toread);
/*    printf("DEBUG %08X\n", rb->rp); */
    rb->written = safe_sub(rb->written, toread);
    return toread;
}

