
#include <pj/types.h>

#include <pjsip-ua/sip_inv.h>
#include <pjsip/sip_types.h>

#include <pjsua.h>

#include <pjnat64/pj-nat64.h>

#define THIS_FILE "pj-nat64.c"

/**
 * Use algorithmic map
 *
 * @see https://datatracker.ietf.org/doc/html/rfc6052#section-2.1
 */
#ifndef USE_RFC6052
# define USE_RFC6052 1
#endif

/* Interface */

static void patch_sdp_attr(pj_pool_t *pool, pjmedia_sdp_media *media, pjmedia_sdp_attr *attr, pj_str_t addr_type, pj_str_t addr);
static void patch_sdp_addr(pj_pool_t *pool, pjmedia_sdp_session *sdp, pj_str_t addr_type, pj_str_t addr);

static void patch_msg_sdp_body_old(pj_pool_t *pool, pjsip_msg *msg, pjmedia_sdp_session *sdp);
static void patch_msg_sdp_body(pj_pool_t *pool, pjsip_msg *msg, pjmedia_sdp_session *sdp);

static void modern_sip_msg_dump(const char *title, pjsip_msg *msg);
static void modern_sip_msg_sdp_dump(const char *title, pjsip_msg *msg);
static void modern_sdp_dump(const char *title, pjmedia_sdp_session *sdp);

static pjmedia_sdp_session *modern_sip_sdp_parse(pj_pool_t *pool, pjsip_msg *msg);

static void modern_update_rx_data(pjsip_rx_data *rdata, pjmedia_sdp_session *sdp);
static void modern_update_tx_data(pjsip_tx_data *tdata, pjsip_msg *msg);

static void patch_sdp_ipv6_with_ipv4(pjsip_tx_data *tdata, pjsip_msg *msg);
static void patch_sdp_ipv4_with_ipv6(pjsip_rx_data *rdata);

/* Struct */

struct addr {
    pj_str_t in4_addr;  /* IPv4 address */
    int      in4_port;  /* IPv4 port    */
    pj_str_t in6_addr;  /* IPv6 address */
    int      in6_port;  /* IPv6 port    */
};

/* Consts */

static pj_caching_pool cp;
static pj_bool_t mod_enable = PJ_TRUE;
static pj_bool_t mod_debug = PJ_FALSE;
static pj_pool_t *mod_pool = NULL;

static struct addr hpbx;
static struct addr acc;

/* Implementation */

/**
 * Initialize address
 *
**/
static void addr_init(struct addr *adr)
{
    /* Initialize IN4 addr */
    adr->in4_addr.slen = 0;
    adr->in4_addr.ptr = NULL;
    adr->in4_port = -1;
    /* Initialize IN6 addr */
    adr->in6_addr.slen = 0;
    adr->in6_addr.ptr = NULL;
    adr->in6_port = -1;
}

static void addr_dump(const char *title, struct addr *adr)
{
    PJ_LOG(4, (THIS_FILE, "%s = [ IN4 = '%.*s':%d IN6 = '%.*s':%d ]",
        title,
        adr->in4_addr.slen, adr->in4_addr.ptr, adr->in4_port,
        adr->in6_addr.slen, adr->in6_addr.ptr, adr->in6_port
    ));
}

/**
 * Modern SDP replace routine on SDP attribute
 *
**/
static void patch_sdp_attr(pj_pool_t *pool, pjmedia_sdp_media *media, pjmedia_sdp_attr *attr, pj_str_t addr_type, pj_str_t addr)
{
    char rtcp_buf[128] = { 0 };
    pj_str_t rtcp_name = pj_str("rtcp");
    pjmedia_sdp_rtcp_attr ra;
    pj_status_t s2;

    if ((attr == NULL) || (attr->name.slen == 0)) {
        return;
    }

    PJ_LOG(4, (THIS_FILE, "Process SDP attribute %.*s", attr->name.slen, attr->name.ptr));

    if (strncmp(attr->name.ptr, rtcp_name.ptr, rtcp_name.slen) == 0) {
        s2 = pjmedia_sdp_attr_get_rtcp(attr, &ra);
        if (s2 != PJ_SUCCESS) {
            PJ_LOG(1, (THIS_FILE, "Error: Patch SDP attribute 'rtcp'"));
            return;
        }

        /* Patch */
        ra.addr_type = addr_type;
        ra.addr = addr;

        // a=rtcp:4001 IN IP6 2001:db8::1
        pj_ansi_snprintf(rtcp_buf, 128, "%u %.*s %.*s %.*s",
            ra.port,
            ra.net_type.slen, ra.net_type.ptr,
            ra.addr_type.slen, ra.addr_type.ptr,
            ra.addr.slen, ra.addr.ptr
        );
        //
        PJ_LOG(4, (THIS_FILE, "Patch SDP attribute rtcp = %s", rtcp_buf));

        // Update value
        pj_strdup2(pool, &attr->value, rtcp_buf);
    }

    // TODO - other attribute may process here...

}

/**
 * Replace SDP address
 *
**/
static void patch_sdp_addr(pj_pool_t *pool, pjmedia_sdp_session *sdp, pj_str_t addr_type, pj_str_t addr)
{

    /* Debug message */
    PJ_LOG(4, (THIS_FILE, "Patch SDP body: TYPE -> %.*s ADDR -> %.*s",
        addr_type.slen, addr_type.ptr,
        addr.slen, addr.ptr
    ));

    /* Patch origin */
    sdp->origin.addr_type = addr_type;
    sdp->origin.addr = addr;

    /* Patch connection */
    if (sdp->conn != NULL) {
        pjmedia_sdp_conn *conn = sdp->conn;
        /* Update connection */
        conn->addr_type = addr_type;
        conn->addr = addr;
    }

    /* Patch attributes */
    for(unsigned idx=0; idx < sdp->attr_count; idx++) {
        pjmedia_sdp_attr *attr = sdp->attr[idx];
        patch_sdp_attr(pool, NULL, attr, addr_type, addr);
    }

    /* Patch each media */
    for(unsigned idx=0; idx < sdp->media_count; idx++) {
        pjmedia_sdp_media *media = sdp->media[idx];
        if (media->conn != NULL) {
            pjmedia_sdp_conn *conn = media->conn;
            /* Update connection */
            conn->addr_type = addr_type;
            conn->addr = addr;
        }
        for(unsigned idx2=0; idx2 < sdp->attr_count; idx2++) {
            pjmedia_sdp_attr *attr = media->attr[idx2];
            patch_sdp_attr(pool, media, attr, addr_type, addr);
        }
    }

}

