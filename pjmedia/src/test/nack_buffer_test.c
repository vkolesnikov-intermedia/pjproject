#include <pjmedia/nack_buffer.h>
#include <pj/math.h>
#include "test.h"

#define THIS_FILE   "nack_buffer_test.c"
#define BUFFER_SIZE 5

static int test_that_new_packets_added(pjmedia_nack_buffer *buffer) {
    unsigned index;
    for (index = 0; index <= PJ_MAX(BUFFER_SIZE, 10); index++) {
        pjmedia_rtcp_fb_nack nack;
        nack.pid = (uint16_t)index;
        nack.blp = 0;
        pj_status_t result = pjmedia_nack_buffer_push(buffer, nack);
        if (result != PJ_SUCCESS) {
            return result; 
        }
        unsigned length = pjmedia_nack_buffer_len(buffer);
        unsigned expected_length = PJ_MIN(index + 1, BUFFER_SIZE);
        if (length != expected_length) {
            PJ_LOG(3, (THIS_FILE, "nack_buffer length is incorrect: Expected: %u. Real: %u", expected_length, length));
            return -1;
        }
    }
    return 0;
}

static int test_that_packet_is_found_in_buffer_by_pid(pjmedia_nack_buffer *buffer) {
    uint16_t pids[2] = { 1, 2 };
    unsigned index;
    for (index = 0; index < PJ_ARRAY_SIZE(pids); index++) {
        pjmedia_rtcp_fb_nack nack;
        nack.pid = pids[index];
        nack.blp = 0;
        pj_status_t status = pjmedia_nack_buffer_push(buffer, nack);
        if (status != PJ_SUCCESS) {
            return status; 
        }
    }

    for (index = 0; index < PJ_ARRAY_SIZE(pids); index++) {
        pj_bool_t is_found = pjmedia_nack_buffer_frame_dequeued(buffer, pids[index]);
        if (!is_found) {
            PJ_LOG(3,(THIS_FILE, "Packet %u was not found in nack buffer", pids[index]));
            return -1;
        }
    }
    
    unsigned length = pjmedia_nack_buffer_len(buffer);
    if (length > 0) {
        PJ_LOG(3,(THIS_FILE, "nack buffer must be empty"));
        return -1; 
    }
    return 0;
}

static int test_that_packet_is_found_in_buffer_by_blp(pjmedia_nack_buffer *buffer) {
    pjmedia_rtcp_fb_nack first_nack;
    pjmedia_rtcp_fb_nack second_nack;
    first_nack.pid = 1;
    first_nack.blp = 0b11;
    second_nack.pid = 20;
    second_nack.blp = 0b1;

    pj_status_t first_push_result = pjmedia_nack_buffer_push(buffer, first_nack);
    if (first_push_result != PJ_SUCCESS) {
        return first_push_result; 
    }
    pj_status_t second_push_result = pjmedia_nack_buffer_push(buffer, second_nack); 
    if (second_push_result != PJ_SUCCESS) {
        return second_push_result; 
    }

    unsigned lost_packets[2] = { 2, 21 };
    unsigned index;
    for (index = 0; index < PJ_ARRAY_SIZE(lost_packets); index++) {
        pj_bool_t is_packet_found = pjmedia_nack_buffer_frame_dequeued(buffer, lost_packets[index]);
        if (!is_packet_found) {
           PJ_LOG(3,(THIS_FILE, "Packet %u was not found in nack buffer", lost_packets[index])); 
           return -1;
        }
    }
    unsigned length = pjmedia_nack_buffer_len(buffer);
    if (length > 0) {
        PJ_LOG(3,(THIS_FILE, "nack buffer must be empty"));
        return -1; 
    }
    return 0;
}

