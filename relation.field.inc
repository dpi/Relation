<?php

/**
 * @file
 * A field storing arbitrary relations between entities.
 */

/**
 * Implements hook_field_info().
 */
function relation_field_info() {
  return array(
    'relation' => array(
      'label' => t('Relation'),
      'description' => t('Stores relationships between entities.'),
      'settings' => array(),
      'default_widget' => 'relation_default',
      'default_formatter' => 'relation_default',
    ),
  );
}

/**
 * Implements hook_field_is_empty().
 */
function relation_field_is_empty($item, $field) {
  return empty($item['other_entity_id']);
}

/**
 * Checks if an entity contains a relation.
 * 
 * @param $entity
 *   Search an entity for a relationship.
 * @param  $relation_id
 *   The relation ID to look for.
 * @param  $field_name
 *   The field name of a Relation -type field.
 * 
 * @return
 *   BOOLEAN
 */
function _relation_entity_has_relation($entity, $relation_id, $field_name) {
  foreach ($entity->{$field_name} as $lang => $items) {
    foreach ($items as $delta => $item) {
      if ($item['relation_id'] == $relation_id) {
        return true;
      }
    }
  }
  
  return false;
}

/**
 * Remove a relationship from an Entity.
 * 
 * @param $entity
 *   An entity to remove a relation from. It is recommended that $entity 
 *   be obtained straight from the database by calling entity_load_unchanged().
 *   This ensures that the data has not gone through any processing, especially
 *   by Form and Field API.
 * @param $relation_id
 *   The Relation ID.
 * @param $field_name
 *   The field name of the Relation field.
 */
function _relation_delete_relation($entity_type, $entity, $relation_id, $field_name) {
  foreach ($entity->{$field_name} as $lc => &$items) {
    foreach ($items as $delta => &$item) {
      if ($item['relation_id'] == $relation_id) {
        unset($items[$delta]);
      }
    }
  }
  
  // @todo: Figure out how to get around recursion issues.
  $entity->relation_recursion = true;
  entity_save($entity_type, $entity);
}

/**
 * Implements hook_field_presave().
 * 
 * We don't act apon the field data being submitted, instead we are reading
 * the fields that are submitted, and creating a mirror opposite of the relationship
 * on Relation field of entities that are submitted here.
 * 
 * hook_field_presave() is executed once for every language supported by $entity.
 * 
 * Creation/deletion of Relation Entities are done here.
 */
function relation_field_presave($entity_type, $entity, $field, $instance, $langcode, &$items) {
  if ($field['type'] != 'relation') {
    return;
  }
  
  $field_name = $field['field_name'];
  list($id, $vid, $bundle) = entity_extract_ids($entity_type, $entity);

  if (isset($entity->relation_recursion)) {
    return;
  }
    
  // Track the current relationships. Used to diff with old relationships later.
  $rid_new = array();
  
  /**
   * Add relationship to opposite Entities.
   */
  foreach ($items as &$item) {
    // Relation is new
    if (empty($item['relation_id'])) {
      $item['relation_id'] = db_next_id();
    }
    
    $rid_new[$item['relation_id']] = array(
    	'entity_type' => $item['other_entity_type'],
    	'etid' => $item['other_entity_id'],
    );
    
    $other = entity_load_unchanged($item['other_entity_type'], $item['other_entity_id']);
    $other_langcode = field_language($item['other_entity_type'], $other, $field_name);
    
    if (false === _relation_entity_has_relation($other, $item['relation_id'], $field_name)) {
      $other->{$field_name}[$other_langcode][] = array( 
        'relation_id' => $item['relation_id'],
        'other_entity_type' => $entity_type,
        'other_entity_id' => $id,
        'other_revision_id' => NULL,
      );
      
      $other->relation_recursion = true;
      $result = entity_save($item['other_entity_type'], $other);
      unset($other->relation_recursion);
    }
  }
  
  /**
   * Users are allowed to recycle each relation on an entity, due to the attached fields.
   * But if the Relation ID is no longer present, then we need to delete relation entities with it.
   */
  
  // Get old relationships
  $entity_db = entity_load_unchanged($entity_type, $id);
  if (isset($entity_db->{$field_name}[$langcode])) {
    foreach ($entity_db->{$field_name}[$langcode] as $db_delta => &$db_item) {
      // Conditions for deleting opposite relationship:
      $delete = false;
      
      // Delete relationships that no longer exist.
      if (isset($rid_new[$db_item['relation_id']])) {
        // Entity type has changed.
        if ($rid_new[$db_item['relation_id']]['entity_type'] != $db_item['other_entity_type']) { $delete = true; }
        // Entity ID has changed.
        if ($rid_new[$db_item['relation_id']]['etid']        != $db_item['other_entity_id'])   { $delete = true; }
      } else {
        // @todo: Delete the Relation entity.
        $delete = true;
      }
      
      // A relationship has changed/removed. Delete relationship of other Entity.
      if ($delete) {
        $entity_rm = entity_load_unchanged($db_item['other_entity_type'], $db_item['other_entity_id']);
        _relation_delete_relation($db_item['other_entity_type'], $entity_rm, $db_item['relation_id'], $field_name);
      }
    }
  }
}

