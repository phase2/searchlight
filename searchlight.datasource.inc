<?php

class SearchlightDatasource {
  var $id;
  var $name;
  var $base_table;
  var $fields;
  var $filters;
  var $options;

  var $view;

  function __construct() {
    $this->id = '';
    $this->name = '';
    $this->base_table = '';
    $this->fields = array();
    $this->filters = array();
    $this->options = array();
  }

  function setId() {
    global $db_url;
    $url = is_array($db_url) ? $db_url['default'] : $db_url;
    $url = parse_url($url);
    $this->id = trim(urldecode($url['path']), '/') .'_'. $this->base_table;
    return $this;
  }

  function init() {
    return empty($this->id) ? $this->setId()->viewInit()->viewSetHandlers() : $this;
  }

  function viewInit() {
    views_include('view');
    $this->view = new view;
    $this->view->base_table = $this->base_table;
    $this->view->api_version = 3.0-alpha1;
    $this->view->new_display('default', 'Default', 'default');
    $this->view->set_display('default');
    return $this;
  }

  function viewSetHandlers() {
    $handler = $this->view->display_handler;
    $handler->override_option('arguments', array());
    $handler->override_option('filters', array());
    $handler->override_option('relationships', array());
    $handler->override_option('sorts', array());

    $fields = array();
    foreach ($this->getFields() as $name => $field) {
      $fields[$name] = array(
        'id' => $field['name'],
        'table' => $field['table'],
        'field' => $field['field'],
        'relationship' => 'none'
      );
    }
    $handler->override_option('fields', $fields);
    return $this;
  }

  /**
   * Given a Views handler return one of the datasource datatypes based on the
   * database column type and other metadata. May be one of the following
   * datasource attribute types:
   *
   *   `text`:      varchar or text column.
   *   `int`:       serial or int column.
   *   `float`:     float column.
   *   `timestamp`: unix timestamp.
   */
  function getDatatype($handler, $table, $field) {
    $attribute = array();

    // Ensure that pseudo table names are traced back to their literal tables
    // that actually have a schema definition.
    $data = views_fetch_data();
    if (isset($data[$table]['table']['join'])) {
      foreach ($data[$table]['table']['join'] as $join_info) {
        if (isset($join_info['table']) && $join_info['table'] !== $table) {
          $table = $join_info['table'];
          break;
        }
      }
    }

    $schema = drupal_get_schema($table);
    if ($schema && isset($field, $schema['fields'][$field])) {
      $class = get_class($handler);

      // Get the datasource attribute type.
      // We use the handler class for special cases like timestamp where DB
      // column type is not enough information to determine the usage of the
      // field.
      $map = array(
        'serial'  => 'int',
        'int'     => 'int',
        'varchar' => 'text',
        'text'    => 'text',
        'float'   => 'float',
      );
      if (isset($map[$schema['fields'][$field]['type']])) {
        $column_type = $map[$schema['fields'][$field]['type']];
        if ($column_type === 'int' && strpos($class, 'date') !== FALSE) {
          return 'timestamp';
        }
        return isset($map[$column_type]) ? $map[$column_type] : FALSE;
      }
    }
    return FALSE;
  }

  /**
   * Retrieve fields currently enabled for this datasource.
   */
  function getFields() {
    $fields = array();
    foreach ($this->fields as $name => $field) {
      if ($field['usage'] != 'multivalue') {
        $fields[$name] = $field;
      }
    }
    return $fields;
  }

  /**
   * Retrieve multivalue fields currently enabled for this datasource.
   */
  function getMultivalues() {
    $fields = array();
    foreach ($this->fields as $name => $field) {
      if ($field['usage'] == 'multivalue') {
        $fields[$name] = $field;
      }
    }
    return $fields;
  }

  /**
   * Retrieve all possible multivalue fields for this base table.
   */
  function buildMultivalues() {
    $multivalues = array();
    foreach (searchlight_get_views_by_display($this->base_table, 'searchlight_multivalue') as $view) {
      if ($view->name !== 'searchlight_node_access') {
        foreach ($view->display as $display) {
          if ($display->display_plugin === 'searchlight_multivalue') {
            $view = views_get_view($view->name);
            $view->set_display($display->id);
            $multivalue = $view->execute_display();
            $multivalues[$multivalue['name']] = $multivalue;
            $view->destroy();
          }
        }
      }
    }
    return $multivalues;
  }

  /**
   * Build and retrieve node_access multivalue information.
   */
  function buildNodeaccess() {
    views_invalidate_cache();
    $nodeaccess = array();
    if ($view = views_get_view('searchlight_node_access')) {
      foreach ($view->display as $display) {
        if ($display->display_plugin === 'searchlight_multivalue') {
          $view = views_get_view($view->name);
          $view->set_display($display->id);
          $multivalue = $view->execute_display();
          $nodeaccess[$multivalue['name']] = $multivalue;
          $view->destroy();
        }
      }
    }
    return $nodeaccess;
  }