static int test_that_not_added_packet_is_not_found_in_buffer(pjmedia_nack_buffer *buffer) {
    pjmedia_rtcp_fb_nack first_nack;
    pjmedia_rtcp_fb_nack second_nack;
    first_nack.pid = 1;
    first_nack.blp = 0b11;
    second_nack.pid = 20;
    second_nack.blp = 0b1;

    pjmedia_nack_buffer_push(buffer, first_nack);
    pjmedia_nack_buffer_push(buffer, second_nack); 

    unsigned not_added_packets[3] = { 4, 22, 50 };
    unsigned index;
    for (index = 0; index < PJ_ARRAY_SIZE(not_added_packets); index++) {
        pj_bool_t is_packet_found = pjmedia_nack_buffer_frame_dequeued(buffer, not_added_packets[index]);
        if (is_packet_found) {
           PJ_LOG(3,(THIS_FILE, "Unexpeted packet %u was found in nack buffer", not_added_packets[index])); 
           return -1;
        }
    }
    unsigned length = pjmedia_nack_buffer_len(buffer);
    if (length > 0) {
        PJ_LOG(3,(THIS_FILE, "nack buffer must be empty"));
        return -1; 
    }
    return 0;
}

static int test_that_old_packets_removed_from_buffer_when_new_packet_added(pjmedia_nack_buffer *buffer) {
    uint16_t packets[8] = { 1, 20, 45, 78, 100, 120, 150, 240 };
    unsigned index;

    for (index = 0; index < PJ_ARRAY_SIZE(packets); index++) {
        pjmedia_rtcp_fb_nack nack; 
        nack.pid = packets[index];
        nack.blp = 0;
        pj_status_t status = pjmedia_nack_buffer_push(buffer, nack); 
        if (status != PJ_SUCCESS) {
            return status;
        }
        if (index < BUFFER_SIZE) {
            continue; 
        }
        pj_bool_t is_packet_found = pjmedia_nack_buffer_frame_dequeued(buffer, packets[index - BUFFER_SIZE]);
        if (is_packet_found) {
            PJ_LOG(3,(THIS_FILE, "Unexpeted packet %u was found in nack buffer", packets[index - BUFFER_SIZE])); 
            return -1;
        }
    }
    for (index = BUFFER_SIZE; index < PJ_ARRAY_SIZE(packets); index ++)  {
        pj_bool_t is_packet_found = pjmedia_nack_buffer_frame_dequeued(buffer, packets[index]);
        if (!is_packet_found) {
            PJ_LOG(3,(THIS_FILE, "Packet %u was not found in nack buffer", packets[index])); 
            return -1;
        } 
    }
    return 0;
}

static int test_that_old_packets_removed_from_buffer_when_more_recent_packet_played(pjmedia_nack_buffer *buffer) {
    uint16_t packets[8] = { 1, 20, 45, 78, 100, 120, 150, 240 };
    unsigned index;

    for (index = 0; index < PJ_ARRAY_SIZE(packets); index++) {
        pjmedia_rtcp_fb_nack nack; 
        nack.pid = packets[index];
        nack.blp = 0;
        pjmedia_nack_buffer_push(buffer, nack);
    }

    int played_packets[3] = { 115, 125, 300 };
    unsigned expected_length[3] = { 3, 2, 0 };
    for (index = 0; index < PJ_ARRAY_SIZE(played_packets); index++) {
        pj_bool_t is_packet_found = pjmedia_nack_buffer_frame_dequeued(buffer, played_packets[index]);
        PJ_ASSERT_RETURN(!is_packet_found, -1);

        unsigned length = pjmedia_nack_buffer_len(buffer);
        if (length != expected_length[index]) {
            PJ_LOG(3,(THIS_FILE, "Unexpected buffer length: %u. Expected: %u", length, expected_length[index]));
            return -1; 
        }
    }
    return 0;
}

static int test_that_packet_found_when_integer_overflow_happend(pjmedia_nack_buffer *buffer) {
    uint16_t packets[5] = { 0xFFFB, 0xFFFF, 5, 15, 100 };
    unsigned index;

    for (index = 0; index < PJ_ARRAY_SIZE(packets); index++) {
        pjmedia_rtcp_fb_nack nack; 
        nack.pid = packets[index];
        nack.blp = 0;
        pjmedia_nack_buffer_push(buffer, nack);
    }

    for (index = 0; index < PJ_ARRAY_SIZE(packets); index++) {
        pj_bool_t is_packet_found = pjmedia_nack_buffer_frame_dequeued(buffer, packets[index]);
        if (!is_packet_found) {
           PJ_LOG(3,(THIS_FILE, "Packet %u was not found.", packets[index])); 
           return -1;
        }
        unsigned length = pjmedia_nack_buffer_len(buffer);
        unsigned expected_len = PJ_ARRAY_SIZE(packets) - index - 1;
        if (length != expected_len) {
            PJ_LOG(3,(THIS_FILE, "Unexpected buffer length: %u. Expected: %u", length, expected_len));
            return -1; 
        }
    }
    return 0;
}

