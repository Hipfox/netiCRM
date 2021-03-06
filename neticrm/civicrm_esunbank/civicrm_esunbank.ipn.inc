<?php
civicrm_initialize();
require_once 'CRM/Core/Payment/BaseIPN.php';

/**
 * Helper function for receive pingback from payment server
 */
function civicrm_esunbank_ipn($post = NULL, $slient = NULL){
  // detect variables
  if(empty($_POST) && empty($post)){
    watchdog("civicrm_esunbank", "Could not find POST data from payment server");
  }
  else{
    if(empty($post)){
      $post = $_POST;
    }
    // process post data first
    $data = !empty($post['Data']) ? $post['Data'] : $post['data'];
    $output = NULL;
    if($data){
      $parsed_data = civicrm_esunbank_parse_response($data);
      list($last_date, $last_sequence) = explode(':' , variable_get('civicrm_esunbank_sequence', '0:0'));
      $return = array();
      foreach($parsed_data as $response){
        $sequence = $response['sequence'];
        $payment_date = date('Ymd', strtotime($response['payment_datetime']));
        if($silent && $sequence <= $last_sequence && $payment_date == $last_date ){
          $return[$sequence] = -1;
          continue;
        }
        // try to find out response component
        if(strlen($response['serial'])){
          $query = CRM_Core_DAO::executeQuery("SELECT c.*, p.participant_id FROM civicrm_contribution c LEFT JOIN civicrm_participant_payment p ON p.contribution_id = c.id WHERE c.trxn_id = %1 AND c.created_date > '%2' AND c.contribution_status_id = 2", array(1 => array($response['serial'], 'String'), 2 => array(date('Y-01-01 00:00:00'), 'Timestamp')) );
          $query->fetch();
          $response['cid'] = $query->id;
          drupal_write_record('civicrm_contribution_esunbank', $response, 'cid');

          if($query->id){
            if(empty($query->participant_id)){
              $component = 'contribute';
            }
            else{
              $component = 'event';
            }
            $ipn = new CRM_Core_Payment_ESUNBANKIPN();
            $status = $ipn->main($component, $response, $query);
          }
          else{
            $status = 1;
          }

          $return[$sequence] = $status;
        }
      }
      if($slient){
        variable_set("civicrm_esunbank_sequence", "$payment_date:$sequence");
        return $return;
      }
      // after all script processed, we always output OK for esunbank https
      else{
        print 'OK';
      }
    }
  }
  return FALSE;
}

function civicrm_esunbank_parse_response($data){
  $r = array();
  if (substr($data, 0,3) == pack("CCC",0xef,0xbb,0xbf)) {
    $data = substr($data, 3);
  }
  $lines = explode(pack("CC",0x0d,0x0a), $data);

  foreach($lines as $l){
    if($l[0] == '#') continue;
    $d = explode(',', $l);
    if(count($d) < 6){
      watchdog('civicrm_esunbank', 'Wrong format when parsing: '.$l, NULL, WATCHDOG_ERROR);
      continue;
    }
    $ary = array(
      'process_date' => $d[0],
      'instrument' => $d[1],
      'sequence' => $d[2],
      'serial' => $d[3],
      'amount' => $d[4],
      'payment_datetime' => date("Y-m-d H:i:s", strtotime($d[5])),
    );
    if(!empty($d[6])){
      $ary['data'] = $d[6];
    }
    $r[] = $ary;
  }
  return $r;
}

class CRM_Core_Payment_ESUNBANKIPN extends CRM_Core_Payment_BaseIPN {
  static $payment_processor= null;
  function __construct( ) {
    parent::__construct( );
  }

  function main(  $component, $response, $contribution){
    // get the contribution and contact ids from the GET params
    $objects = $ids = $input = array();
    $input = $response;
    $input['component'] = $component;

    // ids
    $ids['contact'] = $contribution->contact_id;
    $ids['contribution'] = $contribution->id;
    if ( $component == 'event' ) {
      $ids['event'] = CRM_Core_DAO::getFieldValue("CRM_Event_DAO_Participant", $contribution->participant_id, 'event_id', 'id');
      $ids['participant'] = $contribution->participant_id;
    }
    else {
      $ids['membership'] = $_GET['mid'];
      $ids['contributionRecur'] = $contribution->contribution_recur_id;
      $ids['contributionPage'] = $contribution->contribution_page_id;
      /*
      $ids['related_contact'] = $_GET['rid'];
      $ids['onbehalf_dupe_alert'] = $_GET['onbehalf_dupe_alert'];
      */
    }
    

    $qfKey = $_GET['qfKey'] ? $_GET['qfKey'] : '';
    $civi_base_url = $component == 'event' ? 'civicrm/event/register' : 'civicrm/contribute/transact';

    // now, retrieve full object by validateData, or false fallback
    if ( ! $this->validateData( $input, $ids, $objects ) ) {
      return false;
    }

    // set global variable for paymentProcessor
    self::$payment_processor =& $objects['paymentProcessor'];
    if($objects['contribution']->contribution_status_id == 1){
      // already completed, skip and redirect to thank you page
      if($qfKey){
        drupal_goto($civi_base_url, "_qf_ThankYou_display=true&qfKey={$qfKey}");
      }
      else{
        // do nothing for now. We should never have this page
      }
    }
    else{
      // start validation
      $transaction = new CRM_Core_Transaction();
      $note = '';
      $contribution =& $objects['contribution'];
      $contribution->receive_date = date('YmdHis', strtotime($input['payment_datetime']));
      list($source) = explode('-玉山', $contribution->source);
      $contribution->source = $source.'-玉山('.$input['instrument'].')';
      $input['trxn_id'] = $input['serial'];
      $this->completeTransaction( $input, $ids, $objects, $transaction, $recur );
      $note .= ts('Completed')."\n";
      $note .= $input['instrument']."\n";
      $note .= 'Bank process date:'. $input['process_date'];
      $this->addNote($note, $contribution);

      // printing OK!!
      return 'OK';
    }
    $objects = null;
    return false;
  }

  function addNote($note, &$contribution){
    require_once 'CRM/Core/BAO/Note.php';
    $note = date("Y/m/d H:i:s"). ts("Transaction record").": \n".$note."\n===============================\n";
    $note_exists = CRM_Core_BAO_Note::getNote( $contribution->id, 'civicrm_contribution' );
    if(count($note_exists)){
      $note_id = array( 'id' => reset(array_keys($note_exists)) );
      $note = $note . reset($note_exists);
    }
    else{
      $note_id = NULL;
    }
    
    $noteParams = array(
      'entity_table'  => 'civicrm_contribution',
      'note'          => $note,
      'entity_id'     => $contribution->id,
      'contact_id'    => $contribution->contact_id,
      'modified_date' => date('Ymd')
    );
    CRM_Core_BAO_Note::add( $noteParams, $note_id );
  }
}
