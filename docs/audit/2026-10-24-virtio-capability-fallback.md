# Iron Shield VirtIO PCI capability fallback — 2026-10-24

VirtIO 1.2 §4.1.4.1 requires ignoring vendor-specific capabilities with a
reserved BAR value and allows multiple instances of the same configuration
type. The original transport parser instead rejected the *whole* capability
list when the first otherwise valid Notify mapping used a reserved BAR.

Now the candidate is treated as unsupported and skipped. The next usable
capability of the same type is accepted. For repeated usable capabilities, the
first remains preferred. Malformed length/overflow and cyclic lists still
fail. New host regressions exercise both fallback and precedence.

Spec: https://docs.oasis-open.org/virtio/virtio/v1.2/virtio-v1.2.html

Qualify with the 7.0 exact-commit VirtIO-net+block QEMU workflow and full
host suite. This is not physical hardware qualification or 7.0 stable.
