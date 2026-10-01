<?php


/**
 * @file
 * Admin forms for AI Agent Maintenance.
 */

/**
 * Dashboard form.
 */
function ai_agents_maintenance_dashboard_form($form, &$form_state) {
  $counts = ai_agents_maintenance_queue_counts();
  $recent = ai_agents_maintenance_recent_items(10);

  $form['intro'] = [
    '#markup' => '<p>' . t('Cron scans for small cleanup tasks, queues proposals, and applies only approved items.') . '</p>',
  ];

  $form['counts'] = [
    '#theme' => 'table',
    '#header' => [t('Status'), t('Count')],
    '#rows' => [
      [t('Pending'), isset($counts['pending']) ? $counts['pending'] : 0],
      [t('Approved'), isset($counts['approved']) ? $counts['approved'] : 0],
      [t('Applied'), isset($counts['applied']) ? $counts['applied'] : 0],
      [t('Rejected'), isset($counts['rejected']) ? $counts['rejected'] : 0],
      [t('Failed'), isset($counts['failed']) ? $counts['failed'] : 0],
    ],
  ];

  $form['actions'] = ['#type' => 'actions'];
  $form['actions']['scan_now'] = [
    '#type' => 'submit',
    '#value' => t('Scan now'),
    '#op' => 'scan',
  ];
  $form['actions']['apply_now'] = [
    '#type' => 'submit',
    '#value' => t('Apply approved now'),
    '#op' => 'apply',
  ];

  $rows = [];
  foreach ($recent as $item) {
    $rows[] = [
      check_plain($item['status']),
      check_plain($item['task_type']),
      _ai_agents_maintenance_item_target_label($item),
      check_plain($item['after_value']),
      check_plain($item['reason']),
      format_date($item['created'], 'short'),
    ];
  }

  $form['recent'] = [
    '#theme' => 'table',
    '#header' => [t('Status'), t('Task'), t('Target'), t('Proposed change'), t('Reason'), t('Created')],
    '#rows' => $rows,
    '#empty' => t('No queued maintenance items yet.'),
  ];

  return $form;
}

/**
 * Dashboard submit.
 */
function ai_agents_maintenance_dashboard_form_submit($form, &$form_state) {
  $op = !empty($form_state['triggering_element']['#op']) ? $form_state['triggering_element']['#op'] : '';
  $config = config('ai_agents_maintenance.settings');
  $scan_limit = (int) ($config->get('scan_batch_size') ?: 10);
  $apply_limit = (int) ($config->get('apply_batch_size') ?: 10);

  if ($op === 'scan') {
    $result = ai_agents_maintenance_scan_alt_candidates($scan_limit);
    backdrop_set_message(t('Queued @queued candidate(s).', ['@queued' => !empty($result['queued']) ? $result['queued'] : 0]));
  }
  elseif ($op === 'apply') {
    $applied = ai_agents_maintenance_apply_approved_items($apply_limit);
    backdrop_set_message(t('Applied @count approved item(s).', ['@count' => $applied]));
  }

  $form_state['redirect'] = 'admin/config/ai/ai-agents/maintenance';
}

/**
 * Queue review form.
 */