/**
 * Update TX data
 *
**/
static void modern_update_tx_data(pjsip_tx_data *tdata, pjsip_msg *msg)
{
    pj_pool_t *pool = tdata->pool;
    char *payload = NULL;
    int size = 0;

    /* Step 1. Update TX buffer */
    if (tdata->buf.start == NULL) {
        PJ_LOG(1, (THIS_FILE, "Error TX buffer does not create by sender"));
        return;
    }
    tdata->buf.cur = tdata->buf.start;
    tdata->buf.end = tdata->buf.start + PJSIP_MAX_PKT_LEN;
    size = pjsip_msg_print(msg, tdata->buf.start, tdata->buf.end - tdata->buf.start);
    if (size <= 0) {
        PJ_LOG(1, (THIS_FILE, "Error print SIP message"));
        return;
    }
    tdata->buf.cur[size] = '\0';
    tdata->buf.cur += size;

}

/**
 * Modern SDP replace routine
 *
**/
static void patch_sdp_ipv6_with_ipv4(pjsip_tx_data *tdata, pjsip_msg *msg)
{
    pj_pool_t *pool = tdata->pool;
    pj_status_t status;
    pjmedia_sdp_session *sdp = NULL;
    pjmedia_sdp_session *new_sdp = NULL;
    int size = 0;
    pj_str_t addr_type;
    pj_str_t addr;
    pj_str_t sip_msg;
    pj_str_t sdp_payload;
    pj_str_t new_sdp_payload;
    pj_str_t new_sip_msg;

    /* Step 1. Determine our IPv4 address */
    pj_strdup2(pool, &addr_type, "IP4");
    pj_strdup(pool, &addr, &acc.in4_addr);

    /* Step 2. Get SIP message SDP body */
    pjsip_tdata_sdp_info *sdp_info = pjsip_tdata_get_sdp_info(tdata);
    sdp = sdp_info->sdp;
    if (sdp == NULL) {
        PJ_LOG(4, (THIS_FILE, "No SDP info on TX"));
        return;
    }
    new_sdp = pjmedia_sdp_session_clone(pool, sdp);

    /* Step 3. Process SDP body attrs */
    patch_sdp_addr(pool, new_sdp, addr_type, addr);

    /* Step 4. Patch SDP body */
    patch_msg_sdp_body(pool, msg, new_sdp);

}

/**
 * Patch SIP message with new SDP body
 *
**/
static void patch_msg_sdp_body(pj_pool_t *pool, pjsip_msg *msg, pjmedia_sdp_session *sdp)
{
    pj_status_t status;

    /* Step 0. Debug message */
    PJ_LOG(4, (THIS_FILE, "Patch SDP body"));

    /* Step 1. Remove SIP automatic update headers */
    while (pjsip_msg_find_remove_hdr(msg, PJSIP_H_CONTENT_LENGTH, NULL) != NULL);
    while (pjsip_msg_find_remove_hdr(msg, PJSIP_H_CONTENT_TYPE, NULL) != NULL);

    /* Step 2. Update SDP body */
    status = pjsip_create_sdp_body(pool, sdp, &msg->body);
    if (status != PJ_SUCCESS) {
        PJ_LOG(1, (THIS_FILE, "Error: Create SDP structure"));
        return;
    }

}

static void modern_sip_msg_dump(const char *title, pjsip_msg *msg)
{
    char packet[PJSIP_MAX_PKT_LEN] = { 0 };
    int size;

    /* Step 0. Check debug mode */
    if (!mod_debug) {
        return;
    }

    /* Step 1. Print SIP message */
    size = pjsip_msg_print(msg, packet, PJSIP_MAX_PKT_LEN);
    if (size <= 0) {
        PJ_LOG(1, (THIS_FILE, "Error dump SIP message"));
        return;
    }

    /* Step 2. Debug SIP message */
    PJ_LOG(4, (THIS_FILE, "--- BEGIN %s ---\n%.*s\n--- END %s ---", title, size, packet, title));

}

static pjmedia_sdp_session *modern_sip_sdp_parse(pj_pool_t *pool, pjsip_msg *msg)
{
    pj_status_t status;
    char *packet = NULL;
    char *payload = NULL;
    int size;
    pjmedia_sdp_session *sdp = NULL;
    pjsip_msg *tmp_msg;

    /* Step 1. Print SIP message */
    packet = pj_pool_alloc(pool, PJSIP_MAX_PKT_LEN);
    size = pjsip_msg_print(msg, packet, PJSIP_MAX_PKT_LEN);
    if (size <= 0) {
        PJ_LOG(1, (THIS_FILE, "Error print SIP message"));
        return NULL;
    }

    /* Step 2. Parse SIP message */
    tmp_msg = pjsip_parse_msg(pool, packet, size, NULL);
    if (tmp_msg == NULL) {
        PJ_LOG(1, (THIS_FILE, "Error parse SIP message"));
        return NULL;
    }

    /* Step 3. Check SDP body exists */
    if (tmp_msg->body == NULL) {
        PJ_LOG(4, (THIS_FILE, "SIP message without SIP payload"));
        return NULL;
    }

    /* Step 4. Print SDP body */
    payload = pj_pool_alloc(pool, PJSIP_MAX_PKT_LEN);
    size = pjsip_print_text_body(tmp_msg->body, payload, PJSIP_MAX_PKT_LEN);
    if (size <= 0) {
        PJ_LOG(1, (THIS_FILE, "Error print SIP message"));
        return NULL;
    }

    /* Step 5. Parse SDP body */
    status = pjmedia_sdp_parse(pool, payload, size, &sdp);
    if (status != PJ_SUCCESS) {
        PJ_LOG(1, (THIS_FILE, "Error parse SDP body"));
        return NULL;
    }

    /* Step 6. Done */
    return sdp;

}

static void modern_sdp_dump(const char *title, pjmedia_sdp_session *sdp)
{
    char payload[PJSIP_MAX_PKT_LEN] = { 0 };
    int size;

    /* Step 0. Check debug mode */
    if (!mod_debug) {
        return;
    }

    /* Step 1. Print SDP payload */
    size = pjmedia_sdp_print(sdp, payload, PJSIP_MAX_PKT_LEN);
    if (size <= 0) {
        PJ_LOG(1, (THIS_FILE, "Error dump SDP body"));
        return;
    }

    /* Step 2. Debug SDP payload */
    PJ_LOG(4, (THIS_FILE, "--- BEGIN %s ---\n%.*s\n--- END %s ---\n", title, size, payload, title));

}

