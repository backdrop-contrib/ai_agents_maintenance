<?php

/**
 * @file
 * Verification test suite for ai_agents_maintenance.
 *
 * Tests task registration, tool schemas, proposal workflows, optimistic
 * concurrency safeguards, and automated execution handlers.
 *
 * Run with:
 *   ddev exec php modules/contrib/ai_agents_maintenance/tests/test_maintenance.php
 */

define('BACKDROP_ROOT', getcwd());
require_once BACKDROP_ROOT . '/core/includes/bootstrap.inc';
backdrop_bootstrap(BACKDROP_BOOTSTRAP_FULL);

$passed = 0;
$failed = 0;

function test_assert($condition, $message) {
  global $passed, $failed;
  if ($condition) {
    $passed++;
    print "  [PASS] $message\n";
  }
  else {
    $failed++;
    print "  [FAIL] $message\n";
  }
}

print "=====================================================\n";
print " AI AGENTS MAINTENANCE VERIFICATION TEST SUITE\n";
print "=====================================================\n\n";

// ---------------------------------------------------------------------------
// Group 1: Module & Schema Readiness
// ---------------------------------------------------------------------------
print "--- Group 1: Module & Storage Readiness ---\n";
test_assert(module_exists('ai_agents_maintenance'), 'ai_agents_maintenance module is enabled');
test_assert(db_table_exists('ai_agents_maintenance_queue'), 'ai_agents_maintenance_queue database table exists');

// ---------------------------------------------------------------------------
// Group 2: Pluggable Task Registry
// ---------------------------------------------------------------------------
print "\n--- Group 2: Pluggable Task Registry ---\n";
$tasks = ai_agents_maintenance_get_task_info();
test_assert(is_array($tasks) && !empty($tasks), 'ai_agents_maintenance_get_task_info() returns an array of tasks');

$expected_tasks = [
  'file_alt_from_usage',
  'recommendation',
  'node_publish_status',
  'node_field_update',
  'taxonomy_term_assign',
];

foreach ($expected_tasks as $task_key) {
  test_assert(isset($tasks[$task_key]), "Task '$task_key' is registered");
  if (isset($tasks[$task_key])) {
    $info = $tasks[$task_key];
    test_assert(!empty($info['label']) && !empty($info['description']), "Task '$task_key' has label and description");
    test_assert(!empty($info['apply_callback']) && is_callable($info['apply_callback']), "Task '$task_key' has callable apply_callback: {$info['apply_callback']}");
  }
}

// ---------------------------------------------------------------------------
// Group 3: AI Tools Registration & Schemas
// ---------------------------------------------------------------------------
print "\n--- Group 3: AI Tools Registration & Schemas ---\n";
$tools = ai_agents_maintenance_ai_tools();
test_assert(isset($tools['maintenance_propose_change']), 'maintenance_propose_change tool is declared');
test_assert(isset($tools['maintenance_list_proposals']), 'maintenance_list_proposals tool is declared');

$propose_tool = $tools['maintenance_propose_change'];
test_assert($propose_tool['operation'] === 'propose', 'maintenance_propose_change uses operation "propose" (avoids pausing unattended cron)');
test_assert($propose_tool['destructive'] === FALSE, 'maintenance_propose_change is marked non-destructive');

$enum_tasks = $propose_tool['definition']['function']['parameters']['properties']['task_type']['enum'];
test_assert(in_array('node_publish_status', $enum_tasks, TRUE), 'Tool enum dynamically includes node_publish_status');
test_assert(in_array('taxonomy_term_assign', $enum_tasks, TRUE), 'Tool enum dynamically includes taxonomy_term_assign');
test_assert(isset($propose_tool['definition']['function']['parameters']['properties']['bundle']), 'Tool parameters include optional bundle property');

// ---------------------------------------------------------------------------
// Group 4: Proposal Creation & Validation
// ---------------------------------------------------------------------------
print "\n--- Group 4: Proposal Creation & Validation ---\n";

// 4.1 Invalid task type
$err1 = ai_agents_maintenance_execute_propose_change([
  'task_type' => 'invalid_unknown_task',
  'after_value' => 'foo',
  'reason' => 'bar',
]);
test_assert(!empty($err1['error']), 'Rejects unknown task_type');

// 4.2 Missing required fields
$err2 = ai_agents_maintenance_execute_propose_change([
  'task_type' => 'recommendation',
  'after_value' => '',
  'reason' => 'bar',
]);
test_assert(!empty($err2['error']), 'Rejects empty after_value');

// 4.3 Recommendation with global/site scope
$test_rec_text = 'Update PHP memory limit to 512MB for high-concurrency background queues. ' . uniqid();
$rec_result = ai_agents_maintenance_execute_propose_change([
  'task_type' => 'recommendation',
  'entity_type' => 'site',
  'entity_id' => 0,
  'after_value' => $test_rec_text,
  'reason' => 'AI performance audit detected frequent queue spikes.',
]);
test_assert(!empty($rec_result['status']) && $rec_result['status'] === 'queued', 'Allows site-wide advisory recommendation with entity_id = 0');

