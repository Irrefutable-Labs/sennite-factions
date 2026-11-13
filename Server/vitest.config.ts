import { defineConfig } from "vitest/config";

/** The faction rules on their own: no account, no storage node. */
export default defineConfig({
  test: {
    include: ["test/faction/**/*.test.ts"],
  },
});
