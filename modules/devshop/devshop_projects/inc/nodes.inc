<?php
/**
 * @file devshop_project.node.inc
 *
 * DevShop Project Node related hooks and support functions.
 *
 */

/**
 * Implementation of hook_node_info().
 */
function devshop_projects_node_info() {
  $types["project"] =  array(
    "name" => 'DevShop Project',
    "base" => 'devshop_projects',
    "description" => t('A project is a website with multiple environments.'),
    "has_title" => TRUE,
    "title_label" => t('Project Codename'),
    "locked" => TRUE,
  );
  return $types;
}

/**
 * Implements hook_node_load().
 *
 * Loads data into Project nodes.
 */
function devshop_projects_node_load($nodes, $types) {

  // Only act on project and related nodes.
  $types = array(
    'project',
    'site',
    'platform',
    'task',
  );
  if (count(array_intersect($types, $types)) == 0) {
    return;
  }

  global $user;
  $project = new stdClass();

  $refs_types = db_select('node', 'n')
    ->fields('n', array('nid', 'type'))
    ->condition('type', array('site', 'platform', 'project', 'server'), 'IN')
    ->execute()
    ->fetchAllKeyed();

  foreach ($nodes as $nid => &$node) {

    // Tasks
    if ($node->type == 'task') {

      // Prepare bootstrap-compatible things.
      // Load up task types to get our language.
      $task_types = hosting_available_tasks();

      // In Aegir, "executed" and "delta" does not get updated properly.
      // Handle "delta" being not computed yet
      if ($node->task_status == HOSTING_TASK_PROCESSING) {
        $node->duration = format_interval(time() - $node->executed, 1);
      }
      elseif ($node->task_status == HOSTING_TASK_QUEUED) {
        $node->duration = t('Queued for %time',
          array('%time' => format_interval(time() - $node->changed), 1));
      }
      else {
        $node->duration = format_interval($node->delta, 1);
      }

      // To test out constantly changing tasks, uncomment this line.
      //        $node->task_status = rand(-1, 3);
      $node->status_class = devshop_task_status_class($node->task_status);
      $node->status_name = _hosting_parse_error_code($node->task_status);

      // Override "ago" text.
      $node->ago = format_interval(time() - $node->executed,
          1) . ' ' . t('ago');
      if ($node->task_status == HOSTING_TASK_QUEUED) {
        $node->ago = t('Queued for %time',
          array('%time' => format_interval(time() - $node->changed)));
      }
      elseif ($node->task_status == HOSTING_TASK_PROCESSING) {
        $node->ago = format_interval(time() - $node->changed);
      }

      // ISO 8601
      if ($node->executed == 0) {
        $executed = $node->created;
      }
      else {
        $executed = $node->executed;
      }
      $node->task_timestamp = date('c', $executed);
      $node->task_date = format_date($executed);

      // Set task type
      $node->type_name = $task_types[$refs_types[$node->rid]][$node->task_type]['title'];

      // If platform's first verify...
      if (isset($node->ref) && $node->ref->type == 'platform' && $node->ref->verified == 0) {
        $node->type_name = t('Cloning Codebase');
      }

      // Get Icon
      $node->icon = devshop_task_status_icon($node->task_status);

      // Get desired URL
      $node->url = url("node/{$node->nid}");

    }
    // Projects
    elseif ($node->type == 'project') {

      // Load the "hosting context". The unique name in the aegir system.
      $node->project_name = $node->title;
      $node->hosting_name = 'project_' . $node->title;

      $node->project = devshop_project_load($node);
    }
    elseif ($node->type == 'site') {

      // @TODO: Check if this is ever needed now that install_method is in core aegir.
      // Load install method into an easy to access property.
      if (empty($node->install_method) && isset($node->environment->settings->install_method)) {
        $node->install_method = $node->environment->settings->install_method['method'];
      }

      // If profile ID is empty but name is stored, load to $node->profile_name so that provision will load it.
      if (empty($node->profile) && !empty($node->environment->settings->install_method['profile'])) {
        $node->profile_name = $node->environment->settings->install_method['profile'];
      }
    }
  }
}

