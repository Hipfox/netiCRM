<?php

define('GW_EXPIRE_DAY', 7);

/**
 * Checkout functions
 *
 * We place the code into drupal inorder to seperate the civicrm role.
 * Called by Civicrm doTransferCheckout of Class Payment_GW
 * 
 * @param $vars 
 * @param $component 
 * @param $payment_processor 
 */
function civicrm_gw_do_transfer_checkout(&$vars, &$component, &$payment_processor){
  // to see what instrument for newweb
  $instrument_id = $vars['civicrm_instrument_id'];
  $instrument_name = civicrm_instrument_by_id($instrument_id, 'name');
  $gw_instruments = _civicrm_gw_instrument('code');
  $instrument_code = $gw_instruments[$instrument_name];
  $form_key = $component == 'event' ? 'CRM_Event_Controller_Registration_'.$vars['qfKey'] : 'CRM_Contribute_Controller_Contribution_'.$vars['qfKey'];

  // The first, we insert every contribution into record. After this, we'll use update for the record.
  $record = array('cid' => $vars['contributionID'], 'created' => time());
  drupal_write_record("civicrm_contribution_gw", $record);

  $is_pay_later = TRUE;
  switch($instrument_code){
    case 'Credit Card':
    case 'unionpay':
      $redirect_vars = _civicrm_gw_ecpay($vars, $component, $payment_processor, $instrument_code);
      $is_pay_later = FALSE;
      break;
    case 'vacc':
    case 'web_atm':
    case 'barcode':
    case 'cvs':
    case 'ibon':
      $redirect_vars = _civicrm_gw_ecbank($vars, $component, $payment_processor, $instrument_code);
      break;
  }
  // now process contribution to save some default value
  $contrib_params = array( 'id' => $vars['contributionID'] );
  $contrib_values = $contrib_ids = array();
  CRM_Contribute_BAO_Contribution::getValues($contrib_params, $contrib_values, $contrib_ids);
  if($vars['civicrm_instrument_id']){
    $contrib_values['payment_instrument_id'] = $vars['civicrm_instrument_id'];
  }
  $contrib_values['is_pay_later'] = $is_pay_later;
  $contrib_values['trxn_id'] = $vars['contributionID'];
  $contribution =& CRM_Contribute_BAO_Contribution::create($contrib_values, $contrib_ids);

  // Inject in quickform sessions
  // Special hacking for display trxn_id after thank you page.
  $_SESSION['CiviCRM'][$form_key]['params']['trxn_id'] = $contribution->trxn_id;
  $_SESSION['CiviCRM'][$form_key]['params']['is_pay_later'] = $is_pay_later;

  print _civicrm_gw_form_redirect($redirect_vars, $instrument_code);
  // move things to CiviCRM cache as needed
  require_once 'CRM/Core/Session.php';
  CRM_Core_Session::storeSessionObjects( );
  exit;
}

function _civicrm_gw_ecpay(&$vars, $component, &$payment_processor, $instrument_code){
  global $base_url, $base_path, $language;

  // notify url for receive payment result
  $notify_url = _civicrm_gw_notify_url($vars, 'gw/ipn/'.$instrument_code, $component);

  // for union card
  $civi_base_url = $component == 'event' ? 'civicrm/event/register' : 'civicrm/contribute/transact';
  $mallurl = url($civi_base_url, array( 'query' => "_qf_ThankYou_display=1&qfKey={$vars['qfKey']}", 'absolute'=>true));

  // building vars
  $amount = $vars['currencyID'] == 'TWD' && strstr($vars['amount'], '.') ? substr($vars['amount'], 0, strpos($vars['amount'],'.')) : $vars['amount'];
  $name = truncate_utf8($vars['item_name'], 10);
  $notify_url .= "&qfKey=".$vars['qfKey'];

  $redirect_vars = array(
    "act" => "auth",
    "client" => $payment_processor['user_name'],
    "od_sob" => $vars['contributionID'],
    "amount" => $amount,
    "roturl" => $notify_url,
    "bk_posturl" => $notify_url.'&bg=1',
    "qfKey" => $vars['qfKey'],
    "#action" => $payment_processor['url_site'],
  );
  if($instrument_code == 'unionpay'){
    $redirect_vars["mallurl"] = $mallurl;
    $redirect_vars["CUPus"] = 1;
  }
  if(!strstr($language->language, 'zh')){
    $redirect_vars['Enn'] = 'e';
  }

  return $redirect_vars;
}

