#include "bs_sets/bs_sets.h"
#include <iostream>

int main() {
    bs_universe *u = nullptr;
    bs_status s = bs_universe_create(&u);
    bs_value val;
    s = bs_value_from_integer(u, 42, &val);
    if (s != BS_OK || bs_value_hash(val) == 0) {
        bs_universe_destroy(u);
        return 1;
    }
    bs_universe_destroy(u);
    return 0;
}
