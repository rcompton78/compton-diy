# Workflow Config

## Repository

- **Git host:** github
- **Remote:** git@github.com:rcompton78/compton-diy.git
- **Base branch:** master
- **Branch prefix:** feature/
- **Tracker:** linear

## Package Manager

- **Package manager:** pnpm

## Jira

- **Cloud ID:** 7af7e2fb-5718-4894-983e-8b4b396a52e8
- **Default project key:** DIY

## Linear

- **Team key:** COM
- **Team ID:** f1e10efe-5164-4d71-a8b2-b16d93f1447f
- **Default label:** compton-diy
- **Area mappings:**
  - cyd-clock: COM / Cyd Clock
  - tamogatchi-plus: COM / Tamogatchi+

## Commands

- **Lint:** `pnpm nx affected -t lint`
- **Build:** `pnpm nx affected -t build`
- **Test:** `pnpm nx affected -t test`

## Open Code Review Gate

- **Enabled:** no — replaced by the revbot GitHub App (self-hosted on the homelab)

## Bot Thread Resolution

- **pr-revbot[bot]:** auto-resolves