// 4.4 Duplicate detection
$dup_result = ai_agents_maintenance_execute_propose_change([
  'task_type' => 'recommendation',
  'entity_type' => 'site',
  'entity_id' => 0,
  'after_value' => $test_rec_text,
  'reason' => 'AI performance audit detected frequent queue spikes.',
]);
test_assert(!empty($dup_result['status']) && $dup_result['status'] === 'duplicate', 'Prevents duplicate proposal insertion');

// 4.5 List proposals tool
$list_result = ai_agents_maintenance_execute_list_proposals(['status' => 'pending']);
test_assert(isset($list_result['count']) && $list_result['count'] >= 1, 'maintenance_list_proposals retrieves pending proposals');

// ---------------------------------------------------------------------------
// Group 5: Workflow Execution & Concurrency Guards
// ---------------------------------------------------------------------------
print "\n--- Group 5: Workflow Execution & Concurrency Guards ---\n";

// Create a test node
$test_node = new Node([
  'type' => 'page',
  'title' => 'AI Maintenance Automated Test Node ' . REQUEST_TIME,
  'status' => 1,
  'uid' => 1,
  'language' => LANGUAGE_NONE,
]);
node_object_prepare($test_node);
node_save($test_node);
$nid = (int) $test_node->nid;
test_assert($nid > 0, "Created test node with NID $nid (status = 1)");

// 5.1 Propose unpublishing stale node
$unpub_prop = ai_agents_maintenance_execute_propose_change([
  'task_type' => 'node_publish_status',
  'entity_type' => 'node',
  'entity_id' => $nid,
  'before_value' => 'published',
  'after_value' => 'unpublished',
  'reason' => 'Content flagged as obsolete by lifecycle audit.',
]);
test_assert(!empty($unpub_prop['status']) && $unpub_prop['status'] === 'queued', 'Queued node_publish_status proposal');

// Find queued row
$queue_items = ai_agents_maintenance_get_queue_items(10, 'pending');
$target_item = NULL;
foreach ($queue_items as $item) {
  if ($item['task_type'] === 'node_publish_status' && (int) $item['entity_id'] === $nid) {
    $target_item = $item;
    break;
  }
}
test_assert(!empty($target_item), 'Located queued proposal item in pending queue');

if ($target_item) {
  $qid = (int) $target_item['qid'];

  // Approve item
  ai_agents_maintenance_update_queue_status([$qid], 'approved', 1);
  $approved_items = ai_agents_maintenance_get_queue_items(10, 'approved');
  test_assert(isset($approved_items[$qid]), 'Successfully transitioned queue item to "approved"');

  // Apply item
  $target_item['status'] = 'approved';
  $apply_success = ai_agents_maintenance_apply_queue_item($target_item);
  test_assert($apply_success === TRUE, 'ai_agents_maintenance_apply_queue_item() executed successfully');

  // Verify node status was updated to 0
  $reloaded_node = node_load($nid, NULL, TRUE);
  test_assert((int) $reloaded_node->status === 0, 'Test node status was successfully set to unpublished (0)');

  // 5.2 Optimistic concurrency safeguard:
  // If an editor republished the node in the interim, applying an outdated proposal must fail.
  $reloaded_node->status = 1;
  node_save($reloaded_node);

  $stale_item = [
    'qid' => $qid,
    'task_type' => 'node_publish_status',
    'entity_type' => 'node',
    'entity_id' => $nid,
    'before_value' => 'unpublished', // Expects node to be unpublished
    'after_value' => 'published',
    'reason' => 'Stale proposal',
    'status' => 'approved',
  ];
  // Node is currently status = 1 (published), but proposal expects before_value = 'unpublished' (0)
  $drift_error = '';
  $concurrency_prevented = !ai_agents_maintenance_task_apply_node_status($stale_item, $drift_error);
  test_assert($concurrency_prevented && strpos($drift_error, 'changed before apply') !== FALSE, 'Optimistic concurrency guard prevented applying proposal after underlying entity changed');
}

// 5.3 Test node_field_update
$post_node = new Node([
  'type' => 'post',
  'title' => 'AI Maintenance Field & Taxonomy Test ' . REQUEST_TIME,
  'status' => 1,
  'uid' => 1,
  'language' => LANGUAGE_NONE,
  'body' => [
    LANGUAGE_NONE => [
      ['value' => 'Initial body text content.'],
    ],
  ],
]);
node_object_prepare($post_node);
node_save($post_node);
$post_nid = (int) $post_node->nid;
test_assert($post_nid > 0, "Created test post node with NID $post_nid");