static void modern_update_rx_data(pjsip_rx_data *rdata, pjmedia_sdp_session *sdp)
{
    pj_pool_t *pool = rdata->tp_info.pool;
    char *payload = NULL;
    int size = 0;
    pjsip_msg *msg = rdata->msg_info.msg;

    /* Step 1. Update SDP parameters */
    pjsip_rdata_sdp_info *sdp_info = pjsip_rdata_get_sdp_info(rdata);
    modern_sdp_dump("RX SDP INFO", sdp_info->sdp);
    sdp_info->sdp = sdp;
    modern_sdp_dump("NEW RX SDP INFO", sdp_info->sdp);
    if (sdp != NULL) {
        payload = pj_pool_alloc(pool, PJSIP_MAX_PKT_LEN);
        size = pjmedia_sdp_print(sdp, payload, PJSIP_MAX_PKT_LEN);
    }
    sdp_info->body.ptr = payload;
    sdp_info->body.slen = size;

}

/**
 * Modern replace
 *
**/
static void patch_sdp_ipv4_with_ipv6(pjsip_rx_data *rdata)
{
    pj_pool_t *pool = rdata->tp_info.pool;
    pj_status_t status;
    pjmedia_sdp_session *sdp = NULL;
    pjmedia_sdp_session *new_sdp = NULL;
    pjsip_msg *msg = NULL;
    pj_str_t sip_msg;
    pj_str_t new_sip_msg;
    pj_str_t sdp_payload;
    pj_str_t new_sdp_payload;
    pj_str_t addr_type;
    pj_str_t addr;
    int size;

    /* Step 1. Prepare HPBX server IPv6 address */
    pj_strdup2(pool, &addr_type, "IP6");
    pj_strdup(pool, &addr, &hpbx.in6_addr);

    /* Step 2. Get SIP message SDP body */
    pjsip_rdata_sdp_info *sdp_info = pjsip_rdata_get_sdp_info(rdata);
    sdp = sdp_info->sdp;
    if (sdp == NULL) {
        PJ_LOG(4, (THIS_FILE, "No SDP info on RX"));
        return;
    }
    new_sdp = pjmedia_sdp_session_clone(pool, sdp);

    /* Step 3. Patch SDP address */
    patch_sdp_addr(pool, new_sdp, addr_type, addr);

    /* Step 4. Update SIP message with new SDP body */
    patch_msg_sdp_body(pool, rdata->msg_info.msg, new_sdp);

    /* Step 5. Update SIP message */
    modern_update_rx_data(rdata, new_sdp);

}

