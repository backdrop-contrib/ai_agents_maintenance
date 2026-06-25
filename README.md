# AI Agents Maintenance

Provides reviewable maintenance tooling for AI agents, including cron-driven
scans, an approval queue, and admin screens for applying small cleanup tasks.

Assistants run unattended on cron, write proposals into a review queue using
the `maintenance_propose_change` tool, and nothing touches site content until
an administrator approves each proposal.

## Requirements

- `ai_agents` module
- `ai_assistants` module (for scheduled assistant runs)
- `ai_tools` module (for the proposal tools)

## Installation

- Install this module using the official
  [Backdrop CMS instructions](https://backdropcms.org/user-guide/modules).

## Configuration

1. Enable the module.
2. Go to **Administration > Configuration > AI > AI Agents > AI Maintenance**.
3. Configure cron behavior and scheduled assistants.
4. On each backing agent used by a scheduled assistant
   (**admin/config/ai/ai-agents**), enable the `maintenance_propose_change`
   and `maintenance_list_proposals` tools so the agent can queue proposals.
5. Review queued items on the **Queue** tab, approve or reject them, and apply
   approved changes (cron also applies a batch of approved items per run).

## AI Agent Tools

| Tool | Purpose |
|---|---|
| `maintenance_propose_change` | Queue a proposal for human review. `file_alt_from_usage` proposals apply automatically after approval; `recommendation` proposals are advisory. Does not pause agent runs for approval — the queue is the approval gate. |
| `maintenance_list_proposals` | List queued proposals (filter by status) to avoid duplicates and report queue state. |

## Issues

Bugs and feature requests should be reported in the
[Issue Queue](https://github.com/backdrop-contrib/ai_agents_maintenance/issues).

## Current Maintainer

[Justin Keiser](https://github.com/keiserjb)

## Credits

- Created for Backdrop CMS by [Justin Keiser](https://github.com/keiserjb).
- Developed with AI assistance.

## License

This project is GPL v2 software. See the LICENSE.txt file in this directory
for complete text.
