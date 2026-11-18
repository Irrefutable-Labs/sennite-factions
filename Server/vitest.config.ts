import { defineConfig } from "vitest/config";

/** The rules on their own (the faction's, the swap's): no account, no storage node. */
export default defineConfig({
  test: {
    include: ["test/faction/**/*.test.ts", "test/publisher/**/*.test.ts"],
  },
});
