import { defineConfig } from "vitest/config";

/**
 * The suites on DSM (test/real-dsm): dsm-app-host accounts on a storage set,
 * one suite at a time, each a single sequence of steps on the same accounts.
 * See test/real-dsm/hosts.ts for what they need.
 */
export default defineConfig({
  test: {
    include: ["test/real-dsm/**/*.test.ts"],
    fileParallelism: false,
    sequence: { concurrent: false },
    testTimeout: 1_800_000,
    hookTimeout: 1_800_000,
  },
});
