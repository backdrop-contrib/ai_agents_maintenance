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
  $items = ai_agents_maintenance_get_queue_items(100);
  $options = [];
  $rows = [];

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
    '#empty' => t('No maintenance candidates queued.'),
  ];

  $form['actions'] = ['#type' => 'actions'];
  $form['actions']['approve'] = [
    '#type' => 'submit',
    '#value' => t('Approve selected'),
    '#op' => 'approve',
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
  if (!empty($item['entity_type']) && $item['entity_type'] === 'file') {
    $file = file_load((int) $item['entity_id']);
    if ($file) {
      return l(check_plain($file->filename), 'file/' . $file->fid . '/manage');
    }
  }

  return check_plain($item['entity_type'] . ':' . $item['entity_id']);
}

/**
 * Format queue status label.
 */
function _ai_agents_maintenance_queue_status_label($status) {
  return check_plain(ucfirst((string) $status));
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

  $form['assistants'] = [
    '#type' => 'fieldset',
    '#title' => t('Scheduled assistants'),
    '#description' => t('Each assistant runs on cron and can use tools to write proposals into the maintenance queue. Configure assistants at <a href="@url">admin/config/ai/assistants</a>.', ['@url' => url('admin/config/ai/assistants')]),
    '#tree' => TRUE,
  ];

  if (!module_exists('ai_assistant')) {
    $form['assistants']['missing'] = [
      '#markup' => '<p>' . t('<strong>ai_assistant</strong> module is not enabled.') . '</p>',
    ];
  }
  else {
    $assistant_options = ['' => t('— Select assistant —')] + ai_assistant_options();
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
