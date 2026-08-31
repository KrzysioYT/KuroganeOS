#!/usr/bin/env python3
"""Separate stable inode incarnation generation from mutable metadata revision."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    p = ROOT / path
    text = p.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    p.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    replace_once(
        "kernel/fs/kurofs.hpp",
        "    uint32_t link_count;\n    uint32_t generation;\n};\n",
        "    uint32_t link_count;\n"
        "    // Stable incarnation identity. Future inode-slot reuse must advance this.\n"
        "    uint32_t generation;\n"
        "    // Optimistic metadata revision; every successful update_inode advances it.\n"
        "    uint32_t revision;\n};\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "    store_u32(bytes + 40U, inode.link_count);\n"
        "    store_u32(bytes + 44U, inode.generation);\n"
        "    store_u32(bytes + kInodeChecksumOffset, crc32(bytes, kInodeChecksumOffset));\n",
        "    store_u32(bytes + 40U, inode.link_count);\n"
        "    store_u32(bytes + 44U, inode.generation);\n"
        "    store_u32(bytes + 48U, inode.revision);\n"
        "    store_u32(bytes + kInodeChecksumOffset, crc32(bytes, kInodeChecksumOffset));\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "    inode.link_count = load_u32(bytes + 40U);\n"
        "    inode.generation = load_u32(bytes + 44U);\n"
        "    if (inode.id != expected_id ||\n"
        "        (inode.type != InodeType::Regular && inode.type != InodeType::Directory) ||\n"
        "        inode.link_count == 0U || inode.generation == 0U) {\n",
        "    inode.link_count = load_u32(bytes + 40U);\n"
        "    inode.generation = load_u32(bytes + 44U);\n"
        "    inode.revision = load_u32(bytes + 48U);\n"
        "    if (inode.id != expected_id ||\n"
        "        (inode.type != InodeType::Regular && inode.type != InodeType::Directory) ||\n"
        "        inode.link_count == 0U || inode.generation == 0U || inode.revision == 0U) {\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "    root.link_count = 1U;\n    root.generation = 1U;\n    encode_inode(root, sector);\n",
        "    root.link_count = 1U;\n    root.generation = 1U;\n    root.revision = 1U;\n    encode_inode(root, sector);\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "            inode.link_count = 1U;\n"
        "            inode.generation = 1U;\n"
        "            encode_inode(inode, sector + offset);\n",
        "            inode.link_count = 1U;\n"
        "            inode.generation = 1U;\n"
        "            inode.revision = 1U;\n"
        "            encode_inode(inode, sector + offset);\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "    if (current.generation != inode->generation || current.type != inode->type) {\n"
        "        return Status::StaleInode;\n"
        "    }\n"
        "    if (current.generation == UINT32_MAX) return Status::ArithmeticOverflow;\n",
        "    if (current.generation != inode->generation ||\n"
        "        current.revision != inode->revision || current.type != inode->type) {\n"
        "        return Status::StaleInode;\n"
        "    }\n"
        "    if (current.revision == UINT32_MAX) return Status::ArithmeticOverflow;\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "    Inode candidate = *inode;\n"
        "    candidate.generation = current.generation + 1U;\n",
        "    Inode candidate = *inode;\n"
        "    candidate.revision = current.revision + 1U;\n",
    )
    replace_once(
        "kernel/fs/kurofs.cpp",
        "    if (current.generation != inode->generation || current.type != inode->type ||\n"
        "        current.extent_start != inode->extent_start ||\n",
        "    if (current.generation != inode->generation || current.revision != inode->revision ||\n"
        "        current.type != inode->type || current.extent_start != inode->extent_start ||\n",
    )

    test = "tests/test_kurofs_data.cpp"
    replace_once(
        test,
        "    const uint32_t original_generation = inode.generation;\n",
        "    const uint32_t original_generation = inode.generation;\n"
        "    const uint32_t original_revision = inode.revision;\n",
    )
    replace_once(
        test,
        "    if (!expect(inode.generation == original_generation + 1U, \"generation increment\")) return 1;\n\n"
        "    Inode stale = inode;\n"
        "    stale.generation = original_generation;\n",
        "    if (!expect(inode.generation == original_generation, \"incarnation generation stable\")) return 1;\n"
        "    if (!expect(inode.revision == original_revision + 1U, \"metadata revision increment\")) return 1;\n\n"
        "    Inode stale = inode;\n"
        "    stale.revision = original_revision;\n",
    )
    replace_once(
        test,
        "                persisted.size == sizeof(payload) && persisted.generation == inode.generation,\n"
        "                \"persisted inode fields\")) return 1;\n",
        "                persisted.size == sizeof(payload) && persisted.generation == inode.generation &&\n"
        "                persisted.revision == inode.revision, \"persisted inode fields\")) return 1;\n",
    )
    replace_once(
        test,
        "    Inode stale_read = persisted;\n"
        "    stale_read.generation -= 1U;\n",
        "    Inode stale_read = persisted;\n"
        "    stale_read.revision -= 1U;\n",
    )


if __name__ == "__main__":
    main()
