#!/usr/bin/env python3
"""Fix terminated thread-slot reuse during an active scheduler run."""

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
        "kernel/task/thread.cpp",
        "    const uint64_t flags = save_and_disable_interrupts();\n"
        "    size_t index = kInvalidSlot;\n"
        "    for (size_t candidate = 0U; candidate < MAX_THREADS; ++candidate) {\n",
        "    const uint64_t flags = save_and_disable_interrupts();\n"
        "    // A Ring-3 parent may spawn/wait many short-lived children while the\n"
        "    // top-level preemptive scheduler remains active.  Those children are\n"
        "    // already fully terminated before the parent issues the next spawn,\n"
        "    // so recycle their scheduler slots here instead of waiting for the\n"
        "    // outer run to finish and eventually exhausting MAX_THREADS.\n"
        "    reap_terminated();\n"
        "    size_t index = kInvalidSlot;\n"
        "    for (size_t candidate = 0U; candidate < MAX_THREADS; ++candidate) {\n",
    )

    replace_once(
        "tests/test_thread.cpp",
        "bool process_retired = false;\n",
        "bool process_retired = false;\n"
        "size_t churn_children_completed = 0U;\n"
        "size_t churn_children_created = 0U;\n"
        "bool churn_failed = false;\n",
    )
    replace_once(
        "tests/test_thread.cpp",
        "void process_retire_probe(void*) {\n"
        "    assert(threading::current_process() == UINT64_C(42));\n"
        "    assert(threading::retire_current_user_frame() == threading::Status::Ok);\n"
        "    process_retired = true;\n"
        "}\n",
        "void process_retire_probe(void*) {\n"
        "    assert(threading::current_process() == UINT64_C(42));\n"
        "    assert(threading::retire_current_user_frame() == threading::Status::Ok);\n"
        "    process_retired = true;\n"
        "}\n\n"
        "void churn_child(void*) { ++churn_children_completed; }\n\n"
        "void churn_parent(void*) {\n"
        "    constexpr size_t iterations = threading::MAX_THREADS * 4U;\n"
        "    for (size_t iteration = 0U; iteration < iterations; ++iteration) {\n"
        "        threading::ThreadId child = threading::INVALID_THREAD_ID;\n"
        "        if (threading::create_for_process(\n"
        "                \"churn-child\", churn_child, nullptr,\n"
        "                UINT64_C(101), 0U, &child) != threading::Status::Ok ||\n"
        "            child == threading::INVALID_THREAD_ID) {\n"
        "            churn_failed = true;\n"
        "            return;\n"
        "        }\n"
        "        ++churn_children_created;\n"
        "        // Give the child a scheduling turn. It returns immediately and\n"
        "        // becomes Terminated before the parent's next create call.\n"
        "        if (threading::yield() != threading::Status::Ok) {\n"
        "            churn_failed = true;\n"
        "            return;\n"
        "        }\n"
        "    }\n"
        "}\n",
    )
    replace_once(
        "tests/test_thread.cpp",
        "    assert(process_result.completed == 1U);\n"
        "    assert(process_retired);\n"
        "    return 0;\n",
        "    assert(process_result.completed == 1U);\n"
        "    assert(process_retired);\n\n"
        "    // Reproduce the Ring-3 spawn/wait pattern: keep one parent alive\n"
        "    // across far more child lifetimes than the scheduler slot capacity.\n"
        "    threading::ThreadId churn = threading::INVALID_THREAD_ID;\n"
        "    assert(threading::create_for_process(\n"
        "               \"churn-parent\", churn_parent, nullptr, UINT64_C(100), 0U,\n"
        "               &churn) == threading::Status::Ok);\n"
        "    threading::RunResult churn_result{};\n"
        "    assert(threading::run_until_idle(\n"
        "               threading::MAX_THREADS * 16U, &churn_result) ==\n"
        "           threading::Status::Ok);\n"
        "    assert(!churn_failed);\n"
        "    assert(churn_children_created == threading::MAX_THREADS * 4U);\n"
        "    assert(churn_children_completed == churn_children_created);\n"
        "    assert(churn_result.completed == churn_children_created + 1U);\n"
        "    return 0;\n",
    )

    print("[dev-apply-thread-churn-reap] applied active-run terminated slot recycling")


if __name__ == "__main__":
    main()