function _civicrm_gw_ecbank(&$vars, $component, &$payment_processor, $instrument_code){
  // prepare necessery variables
  $post = array();
  $post['mer_id'] = $payment_processor['signature'];
  $post['payment_type'] = $instrument_code;
  $post['od_sob'] = $vars['contributionID'];
  $post['amt'] = ($vars['currencyID'] == 'TWD' && strstr($vars['amount'], '.') ) ? substr($vars['amount'], 0, strpos($vars['amount'],'.'))  : $vars['amount'];
  if($instrument_code != 'web_atm'){
    $post['enc_key'] = $payment_processor['subject'];
    $post['ok_url'] = _civicrm_gw_notify_url($vars, "gw/ipn/$instrument_code", $component).'&bg=1';
  }

  // prepare option variables
  switch($instrument_code){
    // only web_atm use POST method to pass the parameter
    case 'web_atm':
      $post['return_url'] = _civicrm_gw_notify_url($vars, "gw/ipn/$instrument_code", $component)."&qfKey=".$vars['qfKey'];
      $post['#action'] = $payment_processor['url_api'];
      return $post;
    // otherwise, we need to fetch result from backgrouund
    case 'vacc':
      $post['expire_day'] = GW_EXPIRE_DAY;
      $post['setbank'] = 'ESUN';
      break;
    case 'barcode':
      $post['expire_day'] = GW_EXPIRE_DAY;
      break;
    case 'ibon':
      $post['prd_desc'] = $vars['item_name'];
      $post['desc_1'] = '';
      $post['desc_2'] = '';
      $post['desc_3'] = '';
      break;
    case 'cvs':
      $post['prd_desc'] = $vars['item_name'];
      $post['desc_1'] = '';
      $post['desc_2'] = '';
      $post['desc_3'] = '';
      $post['desc_4'] = '';
      break;
  }

  // fetch the result, but exclude web_atm (return in upper case)
  $http_query = http_build_query($post, '', '&');
  $api = url($payment_processor['url_api'], array('query'=>$http_query));
  $return = file_get_contents($api);
  parse_str($return, $res);

  // save fetched result
  $record = array('cid' => $vars['contributionID']);
  $save = array('error', 'bankcode', 'vaccno', 'payno', 'expire_datetime', 'tsr');
  foreach($save as $k){
    $record[$k] = $res[$k];
    if($k == 'expire_datetime'){
      $datetime = $res['expire_date'];
      $datetime .= $res['expire_time'] ? $res['expire_time'] : '000000';
      $record[$k] = strtotime($datetime.' +0800');
    }
  }
  $record['data'] = serialize($res);
  drupal_write_record('civicrm_contribution_gw', $record, 'cid');

  // after saved record, we should modify the status of participant / membership
  if($component == 'event'){
    $participant_id = CRM_Core_DAO::getFieldValue('CRM_Event_DAO_ParticipantPayment', $vars['contributionID'], 'participant_id', 'contribution_id');
    if($participant_id){
      $pstatus = CRM_Event_PseudoConstant::participantStatus();
      if($new_pstatus = array_search('Pending from pay later', $pstatus)){
        CRM_Core_DAO::setFieldValue('CRM_Event_DAO_Participant', $participant_id, 'status_id', $new_pstatus, 'id');
      }
    }
  }

  // inject info
  $form_key = $component == 'event' ? 'CRM_Event_Controller_Registration_'.$vars['qfKey'] : 'CRM_Contribute_Controller_Contribution_'.$vars['qfKey'];
  if($component == 'event'){
    $values =& $_SESSION['CiviCRM'][$form_key]['values']['event'];
  }
  else{
    $values =& $_SESSION['CiviCRM'][$form_key]['values'];
  }

  // After saving the record, we only return the popup javascript for these method
  $civi_base_url = $component == 'event' ? 'civicrm/event/register' : 'civicrm/contribute/transact';
  $thankyou_url = url($civi_base_url, array( 'query' => "_qf_ThankYou_display=1&qfKey={$vars['qfKey']}", 'absolute'=>true));

  if($res['tsr']){
    $redirect_vars = array(
      "#action" => $thankyou_url,
    );
    if($instrument_code == 'barcode'){
      $redirect_vars['#popup'] = _civicrm_gw_barcode($payment_processor['signature'], $res['tsr']);
    }
    else{
      // cvs vacc ibon
      $redirect_vars['#popup'] = _civicrm_gw_popupurl($instrument_code, $vars['contributionID']);
      //dpr($_SESSION['CiviCRM']['CRM_Contribute_Controller_Contribution_'.$vars['qfKey']]['values']['thankyou_text']);
    }
    // send mail
    if(empty($_SESSION['CiviCRM'][$form_key]['params']['sent'])){
      $values['thankyou_text'] .= '<div>如果您沒有看到繳費資訊，點此<strong>開啟'.l('付款資訊', $redirect_vars['#popup'].'&width=900&height=600&iframe=true', array('attributes' => array('class' => 'colorbox-load', 'target' => '_blank'))).'</strong></div>';
      $p['user'] = CRM_Core_DAO::getFieldValue('CRM_Contact_DAO_Contact', $vars['contactID'], 'display_name');

      // prepare template parameter
      $email = $vars['email-5'];
      // default from_email
      $from_emails = array();
      CRM_Core_OptionValue::getValues(array('name' => 'from_email_address'), &$from_emails);
      foreach($from_emails as $id => $v){
        if($v['is_default'] == 1){
          $from_default = $v['name'];
          break;
        }
      }
      if($component == 'event'){
        if($values['event']['confirm_from_name'] && $values['event']['confirm_from_email']){
          $from = $values['event']['confirm_from_name']. '<'.$values['event']['confirm_from_email'];
        }
      }
      else{ // contribute
        if($values['receipt_from_name'] && $values['receipt_from_email']){
          $from = $values['receipt_from_name']. ' <'.$values['receipt_from_email'].'>';
        }
      }
      $html = '您好！<br /><br />這是您在本網站<u>'.variable_get('site_name', '').'</u>的<u>'.$values['title'].'</u>頁面產生的付款資訊，點此可<strong>'.l('開啟付款資訊', $redirect_vars['#popup'], array('attributes' => array('target' => '_blank'))).'</strong>。<br />若您已經繳費，可忽略本訊息，提醒您，本繳費單有7天的期限，超過期限本訂單會自動取消。<br />--<br />'.variable_get('site_name', '');
      $send_params                = array( );
      $send_params['groupName'  ] = 'Activity Email Sender';
      $send_params['from'       ] = $from ? $from : $from_default;
      $send_params['toName'     ] = $p['user'];
      $send_params['toEmail'    ] = $email;
      $send_params['subject'    ] = '您在本站的付款資訊 - 訂單編號#'.$vars['contributionID'];
      $send_params['text'       ] = '';
      $send_params['html'       ] = $html;
      if ( ! CRM_Utils_Mail::send( $send_params ) ) {
        return false;
      }
      // prevent re-send invoice
      $_SESSION['CiviCRM'][$form_key]['params']['sent'] = true;;
    }

    return $redirect_vars;
  }
  else{
    drupal_set_message("Error on fetch response. Code number #{$res['error']}", 'error');
  }
}