function ai_agents_maintenance_queue_form($form, &$form_state) {
  $filter = isset($_GET['status']) ? trim($_GET['status']) : 'active';
  $status_options = [
    'active' => t('Active (Pending & Approved)'),
    'all' => t('All statuses'),
    'pending' => t('Pending'),
    'approved' => t('Approved'),
    'applied' => t('Applied'),
    'rejected' => t('Rejected'),
    'failed' => t('Failed'),
  ];

  $form['filter'] = [
    '#type' => 'fieldset',
    '#title' => t('Filter queue'),
    '#collapsible' => TRUE,
    '#collapsed' => ($filter === 'active'),
  ];
  $form['filter']['status'] = [
    '#type' => 'select',
    '#title' => t('Status'),
    '#options' => $status_options,
    '#default_value' => $filter,
  ];
  $form['filter']['filter_submit'] = [
    '#type' => 'submit',
    '#value' => t('Filter'),
    '#op' => 'filter',
  ];

  $query_status = '';
  if ($filter === 'active') {
    $query_status = ['pending', 'approved'];
  }
  elseif ($filter !== 'all' && isset($status_options[$filter])) {
    $query_status = $filter;
  }

  $items = ai_agents_maintenance_get_queue_items(100, $query_status);
  $options = [];

  foreach ($items as $item) {
    $qid = (int) $item['qid'];
    $options[$qid] = [
      _ai_agents_maintenance_queue_status_label($item['status']),
      check_plain($item['task_type']),
      _ai_agents_maintenance_item_target_label($item),
      check_plain($item['before_value']),
      check_plain($item['after_value']),
      check_plain($item['reason']),
      format_date($item['created'], 'short'),
    ];
  }

  $form['queue'] = [
    '#type' => 'tableselect',
    '#header' => [t('Status'), t('Task'), t('Target'), t('Before'), t('After'), t('Reason'), t('Created')],
    '#options' => $options,
    '#empty' => t('No maintenance candidates queued matching criteria.'),
  ];

  $form['actions'] = ['#type' => 'actions'];
  $form['actions']['approve'] = [
    '#type' => 'submit',
    '#value' => t('Approve selected'),
    '#op' => 'approve',
  ];
  $form['actions']['approve_apply'] = [
    '#type' => 'submit',
    '#value' => t('Approve & apply immediately'),
    '#op' => 'approve_apply',
  ];
  $form['actions']['reject'] = [
    '#type' => 'submit',
    '#value' => t('Reject selected'),
    '#op' => 'reject',
  ];
  $form['actions']['apply'] = [
    '#type' => 'submit',
    '#value' => t('Apply selected approved'),
    '#op' => 'apply',
  ];

  return $form;
}

/**
 * Queue submit.
 */
function ai_agents_maintenance_queue_form_submit($form, &$form_state) {
  $op = !empty($form_state['triggering_element']['#op']) ? $form_state['triggering_element']['#op'] : '';

  if ($op === 'filter') {
    $status_val = $form_state['values']['status'];
    $form_state['redirect'] = [
      'admin/config/ai/ai-agents/maintenance/queue',
      ['query' => ['status' => $status_val]],
    ];
    return;
  }

  $selected = [];
  if (!empty($form_state['values']['queue']) && is_array($form_state['values']['queue'])) {
    foreach ($form_state['values']['queue'] as $qid => $checked) {
      if ($checked) {
        $selected[] = (int) $qid;
      }
    }
  }

  if (empty($selected)) {
    backdrop_set_message(t('No items selected.'), 'warning');
    $form_state['redirect'] = 'admin/config/ai/ai-agents/maintenance/queue';
    return;
  }

  if ($op === 'approve') {
    ai_agents_maintenance_update_queue_status($selected, 'approved', $GLOBALS['user']->uid);
    backdrop_set_message(t('Approved @count item(s).', ['@count' => count($selected)]));
  }
  elseif ($op === 'approve_apply') {
    ai_agents_maintenance_update_queue_status($selected, 'approved', $GLOBALS['user']->uid);
    $applied = 0;
    foreach (ai_agents_maintenance_get_queue_items(100) as $item) {
      if (in_array((int) $item['qid'], $selected, TRUE)) {
        if (ai_agents_maintenance_apply_queue_item($item)) {
          $applied++;
        }
      }
    }
    backdrop_set_message(t('Approved and applied @count item(s).', ['@count' => $applied]));
  }
  elseif ($op === 'reject') {
    ai_agents_maintenance_update_queue_status($selected, 'rejected');
    backdrop_set_message(t('Rejected @count item(s).', ['@count' => count($selected)]));
  }
  elseif ($op === 'apply') {
    $applied = 0;
    foreach (ai_agents_maintenance_get_queue_items(100) as $item) {
      if (in_array((int) $item['qid'], $selected, TRUE) && $item['status'] === 'approved') {
        if (ai_agents_maintenance_apply_queue_item($item)) {
          $applied++;
        }
      }
    }
    backdrop_set_message(t('Applied @count approved item(s).', ['@count' => $applied]));
  }

  $form_state['redirect'] = 'admin/config/ai/ai-agents/maintenance/queue';
}

