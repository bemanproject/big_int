// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
// SPDX-License-Identifier: BSL-1.0

// Mirror the canonical example sources into the Antora "example" family.
//
// The examples/ directory lives at the repository root, outside the docs/
// content source (start_path: docs), so Antora cannot reach it directly and a
// symlink into it escapes the content-source root. Instead we copy the .cpp
// files into modules/ROOT/examples/ (gitignored) before each build; Antora
// reads them from the worktree. Run via `npm run build`.

const fs = require("node:fs");
const path = require("node:path");

const srcDir = path.join(__dirname, "..", "examples");
const destDir = path.join(__dirname, "modules", "ROOT", "examples");

fs.mkdirSync(destDir, { recursive: true });
for (const name of fs.readdirSync(srcDir)) {
    if (name.endsWith(".cpp")) {
        fs.copyFileSync(path.join(srcDir, name), path.join(destDir, name));
    }
}
