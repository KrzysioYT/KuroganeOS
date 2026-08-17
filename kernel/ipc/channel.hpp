#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ipc {

using ProcessId = uint64_t;
using Handle = uint64_t;

constexpr ProcessId INVALID_PROCESS_ID = 0U;
constexpr Handle INVALID_HANDLE = 0U;
constexpr size_t MAX_ENDPOINTS = 16U;
constexpr size_t MAX_CHANNELS = 16U;
constexpr size_t MAX_PENDING_CONNECTIONS = 8U;
constexpr size_t MAX_MESSAGES_PER_DIRECTION = 4U;
constexpr size_t MAX_SERVICE_NAME = 31U;
constexpr size_t MAX_MESSAGE_SIZE = 256U;

enum class Status : uint8_t {
    Ok = 0,
    AlreadyInitialized,
    NotInitialized,
    InvalidArgument,
    InvalidName,
    NameTooLong,
    AlreadyExists,
    NotFound,
    StaleHandle,
    AccessDenied,
    CapacityReached,
    WouldBlock,
    PeerClosed,
};

struct Message {
    ProcessId sender_pid;
    size_t size;
    uint8_t bytes[MAX_MESSAGE_SIZE];
};

Status initialize();
Status bind(
    ProcessId owner_pid,
    const char* name,
    size_t name_length,
    Handle* endpoint);
Status connect(
    ProcessId client_pid,
    const char* name,
    size_t name_length,
    Handle* channel);
Status accept(
    ProcessId server_pid,
    Handle endpoint,
    Handle* channel);
Status send(
    ProcessId sender_pid,
    Handle channel,
    const void* data,
    size_t size);
Status receive(
    ProcessId receiver_pid,
    Handle channel,
    Message* message);
Status close(ProcessId owner_pid, Handle handle);
void release_process(ProcessId pid);
const char* status_message(Status status);

} // namespace ipc
