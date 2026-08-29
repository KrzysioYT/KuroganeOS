# System overview

KuroganeOS is a freestanding x86-64 operating system that currently boots through UEFI and is developed against the formal `3.3.3-dev — Red Flux` baseline while `3.4.0-dev — System Services` is active development.

## Boot and kernel entry

EDK2/UEFI loads `EFI/BOOT/BOOTX64.EFI`. The standalone loader reads the PIE `kernel.elf`, validates and maps load segments, applies supported `R_X86_64_RELATIVE` relocations, obtains GOP and the UEFI memory map, exits boot services, and enters the kernel through the versioned `KuroganeBootInfo` handoff.

The x86-64 kernel establishes its own GDT/TSS and IDT, exception handling, physical and virtual memory managers, page-table ownership, kernel heap, ACPI/MADT discovery, interrupt/timer infrastructure, PCI discovery and the framebuffer console before bringing up higher-level subsystems.

## Processes, memory and scheduling

Red Flux includes ELF64 Ring-3 processes, PID/TID lifecycle, per-process address spaces, user/kernel isolation, syscall validation and resource cleanup. The scheduler supports cooperative kernel-thread execution and timer-preemptive Ring-3 execution. Generation-safe process/resource handles are used where stale-handle reuse would otherwise be unsafe.

The active System Services work also depends on timer-backed blocking semantics for userspace waits; scheduler behavior is part of service runtime qualification rather than a userspace-only concern.

## Storage and filesystems

The storage stack includes a block abstraction, GPT/partition handling and the storage paths used by the installer. Red Flux qualifies writable FAT32/VFS behavior and the public Ring-3 file API, including descriptor/process ownership and cleanup. KuroFS 1.0 and broader storage goals remain later Road-to-15 milestones and must not be inferred from Red Flux filesystem completion.

## Networking and TLS

The qualified Red Flux networking scope includes IPv4, DHCP, DNS, TCP and the supported NIC paths used by the QEMU qualification matrix. TLS/HTTPS is exercised in the guest with certificate validation enabled. IPv6, broader routing/firewall work and production-grade networking expansion remain later milestones.

## Graphics, desktop and audio

The current graphics stack is GOP/framebuffer and software-compositor based. Ring-3 desktop/session/application paths are present, but hardware GPU acceleration and the later Direct3D/compatibility work are not part of Red Flux.

Audio currently exposes the bounded AC'97 Ring-3 PCM path qualified for Red Flux. Full mixing, multiple streams, Intel HDA and broader multimedia infrastructure are later milestones.

## IPC and system services

The kernel provides real named IPC channels, generation-safe event handles and shared-memory objects with PID ownership and process-exit cleanup. `kurogane/service.h` exposes the current service registration/discovery/request-reply layer over named IPC.

`3.4.0-dev — System Services` is building on that foundation. The current `dev/3.4.1-event-broker` workstream contains the Ring-3 `events.v1` broker with bounded clients/subscriptions and subscribe/publish/unsubscribe protocol support. Event Broker runtime qualification, service metadata/version negotiation, recovery/restart behavior and additional settings/notification/account/session services are active or later 3.4 work and must not be described as complete until their runtime gates pass.

## Release boundary

`3.3.3-dev — Red Flux` is qualified only for its explicitly bounded milestone scope. SMP, hardware 3D acceleration, USB expansion, final security hardening, package management, updater/recovery, application compatibility and multiplatform work remain on the Road to 15. VirtualBox is an optional external validation target and is not part of completion percentages.
