#!/usr/bin/env python3
"""Preserve KuroFS v1 inode compatibility after introducing metadata revision."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def replace_once(path: str, old: str, new: str) -> None:
    target = ROOT / path
    text = target.read_text(encoding="utf-8")
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one guarded match, found {count}")
    target.write_text(text.replace(old, new, 1), encoding="utf-8")


def main() -> None:
    replace_once(
        "kernel/fs/kurofs.cpp",
        "    inode.generation = load_u32(bytes + 44U);\n"
        "    inode.revision = load_u32(bytes + 48U);\n"
        "    if (inode.id != expected_id ||\n",
        "    inode.generation = load_u32(bytes + 44U);\n"
        "    inode.revision = load_u32(bytes + 48U);\n"
        "    // Pre-revision KuroFS v1 images left this reserved field zero. Treat\n"
        "    // that encoding as the initial metadata revision so existing v1\n"
        "    // volumes remain mountable and upgrade naturally on the next write.\n"
        "    if (inode.revision == 0U) inode.revision = 1U;\n"
        "    if (inode.id != expected_id ||\n",
    )

    test = "tests/test_kurofs_data.cpp"
    replace_once(
        test,
        "bool expect(bool condition, const char* message) {\n"
        "    if (!condition) std::fprintf(stderr, \"FAIL: %s\\n\", message);\n"
        "    return condition;\n"
        "}\n",
        "uint32_t test_crc32(const uint8_t* data, size_t size) {\n"
        "    uint32_t crc = UINT32_C(0xffffffff);\n"
        "    for (size_t index = 0U; index < size; ++index) {\n"
        "        crc ^= static_cast<uint32_t>(data[index]);\n"
        "        for (uint32_t bit = 0U; bit < 8U; ++bit) {\n"
        "            const uint32_t mask = static_cast<uint32_t>(\n"
        "                -static_cast<int32_t>(crc & UINT32_C(1)));\n"
        "            crc = (crc >> 1U) ^ (UINT32_C(0xedb88320) & mask);\n"
        "        }\n"
        "    }\n"
        "    return ~crc;\n"
        "}\n\n"
        "void test_store_u32(uint8_t* bytes, uint32_t value) {\n"
        "    for (size_t index = 0U; index < 4U; ++index) {\n"
        "        bytes[index] = static_cast<uint8_t>((value >> (index * 8U)) & UINT32_C(0xff));\n"
        "    }\n"
        "}\n\n"
        "uint32_t test_load_u32(const uint8_t* bytes) {\n"
        "    return static_cast<uint32_t>(bytes[0]) |\n"
        "        (static_cast<uint32_t>(bytes[1]) << 8U) |\n"
        "        (static_cast<uint32_t>(bytes[2]) << 16U) |\n"
        "        (static_cast<uint32_t>(bytes[3]) << 24U);\n"
        "}\n\n"
        "bool expect(bool condition, const char* message) {\n"
        "    if (!condition) std::fprintf(stderr, \"FAIL: %s\\n\", message);\n"
        "    return condition;\n"
        "}\n",
    )

    replace_once(
        test,
        "    if (!expect(write_extent_data(&remounted, extent, 2U, 1000U, patch, sizeof(patch)) == Status::NoSpace,\n"
        "                \"reject extent overflow\")) return 1;\n\n"
        "    Inode stale_read = persisted;\n",
        "    if (!expect(write_extent_data(&remounted, extent, 2U, 1000U, patch, sizeof(patch)) == Status::NoSpace,\n"
        "                \"reject extent overflow\")) return 1;\n\n"
        "    // Simulate an older KuroFS v1 inode whose formerly reserved revision\n"
        "    // field is zero while preserving a valid inode checksum.\n"
        "    const uint64_t root_byte = remounted.geometry.inode_table_start * SECTOR_SIZE;\n"
        "    uint8_t* const legacy_root = storage_bytes + root_byte;\n"
        "    test_store_u32(legacy_root + 48U, 0U);\n"
        "    test_store_u32(legacy_root + INODE_SIZE - sizeof(uint32_t),\n"
        "                   test_crc32(legacy_root, INODE_SIZE - sizeof(uint32_t)));\n"
        "    FileSystem legacy_mount{};\n"
        "    if (!expect(mount(&legacy_mount, &device) == Status::Ok, \"mount legacy v1 revision-zero inode\")) return 1;\n"
        "    Inode legacy_root_inode{};\n"
        "    if (!expect(read_inode(&legacy_mount, ROOT_INODE, &legacy_root_inode) == Status::Ok &&\n"
        "                legacy_root_inode.revision == 1U, \"normalize legacy revision\")) return 1;\n"
        "    if (!expect(update_inode(&legacy_mount, &legacy_root_inode) == Status::Ok &&\n"
        "                legacy_root_inode.revision == 2U, \"upgrade legacy inode on write\")) return 1;\n"
        "    if (!expect(test_load_u32(legacy_root + 48U) == 2U, \"persist upgraded revision encoding\")) return 1;\n\n"
        "    Inode stale_read = persisted;\n",
    )


if __name__ == "__main__":
    main()