/**
 * When install tasks are created, add "force-reinstall" task argument.
 *
 * @param $node
 */
function devshop_projects_node_presave($node) {
  if ($node->type == 'task' && $node->task_type == 'install' && $node->is_new) {
    $node->task_args['force-reinstall'] = 1;
  }
}

/**
 * Implementation of hook_insert().
 *
 *   1. Saves data into our table.
 *   2. Saves a hosting context name.
 *   3. Adds a "Verify" task for this project.
 *
 *  @see hosting_platform_insert()
 */
function devshop_projects_node_insert($node) {

  // On task insert, save last task.
  if ($node->type == 'task') {
    $site = node_load($node->rid);

    // @TODO: task_type should be task_status... but the system still works.  Was this never running? It's possible the other hooks that save last task info is all we need.
    if (($site->type == 'site' || $site->type == 'platform') && isset($site->environment) && ($node->task_type == HOSTING_TASK_QUEUED || $node->task_type == HOSTING_TASK_PROCESSING)) {
      $site->environment->last_task = $node->nid;
      devshop_environment_save_last_task($site->environment);
    }

    // When a task is created or updated that has a project, redirect to the project.
    if (isset($_GET['redirect']) && $_GET['redirect'] == 'task-page') {
      drupal_goto("node/{$node->nid}");
    }
    elseif (isset($site->project) && !empty($site->project->nid) && arg(0) == 'hosting_confirm' && arg(1) == $site->nid) {
      drupal_goto("node/{$site->project->nid}");
    }
  }

  if ($node->type != 'project') {
    return;
  }

  if (!isset($node->no_verify)) {
    hosting_add_task($node->nid, 'verify');
  }

  $info = new stdClass();
  $info->nid = $node->nid;
  $info->git_url = $node->project->git_url;
  $info->code_path = hosting_path_normalize($node->project->code_path);
  $info->drupal_path = hosting_path_normalize($node->project->drupal_path);
  $info->base_url = $node->project->base_url;
  $info->install_profile = $node->project->install_profile;

  // Save serialized data, minus environments
  $info->settings = serialize($node->project->settings);

  drupal_write_record('hosting_devshop_project', $info);

  // Save hosting context
  if (!$node->old_vid) {

    // Save the "hosting context", which includes setting the path alias.
    $name = ($node->hosting_name) ? $node->hosting_name : $node->title;

    // Ensure "project_" prefix on hosting context name.
    if (strpos($name, 'project_') !== 0) {
      $context_name = 'project_' . $name;
    }
    else {
      $context_name = $name;
    }

    hosting_context_register($node->nid, $context_name);

    // Replace the alias created by hosting_context_register.
    $path['source'] = "node/{$node->nid}";
    $path['alias'] = "project/{$name}";
    path_save($path);
  }

  // Save Environment records, if present.
  // If using Start a Project wizard, environments is NOT set on project node insert.
  // If using drush hosting-import, environments MIGHT be in the data.
  // Soon CLI and API commands will exist to create all of them at once.
  if (!empty($node->project->environments)) {
    $node->project->nid = $node->nid;
    $node->project->name = $node->title;
    foreach ($node->project->environments as $name => $environment) {
      // If site node does not exist, create it.
      if (empty($environment->site)) {
        $environment = devshop_environment_normalize($environment);
        hosting_create_environment($node->project, $name, $environment->git_ref, NULL, $environment->settings, 'create', $environment->git_url);
      }
    }
  }
}

/**
 * Implementation of hook_update().
 *
 *  1. Updates our table.
 *  2. Adds a "Verify" task for this project.
 *
 */
