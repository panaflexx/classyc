/* cyfiber.c — host-compiled fiber/channel runtime for the classyc driver.
 *
 * Compiled into the `classyc` executable so that JIT (-eg) programs using
 * go/await/Chan can resolve cy_* symbols through the driver's
 * import_resolver.  The same implementation is compiled for AOT via
 * src/mir-aot-runtime.c under #ifdef CHANFIBERS.
 *
 * Build needs -I include -I ext/ccchan (set in CMakeLists.txt).
 */

#define CYFIBER_IMPLEMENTATION
#include "cyfiber.h"
