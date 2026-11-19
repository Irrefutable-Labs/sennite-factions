import { defineConfig } from "vitest/config";

/**
 * The check that runs the game itself with a storage seat taken offline (test/seats/game). It needs
 * what the suites that take seats offline need (vitest.seats.config.ts) and Unreal Engine on this
 * machine. Tools/check_building_paused.sh runs it.
 */
export default defineConfig({
  test: {
    include: ["test/seats/game/*.test.ts"],
    fileParallelism: false,
    sequence: { concurrent: false },
    testTimeout: 1_800_000,
    hookTimeout: 1_800_000,
  },
});