function _civicrm_gw_form_redirect($redirect_vars, $instrument_code){
  header('Pragma: no-cache');
  header('Cache-Control: no-store, no-cache, must-revalidate');
  header('Expires: 0');

  switch($instrument_code){
    case 'Credit Card':
    case 'web_atm':
    case 'unionpay':
      $js = 'document.forms.redirect.submit();';
      $o .= '<form action="'.$redirect_vars['#action'].'" name="redirect" method="post" id="redirect-form">';
      foreach($redirect_vars as $k=>$p){
        if($k[0] != '#'){
          $o .= '<input type="hidden" name="'.$k.'" value="'.$p.'" />';
        }
      }
      $o .= '</form>';
      break;
    case 'vacc':
    case 'barcode':
    case 'cvs':
    case 'ibon':
      $js = '
    function print_redirect(){
      // creating the "paymentpop" window with custom features prior to submitting the form
      window.open("", "paymentpop", "scrollbars=yes,menubar=no,height=300,width=600,resizable=yes,toolbar=no,status=no,left=150,top=500");
      document.forms.print.submit();
      window.location = "'.$redirect_vars['#action'].'";
    }
    //window.open("'.$redirect_vars['#popup'].'", "paymentinfo", "scrollbars=yes,menubar=no,height=300,width=600,resizable=yes,toolbar=no,status=no,left=150,top=500");
      ';

      $o .= '<form action="'.$redirect_vars['#popup'].'" name="print" method="post" id="redirect-form" target="paymentpop">';
      foreach($redirect_vars as $k=>$p){
        if($k[0] != '#'){
          $o .= '<input type="hidden" name="'.$k.'" value="'.$p.'" />';
        }
      }
      $o .= '</form>';
      $o .= '<div align="center"><p>請按「取得付款資訊」按鈕以獲得繳款訊息</p><div><input type="button" value="取得付款資訊" onclick="print_redirect();" /></div></div>';
          break;
  }
  return '
<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.0 Strict//EN"
  "http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd"> 
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="en" lang="en" dir="ltr"> 
<head>
<meta http-equiv="Content-Type" content="text/html; charset=utf-8" />
</head>
<body>
  '.$o.'
  <script type="text/javascript">
  '.$js.'
  </script>
</body>
<html>
';
}

function _civicrm_gw_notify_url(&$vars, $path, $component){
  $query = array();
  $query[] = "contact_id={$vars['contactID']}";
  $query[] = "cid={$vars['contributionID']}";
  $query[] = "module={$component}";

  if ( $component == 'event' ) {
    $query[] = "eid={$vars['eventID']}";
    $query[] = "pid={$vars['participantID']}";
  }
  else {
    if ( $vars['membershipID'] ) {
      $query[] = "mid=".$vars['membershipID'];
    }
    if ($vars['related_contact']) {
      $query[] = "rid=".$vars['related_contact'];
      if ($vars['onbehalf_dupe_alert']) {
        $query[] = "onbehalf_dupe_alert=".$vars['onbehalf_dupe_alert'];
      }
    }
  }

  // if recurring donations, add a few more items
  if ( !empty( $vars['is_recur']) ) {
     if ($vars['contributionRecurID']) {
       $query[] = "crid={$vars['contributionRecurID']}";
       $query[] = "cpid={$vars['contributionPageID']}";
     }
  }

  $url = url(
    $path,
    array(
      'query'=> implode('&', $query),
      'absolute' => true,
    )
  );
  if($_SERVER['HTTP_HTTPS'] == 'on' || $_SERVER['HTTPS'] == 'on'){
    return str_replace('http://', 'https://', $url);
  }
  else{
    return $url;
  }
}