function devshop_projects_node_update($node) {


  // On task insert, save last task.
  if ($node->type == 'task') {
    $site = node_load($node->rid);

    // When a task is created or updated that has a project, redirect to the project.
    if (isset($site->project) && arg(0) == 'hosting_confirm' && arg(1) == $site->nid) {
      drupal_goto("node/{$site->project->nid}");
    }
  }

  if ($node->type != 'project') {
    return;
  }

  $project = (object) $node->project;
  $project->settings = (object) $project->settings;

  $info = new stdClass();
  $info->nid = $node->nid;
  $info->git_url = $project->git_url;
  $info->code_path = hosting_path_normalize($project->code_path);
  $info->drupal_path = hosting_path_normalize($project->drupal_path);
  $info->base_url = $project->base_url;
  $info->install_profile = $project->install_profile? $project->install_profile: $project->settings->default_environment['install_profile'];

  // Save serialized data, minus environments
  $info->settings = serialize($project->settings);

  // Write project record.
  drupal_write_record('hosting_devshop_project', $info, 'nid');
  if (!isset($node->no_verify) || $node->no_verify == FALSE) {
    hosting_add_task($node->nid, 'verify');
  }

  $project_node = node_load($node->nid, NULL, TRUE);

  // Environment data (without sites) are present during `hosting-import` drush calls.
  if (!empty($node->project->environments)) {
    $node->project->nid = $node->nid;
    $node->project->name = $node->title;
    foreach ($node->project->environments as $name => $environment) {

      // If site node does not exist, create it.
      $environment = devshop_environment_normalize($environment);
      if (empty($project_node->project->environments[$name]->site)) {
        hosting_create_environment($node->project, $name, $environment->git_ref, NULL, $environment->settings, 'create', $environment->git_url);
      }
      // If site does exist, save the environment data.
      else {
        $environment->project_nid = $node->nid;
        devshop_environment_save($environment);
      }
    }
  }
}

/**
 * A hack until we have actual OOP.
 *
 * @param $environment
 */
function devshop_environment_normalize($environment) {
  // Make sure environments are objects and settings are arrays
  // Preserve settings, convert the rest to all objects.
  if (is_array($environment)) {
   $settings = $environment['settings'];
  }
  elseif (is_object($environment)) {
    $settings = $environment->settings;
  }
  else {
    throw new \Exception(t('devshop_environment_normalize() function requires an object or array'));
  }

  $environment = convert_to_object($environment);
  $environment->settings = (array) $settings;
  return $environment;
}

/**
 * Implements hook_node_delete().
 */
function devshop_projects_node_delete($node) {

  // When a platform or site node is FULLY deleted, remove the environment record.
  // This should never happen, site and platform nodes are never fully deleted.
  if (($node->type == 'site' || $node->type == 'platform') && !empty($node->project)) {
    db_delete('hosting_devshop_project_environment')
      ->condition($node->type, $node->nid)
      ->execute();
  }
}

/**
 * Implementation of hook_delete().
 */
function devshop_projects_delete($node) {

  db_delete('hosting_devshop_project')
    ->condition('nid', $node->nid)
    ->execute();

  db_delete('hosting_devshop_project_environment')
  ->condition('project_nid', $node->nid)
  ->execute();

  hosting_context_delete($node->nid);
  hosting_task_delete_related_tasks($node->nid);
}

/**
 * @param $environment
 */
function devshop_environment_save(stdClass &$environment) {
  $environment->settings = (array) $environment->settings;

  // Prepare record for saving
  $info = new stdClass();
  $info->project_nid = $environment->project_nid;
  $info->name = $environment->name;
  $info->site = $environment->site;
  $info->platform = $environment->platform;
  $info->settings = serialize($environment->settings);
  $info->last_task = $environment->last_task;

  // Check for existing records
  $result = db_select('hosting_devshop_project_environment', 'e')
    ->fields('e', array('project_nid'))
    ->condition('e.name', $info->name)
    ->condition('e.project_nid', $info->project_nid)
    ->execute()
    ->fetchField()
  ;

  $keys = $result? array('project_nid', 'name'): array();

  // Save environment record.
  if (drupal_write_record('hosting_devshop_project_environment', $info, $keys)) {
    watchdog('ok', 'Environment record saved: ' . print_r($info, 1));
    return TRUE;
  }
  else {
    watchdog('error', 'Environment record not saved: ' . print_r($info, 1));
    return FALSE;
  }
}

/**
 * Helper to save last task info.
 */
function devshop_environment_save_last_task($environment) {

  db_update('hosting_devshop_project_environment')
    ->fields(array(
      'last_task' => $environment->last_task,
    ))
    ->condition('name', $environment->name)
    ->condition('project_nid', $environment->project_nid)
    ->execute();
}
