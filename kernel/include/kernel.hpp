#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../common/boot_protocol.h"

using Framebuffer = KuroganeFramebuffer;

extern "C" KUROGANE_SYSV_ABI void kmain(void* boot_argument);