static int test_that_packet_found_by_blp_when_integer_overflow_happend(pjmedia_nack_buffer *buffer) {
    pjmedia_rtcp_fb_nack nack; 
    nack.pid = 0xFFFF;
    nack.blp = 0b11;
    pjmedia_nack_buffer_push(buffer, nack);

    pj_bool_t is_packet_found = pjmedia_nack_buffer_frame_dequeued(buffer, 2);
    if (!is_packet_found) {
        PJ_LOG(3,(THIS_FILE, "Packet 2 was not found.")); 
        return -1;
    }
    unsigned length = pjmedia_nack_buffer_len(buffer);
    if (length > 0) {
        PJ_LOG(3,(THIS_FILE, "Nack buffer must be empty"));
        return -1; 
    }
    return 0;
}

static int test_buffer_reset(pjmedia_nack_buffer *buffer) {
    unsigned index;
    for (index = 0; index <= PJ_MAX(BUFFER_SIZE, 10); index++) {
        pjmedia_rtcp_fb_nack nack;
        nack.pid = (uint16_t)index;
        nack.blp = 0;
        pj_status_t result = pjmedia_nack_buffer_push(buffer, nack);
        if (result != PJ_SUCCESS) {
            return result; 
        }
    }
    pjmedia_nack_buffer_reset(buffer);
    if (pjmedia_nack_buffer_len(buffer) > 0) {
        PJ_LOG(3, (THIS_FILE, "Nack buffer must be empty."));
        return -1;
    }
    return 0;
}

static struct test
{
    const char *title;
    int (*test_function)(pjmedia_nack_buffer*);
} test[] =
{
    {
        "Nack buffer push",
        test_that_new_packets_added 
    },
    {
        "Find packet by PID in nack buffer",
        test_that_packet_is_found_in_buffer_by_pid 
    },
    {
        "Find packet by BLP in nack buffer",
        test_that_packet_is_found_in_buffer_by_blp
    },
    {
        "Not added packets not found in nack buffer",
        test_that_not_added_packet_is_not_found_in_buffer 
    },
    {
        "Old packet removed when new packet added",
        test_that_old_packets_removed_from_buffer_when_new_packet_added
    },
    {
        "Old packet removed when frame from more recent packet played",
        test_that_old_packets_removed_from_buffer_when_more_recent_packet_played
    },
    {
        "Packet found by PID when integer overflow happend",
        test_that_packet_found_when_integer_overflow_happend
    },
    {
        "Packet found by BLP when integer overflow happend",
        test_that_packet_found_by_blp_when_integer_overflow_happend
    },
    {
        "Buffer reset",
        test_buffer_reset
    }
};

int nack_buffer_test()
{
    pj_pool_t *pool = pj_pool_create(mem, "nack_buffer_test", 4000, 4000, NULL);
    if (!pool) {
        return PJ_ENOMEM;
    }

    unsigned test_index;
    int result = 0;
    for (test_index = 0; test_index < PJ_ARRAY_SIZE(test); test_index++) {
        PJ_LOG(3, (THIS_FILE,"  test %d: %s", test_index, test[test_index].title));

        pjmedia_nack_buffer *buffer;
        pj_status_t status = pjmedia_nack_buffer_create(pool, BUFFER_SIZE, &buffer);
        if (status != PJ_SUCCESS) {
            result = status;
            break;
        }

        int test_result = test[test_index].test_function(buffer);
        if (test_result != 0) {
            result = test_result;
            break;
        } else {
            PJ_LOG(3,(THIS_FILE, "%s test succeeded", test[test_index].title));
        }
    }

    pj_pool_release(pool);
    return result;
}