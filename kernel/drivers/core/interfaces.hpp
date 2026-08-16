#pragma once

#include <stddef.h>
#include <stdint.h>

#include "../../libk/status.hpp"

namespace drivers::interfaces {

struct BlockDeviceOps {
    KStatus (*read_blocks)(
        void* context,
        uint64_t first_block,
        uint64_t block_count,
        void* destination,
        size_t destination_size);
    KStatus (*write_blocks)(
        void* context,
        uint64_t first_block,
        uint64_t block_count,
        const void* source,
        size_t source_size);
    KStatus (*flush)(void* context);
    uint32_t (*get_block_size)(void* context);
    uint64_t (*get_block_count)(void* context);
};

struct NetworkDeviceOps {
    KStatus (*send)(void* context, const void* frame, size_t size);
    KStatus (*receive)(
        void* context,
        void* frame,
        size_t capacity,
        size_t* received);
};

enum class InputEventType : uint8_t {
    KeyDown = 0,
    KeyUp,
    TextInput,
    MouseMove,
    MouseButtonDown,
    MouseButtonUp,
    MouseWheel,
};

struct InputEvent {
    InputEventType type;
    uint32_t code;
    int32_t value_x;
    int32_t value_y;
    uint32_t unicode;
};

struct InputDeviceOps {
    KStatus (*get_event)(void* context, InputEvent* event);
};

} // namespace drivers::interfaces
