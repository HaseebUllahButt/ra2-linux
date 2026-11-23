/* order-replay hooks for the JOIN native build (private binary ra2-orders only).
 * build_join.py puts one call before each of these guest instructions in private copies of the
 * generated sources: 0x005158C4 (seed), 0x00540643 (call Queue_AI), 0x0054068A (frame boundary),
 * and replaces the measured-fps store at 0x00540BB5 by the constant 20, like orders.dll does.
 * The host FPU/SSE state (the guest x87 lives there) is saved and restored around each call.
 */
#define _GNU_SOURCE
#include <stdint.h>
#include <unistd.h>

#define G8(a)  (*(volatile uint8_t  *)(uintptr_t)(a))
#define G16(a) (*(volatile uint16_t *)(uintptr_t)(a))
#define G32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define S8(a, v)  (G8(a) = (uint8_t)(v))
#define S32(a, v) (G32(a) = (uint32_t)(v))
#define OR_EXIT() do { fflush(NULL); _exit(0); } while (0)
#include "orders_core.h"

static unsigned char fx_area[512 + 16];
#define FX_SAVE   unsigned char *fx = (unsigned char *)(((uintptr_t)fx_area + 15) & ~(uintptr_t)15); \
                  __asm__ volatile("fxsave (%0)\n\tfninit" :: "r"(fx) : "memory")
#define FX_REST   __asm__ volatile("fxrstor (%0)" :: "r"(fx) : "memory")

void join_orders_seed(void) { FX_SAVE; core_seed(); FX_REST; }
void join_orders_queue(void) { FX_SAVE; core_pre_queue(); FX_REST; }
void join_orders_boundary(void) { FX_SAVE; int end = core_boundary(); FX_REST; if (end) OR_EXIT(); }
