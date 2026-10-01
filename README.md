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
| `maintenance_propose_change` | Queue a proposal for human review. Built-in task types: `file_alt_from_usage`, `recommendation`, `node_publish_status`, `node_field_update`, and `taxonomy_term_assign`. Does not pause agent runs for live approval — the queue serves as the human-in-the-loop approval gate. |
| `maintenance_list_proposals` | List queued proposals (filterable by status: `pending`, `approved`, `applied`, `rejected`, `failed`) to avoid duplicates and report queue state back to agents. |

## Pluggable Task Handlers API

Other modules (such as `ai_content_audit`, `ai_seo_advisor`, or custom site features) can register custom maintenance task types using `hook_ai_agents_maintenance_task_info()`:

```php
/**
 * Implements hook_ai_agents_maintenance_task_info().
 */
function mymodule_ai_agents_maintenance_task_info() {
  return [
    'custom_cleanup_task' => [
      'label' => t('Custom Cleanup Task'),
      'description' => t('Executes automated site hygiene upon admin approval.'),
      'entity_type' => 'node',
      'has_automatic_apply' => TRUE,
      'apply_callback' => 'mymodule_apply_cleanup_task',
    ],
  ];
}

/**
 * Task execution callback.
 */
function mymodule_apply_cleanup_task(array &$item, &$error = '') {
  // Apply changes safely with optimistic concurrency checks.
  // Return TRUE on success, or set $error and return FALSE.
  return TRUE;
}
```

Registered task types are automatically added to the `maintenance_propose_change` tool's schema and displayed in the administrative task matrix at **Admin > Configuration > AI > AI Agents > AI Maintenance > Settings**.

## Security & Service Accounts

Unattended cron processes run in the anonymous session context. To allow scheduled assistants and agents to read site content and propose changes, the module temporarily switches execution context to a configured service account.

To adhere to the **Principle of Least Privilege**, configure a dedicated service account with only the necessary permissions (`administer ai maintenance`, plus required content view/edit permissions) rather than defaulting to User 1 (superuser).

## Automated Testing

To run the verification test suite:

```bash
ddev exec php modules/contrib/ai_agents_maintenance/tests/test_maintenance.php
```

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