  /**
   * Provide the default form for setting options.
   */
  function optionsForm(&$form, &$form_state) {
    views_include('admin');
    views_include('form');

    $form['name'] = array(
      '#title' => t('Name'),
      '#type' => 'item',
      '#value' => $this->name,
    );

    $base_tables = views_fetch_base_tables();
    $form['base_table'] = array(
      '#title' => t('Base table'),
      '#type' => 'item',
      '#value' => isset($base_tables[$this->base_table]['title']) ? $base_tables[$this->base_table]['title'] : $this->base_table,
    );

    $form['help'] = array(
      '#type' => 'item',
      '#value' => t('Choose a usage type for each field in the search datasource. <strong>Content</strong> fields will be used to perform text searches. <strong>Attributes</strong> can be used to filter, sort or group the search results.')
    );

    $form['fields'] = array(
      '#prefix' => "<div id='datasource-fields'>",
      '#suffix' => "</div>",
      '#tree' => TRUE,
      '#theme' => 'searchlight_admin_datasource_fields',
      '#title' => t('Fields'),
    );

    // Add fields
    $field_options = array();
    $fields = views_fetch_fields($this->base_table, 'field', TRUE);
    foreach ($fields as $field_id => $info) {
      $field_options[$info['group']][$field_id] = $info['title'];
    }
    $form['fields']['new']['#tree'] = TRUE;
    $form['fields']['new']['field'] = array(
      '#type' => 'select',
      '#options' => $field_options,
    );
    $form['fields']['new']['add'] = array(
      '#value' => t('Add field'),
      '#type' => 'submit',
      '#submit' => array('searchlight_admin_datasource_edit_submit'),
      '#ahah' => array(
        'path' => 'admin/settings/search/datasource/ahah/edit-fields-new-add/fields',
        'wrapper' => 'datasource-fields',
        'method' => 'replace',
      ),
    );

    // Remove fields
    if (count($this->getFields())) {
      $form['fields']['remove'] = array(
        '#value' => t('Remove selected fields'),
        '#type' => 'submit',
        '#submit' => array('searchlight_admin_datasource_edit_submit'),
        '#ahah' => array(
          'path' => 'admin/settings/search/datasource/ahah/edit-fields-remove/fields',
          'wrapper' => 'datasource-fields',
          'method' => 'replace',
        ),
      );
    }

    // Adjust existing fields
    $form['fields']['fields'] = array('#tree' => TRUE);
    foreach ($this->getFields() as $name => $info) {
      $form['fields']['fields'][$name] = array();

      // Remove field checkbox.
      $form['fields']['fields'][$name]['remove'] = array(
        '#type' => 'checkbox',
        '#default_value' => FALSE,
      );

      // Field label.
      $form['fields']['fields'][$name]['label'] = array(
        '#type' => 'markup',
        '#value' => $info['label'],
      );

      // Datatype
      $form['fields']['fields'][$name]['datatype'] = array(
        '#type' => 'markup',
        '#value' => "<code>{$info['datatype']}</code>",
      );

      // Usage
      $default_usage = ($info['datatype'] === 'text') ? 'content' : 'attribute';
      $default_usage = isset($this->fields[$name]['usage']) ? $this->fields[$name]['usage'] : $default_usage;
      $form['fields']['fields'][$name]['usage'] = array(
        '#type' => 'select',
        '#options' => array( 'content' => t('Content'), 'attribute' => t('Attribute')),
        '#default_value' => $default_usage,
      );
    }

    // @TODO: Handle multivalues.
    $form['multivalues'] = array(
      '#tree' => TRUE,
      '#theme' => 'searchlight_admin_datasource_fields',
      '#title' => t('Multivalues'),
      '#prefix' => "<div id='datasource-multivalues'>",
      '#suffix' => "</div>",
    );

    // Add fields
    $multivalue_options = array();
    foreach ($this->buildMultivalues() as $name => $info) {
      if (!array_key_exists($name, $this->getMultivalues())) {
        $multivalue_options[$name] = $info['label'];
      }
    }
    $form['multivalues']['new']['#tree'] = TRUE;
    $form['multivalues']['new']['field'] = array(
      '#type' => 'select',
      '#options' => $multivalue_options,
    );
    $form['multivalues']['new']['add'] = array(
      '#value' => t('Add multivalue'),
      '#type' => 'submit',
      '#submit' => array('searchlight_admin_datasource_edit_submit'),
      '#ahah' => array(
        'path' => 'admin/settings/search/datasource/ahah/edit-multivalues-new-add/multivalues',
        'wrapper' => 'datasource-multivalues',
        'method' => 'replace',
      ),
    );

    // Remove fields
    if (count($this->getMultivalues())) {
      $form['multivalues']['remove'] = array(
        '#value' => t('Remove selected multivalues'),
        '#type' => 'submit',
        '#submit' => array('searchlight_admin_datasource_edit_submit'),
        '#ahah' => array(
          'path' => 'admin/settings/search/datasource/ahah/edit-multivalues-remove/multivalues',
          'wrapper' => 'datasource-multivalues',
          'method' => 'replace',
        ),
      );
    }

    foreach ($this->getMultivalues() as $name => $info) {
      $form['multivalues']['fields'][$name] = array();

      // Remove field checkbox.
      $form['multivalues']['fields'][$name]['remove'] = array(
        '#type' => 'checkbox',
        '#default_value' => FALSE,
      );

      // Field label.
      $form['multivalues']['fields'][$name]['label'] = array(
        '#type' => 'markup',
        '#value' => $info['label'],
      );

      // Datatype
      $form['multivalues']['fields'][$name]['datatype'] = array(
        '#type' => 'markup',
        '#value' => "<code>{$info['datatype']}</code>",
      );

      $form['multivalues']['fields'][$name]['usage'] = array(
        '#type' => 'markup',
        '#value' => t('Multivalue'),
      );
    }

    $form['options']['#tree'] = TRUE;
    // In reality, these are multivalue fields that are generated
    // programmatically.
    if ($this->base_table === 'node') {
      $form['options']['node_access'] = array(
        '#title' => t('Node access'),
        '#description' => t('Include node access information in datasource'),
        '#type' => 'checkbox',
        '#default_value' => $this->options['node_access'] ? $this->options['node_access'] : TRUE,
      );
    }
    return $form;
  }

