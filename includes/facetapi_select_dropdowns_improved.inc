<?php
/**
 * @file
 *  Contains class FacetApiDropdownsImproved
 */


/**
 * Class FacetApiDropdownsImproved
 */
class FacetApiSelectDropdownsImproved extends FacetapiSelectDropdowns {

  /**
   * @var bool|array
   *
   * @see FacetApiSelectDropdownsImproved::simulateNoActiveItemsState()
   * @see FacetApiSelectDropdownsImproved::revertToActiveItemsState()
   */
  private $state_backup = false;

  /**
   * @var FacetapiUrlProcessor
   *  Replacement for the one provided by adapter
   *
   * @see FacetApiSelectDropdownsImproved::getFreshUrlProcessor()
   * @see FacetApiSelectDropdownsImproved::simulateNoActiveItemsState()
   */
  private $fresh_url_processor = null;

  public function init() {
    parent::init();
    if ($this->settings->settings['one-value-only']) {
      $this->onlyOneValueAlter();
    }
  }

  /**
   * {@inheritdoc}
   */
  public function execute() {

    // Do not call FacetapiSelectDropdowns::execute(). Instead build select
    // here. The main difference of our select is that it can be "deselected".
    static $count = 0;
    $count++;
    $element = &$this->build[$this->facet['field alias']];
    $options = array();
    $active_url = '_current';
    foreach ($element as $value => $item) {
      $path = !empty($this->settings->settings['submit_page']) ? $this->settings->settings['submit_page'] : $item['#path'];
      $path = strpos($item['#path'], $path) === 0 ? $item['#path'] : $path;
      $url = url($path, array('query' => $item['#query']));
      if ($item['#active']) {
        $active_url = $url;
        $url = '_current';
      }
      $options[$url] = $item['#markup'].' ('.$item['#count'].')';
    }
    $options = array($active_url => t('--Choose--')) + $options;
    $element = facetapi_select_facet_form(array(), $options, $count);
    $element['facets']['#value'] = '_current';
  }

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
          $this->processFilterURLs($build[$this->facet['field alias']]);

          // Apply sorts.
          $this->applySorts($build[$this->facet['field alias']]);
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
   * @param array &$build
   *   The initialized render array.
   */
  protected function processFilterURLs(array &$build) {
    foreach ($build as $value => &$item) {
      // For inactive items remove active_items before generating the query and path
      $revert = false;
      if ($item['#active'] == false) {
        $revert = $this->simulateNoActiveItemsState();
      }

      // Build path and query for this item
      $this->processItemPathAndQuery($value, $item);

      if ($revert) {
        $this->revertToActiveItemsState();
      }
    }
  }

  /**
   * Simulates the state when there is no active item for this facet
   *
   * @return bool
   *  True if state was changed
   */
  private function simulateNoActiveItemsState() {
    if ($this->state_backup !== false) { return false; } // state is already changed

    $this->state_backup['url_processor_params'] = $this->removeActiveItemsFromUrlProcessorParams();
    $this->state_backup['request_get'] = $this->removeActiveItemsFromGET();
    $this->refreshFacetAndUrlProcessor();

    return true;
  }

  /**
   * Reverts state before simulateNoActiveItemsState() was called
   *
   */
  private function revertToActiveItemsState() {
    if ($this->state_backup === false) { return; } // nothing to restore

    if (isset($this->state_backup['url_processor_params'])) {
      $this->revertActiveItemsFromUrlProcessorParams($this->state_backup['url_processor_params']);
    }
    if (isset($this->state_backup['request_get'])) {
      $this->restoreActiveItemsInGET($this->state_backup['request_get']);
    }

    $this->refreshFacetAndUrlProcessor();
    $this->state_backup = false;
  }