/**
 * Read-only log form.
 */
function ai_agents_maintenance_log_form($form, &$form_state) {
  $items = ai_agents_maintenance_get_queue_items(50);
  $rows = [];

  foreach ($items as $item) {
    $rows[] = [
      check_plain($item['status']),
      check_plain($item['task_type']),
      _ai_agents_maintenance_item_target_label($item),
      check_plain($item['before_value']),
      check_plain($item['after_value']),
      check_plain($item['error_message']),
      format_date($item['updated'], 'short'),
    ];
  }

  $form['log'] = [
    '#theme' => 'table',
    '#header' => [t('Status'), t('Task'), t('Target'), t('Before'), t('After'), t('Error'), t('Updated')],
    '#rows' => $rows,
    '#empty' => t('No maintenance log entries yet.'),
  ];

  return $form;
}

/**
 * Format queue target label.
 */
function _ai_agents_maintenance_item_target_label(array $item) {
  $entity_type = !empty($item['entity_type']) ? $item['entity_type'] : '';
  $entity_id = !empty($item['entity_id']) ? (int) $item['entity_id'] : 0;

  if ($entity_type === 'file' && $entity_id) {
    $file = file_load($entity_id);
    if ($file) {
      return l(check_plain($file->filename), 'file/' . $file->fid . '/manage');
    }
  }
  elseif ($entity_type === 'node' && $entity_id) {
    $node = node_load($entity_id);
    if ($node) {
      return l(check_plain($node->title), 'node/' . $node->nid);
    }
  }
  elseif ($entity_type === 'taxonomy_term' && $entity_id) {
    $term = taxonomy_term_load($entity_id);
    if ($term) {
      return l(check_plain($term->name), 'taxonomy/term/' . $term->tid);
    }
  }
  elseif ($entity_type === 'user' && $entity_id) {
    $u = user_load($entity_id);
    if ($u) {
      return l(check_plain($u->name), 'user/' . $u->uid);
    }
  }
  elseif ($entity_type === 'site' || empty($entity_type)) {
    return '<em>' . t('Site / Global') . '</em>';
  }

  return check_plain(($entity_type ?: 'unknown') . ':' . $entity_id);
}

/**
 * Format queue status label.
 */
function _ai_agents_maintenance_queue_status_label($status) {
  $status = (string) $status;
  $colors = [
    'pending' => '#d97706',   // amber
    'approved' => '#2563eb',  // blue
    'applied' => '#059669',   // green
    'rejected' => '#6b7280',  // gray
    'failed' => '#dc2626',    // red
  ];
  $color = isset($colors[$status]) ? $colors[$status] : '#4b5563';
  return '<span style="display:inline-block;padding:2px 8px;border-radius:3px;background:' . $color . ';color:#fff;font-weight:600;font-size:0.85em;text-transform:uppercase;letter-spacing:0.04em">'
    . check_plain(ucfirst($status)) . '</span>';
}

/**
 * Settings form.
 */
