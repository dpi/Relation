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
 * Checks if an entity contains a relation ID.
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
function _relation_entity_has_relation_id($entity, $relation_id, $field_name) {
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
 * Checks if a Entity-type/Entity ID combination exists in an entity.
 * 
 * @param $entity
 *   Search an entity for a relationship.
 * @param $other_entity_type
 *   The type the Entity to look for.
 * @param $other_etid
 *   The ID of the Entity to look for.
 * @param $field_name
 *   The field name of a Relation -type field.
 * @param $langcode
 *   The language to check against.
 * @return
 *   BOOLEAN
 */
function _relation_entity_has_relation($entity, $other_entity_type, $other_etid, $field_name, $langcode) {
  if (isset($entity->{$field_name}[$langcode])) {
    foreach ($entity->{$field_name}[$langcode] as $delta => $item) {
      if ($item['other_entity_type'] == $other_entity_type && $item['other_entity_id'] == $other_etid) {
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
  if (isset($entity->{$field_name})) {
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
  
  $entity_db = entity_load_unchanged($entity_type, $id);
  
  // Track the current relationships. Used to diff with old relationships later.
  $rid_new = array();
  
  /**
   * Add relationship to opposite Entities.
   */
  foreach ($items as $item_delta => &$item) {
    // Relation is new
    
    $other = entity_load_unchanged($item['other_entity_type'], $item['other_entity_id']);
    list( , , $other_bundle) = entity_extract_ids($item['other_entity_type'], $other);
    
    $rid_new[$item['relation_id']] = array(
    	'entity_type' => $item['other_entity_type'],
    	'etid' => $item['other_entity_id'],
    );

    $item['other_bundle'] = $other_bundle;

    // Prevent duplicate relationships.
    if (empty($item['relation_id'])) {
      if (!_relation_entity_has_relation($entity_db, $item['other_entity_type'], $item['other_entity_id'], $field_name, $langcode)) {
        // Lazy add new relation entity - move to entity_create.
        $record = array('field_name' => $field['field_name']);
        drupal_write_record('relation', $record);
        $item['relation_id'] = $record['relation_id'];
      } else {
        unset($items[$item_delta]);
        continue;
      }
    }
    
    $other_langcode = $langcode;
    
    if (false === _relation_entity_has_relation_id($other, $item['relation_id'], $field_name)) {
      $other->{$field_name}[$other_langcode][] = array( 
        'relation_id' => $item['relation_id'],
        'other_entity_type' => $entity_type,
        'other_bundle' => $bundle,
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
  // All languages need to be checked. 
  
  foreach ($entity_db->{$field_name} as $db_lc => &$db_items) {
    foreach ($db_items as $db_delta => &$db_item) {
      // Conditions for deleting opposite relationship:
      $delete = false;
      
      // Delete relationships that no longer exist.
      if (true === _relation_entity_has_relation_id($entity, $db_item['relation_id'], $field_name)) {
        if (isset($rid_new[$db_item['relation_id']])) {
          // Entity type has changed.
          if ($rid_new[$db_item['relation_id']]['entity_type'] != $db_item['other_entity_type']) { $delete = true; }
          // Entity ID has changed.
          if ($rid_new[$db_item['relation_id']]['etid']        != $db_item['other_entity_id'])   { $delete = true; }
        }
      } else {
        $delete = true; // Deleted entirely
        // @todo: Delete the Relation entity.
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
 * This hook is invoked when an invidiual Entity is deleted. All entities that are
 * referencing the deleted entity need to be updated to remove the relation(s).
 */
function relation_field_delete($entity_type, $entity, $field, $instance, $langcode, &$items) {
  $field_name = $field['field_name'];
  // Delete all references to this item, from other Entities.
  // Entities this Entity is referencing will be deleted by Field API.
  foreach ($items as &$item) {
    $entity_db = entity_load_unchanged($item['other_entity_type'], $item['other_entity_id']);
    _relation_delete_relation($item['other_entity_type'], $entity_db, $item['relation_id'], $field_name);
  }
}

/**
 * Implements hook_field_delete_instance().
 *
 * This hook is invoked after a field instance has been marked for deletion.
 * This means that an entire relation endpoint has been removed, so all
 * relations to entities of the field instance need to be deleted.
 */
function relation_field_delete_instance($instance) {
  $field_name = $instance['field_name'];
  $field = field_info_field($field_name);

  if ($field['type'] != 'relation') {
    return;
  }

  // Get all entities that relate back to this instance
  $other_entities_query = new EntityFieldQuery;

  $other_types = $other_entities_query
  ->fieldCondition($field_name, 'other_entity_type', $instance['entity_type'], '=')
  ->fieldCondition($field_name, 'other_bundle', $instance['bundle'], '=')
  ->execute();

  foreach ($other_types as $other_entity_type => $other_entities) {
    $other_entities = entity_load($other_entity_type, array_keys($other_entities));
    foreach ($other_entities as $other_etid => $other_entity) {
      // $etid is checked in edge cases where there is a zero ID in database.
      if ($other_etid && isset($other_entity->{$field_name})) {
        foreach ($other_entity->{$field_name} as $lang => &$items) {
          foreach ($items as $delta => &$item) {
            if ($item['other_entity_type'] == $instance['entity_type'] && $item['other_bundle'] == $instance['bundle']) {
              unset($items[$delta]);
            }
          }
        }
        entity_save($other_entity_type, $other_entity);
      }
    }
  }
}

/**
 * Implements hook_field_validate().
 */
function relation_field_validate($entity_type, $entity, $field, $instance, $langcode, $items, &$errors) {
  foreach ($items as $delta => $item) {
    // NOTE: $items passes all values, even if they are empty. 
    //       It appears is_empty is not executed before field_validate..
    if (relation_field_is_empty($item, $field)) {
      continue;
    }

    $others = entity_load($item['other_entity_type'], array($item['other_entity_id']));

    // Check that $other entity exists.
    if (!isset($others[$item['other_entity_id']]) || empty($others)) {
      $errors[$field['field_name']][$langcode][$delta][] = array(
        'error' => 'relation_error_missing_other_entity',
        'message' => t('The %type entity %id does not exist.', array(
          '%type' => $item['other_entity_type'],
          '%id' => $item['other_entity_id'])
        ),
      );
      continue;
    }

    $other = $others[$item['other_entity_id']];

    // Check that $other's bundle has the field attached.
    if (!isset($other->{$field['field_name']})) {
      $errors[$field['field_name']][$langcode][$delta][] = array(
        'error' => 'relation_error_missing_field',
        'message' => t('The %bundle bundle does not have a %field_name field attached.', array(
          '%bundle' => $other->type,
          '%field_name' =>  $field['field_name'])
        ),
      );
      continue;
    }

    // Check that number of fields on $other is < cardinality.
    if ($field['cardinality'] != -1 && isset($other->{$field['field_name']}[$langcode]) && count($other->{$field['field_name']}[$langcode]) >= $field['cardinality']) {
      // Check and see if one of the fields on $other is refering back to $entity.
      $exists = FALSE;
      foreach ($other->{$field['field_name']}[$langcode] as $other_value) {
        if ($other_value['other_entity_id'] == $item['other_entity_id'] && $other_value['other_entity_type'] == $item['other_entity_type']) {
          $exists = TRUE;
          break;
        }
      }
      if ($exists) {
        // Change $instance->cardinality to overcome this message.
        // Or set to Unlimited.
        $errors[$field['field_name']][$langcode][$delta][] = array(
          'error' => 'relation_error_cardinality_max',
          'message' => t('The %type entity %id cannot accept any more `%label` relationship.', array(
            '%type' => $item['other_entity_type'],
            '%id' => $item['other_entity_id'],
            // Label for the instance of this Relation on this entity, not $other
            '%label' => $instance['label'],
          )),
        );
        continue;
      }
    }
  }
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
      'other_bundle' => '',
      'other_entity_id' => '',
    );
  }

  $element['#type'] = 'fieldset';
  $element['relation_id'] = array(
    '#type' => 'value',
    '#value' => $items[$delta]['relation_id'],
  );

  $element['other_bundle'] = array(
    '#type' => 'value',
    '#value' => $items[$delta]['other_bundle'],
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
 * Implement hook_field_widget_error()
 */
function relation_field_widget_error($element, $error, $form, &$form_state) {
  switch ($error['error']) {
    case 'relation_error_missing_field':
      form_error($element['other_entity_type'], $error['message']);
      break;
    case 'relation_error_missing_other_entity':
      form_error($element['other_entity_id'], $error['message']);
      break;
    case 'relation_error_cardinality_max':
      form_error($element['other_entity_id'], $error['message']);
      break;
    default:
      form_error($element, $error['message']);
      break;
  }
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
function relation_field_prepare_view($entity_type, $entities, $field, $instances, $langcode, &$items, $displays) {
  if ($field['type'] != 'relation') {
    return;
  }

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

/**
 * Implements hook_field_create_field().
 * 
 * When a user adds a Relation field for the first time, he does not get an
 * option to choose translatable. Nor can we establish field type defaults in
 * hook_field_info(). When a new field is created, we set the translate
 * variable to OFF.
 * 
 * Rationale: Setting a field to non translatable forces the field to be shown
 * on any translation of an entity, despite its current language. fsd fasdasdsad 
 * 
 * See field_ui_field_overview_form_submit() in field_ui.admin.inc.
 * 
 */
function relation_field_create_field($field) {
  if ($field['type'] == 'relation') {
    $values = array(
    	'field_name' => $field['field_name'],
      'translatable' => FALSE,
    );
    field_update_field($values);
  }
}