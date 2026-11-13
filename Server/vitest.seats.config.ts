import { defineConfig } from "vitest/config";

/**
 * The suites that take storage seats offline (test/seats): real dsm-app-host accounts on a
 * developer's own storage set on this machine, whose storage_node processes the suite stops and
 * starts. They need what the suites on DSM need (test/real-dsm/hosts.ts) and DSM_STORAGE_NODE_BIN
 * (test/real-dsm/seats.ts). Never run beside another suite on the same set.
 */
export default defineConfig({
  test: {
    include: ["test/seats/*.test.ts"],
    fileParallelism: false,
    sequence: { concurrent: false },
    testTimeout: 1_800_000,
    hookTimeout: 1_800_000,
  },
});