/**
 * Implements hook_field_delete().
 *
 * This hook is invoked when an entity is deleted. All entities that are
 * referencing the deleted entity need to be updated to remove the relation(s).
 */
function relation_field_delete($entity_type, $entity, $field, $instance, $langcode, &$items) {
  list($entity_id) = entity_extract_ids($entity_type, $entity);
  $result = db_query('SELECT relation_id FROM {relation_data} WHERE entity_type = :entity_type AND entity_id = :entity_id', array(
    ':entity_type' => $entity_type,
    ':entity_id' => $entity_id,
  ));
  foreach ($result as $row) {
    db_delete('relation')->condition('relation_id', $row->relation_id)->execute();
    db_delete('relation_data')->condition('relation_id', $row->relation_id)->execute();
  }
}

/**
 * Implements hook_field_instance_delete().
 *
 * This hook is invoked after a field instance has been marked for deletion.
 * This means that an entire relation endpoint has been removed, so all
 * relations to entities of the field instance need to be deleted.
 */
function relation_field_instance_delete($instance) {
  // @todo
}

/**
 * Implements hook_field_widget_info().
 */
function relation_field_widget_info() {
  return array(
    'relation_default' => array(
      'label' => t('Relation selector'),
      'field types' => array('relation'),
    ),
  );
}

/**
 * Implements hook_field_widget_form().
 */
function relation_field_widget_form(&$form, &$form_state, $field, $instance, $langcode, $items, $delta, $element) {
  if (!isset($items[$delta])) {
    $items[$delta] = array(
      'relation_id' => NULL,
      'other_entity_type' => '',
      'other_entity_id' => '',
    );
  }

  $element['#type'] = 'fieldset';
  $element['relation_id'] = array(
    '#type' => 'value',
    '#value' => $items[$delta]['relation_id'],
  );
  $element['other_entity_type'] = array(
    '#type' => 'select',
    '#title' => t('Entity type'),
    '#options' => drupal_map_assoc(array_keys($field['bundles'])),
    '#default_value' => $items[$delta]['other_entity_type'],
  );
  $element['other_entity_id'] = array(
    '#title' => t('Entity ID'),
    '#type' => 'textfield',
    '#default_value' => $items[$delta]['other_entity_id'],
  );
  return $element;
}

/**
 * Implements hook_field_formatter_info().
 */
function relation_field_formatter_info() {
  return array(
    'relation_default' => array(
      'label' => t('Default'),
      'field types' => array('relation'),
    ),
  );
}

/**
 * Implements hook_field_formatter_view().
 */
function relation_field_formatter_view($entity_type, $entity, $field, $instance, $langcode, $items, $display) {
  $links = array();
  foreach ($items as $item) {
    $class = $item['other_entity_type'] . '-' . $item['other_entity_id'];
    $uri = entity_uri($item['other_entity_type'], $item['entity']);
    $links[$class] = array(
      'title' => entity_label($item['other_entity_type'], $item['entity']),
      'href' => $uri['path'],
    ) + $uri;
  }
  if ($links) {
    $element[0] = array(
      '#theme' => 'links',
      '#links' => $links,
    );
    return $element;
  }
}

/**
 * Implements hook_field_formatter_prepare_view().
 */
function relation_field_formatter_prepare_view($entity_type, $entities, $field, $instances, $langcode, &$items, $displays) {
  $entities_to_load = array();
  foreach ($items as $key => $item) {
    foreach ($item as $delta => $value) {
      $entities_to_load[$value['other_entity_type']][] = $value['other_entity_id'];
      $lookup[$value['other_entity_type']][$value['other_entity_id']][] = array($key, $delta);
    }
  }
  foreach ($entities_to_load as $entity_type => $ids) {
    $entities = entity_load($entity_type, $ids);
    foreach ($entities as $entity_id => $entity) {
      foreach ($lookup[$entity_type][$entity_id] as $data) {
        $items[$data[0]][$data[1]]['entity'] = $entity;
      }
    }
  }
}