  /**
   * Remove active items from URL Processor params
   *
   * @return array
   *  Old params
   */
  private function removeActiveItemsFromUrlProcessorParams() {
    $url_processor = $this->facet->getAdapter()->getUrlProcessor();
    $original_params = $url_processor->getParams();

    // Remove active items from this facet
    $active_items = $this->facet->getAdapter()->getActiveItems($this->facet->getFacet());
    foreach($active_items as $active_item) {
      $url_processor->removeParam($active_item['pos']);
    }

    return $original_params;
  }

  /**
   * Restore original URL Processor params
   *
   * @param array $original_params
   */
  private function revertActiveItemsFromUrlProcessorParams($original_params) {
    $url_processor = $this->facet->getAdapter()->getUrlProcessor();
    $url_processor->setParams($original_params, $url_processor->getFilterKey());
  }

  /**
   * Refresh url processor and facet
   */
  private function refreshFacetAndUrlProcessor() {
    // This will update active items returned by Facet API adapter
    $this->facet->getAdapter()->processActiveItems();

    // Force generation of a new processor
    $this->fresh_url_processor = null;
  }

  /**
   * Rebuild $_GET super global without active items in it
   *
   * Required by FacetAPI pretty paths which depends on $_GET[q] values
   *
   * @return array
   */
  private function removeActiveItemsFromGET() {
    $_get = $_GET;

    $facet_info = $this->facet->getFacet();
    $adapter = $this->facet->getAdapter();
    $processor = $adapter->getProcessor($facet_info['name']);

    $values = $this->getActiveItemsValues();

    // Built the path and query of the page without active items of this facet
    $path = $processor->getFacetPath($values, true);
    $query = $processor->getQueryString($values, true);

    // Overwrite GET
    $_GET = array_merge($query, array('q' => $path));

    return $_get;
  }

  /**
   * Restore changes made by:
   *  FacetApiSelectDropDownsImproved::removeActiveItemsFromGET()
   *
   * @param $old_values
   */
  private function restoreActiveItemsInGET($old_values) {
    $_GET = $old_values;
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

  /**
   * Rebuild path and query for one item of the facet
   *
   * @param $value
   * @param $item
   */
  protected function processItemPathAndQuery($value, &$item) {
    $values = array($value);
    // Calculate paths for the children.
    if (!empty($item['#item_children'])) {
      $this->processFilterURLs($item['#item_children']);
      // Merges the children's values if the item is active so the children
      // are deactivated along with the parent.
      if ($item['#active']) {
        $values = array_merge(facetapi_get_child_values($item['#item_children']), $values);
      }
    }

    // Formats path and query string for facet item, sets theme function.
    $item['#path'] = $this->getFreshUrlProcessor()->getFacetPath($this->facet->getFacet(), $values, $item['#active']);
    $item['#query'] = $this->getFreshUrlProcessor()->getQueryString($this->facet->getFacet(), $values, $item['#active']);
  }

  /**
   * Get the values from all active items
   *
   * @return array
   */
  private function getActiveItemsValues() {
    $active_values = array();

    $facet = $this->facet->getFacet();
    $active_items = $this->facet->getAdapter()->getActiveItems($facet);
    foreach($active_items as $active_item) {
      $active_values[] = $active_item['value'];
    }

    return $active_values;
  }

  /**
   * Get a freshly initialized url processor
   *
   * @return FacetapiUrlProcessor
   */
  private function getFreshUrlProcessor() {
    if ($this->fresh_url_processor === null) {
      // Rebuild URL processor
      $searcher_info = facetapi_get_searcher_info();
      $searcher_name = $this->facet->getAdapter()->getSearcher();
      $url_processor_id = $searcher_info[$searcher_name]['url processor'];

      // Get a new one
      $this->fresh_url_processor = $this->facet->getAdapter()->loadUrlProcessor($url_processor_id);

      // Initialize values
      $this->fresh_url_processor->fetchParams();
      $this->fresh_url_processor->getFilterKey();
    }

    return $this->fresh_url_processor;
  }
}