#if defined(USE_RFC6052) && (USE_RFC6052 == 1)
static void map_ipv4_with_ipv6(pj_pool_t *pool, pj_str_t *dst, pj_str_t *src)
{
    struct in_addr in_addr;
    struct in6_addr in6_addr;
    pj_status_t status;
    // IPv6 Well Known address - "64:ff96::/96"
    //                   00    64:   ff    9b :  00    00 :  00    00 :   00    00 :  00    00 :  00    00 :  00    00
    //                                                                                             8     8     8     8
    char prefix[16] = { 0x00, 0x64, 0xff, 0x9b, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    if (0 == pj_strcmp2(src, "127.0.0.1")) {
        pj_strdup2(pool, dst, "[::1]");
    } else {

        /* Step 1. Parse source IP address */
        status = pj_inet_pton(PJ_AF_INET, src, &in_addr);
        if (status != PJ_SUCCESS) {
            PJ_LOG(1, (THIS_FILE, "Error parse address '%.*s' (pj_inet_pton)",
                (int)src->slen, src->ptr
            ));
            return;
        }

        /* Step 2. Map address with "::/96" subnet */
        memcpy(&in6_addr, &prefix[0], sizeof(prefix));
        char *ptr = &in6_addr;
        memcpy(ptr + 12, &in_addr, sizeof(in_addr));

        /* Step 3. Print result */
        char tmp[PJ_INET6_ADDRSTRLEN] = { 0 };
        status = pj_inet_ntop(PJ_AF_INET6, &in6_addr, tmp, PJ_INET6_ADDRSTRLEN);
        if (status != PJ_SUCCESS) {
            PJ_LOG(1, (THIS_FILE, "Error print address '%.*s' (pj_inet_ntop)",
                (int)src->slen, src->ptr
            ));
            return;
        }
        pj_strdup2(pool, dst, tmp);
    }

    /* Step 2. Debug message */
    PJ_LOG(4, (THIS_FILE, "AMap IPv4 -> IPv6 address %.*s -> %.*s",
        src->slen, src->ptr,
        dst->slen, dst->ptr
    ));

}
#else
static void map_ipv4_with_ipv6(pj_pool_t *pool, pj_str_t *dst, pj_str_t *src)
{

    /* Step 1. Map */
    if (0 == pj_strcmp2(src, "127.0.0.1")) {
        pj_strdup2(pool, dst, "[::1]");
    } else if (0 == pj_strcmp(src, &acc.in4_addr)) {
        pj_strdup(pool, dst, &acc.in6_addr);
    } else if (0 == pj_strcmp(src, &hpbx.in4_addr)) {
        pj_strdup(pool, dst, &hpbx.in6_addr);
    }

    /* Step 2. Debug message */
    PJ_LOG(4, (THIS_FILE, "TMap IPv4 -> IPv6 address %.*s -> %.*s",
        src->slen, src->ptr,
        dst->slen, dst->ptr
    ));

}
#endif

#if defined(USE_RFC6052) && (USE_RFC6052 == 1)
static void map_ipv6_with_ipv4(pj_pool_t *pool, pj_str_t *dst, pj_str_t *src)
{

    char prefix[4] = { 0x00, 0x64, 0xff, 0x9b };
    struct in_addr in_addr;
    struct in6_addr in6_addr;
    pj_status_t status;

    /* Step 1. Map */
    if (0 == pj_strcmp2(src, "[::1]")) {
        pj_strdup2(pool, dst, "127.0.0.1");
    } else {

        /* Step 1. Parse IPv6 address */
        status = pj_inet_pton(PJ_AF_INET6, src, &in6_addr);
        if (status != PJ_SUCCESS) {
            PJ_LOG(1, (THIS_FILE, "Error parse address '%.*s' (pj_inet_pton)",
                (int)src->slen, src->ptr
            ));
            return;
        }

        /* Step 2. Check IPv6 address use Well Known prefix */
        if (0 != pj_memcmp(&in6_addr, prefix, sizeof(prefix))) {
            PJ_LOG(4, (THIS_FILE, "AMap: no NAT64 network address '%.*s'",
                (int)src->slen, src->ptr
            ));
            return;
        }

        /* Step 3. Map address with "::/96" subnet */
        memset(&in_addr, 0, sizeof(in_addr));
        char *ptr = &in6_addr;
        memcpy(&in_addr, ptr + 12, sizeof(in_addr));

        /* Step 5. Print result */
        char tmp[PJ_INET_ADDRSTRLEN] = { 0 };
        status = pj_inet_ntop(PJ_AF_INET, &in_addr, tmp, PJ_INET_ADDRSTRLEN);
        if (status != PJ_SUCCESS) {
            PJ_LOG(1, (THIS_FILE, "Error print address '%.*s' (pj_inet_ntop)",
                (int)src->slen, src->ptr
            ));
            return;
        }
        pj_strdup2(pool, dst, tmp);
    }

    /* Step 2. Debug message */
    PJ_LOG(4, (THIS_FILE, "AMap IPv6 -> IPv4 address '%.*s' -> '%.*s'",
        src->slen, src->ptr,
        dst->slen, dst->ptr
    ));

}
#else
static void map_ipv6_with_ipv4(pj_pool_t *pool, pj_str_t *dst, pj_str_t *src)
{

    /* Step 1. Map */
    if (0 == pj_strcmp2(src, "[::1]")) {
        pj_strdup2(pool, dst, "127.0.0.1");
    } else if (0 == pj_strcmp(src, &acc.in6_addr)) {
        pj_strdup(pool, dst, &acc.in4_addr);
    } else if (0 == pj_strcmp(src, &hpbx.in6_addr)) {
        pj_strdup(pool, dst, &hpbx.in4_addr);
    }

    /* Step 2. Debug message */
    PJ_LOG(4, (THIS_FILE, "TMap IPv6 -> IPv4 address '%.*s' -> '%.*s'",
        src->slen, src->ptr,
        dst->slen, dst->ptr
    ));

}
#endif

/**
 * Replace SIP message "Record-Route" headers
 *
**/
static void patch_record_route_ipv4_with_ipv6(pjsip_rx_data *rdata)
{
    pj_pool_t *pool = rdata->tp_info.pool;
    pjsip_route_hdr *prev_rr_hdr = NULL;
    pjsip_route_hdr *update_rr_hdr = NULL;
    pjsip_msg *msg = rdata->msg_info.msg;
    pj_str_t addr;
    pjsip_hdr *hdr = (pjsip_hdr *)msg->hdr.next;
    pjsip_hdr *end = &msg->hdr;

    /* Step 1. Search */
    for (; hdr != end; hdr = hdr->next) {
        if (hdr->type == PJSIP_H_RECORD_ROUTE) {
            pjsip_route_hdr *current_rr_hdr = (pjsip_route_hdr *)hdr;
            prev_rr_hdr = update_rr_hdr;
            update_rr_hdr = current_rr_hdr;
        }
    }

    /* Step 2. Update previus */
    if (prev_rr_hdr) {
        if (prev_rr_hdr->name_addr.uri != NULL) {
            /* Step 2. Replace */
            pjsip_sip_uri *sip_uri = NULL;
            sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(prev_rr_hdr->name_addr.uri);
            pj_str_t map_addr = { .slen = 0, .ptr = 0 };
            map_ipv4_with_ipv6(pool, &map_addr, &sip_uri->host);
            if (map_addr.slen != 0) {
                PJ_LOG(4, (THIS_FILE, "RX patch1 Record-Route header %.*s -> %.*s",
                    sip_uri->host.slen, sip_uri->host.ptr,
                    map_addr.slen, map_addr.ptr
                ));
                sip_uri->host.slen = map_addr.slen;
                sip_uri->host.ptr = map_addr.ptr;
            }
        }
    }

    /* Step 3. Update current */
    if (update_rr_hdr) {
        if (update_rr_hdr->name_addr.uri != NULL) {
            /* Step 2. Replace */
            pjsip_sip_uri *sip_uri = NULL;
            sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(update_rr_hdr->name_addr.uri);
            pj_str_t map_addr = { .slen = 0, .ptr = 0 };
            map_ipv4_with_ipv6(pool, &map_addr, &sip_uri->host);
            if (map_addr.slen != 0) {
                PJ_LOG(4, (THIS_FILE, "RX patch2 Record-Route header %.*s -> %.*s",
                    sip_uri->host.slen, sip_uri->host.ptr,
                    map_addr.slen, map_addr.ptr
                ));
                sip_uri->host.slen = map_addr.slen;
                sip_uri->host.ptr = map_addr.ptr;
            }
        }
    }

}

/**
 * Replace SIP message "Record-Route" headers
 *
**/
static void patch_record_route_ipv6_with_ipv4(pjsip_tx_data *tdata, pjsip_msg *msg) {
    pj_pool_t *pool = tdata->pool;
    pjsip_route_hdr *prev_rr_hdr = NULL;
    pjsip_route_hdr *update_rr_hdr = NULL;
    pj_str_t addr;
    pjsip_hdr *hdr = (pjsip_hdr *)msg->hdr.next;
    pjsip_hdr *end = &msg->hdr;

    /* Step 1. Search */
    for (; hdr != end; hdr = hdr->next) {
        if (hdr->type == PJSIP_H_RECORD_ROUTE) {
            pjsip_route_hdr *current_rr_hdr = (pjsip_route_hdr *)hdr;
            prev_rr_hdr = update_rr_hdr;
            update_rr_hdr = current_rr_hdr;
        }
    }

    /* Step 2. Previous */
    if (prev_rr_hdr) {
        if (prev_rr_hdr->name_addr.uri != NULL) {
            /* Step 2. Replace */
            pjsip_sip_uri *sip_uri = NULL;
            sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(prev_rr_hdr->name_addr.uri);
            pj_str_t map_addr = { .slen = 0, .ptr = 0 };
            map_ipv6_with_ipv4(pool, &map_addr, &sip_uri->host);
            if (map_addr.slen > 0) {
                PJ_LOG(4, (THIS_FILE, "TX patch1 Record-Route header '%.*s' -> '%.*s'",
                    (int)sip_uri->host.slen, sip_uri->host.ptr,
                    (int)map_addr.slen, map_addr.ptr
                ));
                sip_uri->host.slen = map_addr.slen;
                sip_uri->host.ptr = map_addr.ptr;
            }
        }
    }

    /* Step 3. Update */
    if (update_rr_hdr) {
        if (update_rr_hdr->name_addr.uri != NULL) {
            /* Step 2. Replace */
            pjsip_sip_uri *sip_uri = NULL;
            sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(update_rr_hdr->name_addr.uri);
            pj_str_t map_addr = { .slen = 0, .ptr = 0 };
            map_ipv6_with_ipv4(pool, &map_addr, &sip_uri->host);
            if (map_addr.slen > 0) {
                PJ_LOG(4, (THIS_FILE, "TX patch2 Record-Route header '%.*s' -> '%.*s'",
                    (int)sip_uri->host.slen, sip_uri->host.ptr,
                    (int)map_addr.slen, map_addr.ptr
                ));
                sip_uri->host.slen = map_addr.slen;
                sip_uri->host.ptr = map_addr.ptr;
            }
        }
    }

}

/**
 * Replace SIP message "Contact" back header
 *
**/
static void patch_contact_ipv4_with_ipv6(pjsip_rx_data *rdata)
{
    pj_pool_t *pool = rdata->tp_info.pool;
    pjsip_contact_hdr *contact = NULL;
    pjsip_msg *msg = rdata->msg_info.msg;
    pj_str_t addr;
    pjsip_hdr *hdr = (pjsip_hdr *)msg->hdr.next;
    pjsip_hdr *end = &msg->hdr;
    unsigned patch_count = 0;

    /* Step 1. Search */
    for (; hdr != end; hdr = hdr->next) {
        if (hdr->type == PJSIP_H_CONTACT) {
            pjsip_contact_hdr *current_contact_hdr = (pjsip_contact_hdr *)hdr;
            contact = current_contact_hdr;
        }
    }

    /* Step 2. Update */
    if (contact == NULL) {
        PJ_LOG(4, (THIS_FILE, "RX no-patch Contact header"));
        return;
    }

    /* Step 1. Common R-URI patch base on IP address mapping */
    if ((patch_count == 0) /* && (msg->type == PJSIP_REQUEST_MSG) */) {
        /* Step 1. Get actual R-URI address */
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(contact->uri);
        if (sip_uri == NULL) {
            return;
        }
        pj_str_t map_addr = { .slen = 0, .ptr = 0 };
        /* Step 2. Map IPv6 -> IPv4 address */
        map_ipv4_with_ipv6(pool, &map_addr, &sip_uri->host);
        /* Step 3. Mapping routine */
        if (map_addr.slen > 0) {
            PJ_LOG(4, (THIS_FILE, "RX patch Contact addr '%.*s':%d -> '%.*s':%d (use mapping)",
                (int)sip_uri->host.slen, sip_uri->host.ptr, sip_uri->port,
                (int)map_addr.slen, map_addr.ptr, sip_uri->port
            ));
            sip_uri->host.slen = map_addr.slen;
            sip_uri->host.ptr = map_addr.ptr;
            /* Update patch count */
            patch_count += 1;
        }
    }

    /* Step 2. R-URI patch base on account address */
    if (acc.in6_addr.slen > 0) {
        /* Step 1. Get actual R-URI address */
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(contact->uri);
        if (sip_uri == NULL) {
            return;
        }
        //
        PJ_LOG(4, (THIS_FILE, "RX patch Contact header %.*s -> %.*s (use account)",
            sip_uri->host.slen, sip_uri->host.ptr,
            addr.slen, addr.ptr
        ));
        sip_uri->host.slen = acc.in6_addr.slen;
        sip_uri->host.ptr = acc.in6_addr.ptr;
        if (acc.in6_port > 0) {
            sip_uri->port = acc.in6_port;
        }
    }

}

/**
 * Patch SIP message "Route" front header
 *
**/
static void patch_route_ipv6_with_ipv4(pjsip_tx_data *tdata, pjsip_msg *msg)
{
    pj_pool_t *pool = tdata->pool;
    pjsip_hdr *hdr = (pjsip_hdr *)msg->hdr.next;
    pjsip_hdr *end = &msg->hdr;
    pjsip_sip_uri *sip_uri = NULL;
    pj_str_t addr;

    for (; hdr != end; hdr = hdr->next) {
        if (hdr->type == PJSIP_H_ROUTE) {
            pjsip_route_hdr *cur = (pjsip_route_hdr *)hdr;
            sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(cur->name_addr.uri);
            pj_str_t map_addr = { .slen = 0, .ptr = 0 };
            map_ipv6_with_ipv4(pool, &map_addr, &sip_uri->host);
            if (map_addr.slen > 0) {
                PJ_LOG(4, (THIS_FILE, "TX patch Route header '%.*s' -> '%.*s'",
                    (int)sip_uri->host.slen, sip_uri->host.ptr,
                    (int)map_addr.slen, map_addr.ptr
                ));
                sip_uri->host.slen = map_addr.slen;
                sip_uri->host.ptr = map_addr.ptr;
            }
        }
    }

}

/**
 * Check IPv4 and IPv6 network address without port
 *
**/
pj_status_t parse_addr(pj_str_t *addr)
{
    pj_sockaddr new_addr;
    pj_status_t status;

    /* Step 1. Try parse IPv4 address */
    status = pj_inet_pton(PJ_AF_INET, addr, &new_addr.ipv6.sin6_addr);
    if (status == PJ_SUCCESS) {
        return PJ_SUCCESS;
    }

    /* Step 2. Try parse IPv6 address */
    status = pj_inet_pton(PJ_AF_INET6, addr, &new_addr.ipv6.sin6_addr);
    if (status == PJ_SUCCESS) {
        return PJ_SUCCESS;
    }

    return PJ_EINVAL;
}

static void patch_r_uri_ipv6_with_ipv4(pjsip_tx_data *tdata, pjsip_msg *msg)
{
    pj_pool_t *pool = tdata->pool;
    unsigned patch_count = 0;
    pj_status_t status;

    /* Step 0. Check request */
    if (msg->type != PJSIP_REQUEST_MSG) {
        return;
    }
    pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(msg->line.req.uri);

    /* Step 1. No process domain name at host */
    status = parse_addr(&sip_uri->host);
    if (status != PJ_SUCCESS) {
        PJ_LOG(4, (THIS_FILE, "TX patch R-URI addr '%.*s' (use FQDN)",
            (int)sip_uri->host.slen, sip_uri->host.ptr
        ));
        return;
    }

    /* Step 1. Common R-URI patch base on IP address mapping */
    if (patch_count == 0) {
        /* Step 1. Map IPv6 -> IPv4 address */
        pj_str_t map_addr = { .slen = 0, .ptr = 0 };
        map_ipv6_with_ipv4(pool, &map_addr, &sip_uri->host);
        /* Step 2. Mapping routine */
        if (map_addr.slen > 0) {
            PJ_LOG(4, (THIS_FILE, "TX patch R-URI addr '%.*s' -> '%.*s' (use mapping)",
                (int)sip_uri->host.slen, sip_uri->host.ptr,
                (int)map_addr.slen, map_addr.ptr
            ));
            sip_uri->host.slen = map_addr.slen;
            sip_uri->host.ptr = map_addr.ptr;
            /* Update patch count */
            patch_count += 1;
        }
    }

    /* Step 2. Patch R-URI with server IPv4 address */
    if ( 1 ) {
        /* Step 1. Replace over server IPv4 address */
        if (hpbx.in4_addr.slen > 0) {
            PJ_LOG(4, (THIS_FILE, "TX patch R-URI addr use server '%.*s' (use server addr)",
                (int)hpbx.in4_addr.slen, hpbx.in4_addr.ptr
            ));
            sip_uri->host.slen = hpbx.in4_addr.slen;
            sip_uri->host.ptr = hpbx.in4_addr.ptr;
            /* Update patch count */
            patch_count += 1;
        }
    }

    /* Step 3. Patch R-URI with custom IPv4 address */
    if (patch_count == 0) {
        /* Step 1. Replace over custom IPv4 address */
        pj_str_t map_addr = { .slen = 8, .ptr = "10.0.0.1" };
        PJ_LOG(4, (THIS_FILE, "TX patch R-URI addr '%.*s' -> '%.*s' (use 10.0.0.1)",
            (int)sip_uri->host.slen, sip_uri->host.ptr,
            (int)map_addr.slen, map_addr.ptr
        ));
        sip_uri->host.ptr = map_addr.ptr;
        sip_uri->host.slen = map_addr.slen;
        /* Update patch count */
        patch_count += 1;
    }

}

static void patch_contact_ipv6_with_ipv4(pjsip_tx_data *tdata, pjsip_msg *msg)
{
    pj_pool_t *pool = tdata->pool;
    unsigned patch_count = 0;

    /* Step 1. Search Contact */
    pjsip_contact_hdr *contact = (pjsip_contact_hdr*)pjsip_msg_find_hdr(msg, PJSIP_H_CONTACT, NULL);
    if (contact == NULL) {
        PJ_LOG(1, (THIS_FILE, "No TX patch Contact"));
        return;
    }

#if 0
    /* Step 1. Common R-URI patch base on IP address mapping */
    if ((patch_count == 0) && (msg->type == PJSIP_REQUEST_MSG)) {
        /* Step 1. Get actual R-URI address */
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(contact->uri);
        if (sip_uri == NULL) {
            return;
        }
        pj_str_t map_addr = { .slen = 0, .ptr = 0 };
        /* Step 2. Map IPv6 -> IPv4 address */
        map_ipv6_with_ipv4(pool, &map_addr, &sip_uri->host);
        /* Step 3. Mapping routine */
        if (map_addr.slen > 0) {
            PJ_LOG(4, (THIS_FILE, "TX patch Contact addr '%.*s':%d -> '%.*s':%d",
                (int)sip_uri->host.slen, sip_uri->host.ptr, sip_uri->port,
                (int)map_addr.slen, map_addr.ptr, sip_uri->port
            ));
            sip_uri->host.slen = map_addr.slen;
            sip_uri->host.ptr = map_addr.ptr;
            /* Update patch count */
            patch_count += 1;
        }
    }
#endif

    /* Step 2. Patch R-URI on request with server IPv4 address */
    if (msg->type == PJSIP_REQUEST_MSG) {
        /* Step 1. Get actual R-URI address */
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(contact->uri);
        if (sip_uri == NULL) {
            return;
        }
        //
        if (acc.in4_addr.slen > 0) {
            PJ_LOG(4, (THIS_FILE, "TX patch Contact addr use account '%.*s':%d (update port only on value >0)",
                (int)acc.in4_addr.slen, acc.in4_addr.ptr, acc.in4_port
            ));
            sip_uri->host.slen = acc.in4_addr.slen;
            sip_uri->host.ptr = acc.in4_addr.ptr;
            if (acc.in4_port > 0) {
                sip_uri->port = acc.in4_port;
            }
            /* Update patch count */
            patch_count += 1;
        }
    }

    /* Step 3. Patch R-URI with network IPv4 address */
    if (patch_count == 0) {
        /* Step 1. Get actual R-URI address */
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(contact->uri);
        if (sip_uri == NULL) {
            return;
        }
        //
        pj_str_t map_addr = { .slen = 8, .ptr = "10.0.0.1" };
        PJ_LOG(4, (THIS_FILE, "TX patch Contact addr '%.*s' -> '%.*s' (failback)",
            (int)sip_uri->host.slen, sip_uri->host.ptr,
            (int)map_addr.slen, map_addr.ptr
        ));
        sip_uri->host.ptr = map_addr.ptr;
        sip_uri->host.slen = map_addr.slen;
        /* Update patch count */
        patch_count += 1;
    }

}

/**
 * Monitoring REGISTER answer with Via parameters with HPBX server address
 *
**/
static void search_ipv4_client_address(pjsip_rx_data *rdata)
{
    pj_pool_t *pool = rdata->tp_info.pool;
    pjsip_via_hdr *cur_via_hdr = NULL;
    pjsip_msg *msg = rdata->msg_info.msg;
    pjsip_cseq_hdr *cseq = rdata->msg_info.cseq;
    pj_str_t addr;
    pjsip_hdr *hdr = (pjsip_hdr *)msg->hdr.next;
    pjsip_hdr *end = &msg->hdr;

    /* Step 1. Process response on REGISTER */
    if ((msg->type == PJSIP_RESPONSE_MSG) && (cseq->method.id == PJSIP_REGISTER_METHOD)) {
    } else {
        return;
    }

    /* Step 2. Search "Via" header on response */
    for (; hdr != end; hdr = hdr->next) {
        if (hdr->type == PJSIP_H_VIA) {
            pjsip_via_hdr *via_hdr = (pjsip_via_hdr *)hdr;
            cur_via_hdr = via_hdr;
        }
    }
    if (cur_via_hdr != NULL) {
        pj_str_t addr = cur_via_hdr->recvd_param;
        int port = cur_via_hdr->rport_param;
        pj_nat64_set_client_addr(&addr, port);
    }

}

/**
 * Monitoring REGISTER answer with Via parameters with HPBX server address
 *
**/
static void search_ipv4_server_address(pjsip_rx_data *rdata)
{
    pjsip_msg *msg = rdata->msg_info.msg;
    pjsip_cseq_hdr *cseq = rdata->msg_info.cseq;

    /* Step 1. Process INVITE request */
    if ((msg->type == PJSIP_REQUEST_MSG) && (cseq->method.id == PJSIP_INVITE_METHOD)) {
        /* Step 1. Parse contact */
        pjsip_contact_hdr *contact = (pjsip_contact_hdr*)pjsip_msg_find_hdr(msg, PJSIP_H_CONTACT, NULL);
        if (contact == NULL) {
            return;
        }
        if (contact->uri == NULL) {
            return;
        }
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(contact->uri);
        if (sip_uri == NULL) {
            return;
        }
        pj_nat64_set_server_addr(&sip_uri->host, -1);
    }

    /* Step 2. Process INVITE response */
    if ((msg->type == PJSIP_RESPONSE_MSG) && (cseq->method.id == PJSIP_INVITE_METHOD)) {
        /* Step 1. Parse contact */
        pjsip_contact_hdr *contact = (pjsip_contact_hdr*)pjsip_msg_find_hdr(msg, PJSIP_H_CONTACT, NULL);
        if (contact == NULL) {
            return;
        }
        if (contact->uri == NULL) {
            return;
        }
        pjsip_sip_uri *sip_uri = (pjsip_sip_uri *)pjsip_uri_get_uri(contact->uri);
        if (sip_uri == NULL) {
            return;
        }
        pj_nat64_set_server_addr(&sip_uri->host, -1);
    }

}

/**
 * Monitoring source SIP message address
 *
**/
static void search_ipv6_client_address(pjsip_rx_data *rdata)
{
    // TODO - Not yet implemented...
}

/**
 * Monitoring source SIP message address
 *
**/
static void search_ipv6_server_address(pjsip_rx_data *rdata)
{
    /* Step 1. Update HPBX server IPv6 address */
    pj_str_t addr;
    pj_cstr(&addr, rdata->pkt_info.src_name);
    // TODO - check IPv6 address...
    pj_nat64_set_server_addr6(&addr, -1);
}

/**
 * Check IPv6 support on stransport
 *
**/
static pj_bool_t is_ipv6(pjsip_transport *transport)
{
    pjsip_transport_key *key = &transport->key;
    pj_bool_t result = PJ_FALSE;
    result |= key->type == PJSIP_TRANSPORT_UDP6;
    result |= key->type == PJSIP_TRANSPORT_TCP6;
    result |= key->type == PJSIP_TRANSPORT_TLS6;
    return result;
}

static pj_bool_t ipv6_mod_on_rx(pjsip_rx_data *rdata)
{
    pjsip_cseq_hdr *cseq = rdata->msg_info.cseq;
    if (cseq == NULL) {
        PJ_LOG(1, (THIS_FILE, "No C-Seq header"));
        return PJ_FALSE;
    }

    /* Step 0. Check enable module */
    if (!mod_enable) {
        PJ_LOG(4, (THIS_FILE, "NAT64 module was disable."));
        return PJ_FALSE;
    }
    if (!is_ipv6(rdata->tp_info.transport)) {
        PJ_LOG(4, (THIS_FILE, "Use non IPv6 transport. NAT64 module was disable."));
        return PJ_FALSE;
    }

    /* Step 1. Debug message */
    PJ_LOG(4, (THIS_FILE, "Process RX message %.*s", cseq->method.name.slen, cseq->method.name.ptr));
    modern_sip_msg_dump("RX source SIP message", rdata->msg_info.msg);

    /* Step 2. Peek HPBX server address */
    PJ_LOG(4, (THIS_FILE, "RX search HPBX server addr"));
    search_ipv4_client_address(rdata);
    search_ipv6_client_address(rdata);
    search_ipv4_server_address(rdata);
    search_ipv6_server_address(rdata);

    /* Step 2. Patch SIP message headers */
    PJ_LOG(4, (THIS_FILE, "RX patch SIP message headers: IPv4 -> IPv6"));
    patch_record_route_ipv4_with_ipv6(rdata);
    patch_contact_ipv4_with_ipv6(rdata);

    /* Step 3. Replace SDP body of SIP message */
    if ((cseq != NULL) && (cseq->method.id == PJSIP_INVITE_METHOD)) {
        PJ_LOG(4, (THIS_FILE, "RX patch SDP on INVITE: IPv4 -> IPv6"));
        patch_sdp_ipv4_with_ipv6(rdata);
    }

    /* Step 4. Dump source SIP message */
    modern_sip_msg_dump("RX patched SIP message", rdata->msg_info.msg);

    return PJ_FALSE;
}

pj_status_t ipv6_mod_on_tx(pjsip_tx_data *tdata)
{
    pjsip_msg *msg = pjsip_msg_clone(tdata->pool, tdata->msg);
    if (msg == NULL) {
        PJ_LOG(1, (THIS_FILE, "No memory"));
        return PJ_ENOMEM;
    }

    pjsip_cseq_hdr *cseq = (pjsip_cseq_hdr*)pjsip_msg_find_hdr(msg, PJSIP_H_CSEQ, NULL);
    if (cseq == NULL) {
        PJ_LOG(1, (THIS_FILE, "No C-Seq header"));
        return PJ_SUCCESS;
    }

    /* Step 0. Check enable module */
    if (!mod_enable) {
        PJ_LOG(4, (THIS_FILE, "NAT64 module was disable."));
        return PJ_SUCCESS;
    }
    if (!is_ipv6(tdata->tp_info.transport)) {
        PJ_LOG(4, (THIS_FILE, "NAT64 processing IPv4 module was disable."));
        return PJ_SUCCESS;
    }

    /* Step 1. Debug message */
    PJ_LOG(4, (THIS_FILE, "Process TX message %.*s", cseq->method.name.slen, cseq->method.name.ptr));
    modern_sip_msg_dump("TX source SIP message", msg);

    /* Step 2. Patch SIP message headers */
    PJ_LOG(4, (THIS_FILE, "TX patch SIP message headers: IPv6 -> IPv4"));
    patch_record_route_ipv6_with_ipv4(tdata, msg);
    patch_route_ipv6_with_ipv4(tdata, msg);
    patch_r_uri_ipv6_with_ipv4(tdata, msg);
    patch_contact_ipv6_with_ipv4(tdata, msg);

    /* Step 3. Replace SDP body of SIP message */
    if ((cseq != NULL) && (cseq->method.id == PJSIP_INVITE_METHOD)) {
        PJ_LOG(4, (THIS_FILE, "TX patch SDP on INVITE: IPv6 -> IPv4"));
        patch_sdp_ipv6_with_ipv4(tdata, msg);
    }

    /* Step 4. Update SIP message */
    modern_update_tx_data(tdata, msg);

    /* Step 5. Dump  */
    modern_sip_msg_dump("TX patched SIP message", msg);

    return PJ_SUCCESS;
}

/* L1 rewrite module for sdp info.*/
static pjsip_module ipv6_module = {
    NULL, NULL,                       /* prev, next.      */
    { "mod-ipv6", 8},                 /* Name.            */
    -1,                               /* Id               */
    0,                                /* Priority         */
    NULL,                             /* load()           */
    NULL,                             /* start()          */
    NULL,                             /* stop()           */
    NULL,                             /* unload()         */
    &ipv6_mod_on_rx,                  /* on_rx_request()  */
    &ipv6_mod_on_rx,                  /* on_rx_response() */
    &ipv6_mod_on_tx,                  /* on_tx_request.   */
    &ipv6_mod_on_tx,                  /* on_tx_response() */
    NULL,                             /* on_tsx_state()   */
};

pj_status_t pj_nat64_enable_rewrite_module()
{
    pjsip_endpoint *endpt = pjsua_get_pjsip_endpt();
    pj_status_t result;

    /* Step 1. Create module memory home */
    if (mod_pool == NULL) {
#ifdef MOD_IPV6_ENDPT_MEMORY
        PJ_LOG(4, (THIS_FILE, "Create memory home on endpoint"));
        /* Step 1. Create module memory home */
        mod_pool = pjsip_endpt_create_pool(endpt, "ipv6", 512, 512);
#else
        PJ_LOG(4, (THIS_FILE, "Create memory home on caching pool"));
        /* Step 1. Create module memory home */
        pj_caching_pool_init( &cp, &pj_pool_factory_default_policy, 0);
        mod_pool = pj_pool_create(&cp.factory, "ipv6", 512, 512, NULL);
#endif
        /* Step 2. Set random account address */
        addr_init(&hpbx);
        addr_init(&acc);

        /* Step 3. Set random account address */
        pj_str_t addr = { .ptr = "10.0.0.1", .slen = 8 };
        pj_nat64_set_client_addr(&addr, -1);
    }

    /* Step 2. Register module */
    result = pjsip_endpt_register_module(endpt, &ipv6_module);

    return result;
}

pj_status_t pj_nat64_disable_rewrite_module()
{
    pjsip_endpoint *endpt = pjsua_get_pjsip_endpt();
    pj_status_t result;
    /* Step 1. Unregister module */
    result = pjsip_endpt_unregister_module(endpt, &ipv6_module);
    /* Step 2. Release memory home */
    if (mod_pool != NULL) {
#ifdef MOD_IPV6_ENDPT_MEMORY
        PJ_LOG(4, (THIS_FILE, "Release memory home on endpoint"));
        pjsip_endpt_release_pool(endpt, mod_pool);
#else
        PJ_LOG(4, (THIS_FILE, "Release memory home on caching pool"));
        pj_pool_release(mod_pool);
#endif
        mod_pool = NULL;
    }
    return result;
}

/**
 * Enable/disable NAT64 features
 *
**/
void pj_nat64_set_enable(pj_bool_t yesno)
{
    mod_enable = yesno;
}

/**
 * Dump address
 *
**/
void pj_nat64_dump()
{
    addr_dump("HPBX", &hpbx);
    addr_dump("ACC", &acc);
}

/**
 * Set HPBX server IPv4 address
 *
**/
void pj_nat64_set_server_addr(pj_str_t *addr, int port)
{
    if (mod_pool == NULL) {
        PJ_LOG(1, (THIS_FILE, "No module memory pool"));
        return;
    }
    /* Step 1. Check arguments*/
    if (addr == NULL) {
        return;
    }
    if ((addr->slen == 0) || (addr->ptr == NULL)) {
        return;
    }
    /* Step 2. Check change exists */
    if (0 == pj_strcmp(&hpbx.in4_addr, addr)) {
        return;
    }
    /* Step 3. Debug message */
    PJ_LOG(4, (THIS_FILE, "NAT64 update HPBX IN4 address '%.*s':%d -> '%.*s':%d",
        hpbx.in4_addr.slen, hpbx.in4_addr.ptr, hpbx.in4_port,
        addr->slen, addr->ptr, port
    ));
    /* Step 4. Update HPBX server addr value */
    pj_strdup_with_null(mod_pool, &hpbx.in4_addr, addr);
    hpbx.in4_port = port;
    /* Step 5. Dump maps */
    pj_nat64_dump();
}

void pj_nat64_set_server_addr6(pj_str_t *addr, int port)
{
    if (mod_pool == NULL) {
        PJ_LOG(1, (THIS_FILE, "No module memory pool"));
        return;
    }
    /* Step 1. Check arguments*/
    if (addr == NULL) {
        return;
    }
    if ((addr->slen == 0) || (addr->ptr == NULL)) {
        return;
    }
    /* Step 2. Check change exists */
    if (0 == pj_strcmp(&hpbx.in6_addr, addr)) {
        return;
    }
    /* Step 3. Debug message */
    PJ_LOG(4, (THIS_FILE, "NAT64 update HPBX IN6 address '%.*s':%d -> '%.*s':%d",
        hpbx.in6_addr.slen, hpbx.in6_addr.ptr, hpbx.in6_port,
        addr->slen, addr->ptr, port
    ));
    /* Step 4. Update HPBX server addr value */
    pj_strdup_with_null(mod_pool, &hpbx.in6_addr, addr);

    /* Step 5. Dump maps */
    pj_nat64_dump();
}

void pj_nat64_set_client_addr(pj_str_t *addr, int port)
{
    if (mod_pool == NULL) {
        PJ_LOG(1, (THIS_FILE, "No module memory pool"));
        return;
    }
    /* Step 1. Check arguments */
    if (addr == NULL) {
        return;
    }
    if ((addr->slen == 0) || (addr->ptr == NULL)) {
        return;
    }
    /* Step 2. Check change exists */
    if (0 == pj_strcmp(&acc.in4_addr, addr)) {
        return;
    }
    /* Step 3. Debug message */
    PJ_LOG(4, (THIS_FILE, "NAT64 update ACC IN4 address '%.*s':%d -> '%.*s':%d",
        acc.in4_addr.slen, acc.in4_addr.ptr, acc.in4_port,
        addr->slen, addr->ptr, port
    ));
    /* Step 4. Update HPBX server addr value */
    pj_strdup_with_null(mod_pool, &acc.in4_addr, addr);
    acc.in4_port = port;
    /* Step 5. Dump maps */
    pj_nat64_dump();
}

void pj_nat64_set_client_addr6(pj_str_t *addr, int port)
{
    if (mod_pool == NULL) {
        PJ_LOG(1, (THIS_FILE, "No module memory pool"));
        return;
    }
    /* Step 1. Check arguments */
    if (addr == NULL) {
        return;
    }
    if ((addr->slen == 0) || (addr->ptr == NULL)) {
        return;
    }
    /* Step 2. Check change exists */
    if (0 == pj_strcmp(&acc.in6_addr, addr)) {
        return;
    }
    /* Step 3. Debug message */
    PJ_LOG(4, (THIS_FILE, "NAT64 update ACC IN6 address '%.*s':%d -> '%.*s':%d",
        acc.in6_addr.slen, acc.in6_addr.ptr, acc.in6_port,
        addr->slen, addr->ptr, port
    ));
    /* Step 4. Update HPBX server addr value */
    pj_strdup_with_null(mod_pool, &acc.in6_addr, addr);
    acc.in6_port = port;
    /* Step 5. Dump maps */
    pj_nat64_dump();
}

/**
 * Debug options
 *
 */
void pj_nat64_set_debug(pj_bool_t yesno)
{
    mod_debug = yesno;
}