function ai_agents_maintenance_settings_form($form, &$form_state) {
  $config = config('ai_agents_maintenance.settings');

  $form['cron'] = [
    '#type' => 'fieldset',
    '#title' => t('Cron behavior'),
  ];
  $form['cron']['cron_enabled'] = [
    '#type' => 'checkbox',
    '#title' => t('Enable automatic cron scanning'),
    '#description' => t('When disabled, assistants only run manually via the dashboard.'),
    '#default_value' => $config->get('cron_enabled') !== NULL ? $config->get('cron_enabled') : 1,
  ];
  $form['cron']['cron_interval'] = [
    '#type' => 'select',
    '#title' => t('Minimum cron interval'),
    '#options' => [
      3600   => t('Hourly'),
      21600  => t('Every 6 hours'),
      86400  => t('Daily'),
      604800 => t('Weekly'),
    ],
    '#default_value' => $config->get('cron_interval') ?: 86400,
  ];
  $form['cron']['apply_batch_size'] = [
    '#type' => 'select',
    '#title' => t('Approved items to apply per cron run'),
    '#options' => [1 => 1, 5 => 5, 10 => 10, 25 => 25],
    '#default_value' => $config->get('apply_batch_size') ?: 5,
  ];

  $cron_uid = (int) $config->get('cron_uid');
  if (empty($cron_uid) || $cron_uid === 1) {
    $form['cron']['security_advisory'] = [
      '#type' => 'help',
      '#markup' => '<div class="messages warning" style="margin: 0.5em 0 1em 0;">'
        . '<strong>' . t('Security advisory: Principle of Least Privilege') . '</strong><br />'
        . t('Unattended cron runs currently default to User 1 (superuser), granting unrestricted access to all site content and administrative configuration. To harden site security against accidental drift or unintended edits, select or create a dedicated service account holding only the permissions required for maintenance.')
        . '</div>',
    ];
  }

  $user_options = [
    0 => t('— Default (User 1: Superuser) —'),
  ];
  $accounts = db_query('SELECT uid, name FROM {users} WHERE status = 1 AND uid > 0 ORDER BY uid ASC LIMIT 50')->fetchAllKeyed(0, 1);
  foreach ($accounts as $uid => $name) {
    $u = user_load($uid);
    $is_super = ($uid == 1);
    $has_maint = user_access('administer ai maintenance', $u);
    $label = $is_super ? t('User 1: @name (Superuser)', ['@name' => $name]) : t('UID @uid: @name (@perm)', [
      '@uid' => $uid,
      '@name' => $name,
      '@perm' => $has_maint ? t('Maintenance Admin') : t('Standard User'),
    ]);
    $user_options[$uid] = $label;
  }

  $form['cron']['cron_uid_select'] = [
    '#type' => 'select',
    '#title' => t('Cron service account'),
    '#options' => $user_options,
    '#default_value' => isset($user_options[$cron_uid]) ? $cron_uid : 0,
    '#description' => t('Select an active account with maintenance permissions, or specify a custom UID below.'),
  ];
  $form['cron']['cron_uid'] = [
    '#type' => 'textfield',
    '#title' => t('Custom service account UID'),
    '#description' => t('Specify a custom UID to impersonate. Overrides dropdown selection if entered.'),
    '#default_value' => $cron_uid ? $cron_uid : '',
    '#size' => 10,
    '#element_validate' => ['element_validate_integer_positive'],
  ];

  $tasks = ai_agents_maintenance_get_task_info();
  $task_rows = [];
  foreach ($tasks as $id => $info) {
    $task_rows[] = [
      '<code>' . check_plain($id) . '</code>',
      check_plain($info['label']),
      !empty($info['entity_type']) ? check_plain($info['entity_type']) : '<em>' . t('Global / Site') . '</em>',
      !empty($info['has_automatic_apply']) ? t('Yes (Automated)') : t('No (Advisory)'),
      check_plain($info['description']),
    ];
  }

  $form['registered_tasks'] = [
    '#type' => 'fieldset',
    '#title' => t('Registered maintenance task handlers (@count)', ['@count' => count($tasks)]),
    '#description' => t('Modules register task handlers via <code>hook_ai_agents_maintenance_task_info()</code>. These determine how proposals are validated and applied.'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  ];
  $form['registered_tasks']['table'] = [
    '#theme' => 'table',
    '#header' => [t('Task Type'), t('Label'), t('Entity Target'), t('Auto-Apply'), t('Description')],
    '#rows' => $task_rows,
  ];

  $form['assistants'] = [
    '#type' => 'fieldset',
    '#title' => t('Scheduled assistants'),
    '#description' => t('Each assistant runs on cron and can use the maintenance_propose_change tool (enable it on the backing agents) to write proposals into the maintenance queue. Configure assistants at <a href="@url">admin/config/ai/ai-assistants</a>.', ['@url' => url('admin/config/ai/ai-assistants')]),
    '#tree' => TRUE,
  ];

  if (!module_exists('ai_assistants')) {
    $form['assistants']['missing'] = [
      '#markup' => '<p>' . t('<strong>ai_assistants</strong> module is not enabled.') . '</p>',
    ];
  }
  else {
    $assistant_options = ['' => t('— Select assistant —')] + ai_assistants_options();
    $scheduled = $config->get('scheduled_assistants') ?: [];

    // Always show at least 3 rows, or one more than currently saved.
    $count = max(3, count($scheduled) + 1);
    if (!empty($form_state['assistant_count'])) {
      $count = $form_state['assistant_count'];
    }

    for ($i = 0; $i < $count; $i++) {
      $entry = isset($scheduled[$i]) ? $scheduled[$i] : [];
      $form['assistants'][$i] = [
        '#type' => 'fieldset',
        '#title' => t('Assistant @n', ['@n' => $i + 1]),
        '#collapsible' => TRUE,
        '#collapsed' => empty($entry['assistant_id']),
      ];
      $form['assistants'][$i]['enabled'] = [
        '#type' => 'checkbox',
        '#title' => t('Enabled'),
        '#default_value' => isset($entry['enabled']) ? $entry['enabled'] : 0,
      ];
      $form['assistants'][$i]['assistant_id'] = [
        '#type' => 'select',
        '#title' => t('Assistant'),
        '#options' => $assistant_options,
        '#default_value' => isset($entry['assistant_id']) ? $entry['assistant_id'] : '',
      ];
      $form['assistants'][$i]['prompt'] = [
        '#type' => 'textarea',
        '#title' => t('Trigger prompt'),
        '#description' => t('Sent to the assistant on each cron run. Should instruct it to scan and queue proposals.'),
        '#default_value' => isset($entry['prompt']) ? $entry['prompt'] : '',
        '#rows' => 3,
      ];
    }

    $form['add_another'] = [
      '#type' => 'submit',
      '#value' => t('Add another assistant'),
      '#submit' => ['ai_agents_maintenance_settings_add_another'],
      '#limit_validation_errors' => [],
    ];
  }

  $form['actions'] = ['#type' => 'actions'];
  $form['actions']['submit'] = [
    '#type' => 'submit',
    '#value' => t('Save configuration'),
  ];

  return $form;
}

/**
 * Add another assistant row.
 */
function ai_agents_maintenance_settings_add_another($form, &$form_state) {
  $current = !empty($form_state['assistant_count']) ? $form_state['assistant_count'] : max(3, count($form_state['values']['assistants'] ?? []) + 1);
  $form_state['assistant_count'] = $current + 1;
  $form_state['rebuild'] = TRUE;
}

/**
 * Settings form submit.
 */
function ai_agents_maintenance_settings_form_submit($form, &$form_state) {
  $config = config('ai_agents_maintenance.settings');
  $config->set('cron_enabled', !empty($form_state['values']['cron_enabled']) ? 1 : 0);
  $config->set('cron_interval', (int) $form_state['values']['cron_interval']);
  $config->set('apply_batch_size', (int) $form_state['values']['apply_batch_size']);

  $cron_uid_select = isset($form_state['values']['cron_uid_select']) ? (int) $form_state['values']['cron_uid_select'] : 0;
  $cron_uid_custom = isset($form_state['values']['cron_uid']) ? (int) $form_state['values']['cron_uid'] : 0;
  $cron_uid = $cron_uid_custom > 0 ? $cron_uid_custom : $cron_uid_select;
  $config->set('cron_uid', $cron_uid > 0 ? $cron_uid : 0);

  $scheduled = [];
  if (!empty($form_state['values']['assistants']) && is_array($form_state['values']['assistants'])) {
    foreach ($form_state['values']['assistants'] as $entry) {
      if (!empty($entry['assistant_id'])) {
        $scheduled[] = [
          'assistant_id' => $entry['assistant_id'],
          'enabled' => !empty($entry['enabled']) ? 1 : 0,
          'prompt' => trim($entry['prompt']),
        ];
      }
    }
  }
  $config->set('scheduled_assistants', $scheduled);
  $config->save();

  backdrop_set_message(t('Maintenance settings saved.'));
}