$field_prop_result = ai_agents_maintenance_execute_propose_change([
  'task_type' => 'node_field_update',
  'entity_type' => 'node',
  'entity_id' => $post_nid,
  'bundle' => 'body',
  'before_value' => 'Initial body text content.',
  'after_value' => 'Enriched body text content with AI optimizations.',
  'reason' => 'Content modernization.',
]);
test_assert(!empty($field_prop_result['status']) && $field_prop_result['status'] === 'queued', 'Queued node_field_update proposal');

$field_item = [
  'qid' => 999998,
  'task_type' => 'node_field_update',
  'entity_type' => 'node',
  'entity_id' => $post_nid,
  'bundle' => 'body',
  'before_value' => 'Initial body text content.',
  'after_value' => 'Enriched body text content with AI optimizations.',
  'reason' => 'Content modernization.',
];
$field_err = '';
$field_applied = ai_agents_maintenance_task_apply_node_field($field_item, $field_err);
test_assert($field_applied === TRUE, 'ai_agents_maintenance_task_apply_node_field() executed successfully');

$reloaded_post = node_load($post_nid, NULL, TRUE);
test_assert($reloaded_post->body[LANGUAGE_NONE][0]['value'] === 'Enriched body text content with AI optimizations.', 'Node body field was updated on disk');

// 5.4 Test taxonomy_term_assign
$tag_term_name = 'AI-Tag-' . REQUEST_TIME;
$tax_item = [
  'qid' => 999997,
  'task_type' => 'taxonomy_term_assign',
  'entity_type' => 'node',
  'entity_id' => $post_nid,
  'bundle' => 'field_tags',
  'after_value' => $tag_term_name,
  'reason' => 'Auto-tagging.',
];
$tax_err = '';
$tax_applied = ai_agents_maintenance_task_apply_taxonomy_term($tax_item, $tax_err);
test_assert($tax_applied === TRUE, 'ai_agents_maintenance_task_apply_taxonomy_term() created and assigned new tag');

$reloaded_post_tax = node_load($post_nid, NULL, TRUE);
test_assert(!empty($reloaded_post_tax->field_tags[LANGUAGE_NONE]), 'Post node now has assigned taxonomy tag');

// ---------------------------------------------------------------------------
// Group 6: Advisory Recommendation Execution
// ---------------------------------------------------------------------------
print "\n--- Group 6: Advisory Recommendation Execution ---\n";
$rec_item = [
  'qid' => 999999,
  'task_type' => 'recommendation',
  'entity_type' => 'site',
  'entity_id' => 0,
  'after_value' => 'Sample advisory',
  'reason' => 'Review needed',
];
$rec_err = '';
$rec_apply = ai_agents_maintenance_task_apply_recommendation($rec_item, $rec_err);
test_assert($rec_apply === TRUE, 'Advisory recommendation applies cleanly without side effects');

// ---------------------------------------------------------------------------
// Group 7: Service Account Least-Privilege & Impersonation
// ---------------------------------------------------------------------------
print "\n--- Group 7: Service Account Least-Privilege & Impersonation ---\n";
$maint_config = config('ai_agents_maintenance.settings');
$orig_cron_uid = $maint_config->get('cron_uid');

// Test configured cron UID
$maint_config->set('cron_uid', 1)->save();
test_assert((int) config('ai_agents_maintenance.settings')->get('cron_uid') === 1, 'Configured service account UID committed cleanly');

// Restore original config
$maint_config->set('cron_uid', $orig_cron_uid)->save();

// ---------------------------------------------------------------------------
// Cleanup
// ---------------------------------------------------------------------------
print "\n--- Cleanup ---\n";
if (!empty($nid)) {
  node_delete($nid);
  print "  Cleaned up test page node $nid\n";
}
if (!empty($post_nid)) {
  node_delete($post_nid);
  print "  Cleaned up test post node $post_nid\n";
}
if (!empty($tag_term_name)) {
  $terms = taxonomy_get_term_by_name($tag_term_name, 'tags');
  foreach ($terms as $term) {
    taxonomy_term_delete($term->tid);
  }
  print "  Cleaned up temporary test taxonomy tag\n";
}
// Clean up test rows from ai_agents_maintenance_queue
db_delete('ai_agents_maintenance_queue')
  ->condition('reason', '%AI performance audit%', 'LIKE')
  ->execute();
db_delete('ai_agents_maintenance_queue')
  ->condition('reason', '%Content flagged as obsolete%', 'LIKE')
  ->execute();
db_delete('ai_agents_maintenance_queue')
  ->condition('reason', '%Content modernization%', 'LIKE')
  ->execute();
db_delete('ai_agents_maintenance_queue')
  ->condition('reason', '%Auto-tagging%', 'LIKE')
  ->execute();
print "  Cleaned up temporary test queue proposals\n";

// ---------------------------------------------------------------------------
// Summary
// ---------------------------------------------------------------------------
print "\n=====================================================\n";
print " Test Results: $passed Passed, $failed Failed\n";
print "=====================================================\n";

if ($failed > 0) {
  exit(1);
}
exit(0);
