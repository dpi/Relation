<?php

function relation_manage_relationship($field_name) {
  return $field_name;
}

function relation_manage() {
  $page = '';
  
  $header = array(
    t('Relationship Field'),
		'',
  );
  $rows = array();
  
  $entity = entity_get_info('relation');
  
  $fields = field_read_fields(array('type' => 'relation'));
  
  foreach ($fields as $field_name => $field) {
    $field_info = field_info_field($field_name);
    if (isset($entity['bundles'][$field_name])) {
      $rows[$field_name] = array(
        'data' => array(
          $field_name,
          l(t('edit'), $entity['bundles'][$field_name]['admin']['real path']),
        ),
      );
    
      $field_bundles = array();
      foreach ($field_info['bundles'] as $bundle_entity => $bundle) {
        foreach ($bundle as $bundle_name) {
          $field_bundles[] = $bundle_entity . ':' . $bundle_name;
        }
      }
      $rows[$field_name . '_bundles'] = array(
        'data' => array(
          array('data' => t('Attached to bundles: ') . implode(', ', $field_bundles), 'colspan' => 2),
        ),
      );
    }
  }

  $page['relation_table'] = array(
    '#theme' => 'table',
    '#header' => $header,
    '#rows' => $rows,
    '#empty' => t('No content types available. <a href="@link">Add content type</a>.', array('@link' => url('admin/structure/types/add'))),
  );
  
  return $page;
}