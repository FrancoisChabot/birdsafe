#include "bs_core/bs_truth.h"

const char *bs_truth_string(bs_truth truth) {
    switch (truth) {
        case BS_FALSE:
            return "False";
        case BS_TRUE:
            return "True";
        case BS_UNDECIDED:
            return "Undecided";
        default:
            return "Unknown truth";
    }
}
