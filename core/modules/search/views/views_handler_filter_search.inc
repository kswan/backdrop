<?php
/**
 * @file
 * Contains a search filter handler.
 */

/**
 * Field handler to provide simple renderer that allows linking to a node.
 *
 * @ingroup views_filter_handlers
 */
class views_handler_filter_search extends views_handler_filter {
  var $always_multiple = TRUE;

  /**
   * Stores a viewsSearchQuery object to be able to use the search.module "api".
   *
   * @var viewsSearchQuery
   */
  var $search_query = NULL;

  /**
   * Checks if the search query has been parsed.
   */
  var $parsed = FALSE;

  function option_definition() {
    $options = parent::option_definition();

    $options['operator']['default'] = 'optional';
    $options['remove_score'] = array('default' => FALSE, 'bool' => TRUE);

    return $options;
  }

  /**
   * Overrides views_handler_filter::options_form().
   *
   * Add an option to remove search scores from the query.
   */
  function options_form(&$form, &$form_state) {
    parent::options_form($form, $form_state);

    $form['remove_score'] = array(
      '#type' => 'checkbox',
      '#title' => t('Remove search score'),
      '#description' => t('Check this box to remove the search score from the query. This can help reduce help reduce duplicate search results when using this filter.'),
      '#default_value' => $this->options['remove_score'],
    );
  }


  /**
   * Provide simple equality operator
   */
  function operator_form(&$form, &$form_state) {
    $form['operator'] = array(
      '#type' => 'radios',
      '#title' => t('On empty input'),
      '#default_value' => $this->operator,
      '#options' => array(
        'optional' => t('Show All'),
        'required' => t('Show None'),
      ),
    );
  }

  /**
   * Provide a simple textfield for equality
   */
  function value_form(&$form, &$form_state) {
    $form['value'] = array(
      '#type' => 'textfield',
      '#size' => 15,
      '#default_value' => $this->value,
      '#attributes' => array('title' => t('Enter the terms you wish to search for.')),
      '#title' => empty($form_state['exposed']) ? t('Value') : '',
    );
  }

  /**
   * Validate the options form.
   */
  function exposed_validate(&$form, &$form_state) {
    if (!isset($this->options['expose']['identifier'])) {
      return;
    }

    $key = $this->options['expose']['identifier'];
    if (!empty($form_state['values'][$key])) {
      $this->query_parse_search_expression($form_state['values'][$key]);
      if (count($this->search_query->words()) == 0) {
        form_set_error($key, format_plural(config_get('search.settings', 'search_minimum_word_size'), 'You must include at least one positive keyword with 1 character or more.', 'You must include at least one positive keyword with @count characters or more.'));
      }
    }
  }

  /**
   * Make sure that parseSearchExpression is run and everything is set up for
   * it.
   *
   * @param $input
   *    The search phrase which was input by the user.
   */
  function query_parse_search_expression($input) {
    if (!isset($this->search_query)) {
      $this->parsed = TRUE;
      $this->search_query = db_select('search_index', 'i', array('target' => 'replica'))->extend('viewsSearchQuery');
      $this->search_query->searchExpression($input, $this->view->base_table);
      $this->search_query->publicParseSearchExpression();
    }
  }

  /**
   * Add this filter to the query.
   *
   * Due to the nature of Form API, the value and the operator have an
   * unintended level of indirection. You will find them in $this->operator
   * and $this->value respectively.
   */
  function query() {
    // Since attachment views don't validate the exposed input, parse the search
    // expression if required.
    if (!$this->parsed) {
      $this->query_parse_search_expression($this->value);
    }
    $required = FALSE;
    if (!isset($this->search_query)) {
      $required = TRUE;
    }
    else {
      $words = $this->search_query->words();
      if (empty($words)) {
        $required = TRUE;
      }
    }
    if ($required) {
      if ($this->operator == 'required') {
        $this->query->add_where($this->options['group'], 'FALSE');
      }
    }
    else {
      $search_index = $this->ensure_my_table();

      $search_condition = db_and();

      if (!$this->options['remove_score']) {
        // Create a new join to relate the 'search_total' table to our current 'search_index' table.
        $join = new views_join;
        $join->construct('search_total', $search_index, 'word', 'word');
        $search_total = $this->query->add_relationship('search_total', $join, $search_index);

        $this->search_score = $this->query->add_field('', "$search_index.score * $search_total.count", 'score', array('aggregate' => TRUE, 'function' => 'sum'));
      }

      if (empty($this->query->relationships[$this->relationship])) {
        $base_table = $this->query->base_table;
      }
      else {
        $base_table = $this->query->relationships[$this->relationship]['base'];
      }
      $search_condition->condition("$search_index.type", $base_table);
      if (!$this->search_query->simple()) {
        $search_dataset = $this->query->add_table('search_dataset');
        $conditions = $this->search_query->conditions();
        $condition_conditions =& $conditions->conditions();
        foreach ($condition_conditions  as $key => &$condition) {
          // Take sure we just look at real conditions.
          if (is_numeric($key)) {
            // Replace the conditions with the table alias of views.
            $this->search_query->condition_replace_string('d.', "$search_dataset.", $condition);
          }
        }
        $search_conditions =& $search_condition->conditions();
        $search_conditions = array_merge($search_conditions, $condition_conditions);
      }
      else {
        // Stores each condition, so and/or on the filter level will still work.
        $or = db_or();
        foreach ($words as $word) {
          $or->condition("$search_index.word", $word);
        }

        $search_condition->condition($or);
      }

      $this->query->add_where($this->options['group'], $search_condition);
      $this->query->add_groupby("$search_index.sid");
      $matches = $this->search_query->matches();
      $placeholder = $this->placeholder();
      $this->query->add_having_expression($this->options['group'], "COUNT(*) >= $placeholder", array($placeholder => $matches));
    }
    // Set to NULL to prevent PDO exception when views object is cached.
    $this->search_query = NULL;
  }
}
