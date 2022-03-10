#ifndef __RINGBUFFER_H__
#define __RINGBUFFER_H__

#include <string.h>
#include <stdlib.h>

typedef struct {
    size_t beg_index_;
    size_t end_index_;
    size_t size_;
    size_t capacity_;
    char* data_;
} ringbuf_t;

size_t ring_size(ringbuf_t* r);
size_t ring_capacity(ringbuf_t* r);
void ring_init(size_t capacity, ringbuf_t* r);
void ring_destroy(ringbuf_t* r);
size_t ring_free(ringbuf_t* r);                                  // free space in the ring, it's not to destroy resource allocated by ring
size_t ring_write(const char* data, size_t bytes, ringbuf_t* r); // Return number of bytes written.
size_t ring_peek(char* data, size_t bytes, ringbuf_t* r);
size_t ring_peek_offset(size_t offset, char* data, size_t bytes, ringbuf_t* r);
size_t ring_remove(size_t bytes, ringbuf_t* r);
size_t ring_read(char* data, size_t bytes, ringbuf_t* r); // Return number of bytes read.
char ring_peek_char(size_t pos, ringbuf_t* r);
int ring_indexof(char c, ringbuf_t* r);

#endif