  /**
   * Perform any necessary changes to the form values prior to storage.
   * There is no need for this function to actually store the data.
   */
  function optionsSubmit($form, &$form_state) {
    switch ($form_state['clicked_button']['#id']) {
      case 'edit-fields-new-add':
        list($table, $field) = explode('.', $form_state['values']['fields']['new']['field']);
        $this->addField($table, $field);
        break;
      case 'edit-fields-remove':
        foreach ($form_state['values']['fields']['fields'] as $name => $values) {
          if (!empty($values['remove'])) {
            $this->removeField(NULL, NULL, $name);
          }
        }
        break;
      case 'edit-multivalues-new-add':
        $this->addMultivalue($form_state['values']['multivalues']['new']['field']);
        break;
      case 'edit-multivalues-remove':
        foreach ($form_state['values']['multivalues']['fields'] as $name => $values) {
          if (!empty($values['remove'])) {
            $this->removeMultivalue($name);
          }
        }
        break;
      case 'edit-save':
        // Save additional metadata from fields, multivalues.
        foreach (array('fields', 'multivalues') as $key) {
          if (!empty($form_state['values'][$key]['fields'])) {
            foreach ($form_state['values'][$key]['fields'] as $name => $values) {
              $values = array_diff_key($values, array('remove' => NULL));
              if (isset($this->{$key}[$name])) {
                $this->{$key}[$name] = array_merge($this->{$key}[$name], $values);
              }
            }
          }
        }
        // Save options.
        $this->options = $form_state['values']['options'];
        break;
    }
  }

  function addField($table, $field) {
    // Add the field to the view and build. This will give us an inited handler
    // with full aliases.
    $this->viewInit()->viewSetHandlers();
    $fields = $this->view->display_handler->get_option('fields');
    $fields[$field] = array(
      'id' => $field,
      'table' => $table,
      'field' => $field,
      'relationship' => 'none',
    );
    $this->view->display_handler->set_option('fields', $fields);
    $this->view->build();

    // Retrieve field information for storage with datasource.
    $handler = $this->view->field[$field];
    if ($handler->real_field !== $handler->view->base_field) {
      $this->fields[$handler->field_alias] = array(
        'label' => $handler->ui_name() . " ({$handler->real_field})",
        'datatype' => $this->getDatatype($handler, $handler->table, $handler->real_field),
        'table' => $handler->table,
        'field' => $handler->real_field,
        'name' => $handler->field_alias,
      );
    }
    if (!empty($handler->additional_fields)) {
      foreach ($handler->additional_fields as $identifier => $info) {
        $table = $handler->table;
        $field = $info;
        // If the additional fields have specified table/field in an array,
        // use these.
        if (is_array($info) && isset($info['field'])) {
          $table = isset($info['table']) ? $info['table'] : $table;
          $field = $info['field'];
        }
        if ($field !== $handler->view->base_field || $table !== $handler->view->base_table) {
          $this->fields[$handler->aliases[$field]] = array(
            'label' => $handler->ui_name() . " ({$field})",
            'datatype' => $this->getDatatype($handler, $table, $field),
            'table' => $table,
            'field' => $field,
            'name' => $handler->aliases[$field],
          );
        }
      }
    }
    return $this->viewInit()->viewSetHandlers();
  }

  function removeField($table = NULL, $field = NULL, $name = NULL) {
    if (isset($name)) {
      if (array_key_exists($name, $this->fields)) {
        unset($this->fields[$name]);
      }
    }
    else if (isset($table, $field)) {
      foreach ($this->fields as $name => $field) {
        if ($field['table'] === $table && $field['field'] === $field) {
          unset($this->fields[$name]);
        }
      }
    }
    return $this->viewInit()->viewSetHandlers();
  }

  function addMultivalue($name) {
    $multivalues = $this->buildMultivalues();
    if (isset($multivalues[$name])) {
      $this->fields[$name] = $multivalues[$name];
    }
    return $this;
  }

  function removeMultivalue($name) {
    return $this->removeField(NULL, NULL, $name);
  }
}