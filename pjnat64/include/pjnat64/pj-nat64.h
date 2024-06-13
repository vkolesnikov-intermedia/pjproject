#ifndef PJ_NAT64_H_
#define PJ_NAT64_H_

#include <pj/types.h>

PJ_BEGIN_DECL

/**
 * Enable nat64 rewriting module.
 *
 */
PJ_DECL(pj_status_t) pj_nat64_enable_rewrite_module();

/**
 * Disable rewriting module, for instance when on a ipv4 network
 *
 */
PJ_DECL(pj_status_t) pj_nat64_disable_rewrite_module();

/**
 * Enable/disable all IPv6 rewriting module options
 *
 */
PJ_DECL(void) pj_nat64_set_enable(pj_bool_t yesno);

/**
 * Set HPBX server IPv4 address
 *
**/
PJ_DECL(void) pj_nat64_set_server_addr(pj_str_t *addr, int port);

/**
 * Set HPBX server IPv6 address
 *
**/
PJ_DECL(void) pj_nat64_set_server_addr6(pj_str_t *addr, int port);

/**
 * Set account IPv4 address
 *
**/
PJ_DECL(void) pj_nat64_set_client_addr(pj_str_t *addr, int port);

/**
 * Set account IPv6 address
 *
**/
PJ_DECL(void) pj_nat64_set_client_addr6(pj_str_t *addr, int port);

/** Dump maps
 *
**/
PJ_DECL(void) pj_nat64_dump();

/**
 * Debug options
 *
 */
PJ_DECL(void) pj_nat64_set_debug(pj_bool_t yesno);

PJ_END_DECL

#endif
