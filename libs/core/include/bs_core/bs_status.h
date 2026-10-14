/**
 * @file bs_status.h
 * @brief Unified status and error codes for BirdSafe.
 */

#ifndef BS_CORE_BS_STATUS_H
#define BS_CORE_BS_STATUS_H

#include "bs_config.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Two-tiered packed status code: [16-bit Domain ID | 16-bit Subsystem Code]. */
typedef uint32_t bs_status;

#define BS_STATUS_MAKE(domain, code) (((domain) << 16) | ((code) & 0xFFFF))
#define bs_status_domain(s)          ((uint16_t)(((uint32_t)(s)) >> 16))
#define bs_status_code(s)            ((uint16_t)(((uint32_t)(s)) & 0xFFFF))
#define bs_status_is_ok(s)           (((uint32_t)(s)) == 0)

/** BirdSafe subsystem domains. */
typedef enum bs_domain {
    BS_DOMAIN_CORE = 0,
    BS_DOMAIN_NUM  = 1,
    BS_DOMAIN_SETS = 2,
    BS_DOMAIN_VM   = 3,
} bs_domain;

/** Core domain status and error codes. */
typedef enum bs_core_status {
    BS_OK               = 0,
    BS_INVALID_ARGUMENT = BS_STATUS_MAKE(BS_DOMAIN_CORE, 1),
    BS_OUT_OF_MEMORY    = BS_STATUS_MAKE(BS_DOMAIN_CORE, 2),
} bs_core_status;

/** Returns the canonical name of the status code's domain. */
BS_API const char *bs_status_domain_name(bs_status status);

/** Returns a static human-readable description for a status code. */
BS_API const char *bs_status_string(bs_status status);

#ifdef __cplusplus
}
#endif

#endif /* BS_CORE_BS_STATUS_H */
