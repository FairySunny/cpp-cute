#include <cmath>
#include "vm.h"
#include "misc.h"

void load_misc(obj_def & libs)
{
    // The 'global' object
    libs["G"] = new_empty_object();

    // The nil constant
    libs["null"] = {NIL};

    // Boolean constants
    libs["true"] = {BOOL, {.b = true}};
    libs["false"] = {BOOL, {.b = false}};

    // Float constants
    libs["nan"] = {FLOAT, {.f = NAN}};
    libs["inf"] = {FLOAT, {.f = INFINITY}};
}
