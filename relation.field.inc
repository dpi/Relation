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
 * Implements hook_field_presave().
 */
function relation_field_presave($entity_type, $entity, $field, $instance, $langcode, &$items) {
  $field_name = $field['field_name'];
  list($id, $vid, $bundle) = entity_extract_ids($entity_type, $entity);

  if (isset($entity->relation_recursion)) {
    return;
  }

  //group relations by type
  $relations = array();
  foreach ($items as &$item) {
    // Create a new relation_id for new relations.
    if (!isset($item['relation_id'])) {
      $item['relation_id'] = db_next_id();
    }
    $relations[$item['other_entity_type']][$item['relation_id']] = $item['other_entity_id'];
  }
  
  //remove opposite ends of any field values that have been removed.
  if (!isset($entity->relation_deletion_recursion)) {
    $old_entity = entity_load_unchanged($entity_type, $id);
    if (!empty($old_entity->{$field_name})) {
    //group by entity_type
    $old_relations = array();
      foreach ($old_entity->{$field_name}[$langcode] as $old_item) {
        $old_relations[$old_item['other_entity_type']][$old_item['relation_id']] = $old_item['other_entity_id'];
      }
    }
    foreach ($old_relations as $other_type => $old_entity_relations) {
      foreach ($old_entity_relations as $relation_id => $other_id) {
        if (!isset($relations[$other_type][$relation_id])) {
          //This doesn't seem optimal, but we probably waste more resources loading
          //all entities, because few will be deleted. I think.
          $others = entity_load($other_type, array($other_id));
          $other = $others[$other_id];
          foreach ($other->{$field_name}[$langcode] as $delta => $value)
            if ($value['relation_id'] == $relation_id) {
              unset($other->{$field_name}[$langcode][$delta]);
            }
          // Would be nice to not save here, and just do one save at the end...
          // Prevent infinite recursion; ENTITY_save() calls hook_field_presave().
          $other->relation_deletion_recursion = TRUE;
          // @todo Entity module's entity_save() does not handle core entities yet.
          // @see http://drupal.org/node/988780
          $result = entity_save($other_type, $other);
          if ($result === FALSE && function_exists($other_type . '_save')) {
            $function = $other_type . '_save';
            $function($other);
          }
        }
      }
    }
  }
  
  // $entity_relations describes relations from $entity. In general, $entity_
  // describes the entity being saved, $other_ describes entities on the other
  // end of the relation.
  foreach ($relations as $other_type => $entity_relations) {
    $others = entity_load($other_type, $entity_relations);
    // @todo The else condition must not happen; implement hook_field_validate().
    if (!$others) {
      continue;
    }
    foreach ($others as $other_id => $other) {
      // A new field instance on existing entities contains no structure yet.
      if (!isset($other->{$field_name}[$langcode])) {
        $other->{$field_name}[$langcode] = array();
      }
      $other_items = &$other->{$field_name}[$langcode];

      // Transform current relations on the other into a lookup map keyed by
      // relation_id.
      $other_relations = array();
      foreach ($other_items as $delta => $other_item) {
        $other_relations[$other_item['relation_id']] = array(
          'delta' => $delta,
          'item' => $other_item,
        );
      }
      // Add a new relation, if it does not exist yet.
      foreach ($entity_relations as $relation_id => $other_entity_id) {
        if (!isset($other_relations[$relation_id])) {
          $other_items[] = array(
            'relation_id' => $relation_id,
            'other_entity_type' => $entity_type,
            'other_entity_id' => $id,
            // @todo Add optional revision_id handling.
            // 'other_revision_id' => $vid,
            'other_revision_id' => NULL,
          );
        }
      }
      // Prevent infinite recursion; ENTITY_save() calls hook_field_presave().
      $other->relation_recursion = TRUE;
      // @todo Entity module's entity_save() does not handle core entities yet.
      // @see http://drupal.org/node/988780
      $result = entity_save($other_type, $other);
      if ($result === FALSE && function_exists($other_type . '_save')) {
        $function = $other_type . '_save';
        $function($other);
      }
      unset($other->relation_recursion);
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
  list($id, $vid, $bundle) = entity_extract_ids($entity_type, $entity);

  //group by entity type
  $relations = array();
  foreach ($items as $item) {
    $relations[$item['other_entity_type']][$item['relation_id']] = $item['other_entity_id'];
  }

  foreach ($relations as $other_type => $entity_relations) {
    $others = entity_load($other_type, $entity_relations);
    //nothing to see here, move along.
    if (!$others) {
      continue;
    }
    foreach ($others as $other_id => $other) {
      if (isset($other->{$field['field_name']}[$langcode])) {

        //Delete relation on $other pointing back to $entity.
        foreach ($other->{$field['field_name']}[$langcode] as $delta => $value) {
          if ($value['other_entity_id'] = $id) {
            unset($other->{$field['field_name']}[$langcode][$delta]);
          }
        }
        // @todo Entity module's entity_save() does not handle core entities yet.
        // @see http://drupal.org/node/988780
        $result = entity_save($other_type, $other);
        if ($result === FALSE && function_exists($other_type . '_save')) {
          $function = $other_type . '_save';
          $function($other);
        }
      }
    }
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
 * Implements hook_field_validate().
 */
function relation_field_validate($entity_type, $entity, $field, $instance, $langcode, $items, &$errors) {
  foreach ($items as $delta => $item) {
    $other = entity_load($item['other_entity_type'], array($item['other_entity_id']));
    // Check that $other entity exists.
    if (empty($other)) {
      $errors[$field['field_name']][$langcode][$delta][] = array(
        'error' => 'relation_error_missing_other_entity',
        'message' => t('The %type entity %id does not exist.', array(
          '%type' => $item['other_entity_type'],
          '%id' => $item['other_entity_id'])
        ),
      );
      continue;
    }
    // Check that $other's bundle has the field attached.
    if (!isset($other[$item['other_entity_id']]->{$field['field_name']})) {
      $errors[$field['field_name']][$langcode][$delta][] = array(
        'error' => 'relation_error_missing_field',
        'message' =>  t('The %bundle bundle does not have a %field_name field attached.', array(
          '%bundle' => $other[$item['other_entity_id']]->type,
          '%field_name' =>  $field['field_name'])
        ),
      );
      continue;
    }
    // Check that number of fields on $other is < cardinality.
    if ($field['cardinality'] != -1 && count($other[0]->{fieldset}[$langcode] >= $field['cardinality'] )) {
      // Check and see if one of the fields on $other is refering back to $entity.
      $exists = FALSE;
      foreach ($other[0]->{fieldset}[$langcode] as $value) {
        if ($value['other_entity_id'] == $item['other_entity_type']) {
          $exists = TRUE;
          break;
        }
      }
      if (!$exists) {
        $errors[$field['field_name']][$langcode][$delta][] = array(
          'error' => 'relation_error_missing_field',
          'message' =>  t('The %type entity %id is all full up. If you want to add more relations to it, you need to increase the field cardinality.', array(
            '%type' => $item['other_entity_type'],
            '%id' =>  $item['other_entity_id'])
          ),
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
function relation_field_prepare_view($entity_type, $entities, $field, $instances, $langcode, &$items, $displays) {
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
