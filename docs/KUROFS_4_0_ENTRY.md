# KuroFS 4.0 entry

The 4.0 Pre-Steel storage work begins only after the persistent allocator slice passes the production host regression suite and release kernel build on the self-hosted KVM runner. The allocator must use the production `storage::block::Device` contract and survive remount without overlapping extents or inode reuse.
