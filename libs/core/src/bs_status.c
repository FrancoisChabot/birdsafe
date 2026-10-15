#include "bs_core/bs_status.h"

const char *bs_status_domain_name(bs_status status) {
    switch (bs_status_domain(status)) {
        case BS_DOMAIN_CORE: return "core";
        case BS_DOMAIN_NUM:  return "num";
        case BS_DOMAIN_SETS: return "sets";
        case BS_DOMAIN_VM:   return "vm";
        case BS_DOMAIN_FRONTEND: return "frontend";
        default:             return "unknown";
    }
}

const char *bs_status_string(bs_status status) {
    if (status == BS_OK) {
        return "OK";
    }
    uint16_t domain = bs_status_domain(status);
    uint16_t code = bs_status_code(status);
    switch (domain) {
        case BS_DOMAIN_CORE:
            switch (code) {
                case 1: return "Invalid argument";
                case 2: return "Out of memory";
                default: return "Unknown core status";
            }
        case BS_DOMAIN_NUM:
            switch (code) {
                case 1: return "Overflow";
                case 2: return "Parse error";
                default: return "Unknown num status";
            }
        case BS_DOMAIN_SETS:
            switch (code) {
                case 1: return "Exhausted";
                case 2: return "Type error";
                case 3: return "Out of bounds";
                case 4: return "Callback error";
                default: return "Unknown sets status";
            }
        case BS_DOMAIN_VM:
            switch (code) {
                case 1: return "Undefined symbol";
                case 2: return "Immutable binding";
                case 3: return "Shadow forbidden";
                case 4: return "Redefinition";
                case 5: return "Constraint violation";
                case 6: return "Frame stack overflow";
                case 7: return "Frame stack underflow";
                case 8: return "Unhandled signal";
                case 9: return "Module not found";
                case 10: return "Arity mismatch";
                case 11: return "Type error";
                case 12: return "Invalid block";
                default: return "Unknown vm status";
            }
        case BS_DOMAIN_FRONTEND:
            switch (code) {
                case 1: return "Lexical error";
                case 2: return "Syntax error";
                default: return "Unknown frontend status";
            }
        default:
            return "Unknown status";
    }
}
