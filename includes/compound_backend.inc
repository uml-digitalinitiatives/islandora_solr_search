<?php
/**
 * @file
 * Compound backend stuff.
 */


/**
 * Admin options.
 *
 * @param array $form
 *   The form.
 * @param array $form_state
 *   The form state.
 *
 * @return array
 *   The updated form.
 */
function islandora_solr_compound_backend_admin_form(array $form, array $form_state) {
  $form = array();
  $form['islandora_solr_compound_relationship_field'] = array(
    '#type' => 'textfield',
    '#title' => t('Solr compound relationship field'),
    '#description' => t('Solr field containing the compound relationship. Defaults to RELS_EXT_isConstituentOf_uri_ms'),
    '#default_value' => variable_get('islandora_solr_compound_relationship_field', 'RELS_EXT_isConstituentOf_uri_ms'),
    '#autocomplete_path' => 'islandora_solr/autocomplete_luke',
  );
  $form['islandora_solr_compound_sequence_pattern'] = array(
    '#type' => 'textfield',
    '#title' => t('Solr compound sequence pattern'),
    '#description' => t('Compound sequences are stored with a unique relationship, if you index these in Solr provide the field name with %PID% in place of the actual escaped pid to use the SOLR Compound Member Query. Defaults to RELS_EXT_isSequenceNumberOf%PID%_literal_ms'),
    '#default_value' => variable_get('islandora_solr_compound_sequence_pattern', 'RELS_EXT_isSequenceNumberOf%PID%_literal_ms'),
  );
  return system_settings_form($form);
}

/**
 * Implements hook_form_validate().
 */
function islandora_solr_compound_backend_admin_form_validate(array $form, array &$form_state) {
  $default_pattern = variable_get('islandora_solr_compound_sequence_pattern', 'RELS_EXT_isSequenceNumberOf%PID%_literal_ms');
  if (isset($form_state['values']['islandora_solr_compound_sequence_pattern'])
    && $form_state['values']['islandora_solr_compound_sequence_pattern'] != $default_pattern) {
    if (strpos($form_state['values']['islandora_solr_compound_sequence_pattern'], '%PID%') === FALSE) {
      form_set_error('islandora_solr_compound_sequence_pattern',
        t('Your pattern MUST contain %PID% where the converted PID will appear.'));
    }
  }
}

/**
 * Implements callback_islandora_compound_object_query_backends().
 */
function islandora_solr_compound_object_query($pid) {
  module_load_include('inc', 'islandora_solr', 'includes/breadcrumbs');

  $solr_build = new IslandoraSolrQueryProcessor();
  $rows = 1000;

  $relationship = variable_get('islandora_solr_compound_relationship_field', 'RELS_EXT_isConstituentOf_uri_ms');
  $sequence_pattern = variable_get('islandora_solr_compound_sequence_pattern', 'RELS_EXT_isSequenceNumberOf%PID%_literal_ms');

  if (str_contains($sequence_pattern, ';')) {
    $sequence_patterns = explode(';', $sequence_pattern);
  } else {
    $sequence_patterns = [$sequence_pattern];
  }
  array_walk($sequence_patterns, function(&$value) use ($pid) {
    $value = str_replace('%PID%', str_replace(':', '_', $pid), $value);
  });
  $sequence_pattern = implode(', ', $sequence_patterns);

  $query = format_string('!field:("!pid"+OR+"info:fedora/!pid")', array(
    '!field' => $relationship,
    '!pid' => $pid,
  ));
  $params = array(
    'limit' => $rows,
    'rows' => $rows,
    'fl' => format_string('PID, !seq', array(
      '!seq' => $sequence_pattern,
    )),
  );
  // Page to start on.
  $start = -1;
  // Total results holder.
  $total = NULL;
  // For cumulative storage of constituents.
  $constituents = array();
  // Final output.
  $output = array();

  $solr_build->buildQuery($query, $params);
  $solr_build->solrParams['facet'] = $solr_build->solrParams['hl'] = 'false';
  $solr_build->solrParams = islandora_solr_clean_compound_filters($solr_build->solrParams);

  // Loop in case there are lots.
  do {
    $start += 1;
    $solr_build->solrStart = $start * $rows;
    $solr_build->solrLimit = $rows;
    $solr_build->executeQuery(FALSE);
    $results = (array) $solr_build->islandoraSolrResult['response']['objects'];
    $constituents = array_merge($constituents, $results);

    if (is_null($total) && isset($solr_build->islandoraSolrResult['response']['numFound'])) {
      $total = $solr_build->islandoraSolrResult['response']['numFound'];
    }
  } while ($total > (($start * $rows) + $rows));

  if (count($constituents) > 0) {
    $sort = function($a, $b) use($sequence_patterns) {
      $getSequence = function($a) use ($sequence_patterns) {
        foreach ($sequence_patterns as $pattern) {
          if (!array_key_exists('solr_doc', $a)) {
            continue;
          }
          if (array_key_exists($pattern, $a['solr_doc'])) {
            $value = $a['solr_doc'][$pattern];
            if (is_array($value)) {
              $value = reset($value);
            }
            return intval($value);
          }
        }
        return null;
      };
      $aValue = $getSequence($a);
      $bValue = $getSequence($b);
      if (is_null($aValue) || is_null($bValue)) {
        return 0; // no sorting
      } else if (empty($aValue)) {
        return 1;
      } else if (empty($bValue)) {
        return -1;
      }
      return $aValue - $bValue;
    };
    uasort($constituents, $sort);

    foreach ($constituents as $result) {
      if (array_key_exists('solr_doc', $result)) {
        foreach ($sequence_patterns as $sequence_pattern) {
          if (array_key_exists($sequence_pattern, $result['solr_doc'])) {
            $sequence = $result['solr_doc'][$sequence_pattern];
            if (is_array($sequence)) {
              $sequence = reset($sequence);
            }
            break;
          }
        }
      }
      $output[$result['PID']] = array(
        'pid' => $result['PID'],
        'title' => $result['object_label'],
        'seq' => $sequence ?? 0,
      );
    }
  }

  return $output;
}
