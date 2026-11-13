import { defineConfig } from "vitest/config";

/**
 * The checks that run the game itself (test/game): the Unreal game on the test range, against the
 * publisher's server on real dsm-app-host accounts and a storage set, with a real wallet answering.
 * They need what the suites on DSM need (test/real-dsm/hosts.ts) and Unreal Engine on this machine.
 */
export default defineConfig({
  test: {
    include: ["test/game/**/*.test.ts"],
    fileParallelism: false,
    sequence: { concurrent: false },
    testTimeout: 1_800_000,
    hookTimeout: 1_800_000,
  },
});
