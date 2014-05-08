<?php
/**
 * @file
 *  Contains class FacetApiDropdownsImproved
 */


/**
 * Class FacetApiDropdownsImproved
 */
class FacetApiSelectDropdownsImproved extends FacetapiSelectDropdowns {

  public function init() {
    parent::init();
    if ($this->settings->settings['one-value-only']) {
      $this->onlyOneValueAlter();
    }
  }

  /**
   *
   */
  public function settingsForm(&$form, &$form_state) {
    parent::settingsForm($form, $form_state);

    $form['widget']['widget_settings']['links']['one-value-only'] = array(
      '#type' => 'checkbox',
      '#title' => t('Only one value allowed for this filter'),
      '#description' => $this->getDescriptionText(),
      '#default_value' => $this->settings->settings['one-value-only'],
    );
  }

  /**
   * Adding default value for the extra setting we added into the form
   */
  public function getDefaultSettings() {
    $defaults = parent::getDefaultSettings();
    $defaults['one-value-only'] = false;
    return $defaults;
  }

  /**
   * Allows only one value to be set for this filter.
   *
   * If a value is already set, then it will re-build the facet options without
   * it, to make sure that all counts are ok
   */
  private function onlyOneValueAlter() {
    // check if the facet is active
    try {
      $build = &$this->build;
      $facet = $this->facet;
      $facet_info = $facet->getFacet();
      $adapter = $facet->getAdapter();

      if ($search = $adapter->getCurrentSearch()) {
        $rebuild = false;
        $query = clone $search[0];
        $filters = &$query->getFilter()->getFilters();

        // @todo: replace this with a recursive function
        foreach($filters as $filter_key => $filter) {
          if (!method_exists($filter, 'getFilters')) {
            continue;
          }

          $filter_array = $filter->getFilters();
          if (isset($filter_array[0][0]) && $filter_array[0][0] == $facet_info['field alias']) {
            $rebuild = true;
            unset($filters[$filter_key]);
          }
        }

        if ($rebuild) {
          // Make sure we don't have any other facets in the query
          $query_options = &$query->getOptions();
          $query_options['search_api_facets'] = array();

          // Use the adapter to add current facet into query options. Without this
          // query will run with no facet attached and count is not returned
          $adapter->addFacet($facet_info, $query);

          // Run the query to get the results
          $results = $query->execute();
          if (empty($results['search_api_facets'][$this->facet['field alias']])) {
            // Continue only if new values were returned
            return;
          }

          // Update results count
          foreach($results['search_api_facets'][$this->facet['field alias']] as $facet_value) {
            $filter_value = substr($facet_value['filter'], 1, -1); // remove double quotes
            if (isset($build[$this->facet['field alias']][$filter_value]['#count'])) {
              $build[$this->facet['field alias']][$filter_value]['#count'] = $facet_value['count'];
            }
          }

          // rebuild path & query
          $processor = $adapter->getProcessor($facet_info['name']);
          $active_items = $this->facet->getAdapter()->getActiveItems($this->facet->getFacet());
          $this->processQueryStrings($build[$this->facet['field alias']], $processor, $active_items);
        }
      }
    }
    catch (Exception $exception) {
      watchdog_exception('facetapi_select_improved', $exception);
    }
  }


  /**
   * Initializes the render array's query string variables.
   *
   * @see FacetapiFacetProcessor::processQueryStrings().
   *
   * The following changes were made:
   *  - $processor: added as an argument (cannot use $this anymore)
   *  - $active_items: in order to remove current values form the query we have
   *    to pass active values for every item
   *  - When calling getQueryString(), all items have the $active argument set to
   *    true. This is need to use the $active_items trick.
   *    @see FacetapiUrlProcessorStandard::getQueryString()
   *
   * @param array &$build
   *   The initialized render array.
   * @param FacetApiFacetProcessor $processor
   *   Original processor function
   * @param array $active_items
   *   Active items from this facet in order to remove them
   */
  protected function processQueryStrings(array &$build, FacetApiFacetProcessor $processor, array $active_items = array()) {
    foreach ($build as $value => &$item) {
      $values = array($value);
      // Calculate paths for the children.
      if (!empty($item['#item_children'])) {
        $this->processQueryStrings($item['#item_children'], $processor);
        // Merges the childrens' values if the item is active so the children
        // are deactivated along with the parent.
        if ($item['#active']) {
          $values = array_merge(facetapi_get_child_values($item['#item_children']), $values);
        }
      }

      // For inactive items remove active_items before generating the query and path
      $restore_params = false;
      if ($item['#active'] == false) {
        $url_processor = $this->facet->getAdapter()->getUrlProcessor();
        // Save original params to restore them later
        $original_params = $url_processor->getParams();
        $restore_params = true;

        // Remove active items from this facet
        foreach($active_items as $active_item) {
          $url_processor->removeParam($active_item['pos']);
        }
      }

      // Formats path and query string for facet item, sets theme function.
      $item['#path'] = $processor->getFacetPath($values, $item['#active']);
      $item['#query'] = $processor->getQueryString($values, $item['#active']);

      if ($restore_params) {
        $url_processor->setParams($original_params, $url_processor->getFilterKey());
      }
    }
  }

  /**
   * Description text for the only one value behavior
   *
   * @return string
   */
  private function getDescriptionText() {
    $html = '<p>' . t('Prevents facetapi to add two filtered values for this ' .
        'facet. By default you can select value A, and then B and search will ' .
        'return all the results that have both A and B values. After enabling ' .
        'this option, A value will be removed from the filter once you select ' .
        'B from the facet.') .
      '</p>';

    $html .= '<p>' . t('When enabled, number of results is re-calculated by ' .
        'running an additional search query. Enabling it for many facets might ' .
        'have a performance impact') .
      '</p>';
    return $html;
  }
}

