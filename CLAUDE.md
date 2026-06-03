# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repo Overview

NX monorepo for DIY ESP32 and Arduino firmware projects. Projects live under `apps/` (firmware/executables) and `libs/` (shared C/C++ libraries). NX is used for task orchestration only — the actual build tooling per project will typically be PlatformIO or Arduino CLI.

## Package Manager

Always use `pnpm` and `pnpx` / `pnpm dlx`. Never use `npm` or `npx`.

## Common Commands

```bash
# Install dependencies
pnpm install

# Run a target for all projects
pnpm nx run-many -t build
pnpm nx run-many -t test

# Run a target for a specific project
pnpm nx run <project>:build
pnpm nx run <project>:flash

# Run only affected projects (relative to master)
pnpm nx affected -t build

# Show all registered projects
pnpm nx show projects
```

## Adding a New Project

1. Create the project directory under `apps/<name>/` or `libs/<name>/`.
2. Add a `project.json` to register it with NX and define targets (`build`, `flash`, `test`, etc.).
3. For PlatformIO projects, targets typically wrap `pio run`, `pio run -t upload`, and `pio test`.

Example `project.json` for a PlatformIO app:

```json
{
  "name": "my-sensor",
  "targets": {
    "build": { "executor": "nx:run-commands", "options": { "command": "pio run", "cwd": "apps/my-sensor" } },
    "flash": { "executor": "nx:run-commands", "options": { "command": "pio run -t upload", "cwd": "apps/my-sensor" } },
    "test":  { "executor": "nx:run-commands", "options": { "command": "pio test", "cwd": "apps/my-sensor" } }
  }
}
```

## NX Guidelines

<!-- nx configuration start-->
<!-- Leave the start & end comments to automatically receive updates. -->

- For navigating/exploring the workspace, invoke the `nx-workspace` skill first - it has patterns for querying projects, targets, and dependencies
- When running tasks (for example build, lint, test, e2e, etc.), always prefer running the task through `nx` (i.e. `nx run`, `nx run-many`, `nx affected`) instead of using the underlying tooling directly
- Prefix nx commands with `pnpm` (e.g., `pnpm nx build`, `pnpm nx test`) - avoids using globally installed CLI
- You have access to the Nx MCP server and its tools, use them to help the user
- For Nx plugin best practices, check `node_modules/@nx/<plugin>/PLUGIN.md`. Not all plugins have this file - proceed without it if unavailable.
- NEVER guess CLI flags - always check nx_docs or `--help` first when unsure

### Scaffolding & Generators

- For scaffolding tasks (creating apps, libs, project structure, setup), ALWAYS invoke the `nx-generate` skill FIRST before exploring or calling MCP tools

### When to use nx_docs

- USE for: advanced config options, unfamiliar flags, migration guides, plugin configuration, edge cases
- DON'T USE for: basic generator syntax (`nx g @nx/react:app`), standard commands, things you already know
- The `nx-generate` skill handles generator discovery internally - don't call nx_docs just to look up generator syntax

<!-- nx configuration end-->
