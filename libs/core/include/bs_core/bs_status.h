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

/** VM domain status and error codes. */
typedef enum bs_vm_status {
    BS_VM_UNDEFINED_SYMBOL      = BS_STATUS_MAKE(BS_DOMAIN_VM, 1),
    BS_VM_IMMUTABLE_BINDING     = BS_STATUS_MAKE(BS_DOMAIN_VM, 2),
    BS_VM_SHADOW_FORBIDDEN      = BS_STATUS_MAKE(BS_DOMAIN_VM, 3),
    BS_VM_REDEFINITION          = BS_STATUS_MAKE(BS_DOMAIN_VM, 4),
    BS_VM_CONSTRAINT_VIOLATION  = BS_STATUS_MAKE(BS_DOMAIN_VM, 5),
    BS_VM_FRAME_STACK_OVERFLOW  = BS_STATUS_MAKE(BS_DOMAIN_VM, 6),
    BS_VM_FRAME_STACK_UNDERFLOW = BS_STATUS_MAKE(BS_DOMAIN_VM, 7),
    BS_VM_UNHANDLED_SIGNAL      = BS_STATUS_MAKE(BS_DOMAIN_VM, 8),
    BS_VM_MODULE_NOT_FOUND      = BS_STATUS_MAKE(BS_DOMAIN_VM, 9),
    BS_VM_ARITY_MISMATCH        = BS_STATUS_MAKE(BS_DOMAIN_VM, 10),
    BS_VM_TYPE_ERROR            = BS_STATUS_MAKE(BS_DOMAIN_VM, 11),
    BS_VM_INVALID_BLOCK         = BS_STATUS_MAKE(BS_DOMAIN_VM, 12),
} bs_vm_status;

/** Returns the canonical name of the status code's domain. */
BS_API const char *bs_status_domain_name(bs_status status);

/** Returns a static human-readable description for a status code. */
BS_API const char *bs_status_string(bs_status status);

#ifdef __cplusplus
}
#endif

#endif /* BS_CORE_BS_STATUS_H */
