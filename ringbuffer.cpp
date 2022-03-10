#include <string.h>
#include <stdlib.h>
#include "ringbuffer.h"

#define MIN(a,b) (((a)<(b))?(a):(b))

size_t ring_size(ringbuf_t * r) {
    return r->size_;
}

size_t ring_capacity(ringbuf_t * r) {
    return r->capacity_;
}

void ring_init(size_t capacity, ringbuf_t * r) {
    r->beg_index_=0;
    r->end_index_ = 0;
    r->size_ = 0;
    r->capacity_ = capacity;
    r->data_ = (char*)malloc(capacity);
}

void ring_destroy(ringbuf_t * r) {
    free(r->data_);
}

size_t ring_free(ringbuf_t * r) {
    return r->capacity_ - r->size_;
}

// Return number of bytes written.
size_t ring_write(const char* data, size_t bytes, ringbuf_t * r) {
    if (bytes == 0) {
        return 0;
    }
    if (bytes > (r->capacity_ - r->size_)) {
        return 0;
    }
    //size_t bytes_to_write = std::min(bytes, capacity_ - size_);
    size_t bytes_to_write = bytes;

    // Write in a single step
    if (bytes_to_write <= r->capacity_ - r->end_index_) {
        memcpy(r->data_ + r->end_index_, data, bytes_to_write);
        r->end_index_ += bytes_to_write;
        if (r->end_index_ == r->capacity_) {
            r->end_index_ = 0;
        }
    }
    else { // Write in two steps
        size_t size_1 = r->capacity_ - r->end_index_;
        memcpy(r->data_ + r->end_index_, data, size_1);
        size_t size_2 = bytes_to_write - size_1;
        memcpy(r->data_, data + size_1, size_2);
        r->end_index_ = size_2;
    }
    r->size_ += bytes_to_write;
    return bytes_to_write;
}

size_t ring_peek(char* data, size_t bytes, ringbuf_t * r) {
    return ring_peek_offset(0, data, bytes, r);
}

size_t ring_peek_offset(size_t offset, char* data, size_t bytes, ringbuf_t * r) {
    if (bytes == 0) {
        return 0;
    }
    if (offset >= r->size_) {
        return 0;
    }
    size_t bytes_to_read = MIN(bytes + offset, r->size_);
    bytes_to_read -= offset;
    size_t beg1 = (r->beg_index_ + offset) % r->capacity_;
    if (bytes_to_read <= (r->capacity_ - beg1)) { // Read in a single step
        memcpy(data, r->data_ + beg1, bytes_to_read);
    }
    else { // Read in two steps
        size_t size_1 = r->capacity_ - beg1;
        memcpy(data, r->data_ + beg1, size_1);
        size_t size_2 = bytes_to_read - size_1;
        memcpy(data + size_1, r->data_, size_2);
    }
    return bytes_to_read;
}

size_t ring_remove(size_t bytes, ringbuf_t * r) {
    if (bytes == 0) {
        return 0;
    }
    size_t bytes_to_read = MIN(bytes, r->size_);
    if (bytes_to_read <= (r->capacity_ - r->beg_index_)) { // Read in a single step
        r->beg_index_ += bytes_to_read;
        if (r->beg_index_ == r->capacity_) {
            r->beg_index_ = 0;
        }
    }
    else { // Read in two steps
        r->beg_index_ = bytes_to_read - (r->capacity_ - r->beg_index_);
    }
    r->size_ -= bytes_to_read;
    return bytes_to_read;
}

// Return number of bytes read.
size_t ring_read(char* data, size_t bytes, ringbuf_t * r) {
    ring_peek(data, bytes, r);
    return ring_remove(bytes, r);
}

char ring_peek_char(size_t pos, ringbuf_t * r) {
    if (pos >= r->size_) {
        return 0; // caller is resposable for not request pos more than size
    }
    return r->data_[(r->beg_index_ + pos) % r->capacity_];
}

int ring_indexof(char c, ringbuf_t * r) {
    int result = -1;
    size_t offset;
    for (offset = 0; offset < ring_size(r); ++offset) {
        if (ring_peek_char(offset, r) == c) {
            result = (int)offset;
            return result;
        }
    }
    return result;
}
